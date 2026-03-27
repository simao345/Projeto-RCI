#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "routing.h"
#include "colours.h"

#define INF 999

NodeState node;

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

Route *find_route(const char *dest) {
    for (int i = 0; i < node.route_count; i++)
        if (strcmp(node.routing_table[i].dest, dest) == 0)
            return &node.routing_table[i];
    return NULL;
}

Route *find_or_create_route(const char *dest) {
    Route *r = find_route(dest);
    if (r) return r;
    if (node.route_count >= MAX_ROUTES) return NULL;

    r = &node.routing_table[node.route_count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->dest, dest, 3);
    r->dest[3]       = '\0';
    r->distance      = INF;
    r->succ_fd       = -1;
    r->state         = FORWARDING;   /* caller decides if COORD is needed */
    r->succ_coord_fd = -1;
    return r;
}


void send_route_to_fd(Route *r, int fd) {
    if (r->distance >= INF) return;
    char msg[64];
    snprintf(msg, sizeof(msg), "ROUTE %s %d\n", r->dest, r->distance);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s SEND ROUTE %s %d → fd %d\n> ",
               MAGENTA, RESET, r->dest, r->distance, fd);
}

void send_route_to_all(Route *r) {
    for (int j = 0; j < node.neighbor_count; j++)
        send_route_to_fd(r, node.neighbors[j].fd);
}

static void send_coord(const char *dest, int fd) {
    char msg[64];
    snprintf(msg, sizeof(msg), "COORD %s\n", dest);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s SEND COORD %s → fd %d\n> ",
               MAGENTA, RESET, dest, fd);
}

static void send_uncoord(const char *dest, int fd) {
    char msg[64];
    snprintf(msg, sizeof(msg), "UNCOORD %s\n", dest);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s SEND UNCOORD %s → fd %d\n> ",
               MAGENTA, RESET, dest, fd);
}

/* ------------------------------------------------------------------ */
/*  Enter COORDINATION for a route — shared logic used by multiple     */
/*  callers. Sends COORD to all neighbours except `skip_fd` (-1 =      */
/*  send to all). Sets coord_pending for every neighbour we send to.   */
/* ------------------------------------------------------------------ */
static void enter_coordination(Route *r, int skip_fd, int succ_coord_fd) {
    r->state         = COORDINATION;
    r->succ_coord_fd = succ_coord_fd;
    r->distance      = INF;
    r->succ_fd       = -1;
    memset(r->coord_pending, 0, sizeof(r->coord_pending));

    for (int k = 0; k < node.neighbor_count; k++) {
        if (node.neighbors[k].fd == skip_fd) continue;
        send_coord(r->dest, node.neighbors[k].fd);
        r->coord_pending[k] = 1;
    }

    if (node.monitoring)
        printf("\n%s[MONITOR]%s Entered COORDINATION for dest %s\n> ",
               MAGENTA, RESET, r->dest);
}

/* ------------------------------------------------------------------ */
/*  Check whether all coord_pending flags are 0 → leave COORDINATION  */
/* ------------------------------------------------------------------ */
static void check_coord_complete(Route *r) {
    if (r->state != COORDINATION) return;

    for (int k = 0; k < node.neighbor_count; k++)
        if (r->coord_pending[k]) return;

    if (r->distance >= INF) {
        /*
         * No alternative found yet. Stay in COORDINATION — a future ROUTE
         * (from a new edge or a recovering path) will call handle_route
         * which will call check_coord_complete again once dist is known.
         */
        if (node.monitoring)
            printf("\n%s[MONITOR]%s COORD round done for %s — no route, staying in COORDINATION\n> ",
                   MAGENTA, RESET, r->dest);
        return;
    }

    /* Found a route — return to FORWARDING */
    r->state = FORWARDING;

    if (node.monitoring)
        printf("\n%s[MONITOR]%s COORD complete for %s dist=%d succ_fd=%d\n> ",
               MAGENTA, RESET, r->dest, r->distance, r->succ_fd);

    int upstream = r->succ_coord_fd;
    r->succ_coord_fd = -1;

    send_route_to_all(r);

    if (upstream != -1)
        send_uncoord(r->dest, upstream);
}

/* ------------------------------------------------------------------ */
/*  on_edge_added                                                       */
/* ------------------------------------------------------------------ */
void on_edge_added(int slot) {
    int fd = node.neighbors[slot].fd;

    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];

        if (rt->state == FORWARDING && rt->distance < INF) {
            /*
             * We have a valid route — advertise it to the new neighbour.
             * They may have a better one and will reply with ROUTE if so.
             */
            send_route_to_fd(rt, fd);

        } else if (rt->state == COORDINATION) {
            /*
             * We are searching for a route. Ask the new neighbour by
             * sending COORD. coord_pending[slot] stays 0 — we are not
             * depending on this neighbour to complete the current round,
             * so their UNCOORD won't block us. If they reply with
             * ROUTE + UNCOORD (rule 2), handle_route will see
             * coord_pending[slot]==0, clear all pending, and resolve.
             */
            send_coord(rt->dest, fd);

        }
        /* FORWARDING dist=INF: entry exists but is unreachable — do nothing.
           A future ROUTE from this neighbour will update it normally. */
    }
}

/* ------------------------------------------------------------------ */
/*  on_edge_removed                                                     */
/* ------------------------------------------------------------------ */
void on_edge_removed(int idx) {
    int fd = node.neighbors[idx].fd;
    char lost_id[4];
    strncpy(lost_id, node.neighbors[idx].id, 4);

    printf("\n%s[SYSTEM]%s Link to node %s (fd %d) removed.\n> ",
           YELLOW, RESET, lost_id, fd);

    /*
     * Process routing BEFORE shrinking the neighbour array so that
     * coord_pending indices still match node.neighbors[].
     * We must skip `fd` in any COORD broadcasts (link is dead).
     */
    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];

        if (rt->state == FORWARDING && rt->succ_fd == fd) {
            /*
             * Lost our forwarding next-hop. Enter COORDINATION.
             * skip_fd=fd so we don't try to write to the dead socket.
             * succ_coord_fd=-1 (failure-triggered, no upstream to notify).
             */
            enter_coordination(rt, fd, -1);

            /* Check immediately — maybe no other neighbours exist */
            int sent_any = 0;
            for (int k = 0; k < node.neighbor_count; k++)
                if (rt->coord_pending[k]) { sent_any = 1; break; }
            if (sent_any)
                check_coord_complete(rt);

        } else if (rt->state == COORDINATION && rt->coord_pending[idx]) {
            /*
             * Already in COORDINATION and we were waiting on this neighbour.
             * They're gone — mark their slot done and re-check.
             */
            rt->coord_pending[idx] = 0;
            check_coord_complete(rt);
        }
        /* All other cases (not our succ, not pending) need no action */
    }

    /* Now remove the neighbour slot */
    close(fd);
    for (int k = idx; k < node.neighbor_count - 1; k++)
        node.neighbors[k] = node.neighbors[k + 1];
    node.neighbor_count--;

    /* Shift coord_pending arrays to match the compacted neighbour list */
    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];
        for (int k = idx; k < node.neighbor_count; k++)
            rt->coord_pending[k] = rt->coord_pending[k + 1];
        rt->coord_pending[node.neighbor_count] = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  handle_route  (spec §2.2)                                          */
/* ------------------------------------------------------------------ */
void handle_route(const char *dest, int n, int nbr_index) {
    if (strcmp(dest, node.id) == 0) return;

    int candidate = n + 1;
    int fd        = node.neighbors[nbr_index].fd;

    Route *r = find_or_create_route(dest);
    if (!r) return;

    if (candidate >= r->distance) return;   /* not better — ignore */

    r->distance = candidate;
    r->succ_fd  = fd;

    if (r->state == FORWARDING) {
        send_route_to_all(r);
        return;
    }

    /* In COORDINATION */
    if (!r->coord_pending[nbr_index]) {
        /*
         * ROUTE from a neighbour not in the current coordination round
         * (e.g. new edge, or a neighbour that replied with UNCOORD already).
         * Clear all pending — nobody else can do better — and resolve now.
         */
        memset(r->coord_pending, 0, sizeof(r->coord_pending));
    }
    /*
     * If coord_pending[nbr_index]==1 the neighbour is part of the round;
     * they will follow up with UNCOORD which handle_uncoord will process.
     * Just store the improved distance and wait.
     */
    check_coord_complete(r);
}

/* ------------------------------------------------------------------ */
/*  handle_coord  (spec §2.3)                                          */
/* ------------------------------------------------------------------ */
void handle_coord(const char *dest, int nbr_index) {
    int fd = node.neighbors[nbr_index].fd;

    Route *r = find_or_create_route(dest);
    if (!r) return;

    if (r->state == COORDINATION) {
        /*
         * Rule 1: already in COORDINATION → reply UNCOORD immediately.
         *
         * If we had sent COORD to this neighbour (coord_pending=1), their
         * COORD back means they entered COORDINATION themselves and will
         * never send us UNCOORD. Treat it as their reply — clear the flag.
         */
        send_uncoord(dest, fd);
        if (r->coord_pending[nbr_index]) {
            r->coord_pending[nbr_index] = 0;
            check_coord_complete(r);
        }
        return;
    }

    /* FORWARDING */

    if (r->distance < INF && r->succ_fd != fd) {
        /*
         * Rule 2: j ≠ succ[t] and we have a valid route not through j.
         * Safe to share — reply with ROUTE + UNCOORD.
         */
        send_route_to_fd(r, fd);
        send_uncoord(dest, fd);
        return;
    }

    /*
     * Rule 3 (and the cycle-prevention fallthrough from rule 2):
     * Either j == succ[t], or our only route goes back through j.
     * Enter COORDINATION. succ_coord_fd = fd (upstream to notify later).
     * Send COORD to everyone including j — they will reply with UNCOORD
     * immediately (rule 1 on their side) since they are already in
     * COORDINATION or will enter it.
     */
    enter_coordination(r, -1, fd);
    check_coord_complete(r);
}

/* ------------------------------------------------------------------ */
/*  handle_uncoord  (spec §2.4)                                        */
/* ------------------------------------------------------------------ */
void handle_uncoord(const char *dest, int nbr_index) {
    Route *r = find_route(dest);
    if (!r || r->state != COORDINATION) return;

    r->coord_pending[nbr_index] = 0;
    check_coord_complete(r);
}
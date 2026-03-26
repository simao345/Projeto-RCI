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
    r->dest[3]        = '\0';
    r->distance       = INF;
    r->succ_fd        = -1;
    r->state          = FORWARDING;
    r->succ_coord_fd  = -1;
    return r;
}

/* index of neighbour with this fd (-1 if not found) */
static int nbr_index_by_fd(int fd) {
    for (int i = 0; i < node.neighbor_count; i++)
        if (node.neighbors[i].fd == fd) return i;
    return -1;
}

/* send "ROUTE dest n\n" to one fd */
void send_route_to_fd(Route *r, int fd) {
    if (r->distance >= INF) return;   /* never advertise infinity */
    char msg[64];
    snprintf(msg, sizeof(msg), "ROUTE %s %d\n", r->dest, r->distance);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s SEND ROUTE %s %d → fd %d\n> ",
               MAGENTA, RESET, r->dest, r->distance, fd);
}

/* send "ROUTE dest n\n" to every neighbour */
void send_route_to_all(Route *r) {
    for (int j = 0; j < node.neighbor_count; j++)
        send_route_to_fd(r, node.neighbors[j].fd);
}

/* send "COORD dest\n" to one fd */
static void send_coord(const char *dest, int fd) {
    char msg[64];
    snprintf(msg, sizeof(msg), "COORD %s\n", dest);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s SEND COORD %s → fd %d\n> ",
               MAGENTA, RESET, dest, fd);
}

/* send "UNCOORD dest\n" to one fd */
static void send_uncoord(const char *dest, int fd) {
    char msg[64];
    snprintf(msg, sizeof(msg), "UNCOORD %s\n", dest);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s SEND UNCOORD %s → fd %d\n> ",
               MAGENTA, RESET, dest, fd);
}

/* ------------------------------------------------------------------ */
/*  Check whether all coord_pending flags are 0 → leave COORDINATION  */
/* ------------------------------------------------------------------ */
static void check_coord_complete(Route *r) {
    if (r->state != COORDINATION) return;

    for (int k = 0; k < node.neighbor_count; k++)
        if (r->coord_pending[k]) return;   /* still waiting */

    /* All neighbours have replied → return to FORWARDING */
    r->state = FORWARDING;

    if (node.monitoring)
        printf("\n%s[MONITOR]%s COORD complete for dest %s. dist=%d succ_fd=%d\n> ",
               MAGENTA, RESET, r->dest, r->distance, r->succ_fd);

    int succ_coord_fd = r->succ_coord_fd;
    r->succ_coord_fd  = -1;

    if (r->distance < INF) {
        /* Found an alternative: advertise it */
        send_route_to_all(r);
    } else {
        /*
         * No alternative found: remove the entry entirely so sr/chat
         * correctly report "no route", then notify upstream.
         */
        char dest_copy[4];
        strncpy(dest_copy, r->dest, 4);

        /* Compact the routing table: remove this entry */
        int idx = (int)(r - node.routing_table);
        for (int k = idx; k < node.route_count - 1; k++)
            node.routing_table[k] = node.routing_table[k + 1];
        node.route_count--;
        r = NULL;   /* pointer now invalid */

        if (node.monitoring)
            printf("\n%s[MONITOR]%s Route to %s removed (unreachable).\n> ",
                   MAGENTA, RESET, dest_copy);

        /* Notify the neighbour that triggered coordination */
        if (succ_coord_fd != -1)
            send_uncoord(dest_copy, succ_coord_fd);
        return;
    }

    /* Notify the neighbour that triggered coordination */
    if (succ_coord_fd != -1)
        send_uncoord(r->dest, succ_coord_fd);
}

/* ------------------------------------------------------------------ */
/*  on_edge_added                                                       */
/*                                                                      */
/*  Called after a new neighbour slot has been added.                   */
/*  spec §2.5: if in FORWARDING state for dest → send ROUTE to new nbr */
/*             if in COORDINATION state for dest → coord_pending for   */
/*             that nbr is 0 (we don't depend on it yet)               */
/* ------------------------------------------------------------------ */
void on_edge_added(int slot) {
    int fd = node.neighbors[slot].fd;

    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];

        if (rt->state == FORWARDING && rt->distance < INF) {
            /* Tell the new neighbour what we know */
            send_route_to_fd(rt, fd);
        }
        /*
         * If we are in COORDINATION for this dest: the new neighbour
         * hasn't been asked to coordinate, so coord_pending[slot] = 0
         * (already 0 since we just added the slot and it was 0-filled).
         * Nothing extra to do.
         */
    }
}

/* ------------------------------------------------------------------ */
/*  on_edge_removed                                                     */
/* ------------------------------------------------------------------ */
void on_edge_removed(int idx) {
    int fd           = node.neighbors[idx].fd;
    char lost_id[4];
    strncpy(lost_id, node.neighbors[idx].id, 4);

    printf("\n%s[SYSTEM]%s Link to node %s (fd %d) removed.\n> ",
           YELLOW, RESET, lost_id, fd);

    /* --- handle routing state for every route that used this fd --- */
    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];

        if (rt->state == FORWARDING && rt->succ_fd == fd) {
            /*
             * spec §2.5: enter COORDINATION.
             * succ_coord[t] := -1  (triggered by link failure, not by a COORD msg)
             * dist[t] := INF, succ[t] := -1
             * Send COORD to every neighbour, set coord_pending[k] := 1 for each.
             */
            rt->state         = COORDINATION;
            rt->succ_coord_fd = -1;   /* failure-triggered */
            rt->distance      = INF;
            rt->succ_fd       = -1;

            memset(rt->coord_pending, 0, sizeof(rt->coord_pending));

            for (int k = 0; k < node.neighbor_count; k++) {
                if (node.neighbors[k].fd == fd) continue; /* skip dead link */
                send_coord(rt->dest, node.neighbors[k].fd);
                rt->coord_pending[k] = 1;
            }

            if (node.monitoring)
                printf("\n%s[MONITOR]%s Entered COORDINATION for dest %s (link failure)\n> ",
                       MAGENTA, RESET, rt->dest);

            /* If we have no other neighbours, coordination is immediately done */
            check_coord_complete(rt);

        } else if (rt->state == COORDINATION) {
            /*
             * spec §2.5: if already in COORDINATION, the removed neighbour
             * no longer matters → mark coord_pending[idx] = 0 and re-check.
             */
            rt->coord_pending[idx] = 0;
            check_coord_complete(rt);
        }
    }

    /* --- remove the neighbour slot --- */
    close(fd);
    for (int k = idx; k < node.neighbor_count - 1; k++)
        node.neighbors[k] = node.neighbors[k + 1];
    node.neighbor_count--;

    /* After shrinking the array, shift coord_pending arrays too */
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
    /* Never accept routes to ourselves */
    if (strcmp(dest, node.id) == 0) return;

    int candidate = n + 1;
    int fd        = node.neighbors[nbr_index].fd;

    Route *r = find_or_create_route(dest);
    if (!r) return;

    /* spec §2.2 rule 1: if candidate < dist[t] → update */
    if (candidate < r->distance) {
        r->distance = candidate;
        r->succ_fd  = fd;

        if (r->state == FORWARDING) {
            /* Advertise to all neighbours */
            send_route_to_all(r);
        }
        /* If in COORDINATION: just store the better distance; will advertise
           when we return to FORWARDING (check_coord_complete). */
    }
    /*
     * If candidate >= dist[t] → spec says ignore.
     * Exception handled in handle_coord: when we are NOT the succ,
     * we reply with our own ROUTE + UNCOORD.  That case is handled there.
     */
}

/* ------------------------------------------------------------------ */
/*  handle_coord  (spec §2.3)                                          */
/* ------------------------------------------------------------------ */
void handle_coord(const char *dest, int nbr_index) {
    int fd = node.neighbors[nbr_index].fd;

    Route *r = find_or_create_route(dest);
    if (!r) return;

    if (r->state == COORDINATION) {
        /* spec §2.3 rule 1: already in coordination → reply UNCOORD immediately */
        send_uncoord(dest, fd);
        return;
    }

    /* r->state == FORWARDING */

    if (r->succ_fd != fd) {
        /*
         * spec §2.3 rule 2: j ≠ succ[t]
         * Reply with our current distance and UNCOORD.
         */
        if (r->distance < INF)
            send_route_to_fd(r, fd);
        send_uncoord(dest, fd);
        return;
    }

    /*
     * spec §2.3 rule 3: j == succ[t]
     * Enter COORDINATION.
     * succ_coord[t] := succ[t]  (the neighbour that sent us COORD)
     * dist[t] := INF, succ[t] := -1
     * Send COORD to every neighbour k, coord_pending[k] := 1.
     */
    r->state         = COORDINATION;
    r->succ_coord_fd = fd;   /* remember who triggered it */
    r->distance      = INF;
    r->succ_fd       = -1;

    memset(r->coord_pending, 0, sizeof(r->coord_pending));

    for (int k = 0; k < node.neighbor_count; k++) {
        send_coord(dest, node.neighbors[k].fd);
        r->coord_pending[k] = 1;
    }

    if (node.monitoring)
        printf("\n%s[MONITOR]%s Entered COORDINATION for dest %s (COORD from fd %d)\n> ",
               MAGENTA, RESET, dest, fd);

    /* Might complete immediately if no neighbours (shouldn't happen, but safe) */
    check_coord_complete(r);
}

/* ------------------------------------------------------------------ */
/*  handle_uncoord  (spec §2.4 – "expedi" in spec = UNCOORD here)     */
/* ------------------------------------------------------------------ */
void handle_uncoord(const char *dest, int nbr_index) {
    Route *r = find_route(dest);
    if (!r) return;

    if (r->state != COORDINATION) return;   /* shouldn't happen */

    /* spec §2.4 rule 1: coord[t,j] := 0 */
    r->coord_pending[nbr_index] = 0;

    /* spec §2.4 rule 2: if all coord[t,k] == 0 → leave COORDINATION */
    check_coord_complete(r);
}
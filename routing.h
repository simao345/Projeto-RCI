#ifndef ROUTING_H
#define ROUTING_H

#define MAX_NEIGHBORS 10
#define MAX_ROUTES    100

typedef enum { FORWARDING, COORDINATION } RouteState;

/* One entry per (local_node, destination) pair */
typedef struct {
    char dest[4];

    /* --- forwarding state --- */
    int  distance;       /* dist[t]  – INF represented as 999 */
    int  succ_fd;        /* succ[t]  – fd of forwarding neighbour, -1 if none */

    /* --- coordination state --- */
    RouteState state;            /* state[t]  */
    int  succ_coord_fd;          /* succ_coord[t] – fd that triggered coordination,
                                    -1 if triggered by link failure to succ */

    /* coord[t,k] per neighbour slot (index matches node.neighbors[]) */
    int  coord_pending[MAX_NEIGHBORS]; /* 1 = coordination ongoing with that nbr */
} Route;

typedef struct {
    int  fd;
    char id[4];
} Neighbor;

typedef struct {
    char net[4];
    char id[4];
    char myIP[16];
    int  myTCP;
    int  is_joined;

    Neighbor neighbors[MAX_NEIGHBORS];
    int      neighbor_count;

    int monitoring;

    Route routing_table[MAX_ROUTES];
    int   route_count;
} NodeState;

extern NodeState node;

/* ---- routing table helpers ---- */
Route *find_route(const char *dest);
Route *find_or_create_route(const char *dest);

/* ---- protocol event handlers ---- */

/* Called when we add a new neighbour (edge added by us or NEIGHBOR received).
   new_nbr_slot = index in node.neighbors[] of the new neighbour. */
void on_edge_added(int new_nbr_slot);

/* Called when a neighbour's fd is detected as closed / manually removed.
   Removes the neighbour from node.neighbors and triggers coordination. */
void on_edge_removed(int nbr_index);

/* Handle incoming ROUTE dest n from neighbour at nbr_index */
void handle_route(const char *dest, int n, int nbr_index);

/* Handle incoming COORD dest from neighbour at nbr_index */
void handle_coord(const char *dest, int nbr_index);

/* Handle incoming UNCOORD dest from neighbour at nbr_index */
void handle_uncoord(const char *dest, int nbr_index);

/* Send ROUTE dest dist[t] to all neighbours (or just one fd) */
void send_route_to_all(Route *r);
void send_route_to_fd(Route *r, int fd);

#endif /* ROUTING_H */
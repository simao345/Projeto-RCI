#ifndef ROUTING_H
#define ROUTING_H

#define MAX_NEIGHBORS 10
#define MAX_ROUTES 100

typedef enum { FORWARDING, COORDINATION } RouteState;

typedef struct {
    char dest[4];
    int neighbor_fd;
    int distance;
    RouteState state;
} Route;

typedef struct {
    int fd;
    char id[4];
} Neighbor;

typedef struct {
    char net[4];
    char id[4];
    char myIP[16];
    int myTCP;
    int is_joined;

    Neighbor neighbors[MAX_NEIGHBORS];
    int neighbor_count;

    int monitoring;

    Route routing_table[MAX_ROUTES];
    int route_count;

} NodeState;

extern NodeState node;

/* routing table management */
void add_route(char *dest_id, int fd);
void clean_routing_table_by_fd(int fd);

void update_routing_table(char *dest_id, int new_dist, int fd, RouteState state);
void update_route_state(char *dest_id, RouteState new_state);

void handle_uncoord(char *dest_id, int fd);
void propagate_route_request(char *dest_id);

/* neighbor management */
void remove_neighbor_by_index(int index);

#endif
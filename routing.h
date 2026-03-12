// routing.h
#ifndef ROUTING_H
#define ROUTING_H

#define INF 1000
#define MAX_NEIGHBORS 10

typedef enum { FORWARDING, COORDINATION } RouteState;

typedef struct {
    char dest[4];
    int neighbor_fd;
    int distance;
    RouteState state;
    int succ_coord;
    int coord[MAX_NEIGHBORS];
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
    Route routing_table[100];
    int route_count;
} NodeState;

extern NodeState node;

// Protótipos de funções
Route* find_route(char *dest_id);
int find_neighbor_index(int fd);
void send_route(char *dest, int dist);
void send_coord(char *dest);
void start_coordination(char *dest, int origin_fd);
void handle_route(char *dest, int dist, int fd);
void handle_uncoord(char *dest, int fd);
void handle_coord(char *dest, int from_fd);
void add_route(char *dest_id, int fd, int dist, RouteState state);

#endif
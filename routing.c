#include "routing.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>

// find_route
Route* find_route(char *dest_id) {
    for (int i = 0; i < node.route_count; i++) {
        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            return &node.routing_table[i];
        }
    }
    return NULL;
}

// find_neighbor_index
int find_neighbor_index(int fd) {
    for (int i = 0; i < node.neighbor_count; i++) {
        if (node.neighbors[i].fd == fd) return i;
    }
    return -1;
}

// send_route
void send_route(char *dest, int dist) {
    char msg[64];
    sprintf(msg, "ROUTE %s %d\n", dest, dist);
    for (int i = 0; i < node.neighbor_count; i++) {
        write(node.neighbors[i].fd, msg, strlen(msg));
    }
}

// send_coord
void send_coord(char *dest) {
    char msg[32];
    sprintf(msg, "COORD %s\n", dest);
    for (int i = 0; i < node.neighbor_count; i++) {
        write(node.neighbors[i].fd, msg, strlen(msg));
    }
}

// start_coordination
void start_coordination(char *dest, int origin_fd) {
    Route *r = find_route(dest);
    if (!r) return;

    r->state = COORDINATION;
    r->distance = INF;
    r->neighbor_fd = -1;
    r->succ_coord = origin_fd;

    for (int i = 0; i < node.neighbor_count; i++) {
        r->coord[i] = 1;
    }

    send_coord(dest);
}

// handle_coord
void handle_coord(char *dest, int from_fd) {
    Route *r = find_route(dest);
    if (!r) return;
    if (r->state == FORWARDING) {
        start_coordination(dest, from_fd);
    }
}

// handle_uncoord
void handle_uncoord(char *dest, int fd) {
    Route *r = find_route(dest);
    if (!r) return;

    int idx = find_neighbor_index(fd);
    if (idx == -1) return;

    r->coord[idx] = 0;

    int finished = 1;
    for (int i = 0; i < node.neighbor_count; i++) {
        if (r->coord[i]) finished = 0;
    }

    if (finished) {
        r->state = FORWARDING;
        send_route(dest, r->distance);
    }
}

// handle_route
void handle_route(char *dest, int dist, int fd) {
    Route *r = find_route(dest);
    int new_dist = dist + 1;

    if (!r) {
        add_route(dest, fd, new_dist, FORWARDING); // ou update_routing_table
        return;
    }

    if (new_dist < r->distance) {
        r->distance = new_dist;
        r->neighbor_fd = fd;
        r->state = FORWARDING;
        send_route(dest, new_dist);
    }
}

void add_route(char *dest_id, int fd, int dist, RouteState state) {
    for (int i = 0; i < node.route_count; i++) {
        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            node.routing_table[i].neighbor_fd = fd;
            node.routing_table[i].distance = dist;
            node.routing_table[i].state = state;
            return;
        }
    }
    if (node.route_count < 100) {
        strcpy(node.routing_table[node.route_count].dest, dest_id);
        node.routing_table[node.route_count].neighbor_fd = fd;
        node.routing_table[node.route_count].distance = dist;
        node.routing_table[node.route_count].state = state;
        node.route_count++;
    }
}
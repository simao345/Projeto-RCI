#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "routing.h"

NodeState node;

/* add route if not exists */
void add_route(char *dest_id, int fd) {

    for (int i = 0; i < node.route_count; i++) {
        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            node.routing_table[i].neighbor_fd = fd;
            return;
        }
    }

    if (node.route_count < MAX_ROUTES) {
        strcpy(node.routing_table[node.route_count].dest, dest_id);
        node.routing_table[node.route_count].neighbor_fd = fd;
        node.routing_table[node.route_count].distance = 99;
        node.routing_table[node.route_count].state = COORDINATION;
        node.route_count++;
    }
}

/* remove routes that depended on a closed socket */
void clean_routing_table_by_fd(int fd) {

    for (int i = 0; i < node.route_count; i++) {

        if (node.routing_table[i].neighbor_fd == fd) {

            for (int j = i; j < node.route_count - 1; j++) {
                node.routing_table[j] = node.routing_table[j + 1];
            }

            node.route_count--;
            i--;
        }
    }
}

/* update routing table entry */
void update_routing_table(char *dest_id, int new_dist, int fd, RouteState state) {

    if (strcmp(dest_id, node.id) == 0) return;

    if (new_dist >= 99) return;  // bloqueia valores inválidos

    for (int i = 0; i < node.route_count; i++) {

        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {

            if (new_dist < node.routing_table[i].distance ||
                node.routing_table[i].state == COORDINATION) {

                node.routing_table[i].distance = new_dist;
                node.routing_table[i].neighbor_fd = fd;
                node.routing_table[i].state = FORWARDING;
            }

            return;
        }
    }

    if (node.route_count < MAX_ROUTES) {

        strcpy(node.routing_table[node.route_count].dest, dest_id);
        node.routing_table[node.route_count].distance = new_dist;
        node.routing_table[node.route_count].neighbor_fd = fd;
        node.routing_table[node.route_count].state = FORWARDING;

        node.route_count++;
    }
}

void update_route_state(char *dest_id, RouteState new_state) {

    for (int i = 0; i < node.route_count; i++) {

        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            node.routing_table[i].state = new_state;
            return;
        }
    }
}

/* handle UNCOORD message */
void handle_uncoord(char *dest_id, int fd) {

    for (int i = 0; i < node.route_count; i++) {

        if (strcmp(node.routing_table[i].dest, dest_id) == 0 &&
            node.routing_table[i].neighbor_fd == fd) {

            node.routing_table[i].state = FORWARDING;
            node.routing_table[i].distance = 99;
        }
    }
}

/* remove neighbor and trigger coordination */
void remove_neighbor_by_index(int index) {

    int fd_to_close = node.neighbors[index].fd;
    char lost_neighbor_id[4];

    strcpy(lost_neighbor_id, node.neighbors[index].id);

    printf("\n[SYSTEM] Connection lost with node %s (fd %d)\n",
           lost_neighbor_id, fd_to_close);

    for (int i = 0; i < node.route_count; i++) {

        if (node.routing_table[i].neighbor_fd == fd_to_close) {

            node.routing_table[i].state = COORDINATION;
            node.routing_table[i].distance = 99;

            char coord_msg[64];
            sprintf(coord_msg, "UNCOORD %s\n", node.routing_table[i].dest);

            for (int j = 0; j < node.neighbor_count; j++) {

                if (node.neighbors[j].fd != fd_to_close) {
                    write(node.neighbors[j].fd,
                          coord_msg,
                          strlen(coord_msg));
                }
            }
        }
    }

    close(fd_to_close);

    for (int i = index; i < node.neighbor_count - 1; i++) {
        node.neighbors[i] = node.neighbors[i + 1];
    }

    node.neighbor_count--;
}

/* propagate route request */
void propagate_route_request(char *dest_id) {

    if (strcmp(dest_id, node.id) == 0) return;

    char msg[64];
    sprintf(msg, "ROUTE %s 99\n", dest_id);

    for (int i = 0; i < node.neighbor_count; i++) {
        write(node.neighbors[i].fd, msg, strlen(msg));
    }
}
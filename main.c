#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "interface.h"
#include "server_udp.h"
#include "network_tcp.h"
#include "colours.h"
#include "routing.h"

#define BUFFER_SIZE 512

char regIP_buf[128];
char *regIP;
int   regUDP;

void sigint_handler(int sig) {
    (void)sig;
    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id);
    exit(0);
}

/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);

    if (argc < 3 || argc > 5) { fprintf(stderr, "Usage: OWR IP TCP [regIP [regUDP]]\n"); exit(1); }

    memset(&node, 0, sizeof(node));
    node.neighbor_count = 0;
    node.route_count    = 0;
    node.monitoring     = 0;

    strcpy(node.myIP, argv[1]);
    node.myTCP = atoi(argv[2]);
    regIP = regIP_buf;
    strncpy(regIP_buf, (argc >= 4) ? argv[3] : "193.136.138.142", 127);
    regUDP = (argc == 5) ? atoi(argv[4]) : 59000;

    int listen_fd = setup_tcp_server(node.myTCP);
    if (listen_fd == -1) exit(1);

    printf("> "); fflush(stdout);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(listen_fd, &rfds);
        int max_fd = listen_fd;

        for (int i = 0; i < node.neighbor_count; i++) {
            FD_SET(node.neighbors[i].fd, &rfds);
            if (node.neighbors[i].fd > max_fd) max_fd = node.neighbors[i].fd;
        }

        if (select(max_fd + 1, &rfds, NULL, NULL, NULL) < 0) continue;

        /* ---- New TCP connections ---- */
        if (FD_ISSET(listen_fd, &rfds)) {
            int new_fd = accept(listen_fd, NULL, NULL);
            if (new_fd != -1) {
                if (node.neighbor_count < MAX_NEIGHBORS) {
                    int slot = node.neighbor_count;
                    node.neighbors[slot].fd = new_fd;
                    strcpy(node.neighbors[slot].id, "???");
                    node.neighbor_count++;
                    /*
                     * Do NOT call on_edge_added yet: we don't know their id
                     * and we haven't exchanged NEIGHBOR.  We wait for the
                     * incoming NEIGHBOR message before syncing routes.
                     */
                    if (node.monitoring) {
                        printf("\n%s[MONITOR]%s New TCP connection (fd %d). Waiting for NEIGHBOR...\n> ",
                               MAGENTA, RESET, new_fd);
                        fflush(stdout);
                    }
                } else {
                    printf("\n%s[ERRO]%s Neighbour limit (%d) reached. Closing new connection.\n> ",
                           RED, RESET, MAX_NEIGHBORS);
                    close(new_fd);
                    fflush(stdout);
                }
            }
        }

        /* ---- User input ---- */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char buf[BUFFER_SIZE], arg_net[20], arg_id[512];
            if (fgets(buf, BUFFER_SIZE, stdin) == NULL) break;
            buf[strcspn(buf, "\n")] = 0;
            int cmd = parse_user_command(buf, arg_net, arg_id);

            switch (cmd) {

            case 1: /* join */
                if (!node.is_joined) {
                    if (register_node(regIP, regUDP, arg_net, arg_id,
                                      node.myIP, node.myTCP) == 0) {
                        strcpy(node.net, arg_net);
                        strcpy(node.id,  arg_id);
                        node.is_joined = 1;
                        printf("%s[OK]%s Registered on net %s with id %s\n",
                               GREEN, RESET, node.net, node.id);
                    } else {
                        printf("%s[ERRO]%s Registration failed.\n", RED, RESET);
                    }
                } else {
                    printf("%s[AVISO]%s Already joined net %s.\n", YELLOW, RESET, node.net);
                }
                break;

            case 2: /* leave */
                if (!node.is_joined) {
                    printf("%s[AVISO]%s Not joined.\n", YELLOW, RESET);
                } else {
                    /* Remove all edges gracefully */
                    while (node.neighbor_count > 0)
                        on_edge_removed(0);

                    unregister_node(regIP, regUDP, node.net, node.id);
                    node.route_count = 0;
                    memset(node.routing_table, 0, sizeof(node.routing_table));
                    memset(node.net, 0, sizeof(node.net));
                    memset(node.id,  0, sizeof(node.id));
                    node.is_joined = 0;
                    printf("%s[OK]%s Left the network.\n", YELLOW, RESET);
                }
                break;

            case 3: /* exit */
                if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id);
                printf("%s[SISTEMA]%s Shutting down.\n", RED, RESET);
                exit(0);

            case 4: /* nodes */
                if (strlen(arg_net) > 0)
                    nodes_query(arg_net, regIP, regUDP);
                else if (node.is_joined)
                    nodes_query(node.net, regIP, regUDP);
                else
                    printf("%s[ERRO]%s Specify net or join first.\n", RED, RESET);
                break;

            case 6: /* add edge (ae) */
                if (!node.is_joined) {
                    printf("%s[ERRO]%s Join first.\n", RED, RESET);
                    break;
                }
                if (strcmp(arg_net, node.id) == 0) {
                    printf("%s[ERRO]%s Cannot connect to self.\n", RED, RESET);
                    break;
                }
                {
                    int already = 0;
                    for (int i = 0; i < node.neighbor_count; i++)
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) { already = 1; break; }
                    if (already) {
                        printf("%s[AVISO]%s Node %s is already a neighbour.\n",
                               YELLOW, RESET, arg_net);
                        break;
                    }

                    char target_ip[16]; int target_tcp;
                    if (get_node_contact(regIP, regUDP, node.net, arg_net,
                                         target_ip, &target_tcp) != 0) {
                        printf("%s[ERRO]%s Could not resolve contact for %s.\n",
                               RED, RESET, arg_net);
                        break;
                    }

                    int fd = setup_tcp_client(target_ip, target_tcp);
                    if (fd == -1) {
                        printf("%s[ERRO]%s TCP connection to %s:%d failed.\n",
                               RED, RESET, target_ip, target_tcp);
                        break;
                    }

                    int slot = node.neighbor_count;
                    node.neighbors[slot].fd = fd;
                    strncpy(node.neighbors[slot].id, arg_net, 3);
                    node.neighbors[slot].id[3] = '\0';
                    node.neighbor_count++;

                    /* Send our identity first */
                    char msg[64];
                    snprintf(msg, sizeof(msg), "NEIGHBOR %s\n", node.id);
                    write(fd, msg, strlen(msg));

                    /* Sync our forwarding routes to the new neighbour */
                    on_edge_added(slot);

                    printf("%s[OK]%s Edge to %s established (fd %d).\n",
                           GREEN, RESET, arg_net, fd);
                }
                break;

            case 7: /* show neighbors (sg) */
                printf("\n%s--- Neighbours of node %s ---%s\n", MAGENTA, node.id, RESET);
                for (int i = 0; i < node.neighbor_count; i++)
                    printf("  id=%-4s  fd=%d\n",
                           node.neighbors[i].id, node.neighbors[i].fd);
                printf("\n");
                break;

            case 8: /* remove edge (re) */
                {
                    int found = -1;
                    for (int i = 0; i < node.neighbor_count; i++)
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) { found = i; break; }
                    if (found == -1) {
                        printf("%s[ERRO]%s No neighbour with id %s.\n", RED, RESET, arg_net);
                    } else {
                        on_edge_removed(found);
                        printf("%s[OK]%s Edge to %s removed.\n", YELLOW, RESET, arg_net);
                    }
                }
                break;

            case 9: /* announce */
                if (!node.is_joined) {
                    printf("%s[ERRO]%s Join first.\n", RED, RESET);
                    break;
                }
                {
                    /*
                     * Create/reset our own route entry with distance 0.
                     * Then broadcast ROUTE <id> 0 to every neighbour.
                     */
                    Route *r = find_or_create_route(node.id);
                    if (r) {
                        r->distance = 0;
                        r->succ_fd  = -1;   /* we are the destination */
                        r->state    = FORWARDING;
                        /* The standard send_route_to_all skips distance>=INF,
                           so call directly: */
                        char msg[64];
                        snprintf(msg, sizeof(msg), "ROUTE %s 0\n", node.id);
                        for (int j = 0; j < node.neighbor_count; j++)
                            write(node.neighbors[j].fd, msg, strlen(msg));
                        printf("%s[OK]%s Announced self to all neighbours.\n", GREEN, RESET);
                    }
                }
                break;

            case 10: /* show routing (sr) dest */
                if (arg_net[0] == '\0') {
                    printf("%s[AVISO]%s Usage: sr <dest>\n", YELLOW, RESET);
                    break;
                }
                if (strcmp(arg_net, node.id) == 0) {
                    printf("\n  dest=%-4s  state=FORWARDING  dist=0  succ=local\n\n",
                           node.id);
                    break;
                }
                {
                    Route *r = find_route(arg_net);
                    if (!r) {
                        printf("\n%s[ERRO]%s No route to %s.\n\n", RED, RESET, arg_net);
                    } else if (r->state == FORWARDING) {
                        printf("\n  dest=%-4s  state=%sFORWARDING%s  dist=%d  succ_fd=%d\n\n",
                               r->dest, GREEN, RESET, r->distance, r->succ_fd);
                    } else {
                        printf("\n  dest=%-4s  state=%sCOORDINATION%s  dist=%d  succ_fd=%d\n\n",
                               r->dest, YELLOW, RESET, r->distance, r->succ_fd);
                    }
                }
                break;

            case 11: /* start monitor */
                node.monitoring = 1;
                printf("Monitoring ON.\n");
                break;

            case 12: /* end monitor */
                node.monitoring = 0;
                printf("Monitoring OFF.\n");
                break;

            case 13: /* message dest text */
                {
                    Route *r = find_route(arg_net);
                    if (!r || r->state != FORWARDING || r->distance >= 999) {
                        printf("%s[ERRO]%s No forwarding route to %s.\n",
                               RED, RESET, arg_net);
                        break;
                    }
                    char chat[1024];
                    snprintf(chat, sizeof(chat), "CHAT %s %s %s\n",
                             node.id, arg_net, arg_id);
                    write(r->succ_fd, chat, strlen(chat));
                }
                break;

            /* dj / dae (direct join / direct add edge) could go here */

            default:
                break;
            }

            printf("> "); fflush(stdout);
        }

        /* ---- Messages from neighbours ---- */
        for (int i = 0; i < node.neighbor_count; i++) {
            if (!FD_ISSET(node.neighbors[i].fd, &rfds)) continue;

            int  current_fd = node.neighbors[i].fd;
            char buf[BUFFER_SIZE];
            ssize_t n = read(current_fd, buf, sizeof(buf) - 1);

            if (n <= 0) {
                /* Connection closed / error */
                on_edge_removed(i);
                i--;   /* array shrunk */
                printf("> "); fflush(stdout);
                continue;
            }

            buf[n] = '\0';

            /* Split on '\n' – a single read may contain multiple messages */
            char *line = buf;
            char *nl;
            while ((nl = strchr(line, '\n')) != NULL) {
                *nl = '\0';

                if (node.monitoring) {
                    printf("\n%s[MONITOR]%s fd %d: %s\n> ", MAGENTA, RESET, current_fd, line);
                    fflush(stdout);
                }

                char cmd[32] = {0};
                sscanf(line, "%31s", cmd);

                if (strcmp(cmd, "NEIGHBOR") == 0) {
                    /* ---- NEIGHBOR id ---- */
                    char nbr_id[4] = {0};
                    if (sscanf(line, "%*s %3s", nbr_id) == 1) {
                        strncpy(node.neighbors[i].id, nbr_id, 3);
                        node.neighbors[i].id[3] = '\0';

                        /*
                         * Now that we know their id, exchange routes.
                         * on_edge_added sends our FORWARDING routes to them;
                         * they will do the same to us via their own on_edge_added.
                         * We do NOT create a 1-hop route to them here:
                         * a direct neighbour only appears in the routing table
                         * once they announce themselves with "announce".
                         */
                        on_edge_added(i);
                    }

                } else if (strcmp(cmd, "ROUTE") == 0) {
                    /* ---- ROUTE dest n ---- */
                    char dest[4] = {0}; int dist_recv = 0;
                    if (sscanf(line, "%*s %3s %d", dest, &dist_recv) == 2)
                        handle_route(dest, dist_recv, i);

                } else if (strcmp(cmd, "COORD") == 0) {
                    /* ---- COORD dest ---- */
                    char dest[4] = {0};
                    if (sscanf(line, "%*s %3s", dest) == 1)
                        handle_coord(dest, i);

                } else if (strcmp(cmd, "UNCOORD") == 0) {
                    /* ---- UNCOORD dest ---- */
                    char dest[4] = {0};
                    if (sscanf(line, "%*s %3s", dest) == 1)
                        handle_uncoord(dest, i);

                } else if (strcmp(cmd, "CHAT") == 0) {
                    /* ---- CHAT origin dest text ---- */
                    char origin[4] = {0}, dest[4] = {0}, text[256] = {0};
                    sscanf(line, "%*s %3s %3s %255[^\r]", origin, dest, text);

                    if (strcmp(dest, node.id) == 0) {
                        printf("\n%s[CHAT]%s from %s: %s\n> ",
                               GREEN, RESET, origin, text);
                        fflush(stdout);
                    } else {
                        /* Forward */
                        Route *r = find_route(dest);
                        if (r && r->state == FORWARDING && r->succ_fd != -1) {
                            char fwd[1024];
                            snprintf(fwd, sizeof(fwd), "CHAT %s %s %s\n",
                                     origin, dest, text);
                            write(r->succ_fd, fwd, strlen(fwd));
                            if (node.monitoring) {
                                printf("\n%s[MONITOR]%s Forwarding CHAT to fd %d\n> ",
                                       MAGENTA, RESET, r->succ_fd);
                                fflush(stdout);
                            }
                        } else {
                            printf("\n%s[AVISO]%s CHAT for %s: no route.\n> ",
                                   YELLOW, RESET, dest);
                            fflush(stdout);
                        }
                    }
                }

                line = nl + 1;
            }
        }
    }

    return 0;
}
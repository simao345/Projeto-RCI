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
    /* Remove o registo no servidor de nós antes de terminar */
    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id);
    exit(0);
}

/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);

    if (argc < 3 || argc > 5) { fprintf(stderr, "Uso: OWR IP TCP [regIP [regUDP]]\n"); exit(1); }

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

        /* ---- Novas ligações TCP de entrada ---- */
        if (FD_ISSET(listen_fd, &rfds)) {
            int new_fd = accept(listen_fd, NULL, NULL);
            if (new_fd != -1) {
                if (node.neighbor_count < MAX_NEIGHBORS) {
                    int slot = node.neighbor_count;
                    node.neighbors[slot].fd = new_fd;
                    strcpy(node.neighbors[slot].id, "???");
                    node.neighbor_count++;
                    /*
                     * on_edge_added não é invocado ainda: o identificador do
                     * vizinho é desconhecido até à recepção da mensagem NEIGHBOR.
                     * A sincronização de rotas fica suspensa até esse momento.
                     */
                    if (node.monitoring) {
                        printf("\n%s[MONITOR]%s Nova ligação TCP (fd %d). À espera de NEIGHBOR...\n> ",
                               MAGENTA, RESET, new_fd);
                        fflush(stdout);
                    }
                } else {
                    printf("\n%s[ERRO]%s Limite de vizinhos (%d) atingido. Ligação recusada.\n> ",
                           RED, RESET, MAX_NEIGHBORS);
                    close(new_fd);
                    fflush(stdout);
                }
            }
        }

        /* ---- Comandos do utilizador ---- */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char buf[BUFFER_SIZE], arg_net[20], arg_id[512];
            if (fgets(buf, BUFFER_SIZE, stdin) == NULL) break;
            buf[strcspn(buf, "\n")] = 0;
            int cmd = parse_user_command(buf, arg_net, arg_id);

            switch (cmd) {

            case 1: /* join — adesão à rede com registo no servidor de nós */
                if (!node.is_joined) {
                    if (register_node(regIP, regUDP, arg_net, arg_id,
                                      node.myIP, node.myTCP) == 0) {
                        strcpy(node.net, arg_net);
                        strcpy(node.id,  arg_id);
                        node.is_joined = 1;
                        printf("%s[OK]%s Registado na rede %s com id %s\n",
                               GREEN, RESET, node.net, node.id);
                    } else {
                        printf("%s[ERRO]%s Falha no registo no servidor de nós.\n", RED, RESET);
                    }
                } else {
                    printf("%s[AVISO]%s Já pertence à rede %s.\n", YELLOW, RESET, node.net);
                }
                break;

            case 2: /* leave — saída da rede */
                if (!node.is_joined) {
                    printf("%s[AVISO]%s O nó não pertence a nenhuma rede.\n", YELLOW, RESET);
                } else {
                    /*
                     * Fecha todos os sockets directamente — não faz sentido
                     * desencadear rondas de coordenação que nunca serão concluídas.
                     * Os vizinhos detectam o fecho via read() com retorno ≤ 0
                     * e tratam a situação com on_edge_removed do seu lado.
                     */
                    for (int i = 0; i < node.neighbor_count; i++)
                        close(node.neighbors[i].fd);

                    unregister_node(regIP, regUDP, node.net, node.id);
                    node.neighbor_count = 0;
                    memset(node.neighbors, 0, sizeof(node.neighbors));
                    node.route_count = 0;
                    memset(node.routing_table, 0, sizeof(node.routing_table));
                    memset(node.net, 0, sizeof(node.net));
                    memset(node.id,  0, sizeof(node.id));
                    node.is_joined = 0;
                    printf("%s[OK]%s Nó removido da rede.\n", YELLOW, RESET);
                }
                break;

            case 3: /* exit — encerramento da aplicação */
                if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id);
                printf("%s[SISTEMA]%s A encerrar.\n", RED, RESET);
                exit(0);

            case 4: /* nodes — lista os nós da rede */
                if (strlen(arg_net) > 0)
                    nodes_query(arg_net, regIP, regUDP);
                else if (node.is_joined)
                    nodes_query(node.net, regIP, regUDP);
                else
                    printf("%s[ERRO]%s Indique a rede ou execute join primeiro.\n", RED, RESET);
                break;

            case 6: /* ae — estabelece edge com o nó indicado */
                if (!node.is_joined) {
                    printf("%s[ERRO]%s Execute join primeiro.\n", RED, RESET);
                    break;
                }
                if (strcmp(arg_net, node.id) == 0) {
                    printf("%s[ERRO]%s Não é possível ligar o nó a si próprio.\n", RED, RESET);
                    break;
                }
                {
                    int already = 0;
                    for (int i = 0; i < node.neighbor_count; i++)
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) { already = 1; break; }
                    if (already) {
                        printf("%s[AVISO]%s O nó %s já é vizinho.\n", YELLOW, RESET, arg_net);
                        break;
                    }

                    char target_ip[16]; int target_tcp;
                    if (get_node_contact(regIP, regUDP, node.net, arg_net,
                                         target_ip, &target_tcp) != 0) {
                        printf("%s[ERRO]%s Não foi possível obter o contacto do nó %s.\n",
                               RED, RESET, arg_net);
                        break;
                    }

                    int fd = setup_tcp_client(target_ip, target_tcp);
                    if (fd == -1) {
                        printf("%s[ERRO]%s Falha na ligação TCP a %s:%d.\n",
                               RED, RESET, target_ip, target_tcp);
                        break;
                    }

                    int slot = node.neighbor_count;
                    node.neighbors[slot].fd = fd;
                    strncpy(node.neighbors[slot].id, arg_net, 3);
                    node.neighbors[slot].id[3] = '\0';
                    node.neighbor_count++;

                    /* Envia o identificador local antes de sincronizar rotas */
                    char msg[64];
                    snprintf(msg, sizeof(msg), "NEIGHBOR %s\n", node.id);
                    write(fd, msg, strlen(msg));

                    /* Sincroniza as rotas de expedição com o novo vizinho */
                    on_edge_added(slot);

                    printf("%s[OK]%s Edge com %s estabelecido (fd %d).\n",
                           GREEN, RESET, arg_net, fd);
                }
                break;

            case 7: /* sg — apresenta a lista de vizinhos */
                printf("\n%s--- Vizinhos do nó %s ---%s\n", MAGENTA, node.id, RESET);
                for (int i = 0; i < node.neighbor_count; i++)
                    printf("  id=%-4s  fd=%d\n",
                           node.neighbors[i].id, node.neighbors[i].fd);
                printf("\n");
                break;

            case 8: /* re — remove edge com o nó indicado */
                {
                    int found = -1;
                    for (int i = 0; i < node.neighbor_count; i++)
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) { found = i; break; }
                    if (found == -1) {
                        printf("%s[ERRO]%s Nenhum vizinho com id %s.\n", RED, RESET, arg_net);
                    } else {
                        on_edge_removed(found);
                        printf("%s[OK]%s Edge com %s removido.\n", YELLOW, RESET, arg_net);
                    }
                }
                break;

            case 9: /* announce — anuncia o nó na rede sobreposta */
                if (!node.is_joined) {
                    printf("%s[ERRO]%s Execute join primeiro.\n", RED, RESET);
                    break;
                }
                {
                    /*
                     * Cria ou repõe a entrada do próprio nó com distância 0.
                     * send_route_to_all ignora distâncias >= INF, pelo que a
                     * mensagem ROUTE é construída e enviada directamente.
                     */
                    Route *r = find_or_create_route(node.id);
                    if (r) {
                        r->distance = 0;
                        r->succ_fd  = -1;   /* o próprio nó é o destino */
                        r->state    = FORWARDING;
                        char msg[64];
                        snprintf(msg, sizeof(msg), "ROUTE %s 0\n", node.id);
                        for (int j = 0; j < node.neighbor_count; j++)
                            write(node.neighbors[j].fd, msg, strlen(msg));
                        printf("%s[OK]%s Nó anunciado a todos os vizinhos.\n", GREEN, RESET);
                    }
                }
                break;

            case 10: /* sr — apresenta o estado de encaminhamento para um destino */
                if (arg_net[0] == '\0') {
                    printf("%s[AVISO]%s Uso: sr <dest>\n", YELLOW, RESET);
                    break;
                }
                if (strcmp(arg_net, node.id) == 0) {
                    /* O próprio nó é sempre alcançável com distância 0 */
                    printf("\n  dest=%-4s  state=FORWARDING  dist=0  succ=local\n\n",
                           node.id);
                    break;
                }
                {
                    Route *r = find_route(arg_net);
                    if (!r) {
                        printf("\n%s[ERRO]%s Rota para %s desconhecida.\n\n", RED, RESET, arg_net);
                    } else if (r->state == FORWARDING) {
                        printf("\n  dest=%-4s  state=%sFORWARDING%s  dist=%d  succ_fd=%d\n\n",
                               r->dest, GREEN, RESET, r->distance, r->succ_fd);
                    } else {
                        printf("\n  dest=%-4s  state=%sCOORDINATION%s  dist=%d  succ_fd=%d\n\n",
                               r->dest, YELLOW, RESET, r->distance, r->succ_fd);
                    }
                }
                break;

            case 11: /* sm — activa a monitorização de mensagens de encaminhamento */
                node.monitoring = 1;
                printf("Monitorização activada.\n");
                break;

            case 12: /* em — desactiva a monitorização */
                node.monitoring = 0;
                printf("Monitorização desactivada.\n");
                break;

            case 13: /* m — envia mensagem de chat ao nó destino */
                {
                    Route *r = find_route(arg_net);
                    if (!r || r->state != FORWARDING || r->distance >= 999) {
                        printf("%s[ERRO]%s Sem rota de expedição para %s.\n",
                               RED, RESET, arg_net);
                        break;
                    }
                    char chat[1024];
                    snprintf(chat, sizeof(chat), "CHAT %s %s %s\n",
                             node.id, arg_net, arg_id);
                    write(r->succ_fd, chat, strlen(chat));
                }
                break;

            case 14: /* dj — adesão directa à rede sem registo no servidor de nós */
                if (node.is_joined) {
                    printf("%s[AVISO]%s Já pertence à rede %s.\n", YELLOW, RESET, node.net);
                    break;
                }
                strcpy(node.net, arg_net);
                strcpy(node.id,  arg_id);
                node.is_joined = 1;
                printf("%s[OK]%s Adesão directa à rede %s com id %s (sem registo no servidor).\n",
                       GREEN, RESET, node.net, node.id);
                break;

            case 15: /* dae — estabelece edge directamente sem consultar o servidor de nós */
                if (!node.is_joined) {
                    printf("%s[ERRO]%s Execute join primeiro.\n", RED, RESET);
                    break;
                }
                {
                    /* arg_net = id do vizinho, arg_id = "IP TCP" */
                    char dae_ip[16] = {0}; int dae_tcp = 0;
                    if (sscanf(arg_id, "%15s %d", dae_ip, &dae_tcp) != 2) {
                        printf("%s[ERRO]%s Uso: dae <id> <IP> <TCP>\n", RED, RESET);
                        break;
                    }
                    if (strcmp(arg_net, node.id) == 0) {
                        printf("%s[ERRO]%s Não é possível ligar o nó a si próprio.\n", RED, RESET);
                        break;
                    }
                    int already = 0;
                    for (int i = 0; i < node.neighbor_count; i++)
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) { already = 1; break; }
                    if (already) {
                        printf("%s[AVISO]%s O nó %s já é vizinho.\n", YELLOW, RESET, arg_net);
                        break;
                    }
                    if (node.neighbor_count >= MAX_NEIGHBORS) {
                        printf("%s[ERRO]%s Limite de vizinhos atingido.\n", RED, RESET);
                        break;
                    }

                    int fd = setup_tcp_client(dae_ip, dae_tcp);
                    if (fd == -1) {
                        printf("%s[ERRO]%s Falha na ligação TCP a %s:%d.\n",
                               RED, RESET, dae_ip, dae_tcp);
                        break;
                    }

                    int slot = node.neighbor_count;
                    node.neighbors[slot].fd = fd;
                    strncpy(node.neighbors[slot].id, arg_net, 3);
                    node.neighbors[slot].id[3] = '\0';
                    node.neighbor_count++;

                    char msg[64];
                    snprintf(msg, sizeof(msg), "NEIGHBOR %s\n", node.id);
                    write(fd, msg, strlen(msg));

                    on_edge_added(slot);

                    printf("%s[OK]%s Edge directo com %s (%s:%d) estabelecido (fd %d).\n",
                           GREEN, RESET, arg_net, dae_ip, dae_tcp, fd);
                }
                break;

            default:
                break;
            }

            printf("> "); fflush(stdout);
        }

        /* ---- Mensagens recebidas dos vizinhos ---- */
        for (int i = 0; i < node.neighbor_count; i++) {
            if (!FD_ISSET(node.neighbors[i].fd, &rfds)) continue;

            int  current_fd = node.neighbors[i].fd;
            char buf[BUFFER_SIZE];
            ssize_t n = read(current_fd, buf, sizeof(buf) - 1);

            if (n <= 0) {
                /* Ligação fechada ou erro — remove o vizinho e despoleta coordenação */
                on_edge_removed(i);
                i--;   /* o array foi comprimido */
                printf("> "); fflush(stdout);
                continue;
            }

            buf[n] = '\0';

            /*
             * Um único read() pode conter várias mensagens; divide pelo
             * separador '\n' e processa cada linha individualmente.
             */
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
                         * Identificador do vizinho agora conhecido — sincroniza
                         * as rotas de expedição. Não se cria rota directa para
                         * o vizinho: este só surge na tabela após announce.
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
                    /* ---- CHAT origem destino texto ---- */
                    char origin[4] = {0}, dest[4] = {0}, text[256] = {0};
                    sscanf(line, "%*s %3s %3s %255[^\r]", origin, dest, text);

                    if (strcmp(dest, node.id) == 0) {
                        /* Mensagem destinada a este nó */
                        printf("\n%s[CHAT]%s de %s: %s\n> ",
                               GREEN, RESET, origin, text);
                        fflush(stdout);
                    } else {
                        /* Reencaminha pelo vizinho de expedição */
                        Route *r = find_route(dest);
                        if (r && r->state == FORWARDING && r->succ_fd != -1) {
                            char fwd[1024];
                            snprintf(fwd, sizeof(fwd), "CHAT %s %s %s\n",
                                     origin, dest, text);
                            write(r->succ_fd, fwd, strlen(fwd));
                            if (node.monitoring) {
                                printf("\n%s[MONITOR]%s CHAT reencaminhado para fd %d\n> ",
                                       MAGENTA, RESET, r->succ_fd);
                                fflush(stdout);
                            }
                        } else {
                            printf("\n%s[AVISO]%s CHAT para %s: sem rota.\n> ",
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
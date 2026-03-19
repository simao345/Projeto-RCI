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

#define BUFFER_SIZE 256

char regIP_buf[128];  
char *regIP; 
int regUDP;   

void sigint_handler(int sig) { 
    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id); 
    exit(0); 
}

// --- MAIN ---

int main(int argc, char *argv[]) { 
    signal(SIGINT, sigint_handler); 

    if (argc < 3 || argc > 5) exit(1); 
    
    memset(&node, 0, sizeof(node)); 
    node.neighbor_count = 0;
    node.route_count = 0;
    node.monitoring = 0;

    strcpy(node.myIP, argv[1]); 
    node.myTCP = atoi(argv[2]); 
    regIP = regIP_buf; 
    strncpy(regIP_buf, (argc >= 4) ? argv[3] : "193.136.138.142", 127);
    regUDP = (argc == 5) ? atoi(argv[4]) : 59000; 

    int listen_fd = setup_tcp_server(node.myTCP); 
    if (listen_fd == -1) exit(1); 

    while (1) { 
        fd_set rfds; 
        FD_ZERO(&rfds); 
        FD_SET(STDIN_FILENO, &rfds); 
        FD_SET(listen_fd, &rfds); 
        int max_fd = (listen_fd > STDIN_FILENO) ? listen_fd : STDIN_FILENO; 

        // Adicionar todos os vizinhos do array ao select
        for (int i = 0; i < node.neighbor_count; i++) {
            FD_SET(node.neighbors[i].fd, &rfds);
            if (node.neighbors[i].fd > max_fd) max_fd = node.neighbors[i].fd;
        }
         
        if (select(max_fd + 1, &rfds, NULL, NULL, NULL) < 0) continue; 

        // Novas ligações TCP
        if (FD_ISSET(listen_fd, &rfds)) { 
            int new_fd = accept(listen_fd, NULL, NULL); 
            if (new_fd != -1) { 
                if (node.neighbor_count < MAX_NEIGHBORS) {
                    node.neighbors[node.neighbor_count].fd = new_fd;
                    strcpy(node.neighbors[node.neighbor_count].id, "???"); 
                    node.neighbor_count++;
                    // Mensagem de sistema (Monitorização)
                    if (node.monitoring) {
                        printf("\n%s[MONITOR]%s Nova ligação TCP detetada (FD %d). A aguardar NEIGHBOR...\n> ", MAGENTA, RESET, new_fd);
                        fflush(stdout);
                    }
                } else {
                    printf("\n%s[ERRO]%s Limite de vizinhos (%d) atingido. Ligação recusada.\n> ", RED, RESET, MAX_NEIGHBORS);
                    close(new_fd);
                    fflush(stdout);
                }
            } 
        }

        // Input do utilizador
        if (FD_ISSET(STDIN_FILENO, &rfds)) { 
            char buffer[BUFFER_SIZE], arg_net[20], arg_id[512]; 
            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break; 
            buffer[strcspn(buffer, "\n")] = 0; 
            int cmd_type = parse_user_command(buffer, arg_net, arg_id); 

            switch (cmd_type) {
                case 1: // JOIN
                    if (!node.is_joined) {
                        if (register_node(regIP, regUDP, arg_net, arg_id, node.myIP, node.myTCP) == 0) { 
                            strcpy(node.net, arg_net);
                            strcpy(node.id, arg_id);
                            node.is_joined = 1;
                            update_routing_table(node.id, 0, -1, FORWARDING);
                            printf("%s[SUCESSO]%s Registado na rede %s com ID %s\n", GREEN, RESET, node.net, node.id);
                        } else {
                            printf("%s[ERRO]%s Falha ao registar no servidor UDP.\n", RED, RESET);
                        }
                    } else {
                        printf("%s[AVISO]%s Já estás registado na rede %s.\n", YELLOW, RESET, node.net);
                    }
                    break; 

                case 2: // LEAVE
                    if (!node.is_joined) {
                        printf("%s[AVISO]%s O nó não está registado em nenhuma rede.\n", YELLOW, RESET);
                    } 
                    else {
                        if (unregister_node(regIP, regUDP, node.net, node.id) == 0) {
                            // fechar sockets dos vizinhos
                            for(int i = 0; i < node.neighbor_count; i++) close(node.neighbors[i].fd);

                            // limpar estruturas
                            node.neighbor_count = 0;
                            memset(node.neighbors, 0, sizeof(node.neighbors));
                            node.route_count = 0;
                            memset(node.routing_table, 0, sizeof(node.routing_table));
                            memset(node.net, 0, sizeof(node.net));
                            memset(node.id, 0, sizeof(node.id));
                            node.is_joined = 0;

                            printf("%s[SISTEMA]%s Nó saiu da rede e estado foi limpo corretamente.\n", YELLOW, RESET);
                        }
                    }
                    break;

                case 3: // EXIT
                    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id); 
                    printf("%s[SISTEMA]%s A encerrar nó...\n", RED, RESET);
                    exit(0); 

                case 4: // NODES
                    if (strlen(arg_net) > 0) nodes_query(arg_net, regIP, regUDP); 
                    else if (node.is_joined) nodes_query(node.net, regIP, regUDP); 
                    else printf("%s[ERRO]%s Indica a rede (ex: nodes 039) ou regista-te primeiro.\n", RED, RESET); 
                    break; 
                                                                                            /*case 5: // DIRECT
                                                                                                if (node.next_fd != -1) printf("Erro: Já tens ligação de saída.\n"); 
                                                                                                else { 
                                                                                                    int fd = setup_tcp_client(arg_net, atoi(arg_id)); 
                                                                                                    if (fd != -1) node.next_fd = fd; 
                                                                                                } 
                                                                                                break;*/
                case 6: // ADD EDGE (ae)
                    if (node.is_joined) { 
                        if (strcmp(arg_net, node.id) == 0) {
                            printf("%s[ERRO]%s Não podes ligar um nó a si próprio (%s).\n", RED, RESET, node.id);
                            break; 
                        }

                        int ja_e_vizinho = 0;
                        for (int i = 0; i < node.neighbor_count; i++) {
                            if (strcmp(node.neighbors[i].id, arg_net) == 0) { ja_e_vizinho = 1; break; }
                        }

                        if (ja_e_vizinho) {
                            printf("%s[AVISO]%s O nó %s já é teu vizinho.\n", YELLOW, RESET, arg_net);
                            break;
                        }

                        char target_ip[16]; int target_tcp; 
                        if (get_node_contact(regIP, regUDP, node.net, arg_net, target_ip, &target_tcp) == 0) { 
                            int fd = setup_tcp_client(target_ip, target_tcp); 
                            if (fd != -1) { 
                                node.neighbors[node.neighbor_count].fd = fd;
                                strcpy(node.neighbors[node.neighbor_count].id, arg_net);
                                node.neighbor_count++;
                                update_routing_table(arg_net, 1, fd, COORDINATION);

                                char msg[64]; sprintf(msg, "NEIGHBOR %s\n", node.id); 
                                write(fd, msg, strlen(msg));

                                for (int r = 0; r < node.route_count; r++) {
                                    if (node.routing_table[r].state == FORWARDING) {
                                        char sync_msg[64];
                                        sprintf(sync_msg, "ROUTE %s %d\n", node.routing_table[r].dest, node.routing_table[r].distance);
                                        write(fd, sync_msg, strlen(sync_msg));
                                    }
                                }
                                printf("%s[SUCESSO]%s Ligação ao nó %s estabelecida (FD %d).\n", GREEN, RESET, arg_net, fd);
                            } else {
                                printf("%s[ERRO]%s Não foi possível abrir socket TCP para %s:%d.\n", RED, RESET, target_ip, target_tcp);
                            }
                        } 
                    } else {
                        printf("%s[ERRO]%s Faz 'join' antes de tentar adicionar vizinhos.\n", RED, RESET);
                    }
                    break;
                case 7: // SHOW NEIGHBORS (sg)
                    printf("\n%s%s--- LISTA DE VIZINHOS (Nó %s) ---\n", BOLD, MAGENTA, node.id);
                    printf("%-10s %-9s %s\n", "ID NÓ", "SOCKET FD", RESET);
                    printf("--------- ---------\n");

                    for (int i = 0; i < node.neighbor_count; i++) {
                        // IDs em Verde, FDs em Ciano
                        printf("%s%-10s%s %s%-10d%s\n", 
                            GREEN, node.neighbors[i].id, RESET, 
                            CYAN, node.neighbors[i].fd, RESET);
                    }
                    printf("\n");
                    break; 
                case 8: // re (Remove Edge)
                    for (int i = 0; i < node.neighbor_count; i++) {
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) {
                            int fd_removido = node.neighbors[i].fd;

                            if (node.monitoring) {
                                printf("\n%s[MONITOR]%s A remover vizinho %s (FD %d) e a propagar COORD...\n", MAGENTA, RESET, arg_net, fd_removido);
                            }

                            for (int r = 0; r < node.route_count; r++) {
                                if (node.routing_table[r].neighbor_fd == fd_removido) {
                                    char msg_coord[64];
                                    sprintf(msg_coord, "COORD %s\n", node.routing_table[r].dest);
                                    
                                    for (int j = 0; j < node.neighbor_count; j++) {
                                        if (node.neighbors[j].fd != fd_removido) {
                                            write(node.neighbors[j].fd, msg_coord, strlen(msg_coord));
                                        }
                                    }
                                    node.routing_table[r].state = COORDINATION;
                                }
                            }

                            close(fd_removido);
                            for (int k = i; k < node.neighbor_count - 1; k++) {
                                node.neighbors[k] = node.neighbors[k+1];
                            }
                            node.neighbor_count--;
                            printf("%s[SISTEMA]%s Ligação com o nó %s removida com sucesso.\n", YELLOW, RESET, arg_net);
                            break;
                        }
                    }
                    break;
                case 9: // announce
                    {
                        char msg[64];
                        sprintf(msg, "ROUTE %s 0\n", node.id);
                        
                        // NOVIDADE: Adiciona-te a ti próprio à tua tabela (Distância 0)
                        // Isso ajuda a lógica interna a saber que esta rota existe.
                        update_routing_table(node.id, 0, -1, FORWARDING); 

                        for (int j = 0; j < node.neighbor_count; j++) {
                            write(node.neighbors[j].fd, msg, strlen(msg));
                        }
                    }
                    break;
                case 10: // sr ID
                    // 1. Verificação de argumento: Se arg_net estiver vazio, não imprime a tabela
                    if (arg_net[0] == '\0') {
                        printf("%s[AVISO]%s Deves indicar o ID do nó (ex: sr 10).\n", YELLOW, RESET);
                        break;
                    }

                    printf("\n%s%s--- ENCAMINHAMENTO PARA NÓ %s (De: %s) ---%s\n", BOLD, CYAN, arg_net, node.id, RESET);
                    printf("%-10s %-15s %-10s %-15s\n", "DESTINO", "ESTADO", "SALTOS", "VIZINHO (FD)");
                    printf("---------- --------------- ---------- ---------------\n");
                    
                    int encontrou = 0;

                    // 2. Se o destino for o próprio nó, tratamos logo aqui
                    if (strcmp(arg_net, node.id) == 0) {
                        printf("%-10s %s%-15s%s %-10d %-15s\n", node.id, GREEN, "EXPEDIÇÃO", RESET, 0, "local");
                        encontrou = 1;
                    } 
                    else {
                        // 3. Procura apenas o ID específico na tabela
                        for (int r = 0; r < node.route_count; r++) {
                            if (strcmp(node.routing_table[r].dest, arg_net) == 0) {
                                char *color = (node.routing_table[r].state == FORWARDING) ? GREEN : RED;
                                char *state_str = (node.routing_table[r].state == FORWARDING) ? "EXPEDIÇÃO" : "COORDENAÇÃO";

                                printf("%-10s %s%-15s%s %-10d %-15d\n", 
                                    node.routing_table[r].dest, 
                                    color, state_str, RESET,
                                    node.routing_table[r].distance, 
                                    node.routing_table[r].neighbor_fd);
                                
                                encontrou = 1;
                                break; // Encontrou o ID pedido, pára a pesquisa imediatamente
                            }
                        }
                    }

                    if (!encontrou) {
                        printf("\n%s[ERRO]%s Rota para o nó %s desconhecida.\n", RED, RESET, arg_net);
                    }
                    printf("\n");
                    break;
                case 11: // start monitor (sm)
                    node.monitoring = 1;
                    printf("Monitorização ativada.\n");
                    break;
                case 12: // end monitor (em)
                    node.monitoring = 0;
                    printf("Monitorização desativada.\n");
                    break;
                case 13: // CHAT
                    {
                        int found_fd = -1;
                        for (int i = 0; i < node.route_count; i++) {
                            if (strcmp(node.routing_table[i].dest, arg_net) == 0) {
                                found_fd = node.routing_table[i].neighbor_fd;
                                break;
                            }
                        }
                        if (found_fd != -1) {
                            char chat_msg[1024];
                            sprintf(chat_msg, "CHAT %s %s %s\n", node.id, arg_net, arg_id);
                            write(found_fd, chat_msg, strlen(chat_msg));
                        }
                    }
                    break;
            } 
            printf("> "); fflush(stdout); 
        } 

        // TRATAR MENSAGENS DE TODOS OS VIZINHOS NO ARRAY
        for (int i = 0; i < node.neighbor_count; i++) {
            if (FD_ISSET(node.neighbors[i].fd, &rfds)) {
                char buffer[BUFFER_SIZE];
                int current_fd = node.neighbors[i].fd;
                ssize_t n = read(current_fd, buffer, sizeof(buffer) - 1);

                if (n <= 0) {
                    remove_neighbor_by_index(i);
                    i--; 
                } else {
                    buffer[n] = '\0';
                    if (node.monitoring) printf("[MONITOR] FD %d: %s", current_fd, buffer);

                    char cmd[32]; sscanf(buffer, "%s", cmd);

                    // 1. Identificação de Vizinho
                    if (strcmp(cmd, "NEIGHBOR") == 0) {
                        char neighbor_id[4];
                        if (sscanf(buffer, "%*s %s", neighbor_id) == 1) {
                            strcpy(node.neighbors[i].id, neighbor_id);
                            update_routing_table(neighbor_id, 1, current_fd, COORDINATION);

                            if (node.monitoring) {
                                printf("\n%s[MONITOR]%s SYNC: Enviando tabelas para o novo vizinho %s (FD %d)\n> ", MAGENTA, RESET, neighbor_id, current_fd);
                            }

                            char sync_msg[64];
                            for (int r = 0; r < node.route_count; r++) {
                                if (node.routing_table[r].state == FORWARDING) {
                                    sprintf(sync_msg, "ROUTE %s %d\n", node.routing_table[r].dest, node.routing_table[r].distance);
                                    write(current_fd, sync_msg, strlen(sync_msg));
                                }
                            }
                            printf("\n%s[TCP]%s Nó %s identificado e tabelas sincronizadas.\n> ", CYAN, RESET, neighbor_id);
                            fflush(stdout);
                        }
                    }
                    
                    // 2. Protocolo ROUTE
                    else if (strcmp(cmd, "ROUTE") == 0) {
                        char dest_id[4]; int dist_rec;
                        if (sscanf(buffer, "%*s %s %d", dest_id, &dist_rec) == 2) {
                            int nova_dist = dist_rec + 1;
                            int ja_conhecia = 0, dist_antiga = 999;

                            for (int r = 0; r < node.route_count; r++) {
                                if (strcmp(node.routing_table[r].dest, dest_id) == 0) {
                                    ja_conhecia = 1; dist_antiga = node.routing_table[r].distance; break;
                                }
                            }

                            update_routing_table(dest_id, nova_dist, current_fd, FORWARDING);
                            int condicao_prop = (!ja_conhecia || nova_dist < dist_antiga || nova_dist == dist_antiga);
                            
                            if (node.monitoring) {
                                printf("\n%s[MONITOR]%s RECEÇÃO: Rota %s (dist %d) via FD %d. DECISÃO: %s\n> ", 
                                    MAGENTA, RESET, dest_id, dist_rec, current_fd, 
                                    condicao_prop ? "PROPAGAR" : "IGNORAR");
                                fflush(stdout);
                            }

                            if (condicao_prop) {
                                char msg_out[64]; sprintf(msg_out, "ROUTE %s %d\n", dest_id, nova_dist);
                                for (int j = 0; j < node.neighbor_count; j++) {
                                    if (node.neighbors[j].fd != current_fd) write(node.neighbors[j].fd, msg_out, strlen(msg_out));
                                }
                            }
                        }
                    }

                    // 3. Protocolo de Encaminhamento: COORD dest
                    else if (strcmp(cmd, "COORD") == 0) {
                        char dest_id[4];
                        if (sscanf(buffer, "%*s %s", dest_id) == 1) {
                            for (int r = 0; r < node.route_count; r++) {
                                if (strcmp(node.routing_table[r].dest, dest_id) == 0) {
                                    // Se eu uso este vizinho para chegar ao destino, entro em coordenação
                                    if (node.routing_table[r].neighbor_fd == current_fd) {
                                        node.routing_table[r].state = COORDINATION;

                                        // Propago o COORD para os meus outros vizinhos
                                        for (int j = 0; j < node.neighbor_count; j++) {
                                            if (node.neighbors[j].fd != current_fd) {
                                                write(node.neighbors[j].fd, buffer, strlen(buffer));
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }

                    // 4. Protocolo de Encaminhamento: UNCOORD dest
                    else if (strcmp(cmd, "UNCOORD") == 0) {
                        char dest_id[4];
                        if (sscanf(buffer, "%*s %s", dest_id) == 1) {
                            for (int r = 0; r < node.route_count; r++) {
                                if (strcmp(node.routing_table[r].dest, dest_id) == 0) {
                                    if (node.routing_table[r].neighbor_fd == current_fd) {
                                        // Removemos ou invalidamos a rota (distância infinita)
                                        node.routing_table[r].distance = 99; 
                                        node.routing_table[r].state = COORDINATION;
                                        
                                        // Propaga o UNCOORD
                                        for (int j = 0; j < node.neighbor_count; j++) {
                                            if (node.neighbors[j].fd != current_fd) {
                                                write(node.neighbors[j].fd, buffer, strlen(buffer));
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }

                    // 5. Mensagens de Chat
                    else if (strcmp(cmd, "CHAT") == 0) {
                        char origem[4], destino[4], texto[512];
                        sscanf(buffer, "%*s %s %s %[^\n]", origem, destino, texto);
                        if (strcmp(destino, node.id) == 0) {
                            printf("\n[CHAT] De %s: %s\n> ", origem, texto); fflush(stdout);
                        } else {
                            // Reencaminha apenas se o estado for FORWARDING
                            for(int j=0; j<node.route_count; j++) {
                                if(strcmp(node.routing_table[j].dest, destino) == 0 && node.routing_table[j].state == FORWARDING) {
                                    write(node.routing_table[j].neighbor_fd, buffer, n);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    } 
    return 0; 
}

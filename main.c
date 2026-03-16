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
                    // Adicionamos ao array mesmo sem saber o ID ainda
                    node.neighbors[node.neighbor_count].fd = new_fd;
                    strcpy(node.neighbors[node.neighbor_count].id, "???"); 
                    node.neighbor_count++;
                    printf("\n[TCP] Novo vizinho conectado (fd %d). Aguardando identificação...\n", new_fd);
                } else {
                    printf("\n[TCP] Ligação recusada: limite de vizinhos atingido.\n");
                    close(new_fd);
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
                    if (!node.is_joined && register_node(regIP, regUDP, arg_net, arg_id, node.myIP, node.myTCP) == 0) { 
                        strcpy(node.net, arg_net);
                        strcpy(node.id, arg_id);
                        node.is_joined = 1;
                        update_routing_table(node.id, 0, -1, FORWARDING);
                    } 
                    break; 
                case 2: // LEAVE
                    if (!node.is_joined) {
                        printf("O nó não está registado em nenhuma rede.\n");
                    } 
                    else {
                        if (unregister_node(regIP, regUDP, node.net, node.id) == 0) {

                            // fechar sockets dos vizinhos
                            for(int i = 0; i < node.neighbor_count; i++) {
                                close(node.neighbors[i].fd);
                            }

                            // limpar vizinhos
                            node.neighbor_count = 0;
                            memset(node.neighbors, 0, sizeof(node.neighbors));

                            // limpar routing table
                            node.route_count = 0;
                            memset(node.routing_table, 0, sizeof(node.routing_table));

                            // limpar estado do nó
                            memset(node.net, 0, sizeof(node.net));
                            memset(node.id, 0, sizeof(node.id));

                            node.is_joined = 0;

                            printf("Nó saiu da rede e estado foi limpo.\n");
                        }
                    }
                    break;
                case 3: // EXIT
                    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id); 
                    exit(0); 
                case 4: // NODES
                    if (strlen(arg_net) > 0) nodes_query(arg_net, regIP, regUDP); 
                    else if (node.is_joined) nodes_query(node.net, regIP, regUDP); 
                    else printf("Erro: Indica a rede ou regista-te primeiro.\n"); 
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

                                // 2. Enviar a nossa tabela para o nó onde nos estamos a ligar
                                char sync_msg[64];
                                for (int r = 0; r < node.route_count; r++) {
                                    if (node.routing_table[r].state == FORWARDING) {
                                        sprintf(sync_msg, "ROUTE %s %d\n", 
                                                node.routing_table[r].dest, 
                                                node.routing_table[r].distance);
                                        write(fd, sync_msg, strlen(sync_msg));
                                    }
                                }
                            }
                        } 
                    } 
                    break; 
                case 7: // SHOW NEIGHBORS (sg)
                    printf("\n%s--- %sLISTA DE VIZINHOS ATIVOS (Nó %s)%s %s---\n", BOLD, MAGENTA, node.id, RESET, MAGENTA);
                    printf("%s%-10s %-10s %s\n", BOLD, "ID NÓ", "SOCKET FD", RESET);
                    printf("---------- ----------\n");

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

                            // 1. Avisar a rede sobre todas as rotas que dependiam deste FD
                            for (int r = 0; r < node.route_count; r++) {
                                if (node.routing_table[r].neighbor_fd == fd_removido) {
                                    char msg_coord[64];
                                    sprintf(msg_coord, "COORD %s\n", node.routing_table[r].dest);
                                    
                                    // Propagamos o COORD para os OUTROS vizinhos
                                    for (int j = 0; j < node.neighbor_count; j++) {
                                        if (node.neighbors[j].fd != fd_removido) {
                                            write(node.neighbors[j].fd, msg_coord, strlen(msg_coord));
                                        }
                                    }
                                    // Mudamos o estado para COORDINATION (invisível no sr)
                                    node.routing_table[r].state = COORDINATION;
                                }
                            }

                            // 2. Fechar o socket e remover do array de vizinhos
                            close(fd_removido);
                            for (int k = i; k < node.neighbor_count - 1; k++) {
                                node.neighbors[k] = node.neighbors[k+1];
                            }
                            node.neighbor_count--;
                            printf("Ligação com o nó %s removida.\n", arg_net);
                            break;
                        }
                    }
                    break;
                case 9: // announce (a id)
                    // Se o ID for o meu, eu inicio o anúncio
                    if (strcmp(arg_net, node.id) == 0) {
                        char msg[64];
                        sprintf(msg, "ROUTE %s 0\n", node.id);
                        printf("Anúncio de rota iniciado para o Nó %s.\n", node.id);
                        
                        for (int j = 0; j < node.neighbor_count; j++) {
                            write(node.neighbors[j].fd, msg, strlen(msg));
                        }
                    }
                    break;
                case 10: // sr ID
                    // Se o utilizador não especificou um ID (arg_net vazio), 
                    // podemos mostrar a ajuda ou a tabela geral. 
                    // Mas focando no teu objetivo: mostrar o caminho para o ID especificado.
                    
                    printf("\n--- ENCAMINHAMENTO PARA O NÓ %s (A partir do Nó %s) ---\n", arg_net, node.id);
                    printf("%-10s %-15s %-10s %-15s\n", "DESTINO", "ESTADO", "SALTOS", "VIZINHO (FD)");
                    
                    // 1. O próprio nó aparece sempre como ponto de partida
                    printf("%-10s %-15s %-10d %-15s\n", node.id, "EXPEDIÇÃO", 0, "local");

                    int encontrou = 0;
                    
                    // 2. Se o utilizador pediu um ID que não é o próprio nó
                    if (strcmp(arg_net, node.id) != 0) {
                        for (int r = 0; r < node.route_count; r++) {
                            // Filtramos pelo ID especificado E pelo estado FORWARDING (announce feito)
                            if (strcmp(node.routing_table[r].dest, arg_net) == 0) {
                                if (node.routing_table[r].state == FORWARDING) {
                                    printf("%-10s %-15s %-10d %-15d\n", 
                                        node.routing_table[r].dest, 
                                        "EXPEDIÇÃO", 
                                        node.routing_table[r].distance, 
                                        node.routing_table[r].neighbor_fd);
                                    encontrou = 1;
                                } else {
                                    printf("\n[INFO] O nó %s é conhecido, mas ainda não fez 'announce'.\n", arg_net);
                                    encontrou = -1; // Encontrou mas não está ativo
                                }
                                break; // Encontrado o destino, não precisamos de procurar mais
                            }
                        }
                    } else {
                        encontrou = 1; // O destino era o próprio nó
                    }

                    if (encontrou == 0 && arg_net[0] != '\0') {
                        printf("\n[ERRO] Rota para o nó %s desconhecida.\n", arg_net);
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

                    // 1. Identificação Inicial (Apenas guarda o ID, sem criar rota)
                    if (strcmp(cmd, "NEIGHBOR") == 0) {
                        char neighbor_id[4];
                        if (sscanf(buffer, "%*s %s", neighbor_id) == 1) {
                            strcpy(node.neighbors[i].id, neighbor_id);
                            update_routing_table(neighbor_id, 1, current_fd, COORDINATION);

                            // --- SINCRONIZAÇÃO DE BOAS-VINDAS ---
                            // O Nó 4 percorre a sua tabela e envia ao Nó 5 tudo o que já está em FORWARDING
                            char sync_msg[64];
                            for (int r = 0; r < node.route_count; r++) {
                                if (node.routing_table[r].state == FORWARDING) {
                                    // Enviamos ROUTE dest distancia
                                    sprintf(sync_msg, "ROUTE %s %d\n", 
                                            node.routing_table[r].dest, 
                                            node.routing_table[r].distance);
                                    write(current_fd, sync_msg, strlen(sync_msg));
                                }
                            }
                            // ------------------------------------

                            if (node.monitoring) {
                                printf("\n%s[TCP]%s Nó %s ligado. Tabelas sincronizadas.\n> ", CYAN, RESET, neighbor_id);
                                fflush(stdout);
                            }
                        }
                    }
                    
                    // 2. Protocolo de Encaminhamento: ROUTE dest n
                    else if (strcmp(cmd, "ROUTE") == 0) {
                        char dest_id[4];
                        int dist_recebida;
                        if (sscanf(buffer, "%*s %s %d", dest_id, &dist_recebida) == 2) {
                            
                            // CORREÇÃO: A nossa distância para o destino é a distância que o vizinho
                            // nos enviou + o salto para chegar a esse vizinho.
                            int nova_distancia = dist_recebida + 1;

                            // Atualizamos a tabela com a nova distância incrementada
                            update_routing_table(dest_id, nova_distancia, current_fd, FORWARDING);

                            // PROPAGAÇÃO: Enviamos para os outros vizinhos a distância que nós calculámos
                            char msg_to_propagate[64];
                            sprintf(msg_to_propagate, "ROUTE %s %d\n", dest_id, nova_distancia);

                            for (int j = 0; j < node.neighbor_count; j++) {
                                // Split Horizon: não enviar de volta para quem nos deu a rota
                                if (node.neighbors[j].fd != current_fd) {
                                    write(node.neighbors[j].fd, msg_to_propagate, strlen(msg_to_propagate));
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

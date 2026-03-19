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

            switch (cmd_type){
                case 1: // JOIN
                    if (!node.is_joined && register_node(regIP, regUDP, arg_net, arg_id, node.myIP, node.myTCP) == 0) { 
                        strcpy(node.net, arg_net);
                        strcpy(node.id, arg_id);
                        node.is_joined = 1;
                        update_routing_table(node.id, 0, -1, FORWARDING);
                        printf("%s[SUCESSO]%s Registado na rede %s com ID %s\n", GREEN, RESET, node.net, node.id);
                    } else {
                        printf("%s[ERRO]%s Falha ao registar no servidor UDP.\n", RED, RESET);
                    }
                    break; 

                case 2: // LEAVE
                    if (!node.is_joined) {
                        printf("%s[AVISO]%s O nó não está registado em nenhuma rede.\n", YELLOW, RESET);
                    } else {
                        if (unregister_node(regIP, regUDP, node.net, node.id) == 0) {
                            for(int i = 0; i < node.neighbor_count; i++) close(node.neighbors[i].fd);
                            node.neighbor_count = 0;
                            memset(node.neighbors, 0, sizeof(node.neighbors));
                            node.route_count = 0;
                            memset(node.routing_table, 0, sizeof(node.routing_table));
                            memset(node.net, 0, sizeof(node.net));
                            memset(node.id, 0, sizeof(node.id));
                            node.is_joined = 0;

                            printf("%s[SISTEMA]%s Nó saiu da rede e tabelas limpas.\n", YELLOW, RESET);
                        }
                    }
                    break;

                case 3: // EXIT
                    printf("%s[SISTEMA]%s A encerrar nó...\n", RED, RESET);
                    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id); 
                    exit(0); 

                case 4: // NODES
                    if (node.monitoring) printf("%s[UDP]%s Consultando lista de nós...\n", BLUE, RESET);
                    if (strlen(arg_net) > 0) nodes_query(arg_net, regIP, regUDP); 
                    else if (node.is_joined) nodes_query(node.net, regIP, regUDP); 
                    else printf("%s[ERRO]%s Indica a rede ou regista-te primeiro.\n", RED, RESET); 
                    break; 

                case 6: // ADD EDGE (ae)
                    if (node.is_joined) { 
                        char target_ip[16]; int target_tcp; 
                        if (node.monitoring) printf("%s[UDP]%s Pesquisando contacto do nó %s...\n", BLUE, RESET, arg_net);
                        
                        if (get_node_contact(regIP, regUDP, node.net, arg_net, target_ip, &target_tcp) == 0) { 
                            int fd = setup_tcp_client(target_ip, target_tcp); 
                            if (fd != -1) { 
                                node.neighbors[node.neighbor_count].fd = fd;
                                strcpy(node.neighbors[node.neighbor_count].id, arg_net);
                                node.neighbor_count++;

                                update_routing_table(arg_net, 1, fd, COORDINATION);

                                char msg[64]; sprintf(msg, "NEIGHBOR %s\n", node.id); 
                                write(fd, msg, strlen(msg));

                                printf("%s[TCP]%s Ligação estabelecida com nó %s (FD %d)\n", GREEN, RESET, arg_net, fd);

                                // Sincronização
                                for (int r = 0; r < node.route_count; r++) {
                                    if (node.routing_table[r].state == FORWARDING) {
                                        char sync_msg[64];
                                        sprintf(sync_msg, "ROUTE %s %d\n", node.routing_table[r].dest, node.routing_table[r].distance);
                                        write(fd, sync_msg, strlen(sync_msg));
                                    }
                                }
                            }
                        } 
                    } 
                    break; 

                case 7: // SHOW NEIGHBORS (sg)
                    printf("\n%s%s--- LISTA DE VIZINHOS (Nó %s) ---\n", BOLD, MAGENTA, node.id);
                    printf("%-10s %-9s %s\n", "ID NÓ", "SOCKET FD", RESET);
                    printf("--------- ---------\n");
                    for (int i = 0; i < node.neighbor_count; i++) {
                        printf("%s%-10s%s %s%-10d%s\n", GREEN, node.neighbors[i].id, RESET, CYAN, node.neighbors[i].fd, RESET);
                    }
                    printf("\n");
                    break; 

                case 8: // re (Remove Edge)
                    for (int i = 0; i < node.neighbor_count; i++) {
                        if (strcmp(node.neighbors[i].id, arg_net) == 0) {
                            int fd_removido = node.neighbors[i].fd;
                            printf("%s[SISTEMA]%s A cortar ligação com nó %s...\n", RED, RESET, arg_net);
                            
                            for (int r = 0; r < node.route_count; r++) {
                                if (node.routing_table[r].neighbor_fd == fd_removido) {
                                    char msg_coord[64];
                                    sprintf(msg_coord, "COORD %s\n", node.routing_table[r].dest);
                                    for (int j = 0; j < node.neighbor_count; j++) {
                                        if (node.neighbors[j].fd != fd_removido) write(node.neighbors[j].fd, msg_coord, strlen(msg_coord));
                                    }
                                    node.routing_table[r].state = COORDINATION;
                                }
                            }
                            close(fd_removido);
                            for (int k = i; k < node.neighbor_count - 1; k++) node.neighbors[k] = node.neighbors[k+1];
                            node.neighbor_count--;
                            break;
                        }
                    }
                    break;

                case 9: // announce
                    printf("%s[ANNOUNCE]%s Propagando presença do nó %s na rede...\n", MAGENTA, RESET, node.id);
                    update_routing_table(node.id, 0, -1, FORWARDING); 
                    char ann_msg[64]; sprintf(ann_msg, "ROUTE %s 0\n", node.id);
                    for (int j = 0; j < node.neighbor_count; j++) write(node.neighbors[j].fd, ann_msg, strlen(ann_msg));
                    break;

                case 10: // sr ID
                    printf("\n%s%s--- TABELA DE ENCAMINHAMENTO (PARA %s) ---\n", BOLD, CYAN, (arg_net[0] == '\0' ? "TODOS" : arg_net));
                    printf("%-10s %-13s %-10s %-15s %s\n", "DESTINO", "ESTADO", "SALTOS", "VIZINHO (FD)", RESET);
                    printf("---------- ------------- ---------- ------------\n");
                    printf("%-10s %s%-13s%s %-10d %-15s\n", node.id, GREEN, "EXPEDIÇÃO", RESET, 0, "local");

                    int encontrou = 0;
                    for (int r = 0; r < node.route_count; r++) {
                        if (arg_net[0] == '\0' || strcmp(node.routing_table[r].dest, arg_net) == 0) {
                            if (node.routing_table[r].state == FORWARDING) {
                                printf("%-10s %s%-13s%s %-10d %-15d\n", 
                                    node.routing_table[r].dest, GREEN, "EXPEDIÇÃO", RESET, 
                                    node.routing_table[r].distance, node.routing_table[r].neighbor_fd);
                                encontrou = 1;
                            } else if (arg_net[0] != '\0') {
                                printf("%-10s %s%-13s%s %-10s %-15s\n", arg_net, YELLOW, "COORDENAÇÃO", RESET, "--", "--");
                                encontrou = -1;
                            }
                        }
                    }
                    if (encontrou == 0 && arg_net[0] != '\0') printf("%s[ERRO]%s Rota para %s desconhecida.\n", RED, RESET, arg_net);
                    printf("\n");
                    break;

                case 11: // start monitor
                    node.monitoring = 1;
                    printf("%s[MONITOR]%s Ativado.\n", MAGENTA, RESET);
                    break;

                case 12: // end monitor
                    node.monitoring = 0;
                    printf("%s[MONITOR]%s Desativado.\n", MAGENTA, RESET);
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
                            printf("%s[CHAT]%s Mensagem enviada para %s via FD %d\n", GREEN, RESET, arg_net, found_fd);
                        } else {
                            printf("%s[ERRO]%s Não existe rota para o nó %s.\n", RED, RESET, arg_net);
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
                    printf("\n%s[SISTEMA]%s %sConexão perdida com o vizinho (FD %d)%s\n> ", RED, BOLD, RED, current_fd, RESET);
                    fflush(stdout);
                    remove_neighbor_by_index(i);
                    i--; 
                } else {
                    buffer[n] = '\0';
                    
                    // Monitorização de tráfego bruto (opcional, em Azul)
                    if (node.monitoring) printf("\n%s[TRAFFIC]%s FD %d: %s", BLUE, RESET, current_fd, buffer);

                    char cmd[32]; sscanf(buffer, "%s", cmd);

                    // 1. Identificação Inicial
                    if (strcmp(cmd, "NEIGHBOR") == 0) {
                        char neighbor_id[4];
                        if (sscanf(buffer, "%*s %s", neighbor_id) == 1) {
                            strcpy(node.neighbors[i].id, neighbor_id);
                            update_routing_table(neighbor_id, 1, current_fd, COORDINATION);

                            if (node.monitoring) {
                                printf("%s[MONITOR]%s %sSYNC:%s Enviando tabelas para novo vizinho %s\n", 
                                    MAGENTA, RESET, CYAN, RESET, neighbor_id);
                            }

                            char sync_msg[64];
                            for (int r = 0; r < node.route_count; r++) {
                                if (node.routing_table[r].state == FORWARDING) {
                                    sprintf(sync_msg, "ROUTE %s %d\n", 
                                            node.routing_table[r].dest, 
                                            node.routing_table[r].distance);
                                    write(current_fd, sync_msg, strlen(sync_msg));
                                }
                            }

                            printf("\n%s[TCP]%s Nó %s ligado. Tabelas sincronizadas.\n> ", CYAN, RESET, neighbor_id);
                            fflush(stdout);
                        }
                    }
                    
                    // 2. Protocolo de Encaminhamento: ROUTE
                    else if (strcmp(cmd, "ROUTE") == 0) {
                        char dest_id[4];
                        int dist_recebida;
                        if (sscanf(buffer, "%*s %s %d", dest_id, &dist_recebida) == 2) {
                            int nova_dist = dist_recebida + 1;

                            if (node.monitoring) {
                                printf("\n%s[MONITOR]%s %sRECEÇÃO:%s Rota para %s (dist %d) via FD %d\n", 
                                    MAGENTA, RESET, YELLOW, RESET, dest_id, dist_recebida, current_fd);
                            }

                            int ja_conhecia = 0, dist_antiga = 999, fd_antigo = -1;
                            for (int r = 0; r < node.route_count; r++) {
                                if (strcmp(node.routing_table[r].dest, dest_id) == 0) {
                                    ja_conhecia = 1; dist_antiga = node.routing_table[r].distance;
                                    fd_antigo = node.routing_table[r].neighbor_fd;
                                    break;
                                }
                            }

                            update_routing_table(dest_id, nova_dist, current_fd, FORWARDING);

                            int mesma_dist = (nova_dist == dist_antiga);
                            int condicao_prop = (!ja_conhecia || nova_dist < dist_antiga || mesma_dist);

                            if (node.monitoring) {
                                if (condicao_prop) {
                                    printf("%s[MONITOR]%s %sDECISÃO:%s Propagar %s (Nova dist: %d). Motivo: %s\n", 
                                        MAGENTA, RESET, CYAN, RESET, dest_id, nova_dist, 
                                        !ja_conhecia ? "Novo Destino" : (nova_dist < dist_antiga ? "Melhor Rota" : "Refresh"));
                                } else {
                                    printf("%s[MONITOR]%s %sDECISÃO:%s Ignorar %s. Já conheço via FD %d com dist %d\n", 
                                        MAGENTA, RESET, RED, RESET, dest_id, fd_antigo, dist_antiga);
                                }
                            }

                            if (condicao_prop) {
                                char msg_out[64];
                                sprintf(msg_out, "ROUTE %s %d\n", dest_id, nova_dist);
                                for (int j = 0; j < node.neighbor_count; j++) {
                                    if (node.neighbors[j].fd != current_fd) {
                                        int b = write(node.neighbors[j].fd, msg_out, strlen(msg_out));
                                        if (node.monitoring && b > 0) {
                                            printf("%s[MONITOR]%s %sENVIO:%s Encaminhado para %s (FD %d)\n", 
                                                MAGENTA, RESET, GREEN, RESET, node.neighbors[j].id, node.neighbors[j].fd);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 3. Protocolo: COORD (Em Laranja/Amarelo por ser alerta)
                    else if (strcmp(cmd, "COORD") == 0) {
                        char dest_id[4];
                        if (sscanf(buffer, "%*s %s", dest_id) == 1) {
                            if (node.monitoring) printf("%s[MONITOR]%s %sALERTA:%s Recebido COORD para %s\n", MAGENTA, RESET, YELLOW, RESET, dest_id);
                            for (int r = 0; r < node.route_count; r++) {
                                if (strcmp(node.routing_table[r].dest, dest_id) == 0 && node.routing_table[r].neighbor_fd == current_fd) {
                                    node.routing_table[r].state = COORDINATION;
                                    for (int j = 0; j < node.neighbor_count; j++) {
                                        if (node.neighbors[j].fd != current_fd) write(node.neighbors[j].fd, buffer, strlen(buffer));
                                    }
                                    break;
                                }
                            }
                        }
                    }

                    // 4. Protocolo: UNCOORD
                    else if (strcmp(cmd, "UNCOORD") == 0) {
                        char dest_id[4];
                        if (sscanf(buffer, "%*s %s", dest_id) == 1) {
                            if (node.monitoring) printf("%s[MONITOR]%s %sCRÍTICO:%s Recebido UNCOORD para %s\n", MAGENTA, RESET, RED, RESET, dest_id);
                            for (int r = 0; r < node.route_count; r++) {
                                if (strcmp(node.routing_table[r].dest, dest_id) == 0 && node.routing_table[r].neighbor_fd == current_fd) {
                                    node.routing_table[r].distance = 99; 
                                    node.routing_table[r].state = COORDINATION;
                                    for (int j = 0; j < node.neighbor_count; j++) {
                                        if (node.neighbors[j].fd != current_fd) write(node.neighbors[j].fd, buffer, strlen(buffer));
                                    }
                                    break;
                                }
                            }
                        }
                    }

                    // 5. Mensagens de Chat (Destaque em Verde para a mensagem final)
                    else if (strcmp(cmd, "CHAT") == 0) {
                        char origem[4], destino[4], texto[512];
                        sscanf(buffer, "%*s %s %s %[^\n]", origem, destino, texto);
                        
                        if (strcmp(destino, node.id) == 0) {
                            printf("\n%s[CHAT]%s %sDe %s:%s %s\n> ", GREEN, RESET, BOLD, origem, RESET, texto); 
                            fflush(stdout);
                        } else {
                            if (node.monitoring) printf("%s[MONITOR]%s %sREENCAMINHAR:%s Chat de %s para %s\n", MAGENTA, RESET, GREEN, RESET, origem, destino);
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

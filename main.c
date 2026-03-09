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

#define BUFFER_SIZE 256 
#define MAX_NEIGHBORS 10

typedef struct {
    char dest[4];     
    int neighbor_fd;  
} Route;

// Nova estrutura para vizinhos no array
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
    
    // MUDANÇA: Array de vizinhos em vez de prev/next fixos
    Neighbor neighbors[MAX_NEIGHBORS];
    int neighbor_count;
    
    int monitoring;
    Route routing_table[100];
    int route_count;
} NodeState; 

NodeState node; 
char regIP_buf[128];  
char *regIP; 
int regUDP;   

// --- FUNÇÕES AUXILIARES ---

void add_route(char *dest_id, int fd) {
    for (int i = 0; i < node.route_count; i++) {
        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            node.routing_table[i].neighbor_fd = fd;
            return;
        }
    }
    if (node.route_count < 100) {
        strcpy(node.routing_table[node.route_count].dest, dest_id);
        node.routing_table[node.route_count].neighbor_fd = fd;
        node.route_count++;
    }
}

// Limpa rotas que usavam um FD que foi fechado
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

void remove_neighbor_by_index(int index) {
    int fd_to_close = node.neighbors[index].fd;
    printf("A fechar ligação com Nó %s (fd %d)...\n", node.neighbors[index].id, fd_to_close);
    close(fd_to_close);
    clean_routing_table_by_fd(fd_to_close);

    for (int i = index; i < node.neighbor_count - 1; i++) {
        node.neighbors[i] = node.neighbors[i + 1];
    }
    node.neighbor_count--;
}

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
                         strcpy(node.net, arg_net); strcpy(node.id, arg_id); node.is_joined = 1; 
                     } 
                     break; 
                 case 2: // LEAVE
                     if (!node.is_joined) { 
                         printf("O nó não está registado em nenhuma rede.\n"); 
                     } else { 
                         if (unregister_node(regIP, regUDP, node.net, node.id) == 0) { 
                             node.is_joined = 0; 
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
                                char msg[64]; sprintf(msg, "NEIGHBOR %s\n", node.id); 
                                write(fd, msg, strlen(msg)); 
                                add_route(arg_net, fd);
                            }
                        } 
                    } 
                    break; 
                case 7: // SHOW NEIGHBOURS
                    printf("--- Vizinhos do Nó %s (%d/%d) ---\n", node.id, node.neighbor_count, MAX_NEIGHBORS); 
                    for(int i=0; i<node.neighbor_count; i++)
                        printf("ID: %s | FD: %d\n", node.neighbors[i].id, node.neighbors[i].fd);
                    break; 
                case 8: // REMOVE EDGE (re <id>)
                    {
                        int found = 0;
                        for(int i=0; i<node.neighbor_count; i++) {
                            if(strcmp(node.neighbors[i].id, arg_net) == 0) {
                                remove_neighbor_by_index(i);
                                found = 1; break;
                            }
                        }
                        if(!found) printf("Erro: Nó %s não é teu vizinho.\n", arg_net);
                    }
                    break; 
                case 9: // ANNOUNCE
                    for(int i=0; i < node.neighbor_count; i++) {
                        char route_msg[64]; sprintf(route_msg, "ROUTING %s\n", node.id);
                        write(node.neighbors[i].fd, route_msg, strlen(route_msg));
                    }
                    printf("Anúncio enviado a todos os vizinhos.\n");
                    break;
                case 10: // SHOW ROUTING
                    printf("--- Tabela de Encaminhamento (Nó %s) ---\n", node.id); 
                    for (int i = 0; i < node.route_count; i++) { 
                        printf("Destino: %s | Via FD: %d\n", node.routing_table[i].dest, node.routing_table[i].neighbor_fd); 
                    } 
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
                    i--; // Ajustar índice por causa do shift
                } else {
                    buffer[n] = '\0';
                    if (node.monitoring) printf("[MONITOR] FD %d: %s", current_fd, buffer);

                    char cmd[32]; sscanf(buffer, "%s", cmd);

                    if (strcmp(cmd, "NEIGHBOR") == 0) {
                        sscanf(buffer, "%*s %s", node.neighbors[i].id);
                        add_route(node.neighbors[i].id, current_fd);
                        printf("\n[TCP] Vizinho no fd %d identificou-se como Nó %s.\n> ", current_fd, node.neighbors[i].id);
                        fflush(stdout);
                    } else if (strcmp(cmd, "ROUTING") == 0) {
                        char id_rec[16];
                        if (sscanf(buffer, "%*s %s", id_rec) == 1) {
                            add_route(id_rec, current_fd); // current_fd é o FD de quem enviou o anúncio
                            // Propaga para TODOS os outros vizinhos no array
                            for(int j=0; j < node.neighbor_count; j++) {
                                if(node.neighbors[j].fd != current_fd) {
                                    write(node.neighbors[j].fd, buffer, strlen(buffer));
                                }
                            }
                        }
                    } else if (strcmp(cmd, "CHAT") == 0) {
                        char origem[4], destino[4], texto[512];
                        sscanf(buffer, "%*s %s %s %[^\n]", origem, destino, texto);
                        if (strcmp(destino, node.id) == 0) {
                            printf("\n[CHAT] De %s: %s\n> ", origem, texto); fflush(stdout);
                        } else {
                            // Reencaminhar usando a tabela
                            for(int j=0; j<node.route_count; j++) {
                                if(strcmp(node.routing_table[j].dest, destino) == 0) {
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

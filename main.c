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

typedef enum { FORWARDING, COORDINATION } RouteState;
typedef struct {
    char dest[4];     
    int neighbor_fd;
    int distance; // n saltos para o destino
    RouteState state;
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
    char lost_neighbor_id[4];
    strcpy(lost_neighbor_id, node.neighbors[index].id);

    printf("\n[SISTEMA] Ligação perdida com Nó %s (fd %d).\n", lost_neighbor_id, fd_to_close);

    // 1. Identificar rotas que dependiam deste vizinho
    for (int i = 0; i < node.route_count; i++) {
        if (node.routing_table[i].neighbor_fd == fd_to_close) {
            // Entramos em estado de COORDENAÇÃO para este destino
            node.routing_table[i].state = COORDINATION;
            
            // 2. Avisar todos os OUTROS vizinhos da perda
            char coord_msg[64];
            sprintf(coord_msg, "COORD %s\n", node.routing_table[i].dest);
            
            for (int j = 0; j < node.neighbor_count; j++) {
                if (node.neighbors[j].fd != fd_to_close) {
                    write(node.neighbors[j].fd, coord_msg, strlen(coord_msg));
                }
            }
        }
    }

    // 3. Fechar socket e limpar array de vizinhos
    close(fd_to_close);
    for (int i = index; i < node.neighbor_count - 1; i++) {
        node.neighbors[i] = node.neighbors[i + 1];
    }
    node.neighbor_count--;
    printf("> "); fflush(stdout);
}

void sigint_handler(int sig) { 
    if (node.is_joined) unregister_node(regIP, regUDP, node.net, node.id); 
    exit(0); 
}

void update_routing_table(char *dest_id, int new_dist, int fd, RouteState state) {
    for (int i = 0; i < node.route_count; i++) {
        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            // Se encontrarmos um caminho novo (mesmo que mais longo) enquanto estamos em COORD,
            // ou um caminho mais curto enquanto em FORWARDING:
            if (node.routing_table[i].state == COORDINATION || new_dist < node.routing_table[i].distance) {
                node.routing_table[i].distance = new_dist;
                node.routing_table[i].neighbor_fd = fd;
                node.routing_table[i].state = state; // Volta a FORWARDING
                if (node.monitoring) printf("[SISTEMA] Rota para %s recuperada via FD %d.\n", dest_id, fd);
            }
            return;
        }
    }
    // Se não existir e houver espaço, adicionar novo destino
    if (node.route_count < 100) {
        strcpy(node.routing_table[node.route_count].dest, dest_id);
        node.routing_table[node.route_count].distance = new_dist;
        node.routing_table[node.route_count].neighbor_fd = fd;
        node.routing_table[node.route_count].state = state;
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

void handle_uncoord(char *dest_id, int fd) {
    for (int i = 0; i < node.route_count; i++) {
        // Se recebo UNCOORD de quem eu dependia, a rota torna-se inválida ou entra em coordenação
        if (strcmp(node.routing_table[i].dest, dest_id) == 0 && node.routing_table[i].neighbor_fd == fd) {
            node.routing_table[i].state = COORDINATION;
            // Nota: Em implementações mais avançadas, aqui poderias remover a rota para forçar nova descoberta
        }
    }
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
                                char msg[64]; sprintf(msg, "NEIGHBOR %s\n", node.id); 
                                write(fd, msg, strlen(msg)); 
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
                case 9: // ANNOUNCE (a)
                    if (node.is_joined) {
                        update_routing_table(node.id, 0, -1, FORWARDING);
                        char route_msg[64];
                        // Um nó anuncia-se a si próprio com 0 saltos
                        sprintf(route_msg, "ROUTE %s 0\n", node.id); 
                        
                        for (int i = 0; i < node.neighbor_count; i++) {
                            write(node.neighbors[i].fd, route_msg, strlen(route_msg));
                        }
                        printf("Anúncio de rota (ROUTE %s 0) enviado aos vizinhos.\n", node.id);
                    }
                    break;
                case 10: // SHOW ROUTING (sr)
                    printf("\n--- TABELA DE ENCAMINHAMENTO (Nó %s) ---\n", node.id);
                    // Aumentamos os espaços para garantir que nada se sobrepõe
                    printf("%-10s %-13s %-10s %-12s\n", "DESTINO", "ESTADO", "SALTOS", "VIZINHO (FD)");
                    printf("---------- ------------- ---------- ------------\n");
                    
                    for (int i = 0; i < node.route_count; i++) {
                        char *state_str = (node.routing_table[i].state == FORWARDING) ? "EXPEDIÇÃO" : "COORDENAÇÃO";
                        
                        // Usamos exatamente os mesmos números do cabeçalho (-10, -15, -10, -10)
                        printf("%-10s %-15s %-10d ", 
                            node.routing_table[i].dest, 
                            state_str, 
                            node.routing_table[i].distance);

                        if (node.routing_table[i].neighbor_fd == -1) {
                            printf("%-12s\n", "local");
                        } else {
                            printf("%-12d\n", node.routing_table[i].neighbor_fd);
                        }
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
                        sscanf(buffer, "%*s %s", node.neighbors[i].id);
                        printf("\n[TCP] Vizinho no fd %d identificou-se como Nó %s.\n> ", current_fd, node.neighbors[i].id);
                        fflush(stdout);
                    } 
                    
                    // 2. Protocolo de Encaminhamento: ROUTE dest n
                    else if (strcmp(cmd, "ROUTE") == 0) {
                        char dest_id[4];
                        int dist;
                        if (sscanf(buffer, "%*s %s %d", dest_id, &dist) == 2) {
                            // Adiciona ou atualiza a rota com estado FORWARDING e distância +1
                            update_routing_table(dest_id, dist + 1, current_fd, FORWARDING);
                            
                            // Propaga o anúncio para os restantes vizinhos
                            for(int j=0; j < node.neighbor_count; j++) {
                                if(node.neighbors[j].fd != current_fd) {
                                    write(node.neighbors[j].fd, buffer, n);
                                }
                            }
                        }
                    }

                    // 3. Protocolo de Encaminhamento: COORD dest
                    else if (strcmp(cmd, "COORD") == 0) {
                        char dest_id[4];
                        if (sscanf(buffer, "%*s %s", dest_id) == 1) {
                            update_route_state(dest_id, COORDINATION); // Muda estado para coordenação
                        }
                    }

                    // 4. Protocolo de Encaminhamento: UNCOORD dest
                    else if (strcmp(cmd, "UNCOORD") == 0) {
                        char dest_id[4];
                        if (sscanf(buffer, "%*s %s", dest_id) == 1) {
                            // Lógica para remover a dependência deste vizinho para o destino
                            handle_uncoord(dest_id, current_fd);
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

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
    // REGRA: Nunca adicionar o próprio nó à tabela de rotas interna
    if (strcmp(dest_id, node.id) == 0) {
        return;
    }

    for (int i = 0; i < node.route_count; i++) {
        if (strcmp(node.routing_table[i].dest, dest_id) == 0) {
            
            // Condições para atualizar: 
            // - É um anúncio oficial (FORWARDING)
            // - É um caminho mais curto (Atalho)
            // - É uma atualização do vizinho que já usamos
            if (state == FORWARDING || new_dist < node.routing_table[i].distance || fd == node.routing_table[i].neighbor_fd) {
                node.routing_table[i].distance = new_dist;
                node.routing_table[i].neighbor_fd = fd;
                
                // Se recebermos um ROUTE (state FORWARDING), a rota torna-se visível
                if (state == FORWARDING) {
                    node.routing_table[i].state = FORWARDING;
                } else if (state == COORDINATION && fd == node.routing_table[i].neighbor_fd) {
                    // Se o vizinho que usávamos mandou COORD, mudamos o estado
                    node.routing_table[i].state = COORDINATION;
                }
            }
            return;
        }
    }

    // Se o destino não existe na tabela e não é o próprio nó, adicionamos
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

                                update_routing_table(arg_net, 1, fd, COORDINATION);

                                char msg[64]; sprintf(msg, "NEIGHBOR %s\n", node.id); 
                                write(fd, msg, strlen(msg)); 
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
                case 10: // sr
                    printf("\n--- TABELA DE ENCAMINHAMENTO (Nó %s) ---\n", node.id);
                    printf("%-10s %-15s %-10s %-15s\n", "DESTINO", "ESTADO", "SALTOS", "VIZINHO (FD)");
                    
                    // 1. Imprime o próprio nó
                    printf("%-10s %-15s %-10d %-15s\n", node.id, "EXPEDIÇÃO", 0, "local");

                    int encontrou_remoto = 0;
                    for (int r = 0; r < node.route_count; r++) {
                        // 2. Só imprime rotas para OUTROS nós que estejam em estado FORWARDING
                        if (strcmp(node.routing_table[r].dest, node.id) != 0 && node.routing_table[r].state == FORWARDING) {
                            printf("%-10s %-15s %-10d %-15d\n", 
                                node.routing_table[r].dest, 
                                "EXPEDIÇÃO", 
                                node.routing_table[r].distance, 
                                node.routing_table[r].neighbor_fd);
                            encontrou_remoto = 1;
                        }
                    }
                    
                    // CORREÇÃO DO AVISO: Usar a variável para dar feedback ao utilizador
                    if (!encontrou_remoto) {
                        printf("\n(Nenhuma rota remota anunciada até ao momento)\n");
                    }

                    // Se o utilizador pediu um ID específico (arg_net) e ele não está em FORWARDING
                    if (arg_net[0] != '\0' && strcmp(arg_net, node.id) != 0) {
                        int existe_em_coord = 0;
                        for(int r = 0; r < node.route_count; r++) {
                            if(strcmp(node.routing_table[r].dest, arg_net) == 0 && node.routing_table[r].state == COORDINATION) {
                                existe_em_coord = 1; 
                                break;
                            }
                        }
                        if (existe_em_coord) {
                            printf("Nota: O nó %s é conhecido pela topologia, mas ainda não fez 'announce'.\n", arg_net);
                        }
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
                    i--; 
                } else {
                    buffer[n] = '\0';
                    if (node.monitoring) printf("[MONITOR] FD %d: %s", current_fd, buffer);

                    char cmd[32]; sscanf(buffer, "%s", cmd);

                    // 1. Identificação Inicial (Apenas guarda o ID, sem criar rota)
                    if (strcmp(cmd, "NEIGHBOR") == 0) {
                        char neighbor_id[4];
                        
                        // 1. Extrair o ID do vizinho que se acabou de ligar
                        if (sscanf(buffer, "%*s %s", neighbor_id) == 1) {
                            // 2. Guardar o ID no array de vizinhos para futuras comunicações
                            strcpy(node.neighbors[i].id, neighbor_id);

                            // 3. Atualizar a tabela de encaminhamento interna:
                            // - Distância: 1 (é um vizinho direto)
                            // - Estado: COORDINATION (Está na topologia, mas invisível ao 'sr' até haver 'announce')
                            update_routing_table(neighbor_id, 1, current_fd, COORDINATION);

                            if (node.monitoring) {
                                printf("\n%s[TCP]%s Nó %s ligado no FD %d (Estado: COORDINATION).\n", 
                                    CYAN, RESET, neighbor_id, current_fd);
                            }
                        }
                        printf("> "); fflush(stdout);
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

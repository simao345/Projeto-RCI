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

 // Incluir os teus headers 
 #include "interface.h" 
 #include "server_udp.h" 
 #include "network_tcp.h" 

 #define BUFFER_SIZE 256 

 // --- Estrutura para a Tabela de Routing ---
 typedef struct {
     char dest[4];     
     int neighbor_fd;  
 } Route;

 // Estrutura para manter o estado global do nó no main 
 typedef struct { 
     char net[4]; 
     char id[4]; 
     char myIP[16]; 
     int myTCP; 
     int is_joined; 
     int prev_fd; 
     char prev_id[4]; 
     int next_fd; 
     char next_id[4];
     int monitoring;
     // ADIÇÃO: Tabela de Routing
     Route routing_table[100];
     int route_count;
 } NodeState; 

 // globais 
 NodeState node; 
 char regIP_buf[128];  
 char *regIP; 
 int regUDP;   

 // ADIÇÃO: Função auxiliar para gerir rotas
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

 void sigint_handler(int sig) { 
     if (node.is_joined) { 
         unregister_node(regIP, regUDP, node.net, node.id); 
     } 
     printf("\nA terminar aplicação...\n"); 
     exit(0); 
 } 

 int main(int argc, char *argv[]) { 

     signal(SIGINT, sigint_handler); 

     if (argc < 3 || argc > 5) { 
         fprintf(stderr, "Erro de invocação.\nUso correto: %s IP TCP [regIP] [regUDP]\n", argv[0]); 
         exit(1); 
     } 

     if (inet_pton(AF_INET, argv[1], &(struct in_addr){0}) != 1) { 
         fprintf(stderr, "Erro: IP do nó inválido.\n"); 
         exit(1); 
     } 
      
     memset(&node, 0, sizeof(node)); 
     node.is_joined = 0; 
     node.prev_fd = -1; 
     node.next_fd = -1; 
     node.route_count = 0;
     node.monitoring = 0;

     strcpy(node.myIP, argv[1]); 
     node.myTCP = atoi(argv[2]); 

     strncpy(regIP_buf, "193.136.138.142", sizeof(regIP_buf)-1); 
     regIP = regIP_buf; 
     regUDP = 59000; 

     if (argc >= 4) strncpy(regIP_buf, argv[3], sizeof(regIP_buf)-1); 
     if (argc == 5) regUDP = atoi(argv[4]); 

     printf("> Nó OWR iniciado em %s:%d. Servidor: %s:%d\n", node.myIP, node.myTCP, regIP, regUDP); 

     int listen_fd = setup_tcp_server(node.myTCP); 
     if (listen_fd == -1) exit(1); 
     printf("Servidor TCP à escuta na porta %d...\n> ", node.myTCP); 
     fflush(stdout); 

     while (1) { 
         fd_set rfds; 
         FD_ZERO(&rfds); 
         FD_SET(STDIN_FILENO, &rfds); 
         int max_fd = STDIN_FILENO; 
         FD_SET(listen_fd, &rfds); 
         if (listen_fd > max_fd) max_fd = listen_fd; 

         if (node.prev_fd != -1) { 
             FD_SET(node.prev_fd, &rfds); 
             if (node.prev_fd > max_fd) max_fd = node.prev_fd; 
         } 
         if (node.next_fd != -1) { 
             FD_SET(node.next_fd, &rfds); 
             if (node.next_fd > max_fd) max_fd = node.next_fd; 
         } 
          
         if (select(max_fd + 1, &rfds, NULL, NULL, NULL) < 0) continue; 

         if (FD_ISSET(listen_fd, &rfds)) { 
             int new_fd = accept(listen_fd, NULL, NULL); 
             if (new_fd != -1) { 
                 if (node.prev_fd == -1) { 
                     node.prev_fd = new_fd; 
                     printf("\n[TCP] Novo antecessor ligado (fd %d).\n> ", new_fd); 
                 } else { 
                     close(new_fd); 
                 } 
             } 
         } 

         if (FD_ISSET(STDIN_FILENO, &rfds)) { 
             char buffer[BUFFER_SIZE]; 
             char arg_net[20], arg_id[20]; 
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
                 case 5: // DIRECT
                     if (node.next_fd != -1) printf("Erro: Já tens ligação de saída.\n"); 
                     else { 
                         int fd = setup_tcp_client(arg_net, atoi(arg_id)); 
                         if (fd != -1) node.next_fd = fd; 
                     } 
                     break; 
                 case 6: // ADD EDGE
                     if (node.is_joined && node.next_fd == -1) { 
                         char target_ip[16]; int target_tcp; 
                         if (get_node_contact(regIP, regUDP, node.net, arg_net, target_ip, &target_tcp) == 0) { 
                             int fd = setup_tcp_client(target_ip, target_tcp); 
                             if (fd != -1) { 
                                 node.next_fd = fd; strcpy(node.next_id, arg_net); 
                                 char msg[64]; sprintf(msg, "NEIGHBOR %s\n", node.id); 
                                 write(node.next_fd, msg, strlen(msg)); 
                                 add_route(arg_net, node.next_fd); 
                             } 
                         } 
                     } 
                     break; 
                 case 7: // SHOW NEIGHBOURS
                     printf("--- Vizinhos do Nó %s ---\n", node.id); 
                     printf("-> Seguinte: %s (fd %d)\n", node.next_fd != -1 ? node.next_id : "---", node.next_fd); 
                     printf("<- Anterior: %s (fd %d)\n", node.prev_fd != -1 ? node.prev_id : "---", node.prev_fd); 
                     break; 
                 case 8: // REMOVE EDGE
                     if (node.next_fd != -1) { 
                         close(node.next_fd); node.next_fd = -1; node.next_id[0] = '\0'; 
                     } 
                     break; 
                 case 9: // ANNOUNCE
                     if (node.is_joined) { 
                         char route_msg[64]; sprintf(route_msg, "ROUTING %s\n", node.id); 
                         if (node.next_fd != -1) write(node.next_fd, route_msg, strlen(route_msg)); 
                         if (node.prev_fd != -1) write(node.prev_fd, route_msg, strlen(route_msg)); 
                         printf("Anúncio do nó %s enviado.\n", node.id); 
                     } 
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
                case 13: // message (m) <dest> <msg>
                    {
                        int found_fd = -1;
                        // 1. Procurar na tabela de routing qual o vizinho para chegar ao destino
                        for (int i = 0; i < node.route_count; i++) {
                            if (strcmp(node.routing_table[i].dest, arg_net) == 0) { // arg_net tem o <dest>
                                found_fd = node.routing_table[i].neighbor_fd;
                                break;
                            }
                        }

                        if (found_fd != -1) {
                            char chat_msg[512];
                            // Protocolo: CHAT <origem> <destino> <mensagem>
                            sprintf(chat_msg, "CHAT %s %s %s\n", node.id, arg_net, arg_id); // arg_id tem o texto
                            write(found_fd, chat_msg, strlen(chat_msg));
                            printf("Mensagem enviada para %s via FD %d.\n", arg_net, found_fd);
                        } else {
                            printf("Erro: Destino %s não encontrado na tabela de routing.\n", arg_net);
                        }
                    }
                    break;
                 default:
                     printf("Comando desconhecido ou formato inválido.\n");
             } 
             printf("> "); fflush(stdout); 
         } 

         // RECEBER DO ANTECESSOR (prev_fd) 
         if (node.prev_fd != -1 && FD_ISSET(node.prev_fd, &rfds)) { 
             char buffer[256];
             ssize_t n = read(node.prev_fd, buffer, sizeof(buffer) - 1); 
             
             if (n <= 0) { 
                 close(node.prev_fd); node.prev_fd = -1; node.prev_id[0] = '\0'; 
             } else {
                 buffer[n] = '\0';
                 if (node.monitoring) {
                     printf("[MONITOR] Recebido do antecessor: %s", buffer);
                 }

                 char cmd[32]; 
                 sscanf(buffer, "%s", cmd); 

                 if (strcmp(cmd, "NEIGHBOR") == 0) { 
                     char id_rec[16];
                     if (sscanf(buffer, "%*s %s", id_rec) == 1) {
                        strcpy(node.prev_id, id_rec); add_route(id_rec, node.prev_fd); 
                        printf("-> O Nó %s apresentou-se!\n", id_rec); 
                     }
                 } else if (strcmp(cmd, "ROUTING") == 0) { 
                     char id_rec[16];
                     if (sscanf(buffer, "%*s %s", id_rec) == 1) {
                        add_route(id_rec, node.prev_fd); 
                        if (node.next_fd != -1) write(node.next_fd, buffer, strlen(buffer)); 
                     }
                 }
                 else if (strcmp(cmd, "CHAT") == 0) {
                     char origem[4], destino[4], texto[256];
                     sscanf(buffer, "%*s %s %s %[^\n]", origem, destino, texto);

                     if (strcmp(destino, node.id) == 0) {
                         printf("\n[CHAT] De %s: %s\n> ", origem, texto);
                         fflush(stdout);
                     } else {
                         int route_fd = -1;
                         for (int i = 0; i < node.route_count; i++) {
                             if (strcmp(node.routing_table[i].dest, destino) == 0) {
                                 route_fd = node.routing_table[i].neighbor_fd;
                                 break;
                             }
                         }
                         if (route_fd != -1) {
                             write(route_fd, buffer, strlen(buffer));
                             if (node.monitoring) printf("[MONITOR] Reencaminhando CHAT para %s\n", destino);
                         }
                     }
                 }
             } 
         } 

         // RECEBER DO SUCESSOR (next_fd) 
         if (node.next_fd != -1 && FD_ISSET(node.next_fd, &rfds)) { 
             char buffer[BUFFER_SIZE];
             ssize_t n = read(node.next_fd, buffer, sizeof(buffer) - 1); 
             
             if (n <= 0) { 
                 close(node.next_fd); node.next_fd = -1; node.next_id[0] = '\0'; 
             } else { 
                 buffer[n] = '\0';
                 if (node.monitoring) {
                     printf("[MONITOR] Recebido do sucessor: %s", buffer);
                 }
                 
                 char cmd[32]; 
                 sscanf(buffer, "%s", cmd);

                 if (strcmp(cmd, "ROUTING") == 0) { 
                     char id_rec[16];
                     if (sscanf(buffer, "%*s %s", id_rec) == 1) {
                        add_route(id_rec, node.next_fd); 
                        if (node.prev_fd != -1) write(node.prev_fd, buffer, strlen(buffer)); 
                     }
                 }
                 else if (strcmp(cmd, "CHAT") == 0) {
                     char origem[4], destino[4], texto[256];
                     sscanf(buffer, "%*s %s %s %[^\n]", origem, destino, texto);

                     if (strcmp(destino, node.id) == 0) {
                         printf("\n[CHAT] De %s: %s\n> ", origem, texto);
                         fflush(stdout);
                     } else {
                         int route_fd = -1;
                         for (int i = 0; i < node.route_count; i++) {
                             if (strcmp(node.routing_table[i].dest, destino) == 0) {
                                 route_fd = node.routing_table[i].neighbor_fd;
                                 break;
                             }
                         }
                         if (route_fd != -1) {
                             write(route_fd, buffer, strlen(buffer));
                             if (node.monitoring) printf("[MONITOR] Reencaminhando CHAT para %s\n", destino);
                         }
                     }
                 }
             } 
         } 
     } 
     return 0; 
 }

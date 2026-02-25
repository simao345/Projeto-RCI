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

// Estrutura para manter o estado global do nó no main
typedef struct {
    char net[4];
    char id[3];
    char myIP[16];
    int myTCP;
    int is_joined;
    int prev_fd; // Socket de quem se ligou a mim (antecessor no anel)
    char prev_id[4]; // ID do nó antecessor
    int next_fd; // Socket a quem eu me liguei (sucessor no anel)
    char next_id[4]; // ID do nó sucessor
} NodeState;

// globais
NodeState node;
char regIP_buf[128];  // buffer aumentado para suportar nomes de domínio
char *regIP;
int regUDP;  

void sigint_handler(int sig) {
    if (node.is_joined) {
        unregister_node(regIP, regUDP, node.net, node.id);
    }
    printf("\nA terminar aplicação...\n");
    exit(0);
}

int main(int argc, char *argv[]) {

    signal(SIGINT, sigint_handler);

    // 1. Validar número de argumentos (Entre 3 e 5)
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Erro de invocação.\nUso correto: %s IP TCP [regIP] [regUDP]\n", argv[0]);
        exit(1);
    }

    if (inet_pton(AF_INET, argv[1], &(struct in_addr){0}) != 1) {
        fprintf(stderr, "Erro: IP do nó inválido.\n");
        exit(1);
    }
    
    node.myTCP = atoi(argv[2]);
    if (node.myTCP <= 0 || node.myTCP > 65535) {
        fprintf(stderr, "Erro: Porta TCP inválida.\n");
        exit(1);
    }

    memset(&node, 0, sizeof(node));
    node.is_joined = 0;
    node.prev_fd = -1;
    node.prev_id[0] = '\0';
    node.next_fd = -1;
    node.next_id[0] = '\0';

    // Guardar contactos locais
    strcpy(node.myIP, argv[1]);
    node.myTCP = atoi(argv[2]);

    // 2. Definir valores por omissão para o servidor (conforme o enunciado)
    strncpy(regIP_buf, "193.136.138.142", sizeof(regIP_buf)-1);
    regIP_buf[sizeof(regIP_buf)-1] = '\0';
    regIP = regIP_buf;
    regUDP = 59000;

    // 3. Substituir valores por omissão se forem fornecidos na invocação
    if (argc >= 4) {
        strncpy(regIP_buf, argv[3], sizeof(regIP_buf)-1);
        regIP_buf[sizeof(regIP_buf)-1] = '\0';
        regIP = regIP_buf;
    }
    if (argc == 5) {
        regUDP = atoi(argv[4]);
        if (regUDP <= 0 || regUDP > 65535) {
            fprintf(stderr, "Erro: Porta UDP do servidor inválida.\n");
            exit(1);
        }
    }

    printf("> Nó OWR iniciado em %s:%d. Servidor: %s:%d\n", node.myIP, node.myTCP, regIP, regUDP);

    // ==========================================
    // INICIAR SERVIDOR TCP
    // ==========================================
    int listen_fd = setup_tcp_server(node.myTCP);
    if (listen_fd == -1) {
        fprintf(stderr, "Erro ao iniciar o servidor TCP na porta %d.\n", node.myTCP);
        exit(1);
    }
    printf("Servidor TCP à escuta na porta %d...\n> ", node.myTCP);
    fflush(stdout);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        
        // 1. Monitorizar o teclado
        FD_SET(STDIN_FILENO, &rfds);
        int max_fd = STDIN_FILENO;

        // 2. Monitorizar o socket de escuta TCP
        FD_SET(listen_fd, &rfds);
        if (listen_fd > max_fd) {
            max_fd = listen_fd;
        }

        // 3. Monitorizar o socket do antecessor (se existir)
        if (node.prev_fd != -1) {
            FD_SET(node.prev_fd, &rfds);
            if (node.prev_fd > max_fd) max_fd = node.prev_fd;
        }

        // 4. Monitorizar o socket do sucessor (se existir)
        if (node.next_fd != -1) {
            FD_SET(node.next_fd, &rfds);
            if (node.next_fd > max_fd) max_fd = node.next_fd;
        }
        
        int activity = select(max_fd + 1, &rfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("Erro no select");
            continue;
        }

        // ==========================================
        // A. TRATAR NOVAS LIGAÇÕES TCP (O OUTRO NÓ)
        // ==========================================
        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            
            int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
            
            if (new_fd == -1) {
                perror("Erro no accept");
            } else {
                printf("\n[TCP] Nova ligação recebida do IP: %s\n", inet_ntoa(client_addr.sin_addr));
                
                // Se ainda não temos antecessor, aceitamos e guardamos o socket
                if (node.prev_fd == -1) {
                    node.prev_fd = new_fd;
                    printf("[TCP] Nó guardado como meu antecessor (prev_fd = %d).\n", new_fd);
                } else {
                    // Por enquanto rejeitamos se já tivermos um (para simplificar nesta fase)
                    printf("[TCP] Já tenho um antecessor. A fechar a nova ligação.\n");
                    close(new_fd);
                }
                printf("> "); fflush(stdout);
            }
        }

        // ==========================================
        // B. TRATAR ENTRADA DO UTILIZADOR (TECLADO)
        // ==========================================
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char buffer[BUFFER_SIZE];
            char arg_net[20], arg_id[20]; // Mantido com tamanho 20 para o IP do comando direct

            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;

            buffer[strcspn(buffer, "\n")] = 0; // remover o \n

            printf("Li o comando: %s \n", buffer);

            int cmd_type = parse_user_command(buffer, arg_net, arg_id);

            switch (cmd_type) {
                case 1: // JOIN (j)
                    if (node.is_joined) {
                        printf("O nó já está registado na rede %s.\n", node.net);
                    } else {
                        if (register_node(regIP, regUDP, arg_net, arg_id, node.myIP, node.myTCP) == 0) {
                            strcpy(node.net, arg_net);
                            strcpy(node.id, arg_id);
                            node.is_joined = 1;
                            // Frase apagada!
                        }
                    }
                    break;

                case 2: // LEAVE (l)
                    if (!node.is_joined) {
                        printf("O nó não está registado em nenhuma rede.\n");
                    } else {
                        if (unregister_node(regIP, regUDP, node.net, node.id) == 0) {
                            node.is_joined = 0;
                            // Frase apagada!
                        }
                    }
                    break;

                case 3: // EXIT (x)
                    if (node.is_joined) {
                        unregister_node(regIP, regUDP, node.net, node.id);
                    }
                    printf("A terminar aplicação...\n");
                    exit(0);

                case 4: // NODES (n)
                    if (strlen(arg_net) > 0) {
                        nodes_query(arg_net, regIP, regUDP);
                    } else if (node.is_joined) {
                        nodes_query(node.net, regIP, regUDP);
                    } else {
                        printf("Erro: Indica a rede ou regista-te primeiro (uso: n <net>).\n");
                    }
                    break;
                    
                case 5: // DIRECT (d)
                    if (node.next_fd != -1) {
                        printf("Erro: Já tens uma ligação de saída (next_fd = %d).\n", node.next_fd);
                    } else {
                        int target_port = atoi(arg_id); // arg_id tem a porta
                        int fd = setup_tcp_client(arg_net, target_port); // arg_net tem o IP
                        
                        if (fd != -1) {
                            node.next_fd = fd;
                            printf("[TCP] Ligação direta estabelecida com %s:%d (next_fd = %d)\n", arg_net, target_port, fd);
                        }
                    }
                    break;

                case 6: // ADD EDGE (ae <id> ou add edge <id>)
                    if (!node.is_joined) {
                        printf("Erro: O nó não está registado em nenhuma rede.\n");
                    } else if (node.next_fd != -1) {
                        printf("Erro: Já tens uma ligação de saída ativa.\n");
                    } else {
                        char target_ip[16];
                        int target_tcp;
                        
                        // NOTA: O nosso interface.c guardou o ID do vizinho na variável 'arg_net'
                        // 1. Perguntar ao servidor UDP quem é este nó
                        if (get_node_contact(regIP, regUDP, node.net, arg_net, target_ip, &target_tcp) == 0) {
                            printf("-> Contacto obtido! A ligar ao Nó %s (%s:%d)...\n", arg_net, target_ip, target_tcp);
                            
                            // 2. Usar os dados recebidos para estabelecer o TCP
                            int fd = setup_tcp_client(target_ip, target_tcp);
                            
                            if (fd != -1) {
                                node.next_fd = fd;
                                strcpy(node.next_id, arg_net);

                                printf("[TCP] Ligação P2P estabelecida com sucesso com o Nó %s! (next_fd = %d)\n", arg_net, fd);

                                char msg[64];
                                sprintf(msg, "NEIGHBOR %s\n", node.id);
                                write(node.next_fd, msg, strlen(msg)); // Envia para o tubo
                                printf("Enviado no TCP: %s", msg);
                            }
                        } else {
                            printf("-> Erro: Nó %s não encontrado na rede %s.\n", arg_net, node.net);
                        }
                    }
                    break;

               case 7: // SHOW NEIGHBOURS (sg)
                    printf("--- Vizinhos do Nó %s ---\n", node.id);
                    
                    if (node.next_fd != -1) {
                        printf("-> Vizinho Seguinte: Nó %s (fd %d)\n", node.next_id, node.next_fd);
                    } else {
                        printf("-> Sem Vizinho Seguinte\n");
                    }
                    
                    if (node.prev_fd != -1) {
                        printf("<- Vizinho Anterior: Nó %s (fd %d)\n", node.prev_id, node.prev_fd);
                    } else {
                        printf("<- Sem Vizinho Anterior\n");
                    }
                    printf("--------------------------\n");
                    break;

                case 8: // REMOVE EDGE (re / remove edge)
                    if (node.next_fd != -1) {
                        printf("-> A fechar a ligação P2P de saída com o Nó %s (fd %d)...\n", node.next_id, node.next_fd);
                        
                        // 1. Fechar o socket TCP
                        close(node.next_fd);
                        
                        // 2. Limpar o estado do nó (FD e ID)
                        node.next_fd = -1;
                        node.next_id[0] = '\0'; // <--- NOVO: Limpar o ID do vizinho!
                        
                        printf("[TCP] Ligação com o vizinho seguinte fechada com sucesso.\n");
                    } else {
                        printf("Erro: Não tens nenhuma ligação de saída (vizinho seguinte) ativa para remover.\n");
                    }
                    break;

                default:
                    // Comandos desconhecidos
                    break;
            }
            printf("> "); fflush(stdout);
        }

        // ==========================================
        // C. TRATAR MENSAGENS DO ANTECESSOR (prev_fd)
        // ==========================================
        if (node.prev_fd != -1 && FD_ISSET(node.prev_fd, &rfds)) {
            char buffer[256];
            ssize_t n = read(node.prev_fd, buffer, sizeof(buffer) - 1);
            
            if (n <= 0) {
                // Se read retornar 0 ou menos, a ligação caiu
                printf("[Aviso] O vizinho anterior fechou a ligação.\n");
                close(node.prev_fd);
                node.prev_fd = -1;
                node.prev_id[0] = '\0'; // Limpar o ID do vizinho anterior
            } else {
                // Recebemos uma mensagem de texto!
                buffer[n] = '\0'; // Garantir que é uma string válida
                
                char cmd[32], id_recebido[16];
                
                // Extrair o comando e o ID da mensagem (ex: "NEIGHBOR 10")
                if (sscanf(buffer, "%s %s", cmd, id_recebido) == 2) {
                    if (strcmp(cmd, "NEIGHBOR") == 0) {

                        strcpy(node.prev_id, id_recebido);
                        printf("Recebido no TCP: %s", buffer); // O buffer já deve trazer o \n
                        printf("-> O Nó %s apresentou-se como meu novo vizinho anterior!\n", id_recebido);
                        
                        // Dica de Mestre: Se tiveres uma variável tipo 'node.prev_id' na tua struct, 
                        // é aqui que deves fazer: strcpy(node.prev_id, id_recebido);
                    }
                }
            }
        }

        // ==========================================
        // D. TRATAR MENSAGENS DO SUCESSOR (next_fd)
        // ==========================================
        if (node.next_fd != -1 && FD_ISSET(node.next_fd, &rfds)) {
            char buffer[BUFFER_SIZE];
            ssize_t n = read(node.next_fd, buffer, sizeof(buffer) - 1);
            
            if (n <= 0) {
                // Se read() devolve 0 ou erro, a ligação caiu
                printf("\n[Aviso] O nó sucessor desligou-se.\n> ");
                close(node.next_fd);
                node.next_fd = -1;
            } else {
                buffer[n] = '\0';
                printf("\n[Mensagem Recebida do Sucessor]: %s\n> ", buffer);
            }
            fflush(stdout);
        }
    }

    return 0;
}

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
#include "logic.h"

#define BUFFER_SIZE 256


// Estrutura para manter o estado global do nó no main
typedef struct {
    char net[4];
    char id[3];
    char myIP[16];
    int myTCP;
    int is_joined;
    int prev_fd; // Socket de quem se ligou a mim (antecessor no anel)
    int next_fd; // Socket a quem eu me liguei (sucessor no anel)
} NodeState;

// globais
NodeState node;
char regIP_buf[16];   // buffer fixo -> Usar buffer fixo evita dangling pointer e torna SIGINT seguro -> honestamente não sei mas é uma boa prática pelo que estive a ver
char *regIP;
int regUDP;  

//Depois mover isto para um ficheiro separado ?
void sigint_handler(int sig) {
    if (node.is_joined) {
        unregister_node(regIP, regUDP, node.net, node.id);
    }
    printf("\nA terminar aplicação...\n");
    exit(0);
}


int main(int argc, char *argv[]) {

    signal(SIGINT, sigint_handler);

    // 1. Validar argumentos: OWR IP TCP [regIP regUDP]
    if (argc < 3) {
        fprintf(stderr, "Uso: %s IP TCP [regIP regUDP]\n", argv[0]);
        exit(1);
    }

    if (inet_pton(AF_INET, argv[1], &(struct in_addr){0}) != 1) {
        fprintf(stderr, "IP inválido.\n");
        exit(1);
    }
    
    node.myTCP = atoi(argv[2]);
    if (node.myTCP <= 0 || node.myTCP > 65535) {
        fprintf(stderr, "Porta TCP inválida.\n");
        exit(1);
    }

    memset(&node, 0, sizeof(node));
    node.is_joined = 0;
    node.prev_fd = -1;
    node.next_fd = -1;

    // Guardar contactos locais
    strcpy(node.myIP, argv[1]);
    node.myTCP = atoi(argv[2]);

    // Contactos do servidor (valores por omissão do enunciado [cite: 129])
    if (argc > 3) {
        strncpy(regIP_buf, argv[3], sizeof(regIP_buf)-1);
        regIP_buf[sizeof(regIP_buf)-1] = '\0';
    } else {
        snprintf(regIP_buf, sizeof(regIP_buf), "%s", "193.136.138.142");
    }
    regIP = regIP_buf;

    regUDP = (argc > 4) ? atoi(argv[4]) : 59000;
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

        printf("> "); fflush(stdout);

        // Na Etapa 3, adicionarás aqui o socket de escuta TCP
        // 2. Monitorizar o socket de escuta TCP
        FD_SET(listen_fd, &rfds);
        if (listen_fd > max_fd) {
            max_fd = listen_fd;
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

            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;

            buffer[strcspn(buffer, "\n")] = 0; //aquele shenanigan do lab 1 para tirar o \n
            char arg_net[4], arg_id[3];

            printf("> "); fflush(stdout);
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
                            printf("Entrada na rede %s concluida.\n", node.net);

                            //printf("Estado atual: %s%s\n", node.is_joined ? "Registado" : "Não registado", node.is_joined ? node.net : "");
                        }
                    }
                    break;

                case 2: // LEAVE (l)
                    if (!node.is_joined) {
                        printf("O nó não está registado em nenhuma rede.\n");
                    } else {
                        if (unregister_node(regIP, regUDP, node.net, node.id) == 0) {
                            node.is_joined = 0;
                            printf("Saída da rede %s concluída.\n", node.net);

                            //printf("Estado atual: %s%s\n", node.is_joined ? "Registado" : "Não registado", node.is_joined ? node.net : "");
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
                    
                default:
                    // Comandos desconhecidos (tratado no interface.c)
                    break;
            }
        }
    }

    return 0;
}

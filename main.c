#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
} NodeState;

int main(int argc, char *argv[]) {
    // 1. Validar argumentos: OWR IP TCP [regIP regUDP]
    if (argc < 3) {
        fprintf(stderr, "Uso: %s IP TCP [regIP regUDP]\n", argv[0]);
        exit(1);
    }

    NodeState node;
    memset(&node, 0, sizeof(node));
    node.is_joined = 0;

    // Guardar contactos locais
    strcpy(node.myIP, argv[1]);
    node.myTCP = atoi(argv[2]);

    // Contactos do servidor (valores por omissão do enunciado [cite: 129])
    char *regIP = (argc > 3) ? argv[3] : "193.136.138.142";
    int regUDP = (argc > 4) ? atoi(argv[4]) : 59000;

    printf("Nó OWR iniciado em %s:%d. Servidor: %s:%d\n", node.myIP, node.myTCP, regIP, regUDP);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        
        // Monitorizar apenas o teclado para a Etapa 2
        FD_SET(STDIN_FILENO, &rfds);
        int max_fd = STDIN_FILENO;

        // Na Etapa 3, adicionarás aqui o socket de escuta TCP
        
        int activity = select(max_fd + 1, &rfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("Erro no select");
            continue;
        }

        // Tratar entrada do utilizador
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char buffer[BUFFER_SIZE];
            char arg_net[4], arg_id[3];

            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;
            printf("Li o comando: %s", buffer);

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
                        }
                    }
                    break;

                case 3: // EXIT (x)
                    if (node.is_joined) {
                        unregister_node(regIP, regUDP, node.net, node.id);
                    }
                    printf("A terminar aplicação...\n");
                    exit(0);

                default:
                    // Outros comandos (show, add edge) serão tratados nas próximas etapas
                    break;

                case 4: // NODES (n)
                    // Se o utilizador escreveu "n 001", o parse_user_command deve guardar "001" em arg_net
                    // Se o utilizador escreveu apenas "n" e já estiver em join, usamos node.net
                    if (strlen(arg_net) > 0) {
                        nodes_query(arg_net, regIP, regUDP);
                    } else if (node.is_joined) {
                        nodes_query(node.net, regIP, regUDP);
                    } else {
                        printf("Erro: Indica a rede ou regista-te primeiro (uso: n <net>).\n");
                    }
                    break;
            }
            printf("> "); fflush(stdout);
        }
    }

    return 0;
}
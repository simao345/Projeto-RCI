#include "network_tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int setup_tcp_server(int port) {
    int fd;
    struct sockaddr_in server_addr;

    // 1. Criar o socket TCP (SOCK_STREAM)
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Erro ao criar socket TCP");
        return -1;
    }

    // 2. DICA DE OURO: Evitar o erro "Address already in use"
    // Muito útil porque vais fechar e abrir o programa muitas vezes durante os testes
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Erro no setsockopt");
        close(fd);
        return -1;
    }

    // 3. Configurar o endereço
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Ouve em qualquer IP da tua máquina
    server_addr.sin_port = htons(port);

    // 4. Associar o socket à porta (bind)
    if (bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro no bind TCP");
        close(fd);
        return -1;
    }

    // 5. Colocar o socket à escuta (listen)
    // O '5' é o tamanho da fila de nós que podem ficar em espera para se ligarem a ti
    if (listen(fd, 5) == -1) {
        perror("Erro no listen TCP");
        close(fd);
        return -1;
    }

    // Retorna o File Descriptor para o main() poder usá-lo no select()
    return fd;
}
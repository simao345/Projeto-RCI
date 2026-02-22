#include "server_udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Função para registar o nó no servidor (Comando JOIN)
int register_node(char* regIP, int regUDP, char* net, char* id, char* myIP, int myTCP) {
    struct sockaddr_in server_addr;
    char buffer[256];
    char message[256];
    
    // 1. Criar socket UDP
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("Erro ao criar socket UDP");
        return -1;
    }

    // 2. Configurar endereço do servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(regIP);
    server_addr.sin_port = htons(regUDP);

    // 3. Formatar a mensagem: REG net id IP TCP
    sprintf(message, "REG %s %s %s %d\n", net, id, myIP, myTCP);

    // 4. Enviar para o servidor
    if (sendto(fd, message, strlen(message), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro no sendto");
        close(fd);
        return -1;
    }

    // 5. Receber resposta (OKREG DONE / OKREG DUP)
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    int n = recvfrom(fd, buffer, 255, 0, (struct sockaddr*)&from_addr, &addr_len);
    
    if (n > 0) {
        buffer[n] = '\0';
        if (strstr(buffer, "OKREG DONE") != NULL) {
            printf("Sucesso: Nó registado no servidor.\n");
            close(fd);
            return 0; 
        } else if (strstr(buffer, "OKREG DUP") != NULL) {
            printf("Erro: O identificador %s já está em uso na rede %s.\n", id, net);
            close(fd);
            return -1;
        }
    }

    printf("Erro: Servidor de nós não respondeu corretamente.\n");
    close(fd);
    return -1;
}

// Função para remover o registo no servidor (Comando LEAVE / EXIT)
int unregister_node(char* regIP, int regUDP, char* net, char* id) {
    struct sockaddr_in server_addr;
    char message[256];
    char buffer[256];
    
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) return -1;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(regIP);
    server_addr.sin_port = htons(regUDP);

    // 1. Formatar a mensagem: UNREG net id
    sprintf(message, "UNREG %s %s\n", net, id);

    // 2. Enviar
    sendto(fd, message, strlen(message), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    // 3. Receber confirmação: OKUNREG DONE
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    char res_buffer[256];
    int n = recvfrom(fd, res_buffer, 255, 0, (struct sockaddr*)&from_addr, &addr_len);

    if (n > 0) {
        res_buffer[n] = '\0';
    }

    close(fd);
    return 0;
}

void nodes_query(char* net, char* server_ip, int server_port) {
    int fd;
    struct sockaddr_in server_addr;
    char message[128];
    char buffer[2048]; // Buffer maior para a lista de nós
    ssize_t n;
    socklen_t addrlen;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) return;

    // Timeout de 2 segundos para não ficar bloqueado
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    // Formatar comando: NODES <net>\n
    sprintf(message, "NODES %s\n", net);

    sendto(fd, message, strlen(message), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    addrlen = sizeof(server_addr);
    n = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&server_addr, &addrlen);

    if (n > 0) {
        buffer[n] = '\0'; // Garante que a string termina bem
        printf("Lista de Nós na rede %s:\n%s", net, buffer);
    } else {
        printf("Erro: Não foi possível obter a lista de nós (Timeout).\n");
    }

    close(fd);
}

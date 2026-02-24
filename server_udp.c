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
#include <time.h>

// Função auxiliar para gerar um TID (Transaction ID) de 3 dígitos 
void generate_tid(char *tid) {
    sprintf(tid, "%03d", rand() % 1000);
}

// Função para registar o nó no servidor (Comando JOIN)
int register_node(char* regIP, int regUDP, char* net, char* id, char* myIP, int myTCP) {
    struct sockaddr_in server_addr;
    char buffer[256];
    char message[256];
    char tid[4];
    
    // Inicializar seed para o gerador de números aleatórios
    static int seeded = 0;
    if (!seeded) { srand(time(NULL)); seeded = 1; }

    generate_tid(tid);
    
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("Erro ao criar socket UDP");
        return -1;
    }

    // Timeout de 2 segundos para não ficar bloqueado
    struct timeval tv;
    tv.tv_sec = 2; 
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(regIP);
    server_addr.sin_port = htons(regUDP);

    // Formato V2: REG tid op net id IP TCP\n (op = 0 para registar) 
    sprintf(message, "REG %s 0 %s %s %s %d\n", tid, net, id, myIP, myTCP);

    if (sendto(fd, message, strlen(message), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro no sendto");
        close(fd);
        return -1;
    }

    // Receber resposta
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    int n = recvfrom(fd, buffer, 255, 0, (struct sockaddr*)&from_addr, &addr_len);
    
    if (n > 0) {
        buffer[n] = '\0';
        char expected_success[32];
        char expected_full[32];
        sprintf(expected_success, "REG %s 1", tid); // op = 1 (sucesso) 
        sprintf(expected_full, "REG %s 2", tid);    // op = 2 (base de dados cheia) 

        if (strstr(buffer, expected_success) != NULL) {
            printf("Sucesso: Nó registado no servidor.\n");
            close(fd);
            return 0; 
        } else if (strstr(buffer, expected_full) != NULL) {
            printf("Erro: A base de dados do servidor está cheia.\n");
            close(fd);
            return -1;
        } else {
            printf("Erro: Resposta inesperada do servidor: %s\n", buffer);
            close(fd);
            return -1;
        }
    }

    printf("Erro: Servidor de nós não respondeu corretamente (Timeout).\n");
    close(fd);
    return -1;
}

// Função para remover o registo no servidor (Comando LEAVE / EXIT)
int unregister_node(char* regIP, int regUDP, char* net, char* id) {
    struct sockaddr_in server_addr;
    char message[256];
    char buffer[256];
    char tid[4];
    
    static int seeded = 0;
    if (!seeded) { srand(time(NULL)); seeded = 1; }

    generate_tid(tid);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) return -1;

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(regIP);
    server_addr.sin_port = htons(regUDP);

    // Formato V2: REG tid op net id (op = 3 para solicitar remoção, sem campos IP e TCP) 
    sprintf(message, "REG %s 3 %s %s\n", tid, net, id);

    sendto(fd, message, strlen(message), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    int n = recvfrom(fd, buffer, 255, 0, (struct sockaddr*)&from_addr, &addr_len);

    if (n > 0) {
        buffer[n] = '\0';
        char expected_success[32];
        sprintf(expected_success, "REG %s 4", tid); // op = 4 (confirmação de remoção) 
        
        if (strstr(buffer, expected_success) != NULL) {
            // Sucesso
        }
    }

    close(fd);
    return 0;
}

void nodes_query(char* net, char* server_ip, int server_port) {
    int fd;
    struct sockaddr_in server_addr;
    char message[128];
    char buffer[2048]; 
    char tid[4];
    ssize_t n;
    socklen_t addrlen;

    static int seeded = 0;
    if (!seeded) { srand(time(NULL)); seeded = 1; }

    generate_tid(tid);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) return;

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    // Formato V2: NODES tid op net\n (op = 0 para solicitar a lista de nós) 
    sprintf(message, "NODES %s 0 %s\n", tid, net);

    sendto(fd, message, strlen(message), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    addrlen = sizeof(server_addr);
    n = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&server_addr, &addrlen);

    if (n > 0) {
        buffer[n] = '\0'; 
        char expected_success[32];
        sprintf(expected_success, "NODES %s 1", tid); // op = 1 (resposta de sucesso contendo os nós) 

        if (strncmp(buffer, expected_success, strlen(expected_success)) == 0) {
            // Avança o ponteiro para a linha de baixo para não imprimir o "NODES 123 1 001"
            char *list_start = strchr(buffer, '\n');
            if (list_start != NULL) {
                printf("Lista de Nós na rede %s:%s", net, list_start + 1);
            } else {
                printf("Lista de Nós na rede %s:\n%s", net, buffer);
            }
        } else {
            printf("Erro na resposta do servidor: %s\n", buffer);
        }
    } else {
        printf("Erro: Não foi possível obter a lista de nós (Timeout).\n");
    }

    close(fd);
}

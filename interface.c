//Tratamento das strings lidas pelo fgets() (comandos join, add edge, show routing, etc.).

#include "interface.h"
#include <string.h>
#include <stdio.h>

int parse_user_command(char *buffer, char *net, char *id) {
    char cmd[20];
    // Inicializar strings para evitar lixo de memória
    net[0] = '\0';
    id[0] = '\0';
    
    int num = sscanf(buffer, "%s %s %s", cmd, net, id);
    if (num <= 0) return 0;

    // JOIN
    if (strcmp(cmd, "join") == 0 || strcmp(cmd, "j") == 0) {
        if (num < 3) {
            printf("Erro: uso correto: join net id\n");
            return 0;
        }
        return 1;
    }
    // LEAVE
    else if (strcmp(cmd, "leave") == 0 || strcmp(cmd, "l") == 0) {
        return 2;
    }
    // EXIT
    else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "x") == 0) {
        return 3;
    }
    // NODES
    else if (strcmp(cmd, "nodes") == 0 || strcmp(cmd, "n") == 0) {
        // Se o utilizador não deu a rede (ex: escreveu apenas 'n'), 
        // o sscanf não preenche o arg_net, o que está previsto no teu main.
        return 4;
    }
    // DIRECT
    else if (strcmp(cmd, "direct") == 0 || strcmp(cmd, "d") == 0) {
        if (num < 3) {
            printf("Erro: uso correto: d IP TCP\n");
            return 0;
        }
        return 5; // Retornamos 5 para o main saber que é o comando direct
    }
    // ADD EDGE (CONTACT / LIGAÇÃO AUTOMÁTICA)
    else if (strcmp(cmd, "ae") == 0) {
        if (num < 2) {
            printf("Erro: uso correto: ae id\n");
            return 0;
        }
        return 6; 
    }
    // ADD EDGE (Escrito por extenso, com espaço)
    else if (strcmp(cmd, "add") == 0 && strcmp(net, "edge") == 0) {
        if (num < 3) {
            printf("Erro: uso correto: add edge id\n");
            return 0;
        }
        // Como o sscanf partiu nos espaços, o ID ficou guardado no argumento 'id'.
        // Vamos copiá-lo para o 'net' para o main.c o encontrar no mesmo sítio do 'ae'.
        strcpy(net, id);
        id[0] = '\0';
        return 6;
    }
    // SHOW NEIGHBOURS
    else if (strcmp(cmd, "sg") == 0 || (strcmp(cmd, "show") == 0 && strcmp(net, "neighbours") == 0)) {
        return 7; 
    }
    // REMOVE EDGE (re)
    else if (strcmp(cmd, "re") == 0) {
        return 8; 
    }
    // REMOVE EDGE (Escrito por extenso, com espaço)
    else if (strcmp(cmd, "remove") == 0 && strcmp(net, "edge") == 0) {
        return 8;
    }
    
    printf("Comando desconhecido: %s\n", cmd);
    return 0;
}
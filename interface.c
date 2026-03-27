/* Tratamento das strings lidas pelo fgets() (comandos join, add edge, show routing, etc.) */
#include "interface.h"
#include <string.h>
#include <stdio.h>

int parse_user_command(char *buffer, char *net, char *id) {
    char cmd[20];

    /* Inicializa strings para evitar leitura de memória não inicializada */
    net[0] = '\0';
    id[0]  = '\0';

    int num = sscanf(buffer, "%19s %s %s", cmd, net, id);
    if (num <= 0) return 0;

    /* JOIN */
    if (strcmp(cmd, "join") == 0 || strcmp(cmd, "j") == 0) {
        if (num < 3) { printf("Erro: uso correcto: join net id\n"); return 0; }
        return 1;
    }
    /* LEAVE */
    else if (strcmp(cmd, "leave") == 0 || strcmp(cmd, "l") == 0) {
        return 2;
    }
    /* EXIT */
    else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "x") == 0) {
        return 3;
    }
    /* NODES — lista os identificadores dos nós da rede */
    else if (strcmp(cmd, "nodes") == 0 || strcmp(cmd, "n") == 0) {
        return 4;
    }
    /* ADD EDGE — estabelece ligação com o nó indicado (forma abreviada) */
    else if (strcmp(cmd, "ae") == 0) {
        if (num < 2) { printf("Erro: uso correcto: ae id\n"); return 0; }
        return 6;
    }
    /* ADD EDGE — forma por extenso */
    else if (strcmp(cmd, "add") == 0 && strcmp(net, "edge") == 0) {
        if (num < 3) { printf("Erro: uso correcto: add edge id\n"); return 0; }
        /* O sscanf dividiu nos espaços; o id ficou no terceiro campo — move para net */
        strcpy(net, id);
        id[0] = '\0';
        return 6;
    }
    /* SHOW NEIGHBOURS — apresenta a lista de vizinhos */
    else if (strcmp(cmd, "sg") == 0 ||
             (strcmp(cmd, "show") == 0 &&
              (strcmp(net, "neighbours") == 0 || strcmp(net, "neighbors") == 0))) {
        return 7;
    }
    /* REMOVE EDGE — remove a ligação com o nó indicado (forma abreviada) */
    else if (strcmp(cmd, "re") == 0) {
        if (num < 2) { printf("Erro: uso correcto: re id\n"); return 0; }
        return 8;
    }
    /* REMOVE EDGE — forma por extenso */
    else if (strcmp(cmd, "remove") == 0 && strcmp(net, "edge") == 0) {
        if (num < 3) { printf("Erro: uso correcto: remove edge id\n"); return 0; }
        /* Move o id do terceiro campo para net, à semelhança do caso ae */
        strcpy(net, id);
        id[0] = '\0';
        return 8;
    }
    /* ANNOUNCE — anuncia o nó na rede sobreposta */
    else if (strcmp(cmd, "a") == 0 || strcmp(cmd, "announce") == 0) {
        return 9;
    }
    /* SHOW ROUTING — apresenta o estado de encaminhamento para um destino */
    else if (strcmp(cmd, "sr") == 0 ||
             (strcmp(cmd, "show") == 0 && strcmp(net, "routing") == 0)) {
        return 10;
    }
    /* START MONITOR — activa a monitorização de mensagens de encaminhamento */
    else if (strcmp(cmd, "sm") == 0 ||
             (strcmp(cmd, "start") == 0 && strcmp(net, "monitor") == 0)) {
        return 11;
    }
    /* END MONITOR — desactiva a monitorização */
    else if (strcmp(cmd, "em") == 0 ||
             (strcmp(cmd, "end") == 0 && strcmp(net, "monitor") == 0)) {
        return 12;
    }
    /* MESSAGE — envia mensagem de chat ao nó destino */
    else if (strcmp(cmd, "message") == 0 || strcmp(cmd, "m") == 0) {
        if (sscanf(buffer, "%*s %s %[^\n]", net, id) < 2) {
            printf("Erro: uso correcto: m dest mensagem\n");
            return 0;
        }
        return 13;
    }
    /* DIRECT JOIN — adesão à rede sem registo no servidor de nós */
    else if (strcmp(cmd, "dj") == 0) {
        if (num < 3) { printf("Erro: uso correcto: dj net id\n"); return 0; }
        return 14;
    }
    /* DIRECT ADD EDGE — estabelece ligação directa sem consultar o servidor de nós */
    else if (strcmp(cmd, "dae") == 0) {
        char dae_id[20] = {0}, dae_ip[64] = {0}, dae_tcp[16] = {0};
        if (sscanf(buffer, "%*s %19s %63s %15s", dae_id, dae_ip, dae_tcp) < 3) {
            printf("Erro: uso correcto: dae id IP TCP\n");
            return 0;
        }
        /* main espera: net=id, id="IP TCP" */
        strncpy(net, dae_id, 19); net[19] = '\0';
        snprintf(id, 512, "%s %s", dae_ip, dae_tcp);
        return 15;
    }

    printf("Comando desconhecido: %s\n", cmd);
    return 0;
}
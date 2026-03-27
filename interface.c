#include "interface.h"
#include <string.h>
#include <stdio.h>

int parse_user_command(char *buffer, char *net, char *id) {
    char cmd[20];
    net[0] = '\0';
    id[0]  = '\0';

    int num = sscanf(buffer, "%19s %s %s", cmd, net, id);
    if (num <= 0) return 0;

    /* JOIN */
    if (strcmp(cmd, "join") == 0 || strcmp(cmd, "j") == 0) {
        if (num < 3) { printf("Erro: uso correto: join net id\n"); return 0; }
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
    /* NODES */
    else if (strcmp(cmd, "nodes") == 0 || strcmp(cmd, "n") == 0) {
        return 4;
    }
    /* ADD EDGE (ae / add edge) */
    else if (strcmp(cmd, "ae") == 0) {
        if (num < 2) { printf("Erro: uso correto: ae id\n"); return 0; }
        return 6;
    }
    else if (strcmp(cmd, "add") == 0 && strcmp(net, "edge") == 0) {
        if (num < 3) { printf("Erro: uso correto: add edge id\n"); return 0; }
        strcpy(net, id);
        id[0] = '\0';
        return 6;
    }
    /* SHOW NEIGHBOURS (sg / show neighbours) */
    else if (strcmp(cmd, "sg") == 0 ||
             (strcmp(cmd, "show") == 0 &&
              (strcmp(net, "neighbours") == 0 || strcmp(net, "neighbors") == 0))) {
        return 7;
    }
    /* REMOVE EDGE (re / remove edge) */
    else if (strcmp(cmd, "re") == 0) {
        if (num < 2) { printf("Erro: uso correto: re id\n"); return 0; }
        return 8;
    }
    else if (strcmp(cmd, "remove") == 0 && strcmp(net, "edge") == 0) {
        if (num < 3) { printf("Erro: uso correto: remove edge id\n"); return 0; }
        /* id is in the third token — move it to net so main finds it there */
        strcpy(net, id);
        id[0] = '\0';
        return 8;
    }
    /* ANNOUNCE (a / announce) */
    else if (strcmp(cmd, "a") == 0 || strcmp(cmd, "announce") == 0) {
        return 9;
    }
    /* SHOW ROUTING (sr / show routing) */
    else if (strcmp(cmd, "sr") == 0 ||
             (strcmp(cmd, "show") == 0 && strcmp(net, "routing") == 0)) {
        return 10;
    }
    /* START MONITOR (sm / start monitor) */
    else if (strcmp(cmd, "sm") == 0 ||
             (strcmp(cmd, "start") == 0 && strcmp(net, "monitor") == 0)) {
        return 11;
    }
    /* END MONITOR (em / end monitor) */
    else if (strcmp(cmd, "em") == 0 ||
             (strcmp(cmd, "end") == 0 && strcmp(net, "monitor") == 0)) {
        return 12;
    }
    /* MESSAGE (m dest text) */
    else if (strcmp(cmd, "message") == 0 || strcmp(cmd, "m") == 0) {
        if (sscanf(buffer, "%*s %s %[^\n]", net, id) < 2) {
            printf("Erro: uso correto: m dest mensagem\n");
            return 0;
        }
        return 13;
    }
    /* DIRECT JOIN (dj net id) */
    else if (strcmp(cmd, "dj") == 0 || strcmp(cmd, "direct") == 0) {
        if (num < 3) { printf("Erro: uso correto: dj net id\n"); return 0; }
        return 14;
    }
    /* DIRECT ADD EDGE (dae id idIP idTCP) */
    else if (strcmp(cmd, "dae") == 0) {
        /* Need: id  IP  TCP — re-parse so id=id and net="IP TCP" for main */
        char dae_id[20] = {0}, dae_ip[64] = {0}, dae_tcp[16] = {0};
        if (sscanf(buffer, "%*s %19s %63s %15s", dae_id, dae_ip, dae_tcp) < 3) {
            printf("Erro: uso correto: dae id IP TCP\n");
            return 0;
        }
        /* main expects: net=id, id="IP TCP" */
        strncpy(net, dae_id, 19); net[19] = '\0';
        snprintf(id, 512, "%s %s", dae_ip, dae_tcp);
        return 15;
    }

    printf("Comando desconhecido: %s\n", cmd);
    return 0;
}
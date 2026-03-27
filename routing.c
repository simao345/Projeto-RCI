#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "routing.h"
#include "colours.h"

#define INF 999

NodeState node;

/* ------------------------------------------------------------------ */
/*  Funções auxiliares                                                  */
/* ------------------------------------------------------------------ */

Route *find_route(const char *dest) {
    for (int i = 0; i < node.route_count; i++)
        if (strcmp(node.routing_table[i].dest, dest) == 0)
            return &node.routing_table[i];
    return NULL;
}

Route *find_or_create_route(const char *dest) {
    Route *r = find_route(dest);
    if (r) return r;
    if (node.route_count >= MAX_ROUTES) return NULL;

    r = &node.routing_table[node.route_count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->dest, dest, 3);
    r->dest[3]       = '\0';
    r->distance      = INF;
    r->succ_fd       = -1;
    r->state         = FORWARDING;   /* o invocador decide se é necessário entrar em COORDINATION */
    r->succ_coord_fd = -1;
    return r;
}

/* Envia "ROUTE dest n\n" ao fd indicado */
void send_route_to_fd(Route *r, int fd) {
    if (r->distance >= INF) return;   /* nunca se anuncia distância infinita */
    char msg[64];
    snprintf(msg, sizeof(msg), "ROUTE %s %d\n", r->dest, r->distance);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s ENVIO ROUTE %s %d → fd %d\n> ",
               MAGENTA, RESET, r->dest, r->distance, fd);
}

/* Envia "ROUTE dest n\n" a todos os vizinhos */
void send_route_to_all(Route *r) {
    for (int j = 0; j < node.neighbor_count; j++)
        send_route_to_fd(r, node.neighbors[j].fd);
}

/* Envia "COORD dest\n" ao fd indicado */
static void send_coord(const char *dest, int fd) {
    char msg[64];
    snprintf(msg, sizeof(msg), "COORD %s\n", dest);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s ENVIO COORD %s → fd %d\n> ",
               MAGENTA, RESET, dest, fd);
}

/* Envia "UNCOORD dest\n" ao fd indicado */
static void send_uncoord(const char *dest, int fd) {
    char msg[64];
    snprintf(msg, sizeof(msg), "UNCOORD %s\n", dest);
    write(fd, msg, strlen(msg));
    if (node.monitoring)
        printf("\n%s[MONITOR]%s ENVIO UNCOORD %s → fd %d\n> ",
               MAGENTA, RESET, dest, fd);
}

/* ------------------------------------------------------------------ */
/*  Transição para o estado de COORDENAÇÃO — lógica partilhada por     */
/*  múltiplos invocadores. Envia COORD a todos os vizinhos excepto     */
/*  skip_fd (-1 = envia a todos). Actualiza coord_pending em cada      */
/*  vizinho contactado.                                                 */
/* ------------------------------------------------------------------ */
static void enter_coordination(Route *r, int skip_fd, int succ_coord_fd) {
    r->state         = COORDINATION;
    r->succ_coord_fd = succ_coord_fd;
    r->distance      = INF;
    r->succ_fd       = -1;
    memset(r->coord_pending, 0, sizeof(r->coord_pending));

    for (int k = 0; k < node.neighbor_count; k++) {
        if (node.neighbors[k].fd == skip_fd) continue;
        send_coord(r->dest, node.neighbors[k].fd);
        r->coord_pending[k] = 1;
    }

    if (node.monitoring)
        printf("\n%s[MONITOR]%s Entrou em COORDINATION para dest %s\n> ",
               MAGENTA, RESET, r->dest);
}

/* ------------------------------------------------------------------ */
/*  Verifica se todos os coord_pending são 0 e, nesse caso, conclui    */
/*  a fase de coordenação.                                             */
/* ------------------------------------------------------------------ */
static void check_coord_complete(Route *r) {
    if (r->state != COORDINATION) return;

    for (int k = 0; k < node.neighbor_count; k++)
        if (r->coord_pending[k]) return;   /* ainda há vizinhos pendentes */

    if (r->distance >= INF) {
        /*
         * Nenhuma rota alternativa foi encontrada. Permanece em COORDINATION —
         * um ROUTE futuro (via novo edge ou recuperação de caminho) invocará
         * handle_route, que por sua vez chamará check_coord_complete quando
         * a distância for conhecida.
         */
        if (node.monitoring)
            printf("\n%s[MONITOR]%s Ronda de COORD concluída para %s — sem rota, permanece em COORDINATION\n> ",
                   MAGENTA, RESET, r->dest);
        return;
    }

    /* Rota alternativa encontrada — regressa ao estado de expedição */
    r->state = FORWARDING;

    if (node.monitoring)
        printf("\n%s[MONITOR]%s COORD concluída para %s dist=%d succ_fd=%d\n> ",
               MAGENTA, RESET, r->dest, r->distance, r->succ_fd);

    int upstream = r->succ_coord_fd;
    r->succ_coord_fd = -1;

    send_route_to_all(r);

    if (upstream != -1)
        send_uncoord(r->dest, upstream);
}

/* ------------------------------------------------------------------ */
/*  on_edge_added                                                       */
/* ------------------------------------------------------------------ */
void on_edge_added(int slot) {
    int fd = node.neighbors[slot].fd;

    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];

        if (rt->state == FORWARDING && rt->distance < INF) {
            /*
             * Rota válida conhecida — anuncia-a ao novo vizinho.
             * Se ele tiver uma melhor, responderá com ROUTE.
             */
            send_route_to_fd(rt, fd);

        } else if (rt->state == COORDINATION) {
            /*
             * Nó em busca de rota para este destino. Interroga o novo
             * vizinho via COORD. coord_pending[slot] permanece a 0 —
             * não há dependência deste vizinho para concluir a ronda
             * em curso. Se ele responder com ROUTE + UNCOORD (regra 2),
             * handle_route detectará coord_pending[slot]==0, limpará
             * todos os pendentes e resolverá a coordenação.
             */
            send_coord(rt->dest, fd);
        }
        /* FORWARDING dist=INF: entrada existe mas destino inacessível —
           não é necessária qualquer acção. Um ROUTE futuro actualizará
           a entrada pelo caminho normal. */
    }
}

/* ------------------------------------------------------------------ */
/*  on_edge_removed                                                     */
/* ------------------------------------------------------------------ */
void on_edge_removed(int idx) {
    int fd = node.neighbors[idx].fd;
    char lost_id[4];
    strncpy(lost_id, node.neighbors[idx].id, 4);

    printf("\n%s[SYSTEM]%s Ligação ao nó %s (fd %d) removida.\n> ",
           YELLOW, RESET, lost_id, fd);

    /*
     * O encaminhamento é processado ANTES de comprimir o array de vizinhos,
     * para que os índices de coord_pending coincidam com node.neighbors[].
     * O fd morto é excluído de qualquer difusão de COORD.
     */
    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];

        if (rt->state == FORWARDING && rt->succ_fd == fd) {
            /*
             * Perdeu-se o próximo salto de expedição. Inicia coordenação.
             * skip_fd=fd para não escrever no socket inactivo.
             * succ_coord_fd=-1 — falha local, sem nó a montante a notificar.
             */
            enter_coordination(rt, fd, -1);

            /* Verifica imediatamente — pode não existir mais nenhum vizinho */
            int sent_any = 0;
            for (int k = 0; k < node.neighbor_count; k++)
                if (rt->coord_pending[k]) { sent_any = 1; break; }
            if (sent_any)
                check_coord_complete(rt);

        } else if (rt->state == COORDINATION && rt->coord_pending[idx]) {
            /*
             * Já em COORDINATION e à espera de resposta deste vizinho.
             * Como o vizinho desapareceu, marca o slot como concluído
             * e verifica se a coordenação pode terminar.
             */
            rt->coord_pending[idx] = 0;
            check_coord_complete(rt);
        }
        /* Restantes casos (não é o sucessor, não estava pendente) não requerem acção */
    }

    /* Remove o slot do vizinho do array */
    close(fd);
    for (int k = idx; k < node.neighbor_count - 1; k++)
        node.neighbors[k] = node.neighbors[k + 1];
    node.neighbor_count--;

    /* Ajusta os arrays coord_pending para reflectir a lista comprimida de vizinhos */
    for (int r = 0; r < node.route_count; r++) {
        Route *rt = &node.routing_table[r];
        for (int k = idx; k < node.neighbor_count; k++)
            rt->coord_pending[k] = rt->coord_pending[k + 1];
        rt->coord_pending[node.neighbor_count] = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  handle_route  (especificação §2.2)                                 */
/* ------------------------------------------------------------------ */
void handle_route(const char *dest, int n, int nbr_index) {
    if (strcmp(dest, node.id) == 0) return;   /* ignora rotas para o próprio nó */

    int candidate = n + 1;
    int fd        = node.neighbors[nbr_index].fd;

    Route *r = find_or_create_route(dest);
    if (!r) return;

    if (candidate >= r->distance) return;   /* distância não melhora — descarta */

    r->distance = candidate;
    r->succ_fd  = fd;

    if (r->state == FORWARDING) {
        send_route_to_all(r);
        return;
    }

    /* Em estado de COORDINATION */
    if (!r->coord_pending[nbr_index]) {
        /*
         * ROUTE recebido de um vizinho externo à ronda de coordenação em curso
         * (p.ex. novo edge, ou vizinho que já respondeu com UNCOORD).
         * Limpa todos os pendentes — nenhum outro poderá apresentar rota melhor
         * — e resolve a coordenação de imediato.
         */
        memset(r->coord_pending, 0, sizeof(r->coord_pending));
    }
    /*
     * Se coord_pending[nbr_index]==1, o vizinho faz parte da ronda em curso;
     * enviará UNCOORD a seguir, que handle_uncoord processará.
     * Armazena a distância melhorada e aguarda.
     */
    check_coord_complete(r);
}

/* ------------------------------------------------------------------ */
/*  handle_coord  (especificação §2.3)                                 */
/* ------------------------------------------------------------------ */
void handle_coord(const char *dest, int nbr_index) {
    int fd = node.neighbors[nbr_index].fd;

    Route *r = find_or_create_route(dest);
    if (!r) return;

    if (r->state == COORDINATION) {
        /*
         * Regra 1: já em COORDINATION — responde com UNCOORD de imediato.
         *
         * Se havia um COORD pendente para este vizinho (coord_pending=1),
         * o COORD que ele enviou de volta indica que também entrou em
         * COORDINATION e nunca enviará UNCOORD. Trata-se como resposta
         * válida — limpa o flag correspondente.
         */
        send_uncoord(dest, fd);
        if (r->coord_pending[nbr_index]) {
            r->coord_pending[nbr_index] = 0;
            check_coord_complete(r);
        }
        return;
    }

    /* Estado de expedição (FORWARDING) */

    if (r->distance < INF && r->succ_fd != fd) {
        /*
         * Regra 2: j ≠ succ[t] e existe rota válida não passando por j.
         * É seguro partilhar — responde com ROUTE + UNCOORD.
         */
        send_route_to_fd(r, fd);
        send_uncoord(dest, fd);
        return;
    }

    /*
     * Regra 3 (inclui o caso de prevenção de ciclos da regra 2):
     * j == succ[t], ou a única rota conhecida passa por j.
     * Inicia coordenação. succ_coord_fd = fd (nó a montante a notificar).
     * Envia COORD a todos os vizinhos incluindo j — este responderá com
     * UNCOORD de imediato (regra 1 do seu lado) por já estar em
     * COORDINATION ou ao entrar nesse estado.
     */
    enter_coordination(r, -1, fd);
    check_coord_complete(r);
}

/* ------------------------------------------------------------------ */
/*  handle_uncoord  (especificação §2.4)                               */
/* ------------------------------------------------------------------ */
void handle_uncoord(const char *dest, int nbr_index) {
    Route *r = find_route(dest);
    if (!r || r->state != COORDINATION) return;

    /* Marca a coordenação com este vizinho como concluída */
    r->coord_pending[nbr_index] = 0;
    check_coord_complete(r);
}
#ifndef ROUTING_H
#define ROUTING_H

#define MAX_NEIGHBORS 10
#define MAX_ROUTES    100

typedef enum { FORWARDING, COORDINATION } RouteState;

/* Uma entrada por par (nó local, destino) */
typedef struct {
    char dest[4];

    /* --- estado de expedição --- */
    int  distance;       /* dist[t]  — INF representado por 999 */
    int  succ_fd;        /* succ[t]  — fd do vizinho de expedição, -1 se inexistente */

    /* --- estado de coordenação --- */
    RouteState state;        /* state[t] */
    int  succ_coord_fd;      /* succ_coord[t] — fd que despoletou a coordenação;
                                -1 se despoletada por falha de ligação ao sucessor */

    /* coord[t,k] por slot de vizinho (índice coincide com node.neighbors[]) */
    int  coord_pending[MAX_NEIGHBORS]; /* 1 = coordenação em curso com esse vizinho */
} Route;

typedef struct {
    int  fd;
    char id[4];
} Neighbor;

typedef struct {
    char net[4];
    char id[4];
    char myIP[16];
    int  myTCP;
    int  is_joined;

    Neighbor neighbors[MAX_NEIGHBORS];
    int      neighbor_count;

    int monitoring;

    Route routing_table[MAX_ROUTES];
    int   route_count;
} NodeState;

extern NodeState node;

/* ---- funções auxiliares da tabela de encaminhamento ---- */
Route *find_route(const char *dest);
Route *find_or_create_route(const char *dest);

/* ---- tratadores de eventos do protocolo ---- */

/* Invocado após adição de um novo vizinho (edge iniciado localmente ou NEIGHBOR recebido).
   new_nbr_slot = índice em node.neighbors[] do novo vizinho. */
void on_edge_added(int new_nbr_slot);

/* Invocado quando o fd de um vizinho é detectado como fechado ou removido manualmente.
   Remove o vizinho de node.neighbors[] e despoleta coordenação. */
void on_edge_removed(int nbr_index);

/* Tratamento da mensagem ROUTE dest n recebida do vizinho nbr_index */
void handle_route(const char *dest, int n, int nbr_index);

/* Tratamento da mensagem COORD dest recebida do vizinho nbr_index */
void handle_coord(const char *dest, int nbr_index);

/* Tratamento da mensagem UNCOORD dest recebida do vizinho nbr_index */
void handle_uncoord(const char *dest, int nbr_index);

/* Envia ROUTE dest dist[t] a todos os vizinhos, ou apenas ao fd indicado */
void send_route_to_all(Route *r);
void send_route_to_fd(Route *r, int fd);

#endif /* ROUTING_H */
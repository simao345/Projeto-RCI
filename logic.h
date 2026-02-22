#ifndef LOGIC_H
#define LOGIC_H

#define MAX_NODES 100
#define INF 999

typedef struct {
    int dist;           // dist[t]: estimativa da distância ao nó t
    int succ;           // succ[t]: ID do vizinho de expedição para t
    int state;          // state[t]: 0 para expedição, 1 para coordenação
    
    // Variáveis adicionais para o estado de coordenação (state=1)
    int succ_coord;     // Vizinho que causou a transição para coordenação
    int coord[MAX_NODES]; // coord[t,j]: 1 se coordenação em curso com vizinho j
} RoutingEntry;

// Tabela global indexada pelo ID do destino (00-99)
extern RoutingEntry routing_table[MAX_NODES];

// Protótipos das funções principais do protocolo
void handle_route_message(int from_id, int dest_id, int n);
void handle_coord_message(int from_id, int dest_id);
void handle_exped_message(int from_id, int dest_id);
void update_on_edge_failure(int neighbor_id);

#endif
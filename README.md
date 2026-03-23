=========================================================
      GUIA DE TESTES - PROJETO OWR (VERSÃO FINAL)
=========================================================

Este projeto implementa um protocolo de encaminhamento dinâmico baseado em 
Distance Vector (Vetor de Distância), permitindo a comunicação entre nós 
mesmo que não estejam diretamente ligados.

--- FASE 1: PREPARAÇÃO ---
1. Limpar compilações antigas:
   $ make clean

2. Compilar o código:
   $ make

--- FASE 2: GESTÃO DE NÓS (UDP / SERVIDOR DE NOS) ---
3. Iniciar um nó (ex: Nó 10):
   $ ./OWR [local IP] [local port] [server IP] [server port]

4. Registar o nó na rede:
   > j NET ID
   
5. Consultar nós ativos na rede:
   > n NET

--- FASE 3: CONSTRUÇÃO DA TOPOLOGIA (TCP) ---
6. Ligar nós vizinhos para formar uma cadeia (ex: 10 <-> 20 <-> 30):
   - No Nó 20: > ae 10  (Ligar ao 10)
   - No Nó 30: > ae 20  (Ligar ao 20)

7. Verificar vizinhos diretos:
   > sg (show neighbours)
   (Confirma se os FDs e IDs dos vizinhos estão corretamente registados)

--- FASE 4: ENCAMINHAMENTO DINÂMICO (O CORAÇÃO DO PROJETO) ---
8. Anunciar presença na rede:
   No Nó 10: > a (announce)
   
   O que acontece: amento de mensagens CHAT por múltiplos saltos (Multi-hop forwarding).
* [X] Interface não bloqueante com select() para teclado e sockets simultâneos.
   - O Nó 10 envia uma mensagem ROUTE 10 0.
   - O Nó 20 recebe, atualiza a sua tabela (distância 1) e propaga para o Nó 30.
   - O Nó 30 recebe, atualiza (distância 2) e propaga.

9. Verificar a Tabela de Encaminhamento:
   > sr XX (show routing para o nó XX)
   Exemplo no Nó 30: > sr 10
   (Esperado: Destino 10, Saltos: 2, Vizinho: [FD do Nó 20])

--- FASE 5: FUNCIONALIDADES DE DEPURAÇÃO ---
10. Monitorização de Tráfego:
    - O código inclui logs de [DEBUG ROUTE] que mostram:
      * De quem veio a mensagem.
      * Qual a distância recebida vs. calculada.
      * Se a rota foi ignorada ou propagada e porquê.

--- FUNCIONALIDADES IMPLEMENTADAS ---
* [X] Registo/Saída no servidor UDP (j, x).
* [X] Listagem de nós da rede (n).
* [X] Estabelecimento de ligações TCP bidirecionais (ae).
* [X] Protocolo Distance Vector robusto (aceita mesma distância para garantir inundação).
* [X] Encaminhamento de mensagens CHAT por múltiplos saltos (Multi-hop forwarding).
* [X] Interface não bloqueante com select() para teclado e sockets simultâneos.

**Problemas com COORDENAÇÃO**
- **AE provoca um "mini announce":** No `main.c`, o comando `ae` chama `update_routing_table(arg_net, 1, fd, COORDINATION)` e depois envia imediatamente as entradas `ROUTE` ao novo vizinho, que expoe as rotas prematuramente (ver [main.c](main.c)).
- **Handshake `NEIGHBOR` também sincroniza rotas:** Ao receber `NEIGHBOR`, o nó chama `update_routing_table(neighbor_id, 1, current_fd, FORWARDING)` e envia em seguida as suas rotas (`ROUTE`), o que pode duplicar ou revelar rotas antes do esperado (ver [main.c](main.c)).
- **Inconsistência entre `COORD` e `UNCOORD`:** O `main.c` envia `COORD <id>` ao remover uma ligação, enquanto `routing.c` em `remove_neighbor_by_index` constrói e envia `UNCOORD <id>` — o uso de tokens diferentes pode provocar tratamento incoerente (ver [main.c](main.c) e [routing.c](routing.c)).
- **Tratamento conflituoso da distância `99`:** Em `routing.c`, `update_routing_table` ignora (`return`) qualquer `new_dist >= 99`, logo tentativas de definir distância `99` via `update_routing_table` são descartadas, criando inconsistências ao forçar estados de coordenação (ver [routing.c](routing.c) e [routing.h](routing.h)).
- **`propagate_route_request` ignorado pelo guard:** `propagate_route_request` envia `ROUTE <id> 99`, mas como `update_routing_table` ignora distâncias >=99, pedidos de rota com 99 não atualizam a tabela — isto pode quebrar a lógica de recuperação (ver [routing.c](routing.c)).
- **Sincronização de tabela demasiado permissiva:** O código envia todas as rotas em estado `FORWARDING` a um novo vizinho durante `ae` e novamente quando chega `NEIGHBOR`, o que pode mesclar mapas inesperadamente; é recomendável consolidar onde e quando ocorre o sync completo (ver [main.c](main.c)).

**Outros problemas detectados**
- **Propagação de tabela de expedição por ligação nova não condicionada:** A partilha de rotas no `ae` faz-se sem verificar se ambos os lados têm tabela inicial válida, contrariando o comportamento desejado descrito no documento (ver [main.c](main.c)).
- **Possível fuga de lógica ao forçar coordenação e depois adicionar nós:** Forçar `distance=99` e depois associar novos nós pode deixar o sistema em estados estranhos (ver [routing.c](routing.c)).
- **Stack overflow:** Há relatos/observações que stack overflow pode causar falhas completas — exige validação de buffers e limites (ver [main.c](main.c) e [routing.c](routing.c)).

Estas observações correspondem às notas originais em "Problemas com COORDINATION" e indicam locais concretos no código para investigação e correção.
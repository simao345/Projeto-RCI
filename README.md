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
   
   O que acontece: 
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
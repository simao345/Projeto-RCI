=========================================================
      GUIA DE TESTES - PROJETO RCI (VERSÃO 2)
=========================================================

--- FASE 1: PREPARAÇÃO ---
1. Limpar compilações antigas:
   $ make clean

2. Compilar o código novo:
   $ make

--- FASE 2: TESTAR LIGAÇÃO UDP AO SERVIDOR DO IST ---
3. Abrir o Terminal 1 e iniciar o Nó 1 (sem o IP/Porta do simulador para usar o do Técnico por defeito):
   $ ./OWR 127.0.0.1 58000

4. Registar o nó na rede 001 com o ID XX:
   > j 001 XX
   (Esperado: "Sucesso: Nó registado no servidor.")

5. Pedir a lista de nós para confirmar que o IST guardou o registo:
   > n 001
   (Esperado: O servidor deve devolver a lista e o teu nó 55 tem de lá estar.)

--- FASE 3: TESTAR LIGAÇÃO TCP DIRETA (P2P) ---
6. Abrir o Terminal 2 e iniciar o Nó 2 noutra porta:
   $ ./OWR 127.0.0.1 58001

7. No Terminal 2, ligar o Nó 2 ao Nó 1 usando o comando direct:
   > d 127.0.0.1 58000
   
8. Verificar os resultados em ambos os terminais:
   - Terminal 1 (Nó 1): Deve mostrar "[TCP] Nova ligação recebida do IP..." e guardar o prev_fd.
   - Terminal 2 (Nó 2): Deve mostrar "[TCP] Ligação direta estabelecida..." e guardar o next_fd.
   - Ambos os terminais devem continuar a aceitar comandos do teclado sem bloquear.

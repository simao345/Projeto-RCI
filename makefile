# Variáveis de compilação
CC = gcc
CFLAGS = -Wall -g # -Wall mostra todos os avisos, -g permite usar o gdb [cite: 235, 246]
LDFLAGS = 

# Lista de ficheiros objeto (gerados a partir dos teus ficheiros .c)
OBJ = main.o interface.o server_udp.o network_tcp.o logic.o

# Nome do executável final
TARGET = OWR

# Regra principal: compila tudo
all: $(TARGET)

# Ligação dos objetos para criar o executável
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

# Regras para compilar cada ficheiro .c num .o
# O uso de headers como dependências garante que se mudares o .h, o .c é recompilado
main.o: main.c interface.h server_udp.h network_tcp.h logic.h
	$(CC) $(CFLAGS) -c main.c

interface.o: interface.c interface.h logic.h
	$(CC) $(CFLAGS) -c interface.c

server_udp.o: server_udp.c server_udp.h
	$(CC) $(CFLAGS) -c server_udp.c

network_tcp.o: network_tcp.c network_tcp.h logic.h
	$(CC) $(CFLAGS) -c network_tcp.c

logic.o: logic.c logic.h
	$(CC) $(CFLAGS) -c logic.c

# Limpeza de ficheiros temporários
clean:
	rm -f $(OBJ) $(TARGET)

# Comando para "rebuild" total
rebuild: clean all
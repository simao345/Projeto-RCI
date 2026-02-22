#ifndef SERVER_UDP_H
#define SERVER_UDP_H

// Protótipos das funções que o main.c está a tentar usar
int register_node(char* regIP, int regUDP, char* net, char* id, char* myIP, int myTCP);
int unregister_node(char* regIP, int regUDP, char* net, char* id);
void nodes_query(char* net, char* server_ip, int server_port);

#endif
#ifndef SERVER_UDP_H
#define SERVER_UDP_H

// Protótipos das funções que o main.c está a tentar usar
int register_node(char* regIP, int regUDP, char* net, char* id, char* myIP, int myTCP);
int unregister_node(char* regIP, int regUDP, char* net, char* id);
void nodes_query(char* net, char* server_ip, int server_port);
int get_node_contact(char* server_ip, int server_port, char* net, char* id, char* out_ip, int* out_tcp);

#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

Cliente clientes[MAX_CLIENTS];
pthread_mutex_t mutex_clientes = PTHREAD_MUTEX_INITIALIZER;

// Función para enviar texto por el socket
void enviar_mensaje(int socket, const char *mensaje) {
    send(socket, mensaje, strlen(mensaje), 0);
}

// Función  para buscar un usuario por nombre. Devuelve su índice o -1 si no existe.
int buscar_usuario(const char *nombre) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientes[i].ocupado && strcmp(clientes[i].nombre_usuario, nombre) == 0) {
            return i;
        }
    }
    return -1;
}

// Función para revisar si una IP ya está conectada
int existe_ip(const char *ip) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientes[i].ocupado && strcmp(clientes[i].ip, ip) == 0) {
            return 1;
        }
    }
    return 0;
}

// Función para gestionar cada nuevo que se conecta
void *manejar_cliente(void *arg) {
    int client_socket = *((int *)arg);
    free(arg); // Liberar el puntero que usamos para pasar el socket
    
    char buffer[MAX_BUFFER];
    char respuesta[MAX_BUFFER];
    
    int mi_indice = -1;
    char mi_nombre[50] = "";
    char mi_ip[INET_ADDRSTRLEN];
    
    // Obtenemos la IP desde la conexión de red
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    getpeername(client_socket, (struct sockaddr *)&addr, &addr_size);
    strcpy(mi_ip, inet_ntoa(addr.sin_addr));

    printf("[NUEVA CONEXION] IP: %s conectada.\n", mi_ip);

    // Ciclo para escuchar lo que dice el cliente en específico
    while (1) {
        memset(buffer, 0, MAX_BUFFER);
        int recibidos = recv(client_socket, buffer, MAX_BUFFER - 1, 0);
        
        if (recibidos <= 0) {
            printf("[DESCONEXION] El usuario %s se desconectó inesperadamente.\n", mi_nombre[0] ? mi_nombre : "Desconocido");
            break; 
        }

        // PROTOCOLO (ACCION|ORIGEN|DESTINO|TAM|CONTENIDO) ---
        char *campos[5];
        int num_campos = 0;
        char *p = buffer;
        
        campos[num_campos++] = p;
        while (*p != '\0' && num_campos < 5) {
            if (*p == '|') {
                *p = '\0';
                campos[num_campos++] = p + 1;
            }
            p++;
        }
        
        if (num_campos == 5) {
            char *salto = strchr(campos[4], '\n');
            if (salto) *salto = '\0';
        }

        // Si el mensaje no trae las 5 partes, se para evitar que el servidor colapse
        if (num_campos < 5) continue;

        char *accion = campos[0];
        char *origen = campos[1];
        char *destino = campos[2];
        char *contenido = campos[4];
    }
}

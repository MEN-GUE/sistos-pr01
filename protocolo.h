#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <netinet/in.h>

#define MAX_CLIENTS 50
#define MAX_BUFFER 1024 // Esto se lo podemos cambiar

// Los status: ACTIVO, OCUPADO, INACTIVO
typedef enum {
    ACTIVO = 0,
    OCUPADO = 1,
    INACTIVO = 2
} StatusUsuario;

// Estructura cque mantener la sesión de cada cliente (tentativa)
typedef struct {
    int socket_fd;
    char nombre_usuario[50];
    char ip[INET_ADDRSTRLEN];
    StatusUsuario status;
    int ocupado; // 1 ocupado, 0 libre
} Cliente;

#endif

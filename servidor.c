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
        
        // Maneja el registro de usuarios
        if (strcmp(accion, "REG") == 0) {
            pthread_mutex_lock(&mutex_clientes);
            
            if (buscar_usuario(origen) != -1) {
                sprintf(respuesta, "ERR|SERVER|%s|25|Nombre de usuario ya existe\n", origen);
                enviar_mensaje(client_socket, respuesta);
                pthread_mutex_unlock(&mutex_clientes);
                break;
            } 
            else if (existe_ip(mi_ip)) {
                sprintf(respuesta, "ERR|SERVER|%s|16|IP ya registrada\n", origen);
                enviar_mensaje(client_socket, respuesta);
                pthread_mutex_unlock(&mutex_clientes);
                break;
            } 
            else {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!clientes[i].ocupado) {
                        clientes[i].ocupado = 1;
                        clientes[i].socket_fd = client_socket;
                        strcpy(clientes[i].nombre_usuario, origen);
                        strcpy(clientes[i].ip, mi_ip);
                        clientes[i].status = ACTIVO;
                        
                        mi_indice = i;
                        strcpy(mi_nombre, origen);
                        break;
                    }
                }
                sprintf(respuesta, "OK|SERVER|%s|16|Registro exitoso\n", origen);
                enviar_mensaje(client_socket, respuesta);
            }
            pthread_mutex_unlock(&mutex_clientes);
        }
        
        // Funcion que maneja el envío de mensajes entre usuarios
        else if (strcmp(accion, "MSG") == 0) {
            pthread_mutex_lock(&mutex_clientes);
            
            sprintf(respuesta, "MSG|%s|%s|%lu|%s\n", origen, destino, strlen(contenido), contenido);
            
            if (strcmp(destino, "TODOS") == 0) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clientes[i].ocupado && i != mi_indice) {
                        enviar_mensaje(clientes[i].socket_fd, respuesta);
                    }
                }
            } else {
                int target_idx = buscar_usuario(destino);
                if (target_idx != -1) {
                    enviar_mensaje(clientes[target_idx].socket_fd, respuesta);
                } else {
                    sprintf(respuesta, "ERR|SERVER|%s|25|Usuario destino no existe\n", origen);
                    enviar_mensaje(client_socket, respuesta);
                }
            }
            pthread_mutex_unlock(&mutex_clientes);
        }
        
        // Funcino encargada del cambio de estado del usuario
        else if (strcmp(accion, "STS") == 0) {
            pthread_mutex_lock(&mutex_clientes);
        
            if (strcmp(contenido, "ACTIVO") == 0)
                clientes[mi_indice].status = ACTIVO;
            else if (strcmp(contenido, "OCUPADO") == 0)
                clientes[mi_indice].status = OCUPADO;
            else if (strcmp(contenido, "INACTIVO") == 0)
                clientes[mi_indice].status = INACTIVO;
        
            sprintf(respuesta, "STS|SERVER|%s|%lu|%s\n", origen, strlen(contenido), contenido);
            enviar_mensaje(client_socket, respuesta);
        
            pthread_mutex_unlock(&mutex_clientes);
        }
        
        // funcion que se encarga de manejar la solicitud de listado de usuarios conectados
        else if (strcmp(accion, "LST") == 0) {
            char lista_nombres[MAX_BUFFER - 100] = "";
        
            pthread_mutex_lock(&mutex_clientes);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clientes[i].ocupado) {
                    strcat(lista_nombres, clientes[i].nombre_usuario);
                    strcat(lista_nombres, ",");
                }
            }
            pthread_mutex_unlock(&mutex_clientes);
        
            if (strlen(lista_nombres) > 0) {
                lista_nombres[strlen(lista_nombres) - 1] = '\0';
            }
        
            sprintf(respuesta, "LST|SERVER|%s|%lu|%s\n", origen, strlen(lista_nombres), lista_nombres);
            enviar_mensaje(client_socket, respuesta);
        }
        
        //  Funcion donde se hace la solicitud de información de un usuario
        else if (strcmp(accion, "INF") == 0) {
            pthread_mutex_lock(&mutex_clientes);
        
            int target_idx = buscar_usuario(contenido);
            if (target_idx != -1) {
                char *ip_encontrada = clientes[target_idx].ip;
                sprintf(respuesta, "INF|SERVER|%s|%lu|%s\n", origen, strlen(ip_encontrada), ip_encontrada);
                enviar_mensaje(client_socket, respuesta);
            } else {
                sprintf(respuesta, "ERR|SERVER|%s|27|Usuario no esta conectado\n", origen);
                enviar_mensaje(client_socket, respuesta);
            }
        
            pthread_mutex_unlock(&mutex_clientes);
        }
        
        // Maneja la desconexión del usuario
        else if (strcmp(accion, "SAL") == 0) {
            sprintf(respuesta, "OK|SERVER|%s|11|Desconexion\n", origen);
            enviar_mensaje(client_socket, respuesta);
            break;
        }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"
#include <time.h>
#define TIMEOUT_INACTIVIDAD 30

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

        // ACABAMOS DE RECIBIR ALGO VÁLIDO: ACTUALIZAR EL RELOJ
        if (mi_indice != -1) {
            pthread_mutex_lock(&mutex_clientes);
            clientes[mi_indice].ultima_actividad = time(NULL);
            // Si estaba inactivo, lo regresamos a ACTIVO porque ya habló
            if (clientes[mi_indice].status == INACTIVO) {
                 clientes[mi_indice].status = ACTIVO;
                 sprintf(respuesta, "STS|SERVER|%s|6|ACTIVO\n", mi_nombre);
                 enviar_mensaje(client_socket, respuesta);
            }
            pthread_mutex_unlock(&mutex_clientes);
        }

        char *accion = campos[0];
        char *origen = campos[1];
        char *destino = campos[2];
        char *contenido = campos[4];

        // Funcion encargada del registro de usuarios
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
            // Caso REG
            else {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!clientes[i].ocupado) {
                        clientes[i].ocupado = 1;
                        clientes[i].socket_fd = client_socket;
                        strcpy(clientes[i].nombre_usuario, origen);
                        strcpy(clientes[i].ip, mi_ip);
                        clientes[i].status = ACTIVO;
                        clientes[i].ultima_actividad = time(NULL);
                        mi_indice = i;
                        strcpy(mi_nombre, origen);
                        break;
                    }
                }
                sprintf(respuesta, "OK|SERVER|%s|16|Registro exitoso\n", origen);
                enviar_mensaje(client_socket, respuesta);
                printf("[REGISTRO] Usuario '%s' registrado exitosamente.\n", mi_nombre);
            }
            pthread_mutex_unlock(&mutex_clientes); 
        }
        
        // funcion encargada del envio de mensajes entre usuarios
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

        // funcion encargada del cambio de estado
        else if (strcmp(accion, "STS") == 0) {
            pthread_mutex_lock(&mutex_clientes);
            if (strcmp(contenido, "ACTIVO") == 0) clientes[mi_indice].status = ACTIVO;
            else if (strcmp(contenido, "OCUPADO") == 0) clientes[mi_indice].status = OCUPADO;
            else if (strcmp(contenido, "INACTIVO") == 0) clientes[mi_indice].status = INACTIVO;
            
            sprintf(respuesta, "STS|SERVER|%s|%lu|%s\n", origen, strlen(contenido), contenido);
            enviar_mensaje(client_socket, respuesta);
            pthread_mutex_unlock(&mutex_clientes);
        }

        // funcion del listado de usuarios conectados
        else if (strcmp(accion, "LST") == 0) {
            char lista_nombres[MAX_BUFFER - 100] = ""; 
            pthread_mutex_lock(&mutex_clientes);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clientes[i].ocupado) {
                    // Traducimos el status a texto
                    char estado_str[15];
                    if (clientes[i].status == ACTIVO) strcpy(estado_str, "ACTIVO");
                    else if (clientes[i].status == OCUPADO) strcpy(estado_str, "OCUPADO");
                    else strcpy(estado_str, "INACTIVO");
                    
                    // Armamos el formato: Nombre (ESTADO),
                    char info_usuario[100];
                    sprintf(info_usuario, "%s (%s), ", clientes[i].nombre_usuario, estado_str);
                    strcat(lista_nombres, info_usuario);
                }
            }
            pthread_mutex_unlock(&mutex_clientes);
            
            // Quitamos la última coma y el espacio
            if (strlen(lista_nombres) > 2) {
                lista_nombres[strlen(lista_nombres) - 2] = '\0'; 
            }
            
            sprintf(respuesta, "LST|SERVER|%s|%lu|%s\n", origen, strlen(lista_nombres), lista_nombres);
            enviar_mensaje(client_socket, respuesta);
        }

        // funcion encargada de la solicitud de información de un usuario
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

        // Manejo de la desconexion del usuario
        else if (strcmp(accion, "SAL") == 0) {
            sprintf(respuesta, "OK|SERVER|%s|11|Desconexion\n", origen);
            enviar_mensaje(client_socket, respuesta);
            printf("[DESCONEXION] El usuario %s ha salido del chat.\n", mi_nombre);
            break; 
        }
    }
    
    // Funcion encargada del cierre del espacio
    if (mi_indice != -1) {
        pthread_mutex_lock(&mutex_clientes);
        clientes[mi_indice].ocupado = 0; 
        pthread_mutex_unlock(&mutex_clientes);
    }
    close(client_socket); 
    pthread_exit(NULL);   
}

// LO DE LA INACTIVIDAD
void *monitor_inactividad(void *arg) {
    char respuesta[MAX_BUFFER];
    
    while(1) {
        sleep(5);
        
        pthread_mutex_lock(&mutex_clientes);
        time_t ahora = time(NULL);
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clientes[i].ocupado && clientes[i].status != INACTIVO) {
                if (difftime(ahora, clientes[i].ultima_actividad) > TIMEOUT_INACTIVIDAD) {
                    clientes[i].status = INACTIVO;
                    
                    // Le avisamos al cliente que lo cambiamos por inactividad
                    sprintf(respuesta, "STS|SERVER|%s|8|INACTIVO\n", clientes[i].nombre_usuario);
                    enviar_mensaje(clientes[i].socket_fd, respuesta);
                    
                    printf("[SISTEMA] El usuario %s ha sido marcado como INACTIVO por falta de uso.\n", clientes[i].nombre_usuario);
                }
            }
        }
        pthread_mutex_unlock(&mutex_clientes);
    }
    return NULL;
}

// --- FUNCIÓN PRINCIPAL ---
int main(int argc, char *argv[]) {
    // Verificamos que se ejecute correctamente: ./servidor <puerto>
    if (argc != 2) {
        printf("Error. Uso correcto: %s <puertodelservidor>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int puerto = atoi(argv[1]);
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);

    // Creamos el socket principal TCP
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    
    // Evita el error molesto de "Address already in use" si cierras y abres el servidor rápido
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error. ¿El puerto está ocupado?");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) == 0) {
        printf("[INICIO] Servidor encendido y escuchando en el puerto %d...\n", puerto);
    }

    // Aseguramos que toda la memoria de clientes empiece libre
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientes[i].ocupado = 0;
    }

    // Aseguramos que toda la memoria de clientes empiece libre
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientes[i].ocupado = 0;
    }

    // GUARDIAN DE INACTIVIDAD
    pthread_t hilo_monitor;
    pthread_create(&hilo_monitor, NULL, monitor_inactividad, NULL);
    pthread_detach(hilo_monitor);

    // Ciclo infinito para aceptar nuevas conexiones
    while (1) {

    // Ciclo infinito para aceptar nuevas conexiones
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) continue;

        int *new_sock = malloc(sizeof(int));
        *new_sock = client_socket;

        pthread_t thread_id;
        // Creamos un hilo nuevo para atender a este cliente sin bloquear a los demás
        if (pthread_create(&thread_id, NULL, manejar_cliente, (void*)new_sock) != 0) {
            free(new_sock);
            close(client_socket);
        }
        
        // Le decimos al sistema que libere la memoria del hilo automáticamente cuando termine
        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}

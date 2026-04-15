#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

// Variables globales
int socket_cliente;
char mi_usuario[50];
int corriendo = 1; // (Por si seguir si segumos en el chat)

// Escuchar
void *escuchar_servidor(void *arg) {
    char buffer[MAX_BUFFER];
    
    while (corriendo) {
        memset(buffer, 0, MAX_BUFFER);
        int recibidos = recv(socket_cliente, buffer, MAX_BUFFER - 1, 0);
        
        if (recibidos <= 0) {
            if (corriendo) {
                printf("\n[!] Se perdió la conexión con el servidor.\n");
                corriendo = 0; // Apagamos el programa
            }
            break;
        }

        // Protocolo
        char *accion = strtok(buffer, "|");
        char *origen = strtok(NULL, "|");
        char *destino = strtok(NULL, "|");
        char *tam = strtok(NULL, "|");
        char *contenido = strtok(NULL, "\n");

        if (accion && origen && contenido) {
            // Dependiendo de la acción, mostramos el texto de forma amigable
            if (strcmp(accion, "MSG") == 0) {
                if (strcmp(destino, "TODOS") == 0) {
                    printf("\n[Chat General] %s: %s\n> ", origen, contenido);
                } else {
                    printf("\n[Mensaje Privado de %s]: %s\n> ", origen, contenido);
                }
            } else if (strcmp(accion, "OK") == 0) {
                printf("\n[Aviso]: %s\n> ", contenido);
            } else if (strcmp(accion, "ERR") == 0) {
                printf("\n[Error]: %s\n> ", contenido);
                // Si el error fue al registrarse (nombre repetido), salimos del programa
                if (strcmp(destino, mi_usuario) == 0 && corriendo == 1) {
                    printf("Por favor, intenta con otro nombre.\n");
                    corriendo = 0;
                }
            } else if (strcmp(accion, "LST") == 0) {
                printf("\n[Usuarios Conectados]: %s\n> ", contenido);
            } else if (strcmp(accion, "INF") == 0) {
                printf("\n[Información del Usuario]: %s\n> ", contenido);
            } else if (strcmp(accion, "STS") == 0) {
                printf("\n[Status]: El servidor confirmó tu cambio a %s\n> ", contenido);
            }
            fflush(stdout); // Para actualizar la pantalla
        }
    }
    pthread_exit(NULL); //
}


void limpiar_salto(char *cadena) {
    char *salto = strchr(cadena, '\n');
    if (salto) *salto = '\0';
}

// Función por seguridad: Reemplaza los '|' por espacios vacíos
void limpiar_pipe(char *cadena) {
    for (int i = 0; cadena[i] != '\0'; i++) {
        if (cadena[i] == '|') {
            cadena[i] = ' '; // Aquí yo remplacé con espacio, podemos ponerle otra cosa
        }
    }
}

int main(int argc, char *argv[]) {
    // 1. Verificamos 
    if (argc != 4) {
        printf("Uso correcto: %s <nombredeusuario> <IPdelservidor> <puertodelservidor>\n", argv[0]);
        exit(1);
    }

    strcpy(mi_usuario, argv[1]);
    char *ip_servidor = argv[2];
    int puerto = atoi(argv[3]);

    // 2. Preparamos el socket para llamar al servidor
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);
    server_addr.sin_addr.s_addr = inet_addr(ip_servidor);

    // 3. Hacemos la conexión
    if (connect(socket_cliente, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error: No se pudo conectar al servidor en %s:%d\n", ip_servidor, puerto);
        exit(1);
    }

    // 4. Mandamos nuestro mensaje automático de Registro (REG)
    char buffer_envio[MAX_BUFFER];
    sprintf(buffer_envio, "REG|%s|SERVER|0|\n", mi_usuario);
    send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);

    pthread_t hilo_escucha;
    pthread_create(&hilo_escucha, NULL, escuchar_servidor, NULL);

    // Damos un segundo para ver si el servidor nos acepta o nos rechaza por nombre repetido
    sleep(1); 
    if (!corriendo) {
        close(socket_cliente);
        return 0;
    }

    char opcion[10];
    char entrada1[100];
    char entrada2[MAX_BUFFER - 200];

    while (corriendo) {
        printf("\n--- MENÚ DE CHAT ---\n");
        printf("1. Chat General\n");
        printf("2. Mensaje Privado\n");
        printf("3. Cambiar Status\n");
        printf("4. Lista de Usuarios\n");
        printf("5. Información de Usuario\n");
        printf("6. Salir\n");
        printf("Elige una opción: ");
        
        fgets(opcion, sizeof(opcion), stdin);
        limpiar_salto(opcion);
        int elec = atoi(opcion);

        // Si se cayó la conexión mientras elegíamos, salimos
        if (!corriendo) break;

        switch (elec) {
            case 1: // Chat General
                printf("Escribe tu mensaje para todos: ");
                fgets(entrada2, sizeof(entrada2), stdin);
                limpiar_salto(entrada2);
                limpiar_pipe(entrada2);
                sprintf(buffer_envio, "MSG|%s|TODOS|%lu|%s\n", mi_usuario, strlen(entrada2), entrada2);
                send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);
                break;
                
            case 2: // Mensaje Privado
                printf("¿A quién le quieres enviar el mensaje?: ");
                fgets(entrada1, sizeof(entrada1), stdin);
                limpiar_salto(entrada1);
                limpiar_pipe(entrada1);
                printf("Escribe tu mensaje secreto: ");
                fgets(entrada2, sizeof(entrada2), stdin);
                limpiar_salto(entrada2);
                limpiar_pipe(entrada2);
                sprintf(buffer_envio, "MSG|%s|%s|%lu|%s\n", mi_usuario, entrada1, strlen(entrada2), entrada2);
                send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);
                break;

            case 3: // Cambiar Status
                printf("Elige tu nuevo estado (ACTIVO, OCUPADO, INACTIVO): ");
                fgets(entrada1, sizeof(entrada1), stdin);
                limpiar_salto(entrada1);
                limpiar_pipe(entrada1);
                sprintf(buffer_envio, "STS|%s|SERVER|%lu|%s\n", mi_usuario, strlen(entrada1), entrada1);
                send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);
                break;

            case 4: // Lista
                sprintf(buffer_envio, "LST|%s|SERVER|0|\n", mi_usuario);
                send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);
                break;

            case 5: // Info de usuario
                printf("¿De qué usuario quieres saber la IP?: ");
                fgets(entrada1, sizeof(entrada1), stdin);
                limpiar_salto(entrada1);
                limpiar_pipe(entrada1);
                sprintf(buffer_envio, "INF|%s|SERVER|%lu|%s\n", mi_usuario, strlen(entrada1), entrada1);
                send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);
                break;

            case 6: // Salir
                sprintf(buffer_envio, "SAL|%s|SERVER|0|\n", mi_usuario);
                send(socket_cliente, buffer_envio, strlen(buffer_envio), 0);
                corriendo = 0; // Rompemos el ciclo
                break;

            default:
                printf("Opción no válida. Intenta de nuevo.\n");
        }
        // Pausa por si hay errores
        usleep(500000); 
    }

    // Cleanup de despedida
    printf("Cerrando el chat. ¡Hasta luego!\n");
    close(socket_cliente);
 
    pthread_join(hilo_escucha, NULL);
    
    return 0;
}
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <thread>
#include <iostream>

void readSocketThread(int cli) {
    char buffer[300];
    int n;
    while (true) {
        bzero(buffer, 300);
        n = read(cli, buffer, 299);
        if (n <= 0) {
            printf("Servidor desconectado.\n");
            break;
        }
        buffer[n] = '\0';
        printf("Server: %s\n", buffer);
    }
    shutdown(cli, SHUT_RDWR);
    close(cli);
    exit(0); // Cerrar el programa si el servidor se desconecta
}

int main() {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[300];
    int n;

    if (SocketFD == -1) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (connect(SocketFD, (const struct sockaddr*)&stSockAddr, sizeof(struct sockaddr_in)) == -1) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    // 🔹 Iniciar un solo hilo de lectura
    std::thread(readSocketThread, SocketFD).detach();

    while (true) {
        printf("Client: ");
        bzero(buffer, 300);
        fgets(buffer, 299, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';

        n = write(SocketFD, buffer, strlen(buffer));
        if (n < 0) {
            perror("ERROR writing to socket");
            break;
        }

        if (strcmp(buffer, "chau") == 0) {
            printf("Chau.\n");
            break;
        }
    }

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}

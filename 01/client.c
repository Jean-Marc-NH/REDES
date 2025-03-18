#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    int n;
    char buffer[256];

    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server.\n");

    for (int i = 0; i < 10; i++)
    {
        // Enviar mensaje al servidor
        n = write(SocketFD, "Hola soy Jean Marc", 18);
        if (n < 0)
        {
            perror("ERROR writing to socket");
            break;
        }

        // Leer respuesta del servidor
        bzero(buffer, 256);
        n = read(SocketFD, buffer, 255);
        if (n < 0)
        {
            perror("ERROR reading from socket");
            break;
        }
        buffer[n] = '\0';
        printf("Server: %s\n", buffer);
    }


    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}
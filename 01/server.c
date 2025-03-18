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
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    int n;

    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10))
    {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }


    int ConnectFD = accept(SocketFD, NULL, NULL);

    if (0 > ConnectFD)
    {
        perror("error accept failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    printf("Client connected.\n");

    for (int i = 0; i < 10; i++)
    {
        // Leer mensaje del cliente
        bzero(buffer, 256);
        n = read(ConnectFD, buffer, 255);
        if (n < 0)
        {
            perror("ERROR reading from socket");
            break;
        }
        buffer[n] = '\0';
        printf("Recibí tu mensaje: %s\n", buffer);

        // Responder al cliente
        n = write(ConnectFD, "Tengo tu mensaje", 17);
        if (n < 0)
        {
            perror("ERROR writing to socket");
            break;
        }
    }


    shutdown(ConnectFD, SHUT_RDWR);
    close(ConnectFD);
    close(SocketFD);
    return 0;
}
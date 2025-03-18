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

    while (1)
    {

        bzero(buffer, 256);
        n = read(ConnectFD, buffer, 255);
        if (n < 0)
        {
            perror("ERROR reading from socket");
            break;
        }
        buffer[n] = '\0';
        printf("Client: %s\n", buffer);


        if (strcmp(buffer, "chau") == 0)
        {
            printf("Chau.\n");
            break;
        }


        printf("Server: ");
        bzero(buffer, 256);
        fgets(buffer, 255, stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; 

        n = write(ConnectFD, buffer, strlen(buffer));
        if (n < 0)
        {
            perror("ERROR writing to socket");
            break;
        }


        if (strcmp(buffer, "chau") == 0)
        {
            printf("chau\n");
            break;
        }
    }

    shutdown(ConnectFD, SHUT_RDWR);
    close(ConnectFD);
    close(SocketFD);
    return 0;
}

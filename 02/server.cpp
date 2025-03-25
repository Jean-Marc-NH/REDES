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
#include <list>

using namespace std;

void writeSocketThread(int cli)
{
    char buffer[300];
    int n;
    do 
    {
        printf("Server: ");
        bzero(buffer, 256);
        fgets(buffer, 255, stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; 

        n = write(cli, buffer, strlen(buffer));
    }while(strncmp(buffer, "chau", 4) != 0);
    shutdown(cli, SHUT_RDWR);
    close(cli);
}
void readSocketThread(int cli)
{
    char buffer[300];
    int n;
    do 
    {
        bzero(buffer, 256);
        n = read(cli, buffer, 255);
        buffer[n] = '\0';
        printf("Client: %s\n", buffer);
    }while(strncmp(buffer, "chau", 4) != 0);
    shutdown(cli, SHUT_RDWR);
    close(cli);
}
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

    while (1)
    {
        int ConnectFD = accept(SocketFD, NULL, NULL);

        if (0 > ConnectFD)
        {
            perror("error accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }
        
        cout << "NUEVO CLIENTE \n";

        thread(writeSocketThread,ConnectFD).detach();
        thread(readSocketThread,ConnectFD).detach();
    }
    close(SocketFD);
    return 0;
}

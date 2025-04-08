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
#include <sstream>
#include <iomanip>

using namespace std;

string usuario;

string formatMessage(string mensaje, string destino) {
    stringstream ss;
    string msgLen = to_string(mensaje.size());
    string destLen = to_string(destino.size());

    ss << setw(5) << setfill('0') << (1 + 5 + mensaje.size() + 5 + destino.size()); // total length
    ss << 'm';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << mensaje;
    ss << setw(5) << setfill('0') << destino.size();
    ss << destino;

    return ss.str();
}

void readSocketThread(int cli) {
    char buffer[1024];
    int n;
    while ((n = read(cli, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';

        if (buffer[5] == 'M') {
            int msgLen = stoi(string(buffer + 6, 5));
            string mensaje(buffer + 11, msgLen);
            int userLen = stoi(string(buffer + 11 + msgLen, 5));
            string emisor(buffer + 16 + msgLen, userLen);

            cout << "\nMensaje de " << emisor << ": " << mensaje << endl;
        } else if (buffer[5] == 'L') {
            string lista(buffer + 6);
            cout << "\nUsuarios conectados: " << lista << endl;
        }
    }
    shutdown(cli, SHUT_RDWR);
    close(cli);
    exit(0);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];

    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Introduce tu nombre de usuario: ";
    getline(cin, usuario);
    write(SocketFD, usuario.c_str(), usuario.size());

    thread(readSocketThread, SocketFD).detach();

    while (true) {
        cout << "\nMensaje o comando (\"chau\" para salir): ";
        string entrada;
        getline(cin, entrada);

        if (entrada == "chau") {
            shutdown(SocketFD, SHUT_RDWR);
            close(SocketFD);
            break;
        }

        if (entrada == "lista") {
            write(SocketFD, "00001l", 6);
            continue;
        }

        cout << "Destinatario: ";
        string destino;
        getline(cin, destino);

        string mensajeFormateado = formatMessage(entrada, destino);
        write(SocketFD, mensajeFormateado.c_str(), mensajeFormateado.size());
    }

    return 0;
}

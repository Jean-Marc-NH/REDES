#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <thread>
#include <iostream>
#include <map>
#include <sstream>
#include <iomanip>
#include <mutex>

using namespace std;

map<string, int> usuarios;
mutex user_mutex;

string formatServerMessage(string msg, string from) {
    stringstream ss;
    ss << setw(5) << setfill('0') << (1 + 5 + msg.size() + 5 + from.size()); // tamaño total
    ss << 'M';
    ss << setw(5) << setfill('0') << msg.size() << msg;
    ss << setw(5) << setfill('0') << from.size() << from;
    return ss.str();
}

string formatUserList() {
    stringstream ss, payload;
    for (const auto& pair : usuarios) {
        payload << pair.first << ",";
    }
    string data = payload.str();
    if (!data.empty()) data.pop_back(); // quitar la última coma

    ss << setw(5) << setfill('0') << (1 + data.size()); // tamaño total
    ss << 'L' << data;
    return ss.str();
}

void handleClient(int cliSock) {
    char buffer[1024];
    int n;

    // Leer nombre de usuario al inicio
    bzero(buffer, sizeof(buffer));
    n = read(cliSock, buffer, sizeof(buffer) - 1);
    buffer[n] = '\0';
    string nombre(buffer);

    {
        lock_guard<mutex> lock(user_mutex);
        usuarios[nombre] = cliSock;
    }

    cout << nombre << " se ha conectado.\n";

    while ((n = read(cliSock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';

        if (strncmp(buffer, "00001l", 6) == 0) {
            string lista = formatUserList();
            write(cliSock, lista.c_str(), lista.size());
            continue;
        }

        if (buffer[5] == 'm') {
            int msgLen = stoi(string(buffer + 6, 5));
            string mensaje(buffer + 11, msgLen);
            int userLen = stoi(string(buffer + 11 + msgLen, 5));
            string destino(buffer + 16 + msgLen, userLen);

            int destSock;
            {
                lock_guard<mutex> lock(user_mutex);
                if (usuarios.find(destino) == usuarios.end()) {
                    string errorMsg = formatServerMessage("Usuario no disponible", "Servidor");
                    write(cliSock, errorMsg.c_str(), errorMsg.size());
                    continue;
                }
                destSock = usuarios[destino];
            }

            string enviado = formatServerMessage(mensaje, nombre);
            write(destSock, enviado.c_str(), enviado.size());
        }
    }

    // Desconexión
    {
        lock_guard<mutex> lock(user_mutex);
        usuarios.erase(nombre);
    }

    close(cliSock);
    cout << nombre << " se ha desconectado.\n";
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10)) {
        perror("listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Servidor escuchando en puerto 45000...\n";

    while (true) {
        int cliSock = accept(SocketFD, NULL, NULL);
        if (cliSock < 0) {
            perror("accept failed");
            continue;
        }
        thread(handleClient, cliSock).detach();
    }

    close(SocketFD);
    return 0;
}

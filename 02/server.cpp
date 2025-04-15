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
#include <map>
#include <mutex>

using namespace std;

map<string, int> usuarios;
mutex user_mutex;

// Función auxiliar para leer "n" bytes del socket
string readN(int sock, int n) {
    string result;
    char buffer[1];
    int bytesRead;
    while(n > 0 && (bytesRead = read(sock, buffer, 1)) > 0) {
        result.append(buffer, bytesRead);
        n -= bytesRead;
    }
    return result;
}

// Formatea un mensaje normal para enviar del servidor al cliente:
// [5 bytes total] + 'M' + [5 bytes tamaño del mensaje] + mensaje + [5 bytes tamaño del nombre del remitente] + remitente
string formatServerMessage(const string &msg, const string &sender) {
    stringstream ss;
    int total = 1 + 5 + msg.size() + 5 + sender.size();
    ss << setw(5) << setfill('0') << total;
    ss << 'M';
    ss << setw(5) << setfill('0') << msg.size();
    ss << msg;
    ss << setw(5) << setfill('0') << sender.size();
    ss << sender;
    return ss.str();
}

// Formatea un mensaje broadcast desde el servidor a los clientes:
// [5 bytes total] + 'b' + [5 bytes tamaño del mensaje] + [5 bytes tamaño del nombre del emisor] + emisor + mensaje
string formatBroadcastFromServer(const string &mensaje, const string &sender) {
    stringstream ss;
    int total = 1 + 5 + 5 + sender.size() + mensaje.size();
    ss << setw(5) << setfill('0') << total;
    ss << 'b';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << setw(5) << setfill('0') << sender.size();
    ss << sender;
    ss << mensaje;
    return ss.str();
}

// Formatea la lista de usuarios conectados para enviar al cliente:
// [5 bytes total] + 'L' + lista (usuarios separados por comas)
string formatUserList() {
    stringstream ss, payload;
    {
        lock_guard<mutex> lock(user_mutex);
        for (const auto &p : usuarios) {
            payload << p.first << ",";
        }
    }
    string data = payload.str();
    if (!data.empty()) data.pop_back(); // quita la última coma
    int total = 1 + data.size();
    ss << setw(5) << setfill('0') << total;
    ss << 'L' << data;
    return ss.str();
}

void handleClient(int cliSock) {
    // --- Protocolo de login ---
    // Se espera recibir: [5 bytes tamaño del nombre] + 'n' + nombre
    string header = readN(cliSock, 5);
    if(header.size() < 5) { close(cliSock); return; }
    int unameLen = stoi(header);
    string typeStr = readN(cliSock, 1);
    if(typeStr.size() < 1 || typeStr[0] != 'n') {
        close(cliSock);
        return;
    }
    string nombre = readN(cliSock, unameLen);
    {
        lock_guard<mutex> lock(user_mutex);
        usuarios[nombre] = cliSock;
    }
    cout << nombre << " se ha conectado.\n";

    while(true) {
        // Leer la cabecera de cada mensaje: 5 bytes (tamaño total)
        string headerMsg = readN(cliSock, 5);
        if(headerMsg.size() < 5) break;  // desconexión
        int totalLen = stoi(headerMsg);
        // Leer el tipo de mensaje (1 byte)
        string typeMsg = readN(cliSock, 1);
        if(typeMsg.size() < 1) break;
        char type = typeMsg[0];
        if(type == 'l') { // Solicitud de lista ("00001l")
            string lista = formatUserList();
            write(cliSock, lista.c_str(), lista.size());
        } else if(type == 'm') { // Mensaje normal (cliente→servidor)
            string lenMsgStr = readN(cliSock, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cliSock, lenMsg);
            string lenDestStr = readN(cliSock, 5);
            int lenDest = stoi(lenDestStr);
            string destino = readN(cliSock, lenDest);
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
            string mensajeFormateado = formatServerMessage(mensaje, nombre);
            write(destSock, mensajeFormateado.c_str(), mensajeFormateado.size());
        } else if(type == 'b') { // Broadcast enviado por el cliente
            string lenMsgStr = readN(cliSock, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cliSock, lenMsg);
            // Formatear broadcast a ser enviado a todos los usuarios, excepto el emisor
            string bMsg = formatBroadcastFromServer(mensaje, nombre);
            lock_guard<mutex> lock(user_mutex);
            for(auto &p : usuarios) {
                if(p.first != nombre) {
                    write(p.second, bMsg.c_str(), bMsg.size());
                }
            }
        }
    }
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
    if(SocketFD < 0) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    if(bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
        perror("bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    if(listen(SocketFD, 10) < 0) {
        perror("listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    cout << "Servidor escuchando en el puerto 45000...\n";
    while(true) {
        int cliSock = accept(SocketFD, NULL, NULL);
        if(cliSock < 0) {
            perror("accept failed");
            continue;
        }
        thread(handleClient, cliSock).detach();
    }
    close(SocketFD);
    return 0;
}

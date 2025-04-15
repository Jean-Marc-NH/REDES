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

// Construye el mensaje normal (cliente→servidor) utilizando el protocolo:
// [5 bytes total] + 'm' + [5 bytes tamaño mensaje] + mensaje + [5 bytes tamaño destino] + destino
string formatMessage(const string &mensaje, const string &destino) {
    stringstream ss;
    int total = 1 + 5 + mensaje.size() + 5 + destino.size(); // 1 por la 'm'
    ss << setw(5) << setfill('0') << total;
    ss << 'm';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << mensaje;
    ss << setw(5) << setfill('0') << destino.size();
    ss << destino;
    return ss.str();
}

// Construye el mensaje de broadcast desde cliente a servidor:
// [5 bytes total] + 'b' + [5 bytes tamaño del mensaje] + mensaje
string formatBroadcastMessage(const string &mensaje) {
    int total = 1 + 5 + mensaje.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'b';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << mensaje;
    return ss.str();
}

// Lectura dinámica del socket, interpretando el protocolo
void readSocketThread(int cli) {
    while (true) {
        // Leer los 5 bytes que indican el tamaño total del mensaje
        string header = readN(cli, 5);
        if (header.size() < 5) break;  // desconexión
        int totalLen = stoi(header);
        // Leer el tipo de mensaje (1 byte)
        string typeStr = readN(cli, 1);
        if (typeStr.size() < 1) break;
        char type = typeStr[0];
        if (type == 'M') { // Mensaje normal (del servidor)
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cli, lenMsg);
            string lenSenderStr = readN(cli, 5);
            int lenSender = stoi(lenSenderStr);
            string sender = readN(cli, lenSender);
            cout << "\nMensaje de " << sender << ": " << mensaje << endl;
        } else if (type == 'L') { // Lista de usuarios
            int payloadSize = totalLen - 1;
            string lista = readN(cli, payloadSize);
            cout << "\nUsuarios conectados: " << lista << endl;
        } else if (type == 'b') { // Mensaje broadcast enviado desde el servidor
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string lenSenderStr = readN(cli, 5);
            int lenSender = stoi(lenSenderStr);
            string sender = readN(cli, lenSender);
            string mensaje = readN(cli, lenMsg);
            cout << "\nBroadcast de " << sender << ": " << mensaje << endl;
        }
    }
    shutdown(cli, SHUT_RDWR);
    close(cli);
    exit(0);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SocketFD < 0) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);
    if (connect(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    // Enviar nombre de usuario usando el protocolo de login: [5 bytes tamaño] + 'n' + nombre
    cout << "Introduce tu nombre de usuario: ";
    getline(cin, usuario);
    {
        stringstream ss;
        ss << setw(5) << setfill('0') << usuario.size();
        ss << 'n';
        ss << usuario;
        string loginMsg = ss.str();
        write(SocketFD, loginMsg.c_str(), loginMsg.size());
    }
    thread(readSocketThread, SocketFD).detach();
    
    while (true) {
        cout << "\nIngrese comando (mensaje, lista, broadcast o chau): ";
        string entrada;
        getline(cin, entrada);
        if (entrada == "chau") {
            shutdown(SocketFD, SHUT_RDWR);
            close(SocketFD);
            break;
        }
        if (entrada == "lista") {
            // Enviar solicitud de lista de usuarios: "00001l"
            write(SocketFD, "00001l", 6);
            continue;
        }
        if (entrada == "broadcast") {
            cout << "Ingrese mensaje de broadcast: ";
            string bMsg;
            getline(cin, bMsg);
            string bProtocol = formatBroadcastMessage(bMsg);
            write(SocketFD, bProtocol.c_str(), bProtocol.size());
            continue;
        }
        // Caso normal: se envía mensaje a un destinatario particular.
        cout << "Destinatario: ";
        string destino;
        getline(cin, destino);
        string normalMsg = formatMessage(entrada, destino);
        write(SocketFD, normalMsg.c_str(), normalMsg.size());
    }
    return 0;
}

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

string formatMessage(const string &mensaje, const string &destino) {
    stringstream ss;
    int total = 1 + 5 + mensaje.size() + 5 + destino.size();
    ss << setw(5) << setfill('0') << total;
    ss << 'm';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << mensaje;
    ss << setw(5) << setfill('0') << destino.size();
    ss << destino;
    return ss.str();
}

string formatBroadcastMessage(const string &mensaje) {
    int total = 1 + 5 + mensaje.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'b';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << mensaje;
    return ss.str();
}

void readSocketThread(int cli) {
    while (true) {
        string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = stoi(header);
        string typeStr = readN(cli, 1);
        if (typeStr.size() < 1) break;
        char type = typeStr[0];

        if (type == 'M') {
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cli, lenMsg);
            string lenSenderStr = readN(cli, 5);
            int lenSender = stoi(lenSenderStr);
            string sender = readN(cli, lenSender);
            cout << "\nMensaje de " << sender << ": " << mensaje << endl;
        } else if (type == 'L') {
            int payloadSize = totalLen - 1;
            string lista = readN(cli, payloadSize);
            cout << "\nUsuarios conectados: " << lista << endl;
        } else if (type == 'b') {
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string lenSenderStr = readN(cli, 5);
            int lenSender = stoi(lenSenderStr);
            string sender = readN(cli, lenSender);
            string mensaje = readN(cli, lenMsg);
            cout << "\nBroadcast de " << sender << ": " << mensaje << endl;
        } else if (type == 'q') {
            cout << "\nServidor indicó cierre de conexión." << endl;
            break;
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
        cout << "\nIngrese comando (mensaje, broadcast, lista o chau): ";
        string entrada;
        getline(cin, entrada);
        
        if (entrada == "chau") {
            write(SocketFD, "00001q", 6);
            shutdown(SocketFD, SHUT_RDWR);
            close(SocketFD);
            break;
        }
        
        if (entrada == "lista") {
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
        
        cout << "Destinatario: ";
        string destino;
        getline(cin, destino);
        string normalMsg = formatMessage(entrada, destino);
        write(SocketFD, normalMsg.c_str(), normalMsg.size());
    }
    return 0;
}

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
#include <map>
#include <sstream>
#include <iomanip>
#include <mutex>

using namespace std;

map<int, string> clientes;
mutex mtx;

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

void broadcast(const string &mensaje, int fromSock) {
    lock_guard<mutex> lock(mtx);
    string sender = clientes[fromSock];
    int total = 1 + 5 + 5 + sender.size() + mensaje.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'b';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << setw(5) << setfill('0') << sender.size();
    ss << sender;
    ss << mensaje;
    for (auto &[cliSock, name] : clientes) {
        if (cliSock != fromSock) {
            write(cliSock, ss.str().c_str(), ss.str().size());
        }
    }
}

void enviarMensaje(const string &mensaje, const string &destino, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[cliSock, name] : clientes) {
        if (name == destino) {
            destSock = cliSock;
            break;
        }
    }
    if (destSock == -1) return;
    string sender = clientes[fromSock];
    int total = 1 + 5 + mensaje.size() + 5 + sender.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'M';
    ss << setw(5) << setfill('0') << mensaje.size();
    ss << mensaje;
    ss << setw(5) << setfill('0') << sender.size();
    ss << sender;
    write(destSock, ss.str().c_str(), ss.str().size());
}

void enviarLista(int cliSock) {
    lock_guard<mutex> lock(mtx);
    stringstream ssLista;
    for (auto &[sock, name] : clientes) {
        ssLista << name << " ";
    }
    string lista = ssLista.str();
    int total = 1 + lista.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'L';
    ss << lista;
    write(cliSock, ss.str().c_str(), ss.str().size());
}

void readSocketThread(int cli) {
    while (true) {
        string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = stoi(header);
        string typeStr = readN(cli, 1);
        if (typeStr.size() < 1) break;
        char type = typeStr[0];
        
        if (type == 'n') {
            string nombre = readN(cli, totalLen - 1);
            lock_guard<mutex> lock(mtx);
            clientes[cli] = nombre;
            cout << nombre << " se ha conectado." << endl;
        }
        else if (type == 'm') {
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cli, lenMsg);
            string lenDestStr = readN(cli, 5);
            int lenDest = stoi(lenDestStr);
            string destino = readN(cli, lenDest);
            enviarMensaje(mensaje, destino, cli);
        }
        else if (type == 'l') {
            enviarLista(cli);
        }
        else if (type == 'b') {
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cli, lenMsg);
            broadcast(mensaje, cli);
        }
        else if (type == 'q') {
            string usuario;
            {
                lock_guard<mutex> lock(mtx);
                usuario = clientes[cli];
                clientes.erase(cli);
            }
            cout << usuario << " se ha desconectado." << endl;
            break;
        }
    }
    shutdown(cli, SHUT_RDWR);
    close(cli);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SocketFD < 0) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    if (listen(SocketFD, 10) < 0) {
        perror("listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    while (true) {
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if (ConnectFD < 0) {
            perror("accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }
        thread(readSocketThread, ConnectFD).detach();
    }
    close(SocketFD);
    return 0;
}

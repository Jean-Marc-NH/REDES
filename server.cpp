#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdio>      // para snprintf
#include <unistd.h>
#include <thread>
#include <iostream>
#include <map>
#include <mutex>
#include <vector>
#include <fstream>

std::mutex mtx;
std::map<int, std::string> clientes;

std::string readN(int sock, size_t n) {
    std::string result;
    result.reserve(n);
    char buf[1];
    while (n > 0) {
        int r = read(sock, buf, 1);
        if (r <= 0) break;
        result.append(buf, r);
        n -= r;
    }
    return result;
}

void enviarMensaje(const std::string &mensaje, const std::string &destino, int fromSock) {
    std::lock_guard<std::mutex> lock(mtx);
    int destSock = -1;
    for (auto &[cliSock, name] : clientes) {
        if (name == destino) { destSock = cliSock; break; }
    }
    if (destSock < 0) return;

    int total = 1 + 5 + mensaje.size() + 5 + destino.size();
    std::vector<char> buf(total);
    int len = std::snprintf(buf.data(), buf.size(), "%05d%c%05zu%.*s%05zu%.*s",
                            total, 'm',
                            mensaje.size(), (int)mensaje.size(), mensaje.c_str(),
                            destino.size(), (int)destino.size(), destino.c_str());
    write(destSock, buf.data(), len);
}

void broadcast(const std::string &mensaje, int fromSock) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string sender = clientes[fromSock];
    int total = 1 + 5 + mensaje.size() + 5 + sender.size();
    std::vector<char> buf(total);
    int len = std::snprintf(buf.data(), buf.size(), "%05d%c%05zu%.*s%05zu%.*s",
                            total, 'b',
                            mensaje.size(), (int)mensaje.size(), mensaje.c_str(),
                            sender.size(), (int)sender.size(), sender.c_str());
    for (auto &[cliSock, name] : clientes) {
        if (cliSock != fromSock) {
            write(cliSock, buf.data(), len);
        }
    }
}

void enviarLista(int cliSock) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string lista;
    for (auto &[sock, name] : clientes) {
        lista += name + " ";
    }
    int total = 1 + lista.size();
    std::vector<char> buf(total);
    int len = std::snprintf(buf.data(), buf.size(), "%05d%c%s",
                            total, 'l', lista.c_str());
    write(cliSock, buf.data(), len);
}

void forwardFile(const std::string &dest, const std::string &filename, const std::vector<char> &content, int fromSock) {
    std::lock_guard<std::mutex> lock(mtx);
    int destSock = -1;
    for (auto &[sock, name] : clientes) {
        if (name == dest) { destSock = sock; break; }
    }
    if (destSock < 0) return;

    long long contentSize = content.size();
    long long total = 1 + 5 + dest.size() + 5 + filename.size() + 18 + contentSize;
    std::vector<char> buf(total);
    int headerLen = std::snprintf(buf.data(), buf.size(), "%05lld%c%05zu%.*s%05zu%.*s%018lld",
                                   total, 'f',
                                   dest.size(), (int)dest.size(), dest.c_str(),
                                   filename.size(), (int)filename.size(), filename.c_str(),
                                   contentSize);
    std::memcpy(buf.data() + headerLen, content.data(), contentSize);
    write(destSock, buf.data(), headerLen + contentSize);
}

void readSocketThread(int cli) {
    while (true) {
        std::string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = std::stoi(header);
        std::string typeStr = readN(cli, 1);
        if (typeStr.size() < 1) break;
        char type = typeStr[0];

        if (type == 'n') {
            std::string nombre = readN(cli, totalLen);
            std::lock_guard<std::mutex> lock(mtx);
            clientes[cli] = nombre;
            std::cout << nombre << " se ha conectado." << std::endl;
        }
        else if (type == 'm') {
            int lenMsg = std::stoi(readN(cli, 5));
            std::string mensaje = readN(cli, lenMsg);
            int lenDest = std::stoi(readN(cli, 5));
            std::string destino = readN(cli, lenDest);
            enviarMensaje(mensaje, destino, cli);
        }
        else if (type == 'b') {
            int lenMsg = std::stoi(readN(cli, 5));
            std::string mensaje = readN(cli, lenMsg);
            broadcast(mensaje, cli);
        }
        else if (type == 'l') {
            enviarLista(cli);
        }
        else if (type == 'F') {
            int lenDest = std::stoi(readN(cli, 5));
            std::string dest = readN(cli, lenDest);
            int lenName = std::stoi(readN(cli, 5));
            std::string filename = readN(cli, lenName);
            int lenContent = std::stoi(readN(cli, 18));
            std::vector<char> content(lenContent);
            int readBytes = 0;
            while (readBytes < lenContent) {
                int r = read(cli, content.data() + readBytes, lenContent - readBytes);
                if (r <= 0) break;
                readBytes += r;
            }
            forwardFile(dest, filename, content, cli);
        }
        else if (type == 'q') {
            std::string usuario;
            {
                std::lock_guard<std::mutex> lock(mtx);
                usuario = clientes[cli];
                clientes.erase(cli);
            }
            std::cout << usuario << " se ha desconectado." << std::endl;
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
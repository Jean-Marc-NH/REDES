// client.cpp
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
#include <cstdio>      // para snprintf
#include <cstring>
#include <fstream>
#include <vector>

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

std::string formatMessage(const std::string &mensaje, const std::string &destino) {
    int total = 1 + 5 + mensaje.size() + 5 + destino.size();
    std::vector<char> buf(total);
    int len = std::snprintf(buf.data(), buf.size(), "%05d%c%05zu%.*s%05zu%.*s",
                            total, 'm',
                            mensaje.size(), (int)mensaje.size(), mensaje.c_str(),
                            destino.size(), (int)destino.size(), destino.c_str());
    return std::string(buf.data(), len);
}

std::string formatFileMessage(const std::string &filename, const std::string &destino) {
    std::ifstream infile(filename, std::ios::binary);
    std::vector<char> content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
    infile.close();

    long long contentSize = content.size();
    long long total = 1 + 5 + destino.size() + 5 + filename.size() + 18 + contentSize;
    std::vector<char> buf(total);
    int headerLen = std::snprintf(buf.data(), buf.size(), "%05lld%c%05zu%.*s%05zu%.*s%018lld",
                                   total, 'F',
                                   destino.size(), (int)destino.size(), destino.c_str(),
                                   filename.size(), (int)filename.size(), filename.c_str(),
                                   contentSize);
    std::memcpy(buf.data() + headerLen, content.data(), contentSize);
    return std::string(buf.begin(), buf.end());
}

int main(int argc, char *argv[]) {
    // ... Inicialización de SocketFD ...
    std::string entrada;
    std::string nombre = /* tu nombre */;
    // envio de nombre al servidor...
    while (std::getline(std::cin, entrada)) {
        if (entrada == "/file") {
            std::string dest;
            std::cout << "Destinatario: ";
            std::getline(std::cin, dest);
            std::string fileMsg = formatFileMessage(entrada, dest);
            write(SocketFD, fileMsg.c_str(), fileMsg.size());
            continue;
        }
        std::cout << "Destinatario: ";
        std::string destino;
        std::getline(std::cin, destino);
        std::string normalMsg = formatMessage(entrada, destino);
        write(SocketFD, normalMsg.c_str(), normalMsg.size());
    }
    return 0;
}

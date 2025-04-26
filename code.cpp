# Server (SERVER.CPP)
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
#include <mutex>
#include <vector>
#include <fstream>

using namespace std;

map<int, string> clientes;
mutex mtx;

string readN(int sock, int n) {
    string result;
    char buffer[1];
    int bytesRead;
    while (n > 0 && (bytesRead = read(sock, buffer, 1)) > 0) {
        result.append(buffer, bytesRead);
        n -= bytesRead;
    }
    return result;
}

void broadcast(const string &mensaje, int fromSock) {
    lock_guard<mutex> lock(mtx);
    const string &sender = clientes[fromSock];
    int lenMsg = mensaje.size();
    int lenSender = sender.size();
    int total = 1 + 5 + 5 + lenSender + lenMsg;
    char buf[6];
    string out;
    // Header: total length
    snprintf(buf, sizeof(buf), "%05d", total);
    out.append(buf, 5);
    // Message type
    out += 'b';
    // Message length
    snprintf(buf, sizeof(buf), "%05d", lenMsg);
    out.append(buf, 5);
    // Sender length
    snprintf(buf, sizeof(buf), "%05d", lenSender);
    out.append(buf, 5);
    // Sender and message
    out += sender;
    out += mensaje;

    for (auto &[cliSock, name] : clientes) {
        if (cliSock != fromSock) {
            write(cliSock, out.c_str(), out.size());
        }
    }
}

void enviarMensaje(const string &mensaje, const string &destino, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[cliSock, name] : clientes) {
        if (name == destino) { destSock = cliSock; break; }
    }
    if (destSock < 0) return;

    const string &sender = clientes[fromSock];
    int lenMsg = mensaje.size();
    int lenSender = sender.size();
    int total = 1 + 5 + lenMsg + 5 + lenSender;
    char buf[6];
    string out;
    snprintf(buf, sizeof(buf), "%05d", total);
    out.append(buf, 5);
    out += 'M';
    snprintf(buf, sizeof(buf), "%05d", lenMsg);
    out.append(buf, 5);
    out += mensaje;
    snprintf(buf, sizeof(buf), "%05d", lenSender);
    out.append(buf, 5);
    out += sender;

    write(destSock, out.c_str(), out.size());
}

void enviarLista(int cliSock) {
    lock_guard<mutex> lock(mtx);
    string lista;
    for (auto &[sock, name] : clientes) {
        lista += name + " ";
    }
    int lenList = lista.size();
    int total = 1 + lenList;
    char buf[6];
    string out;
    snprintf(buf, sizeof(buf), "%05d", total);
    out.append(buf, 5);
    out += 'L';
    out += lista;

    write(cliSock, out.c_str(), out.size());
}

void forwardFile(const string &dest, const string &filename, const vector<char> &content, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[sock, name] : clientes) {
        if (name == dest) { destSock = sock; break; }
    }
    if (destSock < 0) return;

    long long contentSize = content.size();
    int lenDest = dest.size();
    int lenName = filename.size();
    long long total = 1 + 5 + lenDest + 5 + lenName + 18 + contentSize;
    char buf[32];
    string meta;
    snprintf(buf, 6, "%05lld", total);
    meta.append(buf, 5);
    meta += 'f';
    snprintf(buf, 6, "%05d", lenDest);
    meta.append(buf, 5);
    meta += dest;
    snprintf(buf, 6, "%05d", lenName);
    meta.append(buf, 5);
    meta += filename;
    snprintf(buf, 19, "%018lld", contentSize);
    meta.append(buf, 18);

    write(destSock, meta.c_str(), meta.size());
    write(destSock, content.data(), contentSize);
}

void readSocketThread(int cli) {
    while (true) {
        string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = atoi(header.c_str());
        char type;
        if (read(cli, &type, 1) <= 0) break;

        if (type == 'n') {
            string nombre = readN(cli, totalLen);
            {
                lock_guard<mutex> lock(mtx);
                clientes[cli] = nombre;
            }
            printf("%s se ha conectado.\n", nombre.c_str());
        }
        else if (type == 'm') {
            string lenMsg = readN(cli, 5);
            string mensaje = readN(cli, atoi(lenMsg.c_str()));
            string lenDest = readN(cli, 5);
            string destino = readN(cli, atoi(lenDest.c_str()));
            enviarMensaje(mensaje, destino, cli);
        }
        else if (type == 'l') {
            enviarLista(cli);
        }
        else if (type == 'b') {
            string lenMsg = readN(cli, 5);
            string mensaje = readN(cli, atoi(lenMsg.c_str()));
            broadcast(mensaje, cli);
        }
        else if (type == 'F') {
            string lenDest = readN(cli, 5);
            string dest = readN(cli, atoi(lenDest.c_str()));
            string lenName = readN(cli, 5);
            string filename = readN(cli, atoi(lenName.c_str()));
            string lenContent = readN(cli, 18);
            long long contentSize = atoll(lenContent.c_str());

            vector<char> content(contentSize);
            long long readBytes = 0;
            while (readBytes < contentSize) {
                int r = read(cli, content.data() + readBytes, contentSize - readBytes);
                if (r <= 0) break;
                readBytes += r;
            }

            forwardFile(dest, filename, content, cli);
        }
        else if (type == 'q') {
            string usuario;
            {
                lock_guard<mutex> lock(mtx);
                usuario = clientes[cli];
                clientes.erase(cli);
            }
            printf("%s se ha desconectado.\n", usuario.c_str());
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
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
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


// Client (CLIENT.CPP)
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
#include <mutex>
#include <vector>
#include <fstream>

using namespace std;

map<int, string> clientes;
mutex mtx;

string readN(int sock, int n) {
    string result;
    char buffer[1];
    int bytesRead;
    while (n > 0 && (bytesRead = read(sock, buffer, 1)) > 0) {
        result.append(buffer, bytesRead);
        n -= bytesRead;
    }
    return result;
}

void broadcast(const string &mensaje, int fromSock) {
    lock_guard<mutex> lock(mtx);
    const string &sender = clientes[fromSock];
    int lenMsg = mensaje.size();
    int lenSender = sender.size();
    int total = 1 + 5 + 5 + lenSender + lenMsg;
    char buf[6];
    string out;
    snprintf(buf, sizeof(buf), "%05d", total);
    out.append(buf, 5);
    out += 'b';
    snprintf(buf, sizeof(buf), "%05d", lenMsg);
    out.append(buf, 5);
    snprintf(buf, sizeof(buf), "%05d", lenSender);
    out.append(buf, 5);
    out += sender;
    out += mensaje;
    for (auto &[cliSock, name] : clientes) {
        if (cliSock != fromSock) {
            write(cliSock, out.c_str(), out.size());
        }
    }
}

void enviarMensaje(const string &mensaje, const string &destino, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[cliSock, name] : clientes) {
        if (name == destino) { destSock = cliSock; break; }
    }
    if (destSock < 0) return;

    const string &sender = clientes[fromSock];
    int lenMsg = mensaje.size();
    int lenSender = sender.size();
    int total = 1 + 5 + lenMsg + 5 + lenSender;
    char buf[6];
    string out;
    snprintf(buf, sizeof(buf), "%05d", total);
    out.append(buf, 5);
    out += 'M';
    snprintf(buf, sizeof(buf), "%05d", lenMsg);
    out.append(buf, 5);
    out += mensaje;
    snprintf(buf, sizeof(buf), "%05d", lenSender);
    out.append(buf, 5);
    out += sender;

    write(destSock, out.c_str(), out.size());
}

void enviarLista(int cliSock) {
    lock_guard<mutex> lock(mtx);
    string lista;
    for (auto &[sock, name] : clientes) {
        lista += name + " ";
    }
    int lenList = lista.size();
    int total = 1 + lenList;
    char buf[6];
    string out;
    snprintf(buf, sizeof(buf), "%05d", total);
    out.append(buf, 5);
    out += 'L';
    out += lista;

    write(cliSock, out.c_str(), out.size());
}

void forwardFile(const string &dest, const string &filename, const vector<char> &content, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[sock, name] : clientes) {
        if (name == dest) { destSock = sock; break; }
    }
    if (destSock < 0) return;

    long long contentSize = content.size();
    int lenDest = dest.size();
    int lenName = filename.size();
    long long total = 1 + 5 + lenDest + 5 + lenName + 18 + contentSize;
    char buf[32];
    string meta;
    snprintf(buf, 6, "%05lld", total);
    meta.append(buf, 5);
    meta += 'f';
    snprintf(buf, 6, "%05d", lenDest);
    meta.append(buf, 5);
    meta += dest;
    snprintf(buf, 6, "%05d", lenName);
    meta.append(buf, 5);
    meta += filename;
    snprintf(buf, 19, "%018lld", contentSize);
    meta.append(buf, 18);

    write(destSock, meta.c_str(), meta.size());
    write(destSock, content.data(), contentSize);
}

void readSocketThread(int cli) {
    while (true) {
        string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = atoi(header.c_str());
        char type;
        if (read(cli, &type, 1) <= 0) break;

        if (type == 'n') {
            string nombre = readN(cli, totalLen);
            {
                lock_guard<mutex> lock(mtx);
                clientes[cli] = nombre;
            }
            printf("%s se ha conectado.\n", nombre.c_str());
        }
        else if (type == 'm') {
            string lenMsg = readN(cli, 5);
            string mensaje = readN(cli, atoi(lenMsg.c_str()));
            string lenDest = readN(cli, 5);
            string destino = readN(cli, atoi(lenDest.c_str()));
            enviarMensaje(mensaje, destino, cli);
        }
        else if (type == 'l') {
            enviarLista(cli);
        }
        else if (type == 'b') {
            string lenMsg = readN(cli, 5);
            string mensaje = readN(cli, atoi(lenMsg.c_str()));
            broadcast(mensaje, cli);
        }
        else if (type == 'F') {
            string lenDest = readN(cli, 5);
            string dest = readN(cli, atoi(lenDest.c_str()));
            string lenName = readN(cli, 5);
            string filename = readN(cli, atoi(lenName.c_str()));
            string lenContent = readN(cli, 18);
            long long contentSize = atoll(lenContent.c_str());

            vector<char> content(contentSize);
            long long readBytes = 0;
            while (readBytes < contentSize) {
                int r = read(cli, content.data() + readBytes, contentSize - readBytes);
                if (r <= 0) break;
                readBytes += r;
            }

            forwardFile(dest, filename, content, cli);
        }
        else if (type == 'q') {
            string usuario;
            {
                lock_guard<mutex> lock(mtx);
                usuario = clientes[cli];
                clientes.erase(cli);
            }
            printf("%s se ha desconectado.\n", usuario.c_str());
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
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
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

// Server (SERVER.CPP)
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
    string sender = clientes[fromSock];
    int total = 1 + 5 + 5 + sender.size() + mensaje.size();
    char buf[32];
    sprintf(buf, "%05d", total);
    string meta = buf;
    meta += 'b';
    sprintf(buf, "%05d", (int)mensaje.size());
    meta += buf;
    sprintf(buf, "%05d", (int)sender.size());
    meta += buf;
    meta += sender;
    meta += mensaje;
    for (auto &[cliSock, name] : clientes) {
        if (cliSock != fromSock) {
            write(cliSock, meta.c_str(), meta.size());
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
    char buf[32];
    sprintf(buf, "%05d", total);
    string meta = buf;
    meta += 'M';
    sprintf(buf, "%05d", (int)mensaje.size());
    meta += buf;
    meta += mensaje;
    sprintf(buf, "%05d", (int)sender.size());
    meta += buf;
    meta += sender;
    write(destSock, meta.c_str(), meta.size());
}

void enviarLista(int cliSock) {
    lock_guard<mutex> lock(mtx);
    string lista;
    for (auto &[sock, name] : clientes) {
        lista += name + " ";
    }
    int total = 1 + lista.size();
    char buf[32];
    sprintf(buf, "%05d", total);
    string meta = buf;
    meta += 'L';
    meta += lista;
    write(cliSock, meta.c_str(), meta.size());
}

void forwardFile(const string &dest, const string &filename, const vector<char> &content, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[sock, name] : clientes) {
        if (name == dest) {
            destSock = sock;
            break;
        }
    }
    if (destSock == -1) return;

    long long contentSize = content.size();
    long long total = 1 + 5 + dest.size() + 5 + filename.size() + 18 + contentSize;
    char buf[32];
    sprintf(buf, "%05lld", total);
    string meta = buf;
    meta += 'f';
    sprintf(buf, "%05d", (int)dest.size());
    meta += buf;
    meta += dest;
    sprintf(buf, "%05d", (int)filename.size());
    meta += buf;
    meta += filename;
    sprintf(buf, "%018lld", contentSize);
    meta += buf;

    write(destSock, meta.c_str(), meta.size());
    write(destSock, content.data(), contentSize);
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
            string nombre = readN(cli, totalLen);
            lock_guard<mutex> lock(mtx);
            clientes[cli] = nombre;
            printf("%s se ha conectado.\n", nombre.c_str());
        }
        else if (type == 'm') {
            int lenMsg = stoi(readN(cli, 5));
            string mensaje = readN(cli, lenMsg);
            int lenDest = stoi(readN(cli, 5));
            string destino = readN(cli, lenDest);
            enviarMensaje(mensaje, destino, cli);
        }
        else if (type == 'l') {
            enviarLista(cli);
        }
        else if (type == 'b') {
            int lenMsg = stoi(readN(cli, 5));
            string mensaje = readN(cli, lenMsg);
            broadcast(mensaje, cli);
        }
        else if (type == 'F') {
            int lenDest = stoi(readN(cli, 5));
            string dest = readN(cli, lenDest);
            int lenName = stoi(readN(cli, 5));
            string filename = readN(cli, lenName);
            int lenContent = stoi(readN(cli, 18));

            vector<char> content(lenContent);
            int readBytes = 0;
            while (readBytes < lenContent) {
                int r = read(cli, content.data() + readBytes, lenContent - readBytes);
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
#include <vector>
#include <fstream>

using namespace std;

string usuario;

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

string formatMessage(const string &mensaje, const string &destino) {
    char buf[32];
    int total = 1 + 5 + mensaje.size() + 5 + destino.size();
    string meta;
    sprintf(buf, "%05d", total);
    meta = buf;
    meta += 'm';
    sprintf(buf, "%05d", (int)mensaje.size());
    meta += buf;
    meta += mensaje;
    sprintf(buf, "%05d", (int)destino.size());
    meta += buf;
    meta += destino;
    return meta;
}

string formatBroadcastMessage(const string &mensaje) {
    char buf[32];
    int total = 1 + 5 + mensaje.size();
    string meta;
    sprintf(buf, "%05d", total);
    meta = buf;
    meta += 'b';
    sprintf(buf, "%05d", (int)mensaje.size());
    meta += buf;
    meta += mensaje;
    return meta;
}

string formatFileMessage(const string &filename, const string &destino) {
    ifstream infile(filename, ios::binary);
    vector<char> buffer((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    infile.close();

    long long contentSize = buffer.size();
    long long total = 1 + 5 + destino.size() + 5 + filename.size() + 18 + contentSize;
    char buf[32];
    string meta;
    sprintf(buf, "%05lld", total);
    meta = buf;
    meta += 'F';
    sprintf(buf, "%05d", (int)destino.size());
    meta += buf;
    meta += destino;
    sprintf(buf, "%05d", (int)filename.size());
    meta += buf;
    meta += filename;
    sprintf(buf, "%018lld", contentSize);
    meta += buf;
    meta.insert(meta.end(), buffer.begin(), buffer.end());
    return meta;
}

void readSocketThread(int cli) {
    while (true) {
        string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = stoi(header);
        string typeStr = readN(cli, 1);
        if (typeStr.empty()) break;
        char type = typeStr[0];

        if (type == 'M') {
            int lenMsg = stoi(readN(cli, 5));
            string mensaje = readN(cli, lenMsg);
            int lenSender = stoi(readN(cli, 5));
            string sender = readN(cli, lenSender);
            printf("\nMensaje de %s: %s\n", sender.c_str(), mensaje.c_str());

        } else if (type == 'L') {
            string lista = readN(cli, totalLen - 1);
            printf("\nUsuarios conectados: %s\n", lista.c_str());

        } else if (type == 'b') {
            int lenMsg = stoi(readN(cli, 5));
            int lenSender = stoi(readN(cli, 5));
            string sender = readN(cli, lenSender);
            string mensaje = readN(cli, lenMsg);
            printf("\nBroadcast de %s: %s\n", sender.c_str(), mensaje.c_str());

        } else if (type == 'f') {
            int lenDest = stoi(readN(cli, 5));
            string dest = readN(cli, lenDest);
            int lenName = stoi(readN(cli, 5));
            string filename = readN(cli, lenName);
            int lenContent = stoi(readN(cli, 18));

            vector<char> content(lenContent);
            int readBytes = 0;
            while (readBytes < lenContent) {
                int r = read(cli, content.data() + readBytes, lenContent - readBytes);
                if (r <= 0) break;
                readBytes += r;
            }

            size_t punto = filename.find_last_of('.');
            string copiaNombre = (punto == string::npos)
                ? filename + "_copia"
                : filename.substr(0, punto) + "_copia" + filename.substr(punto);

            ofstream outfile(copiaNombre, ios::binary);
            outfile.write(content.data(), lenContent);
            outfile.close();

            printf("\nArchivo recibido: %s\n", copiaNombre.c_str());

        } else if (type == 'q') {
            printf("\nServidor indicó cierre de conexión.\n");
            break;
        }
    }
    shutdown(cli, SHUT_RDWR);
    close(cli);
    exit(0);
}

int main() {
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

    printf("Introduce tu nombre de usuario: ");
    fflush(stdout);
    getline(cin, usuario);
    {
        char buf[32];
        sprintf(buf, "%05d", (int)usuario.size());
        string loginMsg = string(buf) + 'n' + usuario;
        write(SocketFD, loginMsg.c_str(), loginMsg.size());
    }

    thread(readSocketThread, SocketFD).detach();

    while (true) {
        printf("\nIngrese comando (mensaje, broadcast, lista, archivo o chau): ");
        fflush(stdout);
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
            printf("Ingrese mensaje de broadcast: ");
            fflush(stdout);
            string bMsg;
            getline(cin, bMsg);
            string bProtocol = formatBroadcastMessage(bMsg);
            write(SocketFD, bProtocol.c_str(), bProtocol.size());
            continue;
        }

        if (entrada == "archivo") {
            printf("Nombre del archivo: ");
            fflush(stdout);
            string nombre;
            getline(cin, nombre);
            printf("Destinatario: ");
            fflush(stdout);
            string dest;
            getline(cin, dest);
            string fileMsg = formatFileMessage(nombre, dest);
            write(SocketFD, fileMsg.c_str(), fileMsg.size());
            continue;
        }

        printf("Destinatario: ");
        fflush(stdout);
        string destino;
        getline(cin, destino);
        string normalMsg = formatMessage(entrada, destino);
        write(SocketFD, normalMsg.c_str(), normalMsg.size());
    }
    return 0;
}


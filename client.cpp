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
#include <fstream>
#include <vector>

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

string formatFileMessage(const string &filename, const string &destino) {
    ifstream infile(filename, ios::binary);
    vector<char> buffer((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    infile.close();

    long long contentSize = buffer.size();
    long long total = 1 + 5 + destino.size() + 5 + filename.size() + 18 + contentSize;

    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'F';
    ss << setw(5) << setfill('0') << destino.size();
    ss << destino;
    ss << setw(5) << setfill('0') << filename.size();
    ss << filename;
    ss << setw(18) << setfill('0') << contentSize;

    string msg = ss.str();
    msg.insert(msg.end(), buffer.begin(), buffer.end());
    return msg;
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
            cout << "\nMensaje de " << sender << ": " << mensaje << endl;

        } else if (type == 'L') {
            string lista = readN(cli, totalLen - 1);
            cout << "\nUsuarios conectados: " << lista << endl;

        } else if (type == 'b') {
            int lenMsg = stoi(readN(cli, 5));
            int lenSender = stoi(readN(cli, 5));
            string sender = readN(cli, lenSender);
            string mensaje = readN(cli, lenMsg);
            cout << "\nBroadcast de " << sender << ": " << mensaje << endl;

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

            cout << "\nArchivo recibido: " << copiaNombre << endl;

        } else if (type == 'q') {
            cout << "\nServidor indicó cierre de conexión." << endl;
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
        cout << "\nIngrese comando (mensaje, broadcast, lista, archivo o chau): ";
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

        if (entrada == "archivo") {
            cout << "Nombre del archivo: ";
            string nombre;
            getline(cin, nombre);
            cout << "Destinatario: ";
            string dest;
            getline(cin, dest);
            string fileMsg = formatFileMessage(nombre, dest);
            write(SocketFD, fileMsg.c_str(), fileMsg.size());
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
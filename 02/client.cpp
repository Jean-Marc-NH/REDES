// cliente.cpp
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
#include <algorithm>

using namespace std;

string usuario;
int mainSock;
bool in_client_game = false;
bool spectator_request = false;
char my_symbol = '_';
bool my_turn = false;

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
    ss << 'M';
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
    ss << setw(5) << setfill('0') << destino.size() << destino;
    ss << setw(5) << setfill('0') << filename.size() << filename;
    ss << setw(18) << setfill('0') << contentSize;

    string msg = ss.str();
    msg.insert(msg.end(), buffer.begin(), buffer.end());
    return msg;
}

string formatSpectator() {
    // Payload "ver"
    string payload = "ver";
    stringstream ss;
    ss << setw(5) << setfill('0') << payload.size();
    ss << 'V';
    ss << payload;
    return ss.str();
}

string formatPlayMessage(int pos, char sym) {
    string posStr = to_string(pos);
    int total = 1 + 5 + posStr.size() + 1;
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'P';
    ss << setw(5) << setfill('0') << posStr.size();
    ss << posStr;
    ss << sym;
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

        if (type == 'J') {
            // Join protocol
            cout << "[Sistema] Protocolo Join recibido. Esperando mensaje..." << endl;
        }
        else if (type == 'M') {
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string mensaje = readN(cli, lenMsg);
            string lenSenderStr = readN(cli, 5);
            int lenSender = stoi(lenSenderStr);
            string sender = readN(cli, lenSender);
            cout << "\nMensaje de " << sender << ": " << mensaje << endl;
            if (mensaje == "wait") {
                in_client_game = true;
                my_symbol = 'x';
                cout << "[TTT] Eres jugador X. Esperando segundo jugador..." << endl;
            } else if (mensaje == "start") {
                in_client_game = true;
                my_symbol = 'o';
                cout << "[TTT] Jugadores listos. Eres jugador O." << endl;
            } else if (mensaje == "quieres ver") {
                spectator_request = true;
                cout << "[TTT] El servidor ofrece ser espectador. Escribe 'ver' para aceptar." << endl;
            }
        }
        else if (type == 'X') {
            string lenStr = readN(cli, 5);
            int len = stoi(lenStr);
            string boardState = readN(cli, len);
            cout << "\n[TTT] Estado del tablero:" << endl;
            for (int i = 0; i < 9; ++i) {
                cout << (boardState[i] == '_' ? '.' : boardState[i]) << ' ';
                if (i % 3 == 2) cout << endl;
            }
            if (in_client_game) {
                int countX = count(boardState.begin(), boardState.end(), 'x');
                int countO = count(boardState.begin(), boardState.end(), 'o');
                char turn = (countX <= countO) ? 'x' : 'o';
                if (turn == my_symbol) {
                    my_turn = true;
                    cout << "[TTT] Tu turno. Escribe 'play' para mover." << endl;
                }
            }
        }
        else if (type == 'E') {
            string lenErrStr = readN(cli, 5);
            int lenErr = stoi(lenErrStr);
            string desc = readN(cli, lenErr);
            cout << "[TTT] Error: " << desc << endl;
        }
        else if (type == 'O') {
            char res = readN(cli, 1)[0];
            if (res == 'W') cout << "[TTT] ¡Has ganado!" << endl;
            else if (res == 'L') cout << "[TTT] Has perdido." << endl;
            else if (res == 'D') cout << "[TTT] Empate." << endl;
            in_client_game = false;
            my_turn = false;
            my_symbol = '_';
        }
        else if (type == 'L') {
            int payloadSize = totalLen - 1;
            string lista = readN(cli, payloadSize);
            cout << "\nUsuarios conectados: " << lista << endl;
        }
        else if (type == 'b') {
            string lenMsgStr = readN(cli, 5);
            int lenMsg = stoi(lenMsgStr);
            string lenSenderStr = readN(cli, 5);
            int lenSender = stoi(lenSenderStr);
            string sender = readN(cli, lenSender);
            string mensaje = readN(cli, lenMsg);
            cout << "\nBroadcast de " << sender << ": " << mensaje << endl;
        }
        else if (type == 'f') {
            int lenDest    = stoi(readN(cli, 5));
            string dest    = readN(cli, lenDest);
            int lenName    = stoi(readN(cli, 5));
            string filename= readN(cli, lenName);
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
        }
        else if (type == 'q') {
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
    mainSock = SocketFD;

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
        cout << "\nIngrese comando (mensaje, broadcast, lista, archivo, play, ver o chau): ";
        string entrada;
        getline(cin, entrada);

        if (entrada == "chau") {
            write(SocketFD, "00001q", 6);
            shutdown(SocketFD, SHUT_RDWR);
            close(SocketFD);
            break;
        }
        if (entrada == "ver" && spectator_request) {
            string vMsg = formatSpectator();
            write(SocketFD, vMsg.c_str(), vMsg.size());
            spectator_request = false;
            continue;
        }
        if (entrada == "play") {
            if (in_client_game && my_turn) {
                cout << "Ingresa posición (0-8): ";
                string posStr;
                getline(cin, posStr);
                int pos = stoi(posStr);
                string playMsg = formatPlayMessage(pos, my_symbol);
                write(SocketFD, playMsg.c_str(), playMsg.size());
                my_turn = false;
            } else {
                cout << "[TTT] No es tu turno o no estás en juego aún." << endl;
            }
            continue;
        }
        if (entrada == "lista") {
            write(SocketFD, "00001l", 6);
            continue;
        }
        if (entrada == "broadcast") {
            cout << "Ingrese mensaje de broadcast: ";
            string bMsg; getline(cin, bMsg);
            string bProtocol = formatBroadcastMessage(bMsg);
            write(SocketFD, bProtocol.c_str(), bProtocol.size());
            continue;
        }
        if (entrada == "archivo") {
            cout << "Nombre del archivo: ";
            string nombre; getline(cin, nombre);
            cout << "Destinatario: ";
            string dest; getline(cin, dest);
            string fileMsg = formatFileMessage(nombre, dest);
            write(SocketFD, fileMsg.c_str(), fileMsg.size());
            continue;
        }
        // Mensaje normal
        cout << "Destinatario: ";
        string destino;
        getline(cin, destino);
        string normalMsg = formatMessage(entrada, destino);
        write(SocketFD, normalMsg.c_str(), normalMsg.size());
    }
    return 0;
}

// server.cpp
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
#include <vector>
#include <fstream>

using namespace std;

map<int, string> clientes;
mutex mtx;

// --- Estado del juego ---
bool    game_active = false;
char    board[9];               // 'x', 'o' o '_'
int     player1_sock = -1;
int     player2_sock = -1;
int     turn_sock = -1;         // socket del jugador que debe mover
vector<int> spectators;

// Envía exactamente n bytes
ssize_t writeN(int sockfd, const char *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t sent = write(sockfd, buf + total, n - total);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

// Lee exactamente n bytes
string readN(int sock, int n) {
    string result;
    result.reserve(n);
    char buffer[1];
    int bytesRead;
    while (n > 0 && (bytesRead = read(sock, buffer, 1)) > 0) {
        result.append(buffer, bytesRead);
        n -= bytesRead;
    }
    return result;
}

// Chat y envío de archivos existentes
void broadcast(const string &mensaje, int fromSock);
void enviarMensaje(const string &mensaje, const string &destino, int fromSock);
void enviarLista(int cliSock);
void forwardFile(const string &dest, const string &filename, const vector<char> &content, int fromSock);

// --- Funciones auxiliares para TTT ---
void sendJoin(int sock) {
    // Solo protocolo Join: 5 bytes + 'J'
    string header = "00001J";
    writeN(sock, header.c_str(), header.size());
}

void sendView(int sock) {
    // Protocolo View: 5 bytes (tamaño) + 'X' + 5 bytes(len) + board
    string board_state(board, board + 9);
    int len = board_state.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << (1 + 5 + len);
    ss << 'X';
    ss << setw(5) << setfill('0') << len;
    ss << board_state;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

void sendError(int sock, int code, const string &desc) {
    // Error: 5 B + 'E' + 5 B(msg size) + desc
    int len = desc.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << (1 + 5 + len);
    ss << 'E';
    ss << setw(5) << setfill('0') << len;
    ss << desc;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

void sendOutcome(int sock, char result) {
    // Outcome: 5 B + 'O' + 1 byte result
    stringstream ss;
    ss << setw(5) << setfill('0') << 2;
    ss << 'O';
    ss << result;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

bool check_winner(char sym) {
    int wins[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    for (auto &w : wins) {
        if (board[w[0]] == sym && board[w[1]] == sym && board[w[2]] == sym)
            return true;
    }
    return false;
}

// Maneja lectura de socket por cliente
void readSocketThread(int cli) {
    while (true) {
        string header = readN(cli, 5);
        if (header.size() < 5) break;
        int totalLen = stoi(header);
        string typeStr = readN(cli, 1);
        if (typeStr.empty()) break;
        char type = typeStr[0];

        if (type == 'n') {
            // Nombre
            string nombre = readN(cli, totalLen);
            {
                lock_guard<mutex> lock(mtx);
                clientes[cli] = nombre;
            }
            // Protocolo Join
            sendJoin(cli);
            lock_guard<mutex> lock(mtx);
            if (player1_sock < 0) {
                player1_sock = cli;
                // mensaje de espera a través de chat
                enviarMensaje("wait", nombre, cli);
            } else if (player2_sock < 0) {
                player2_sock = cli;
                game_active = true;
                turn_sock = player1_sock;
                // iniciar juego
                for (int i = 0; i < 9; ++i) board[i] = '_';
                // mensaje start
                enviarMensaje("start", nombre, cli);
                // enviar estado inicial a ambos
                sendView(player1_sock);
                sendView(player2_sock);
            } else {
                // espectadores
                enviarMensaje("quieres ver", nombre, cli);
            }
        }
        else if (type == 'V') {
            // Espectador responde 'ver'
            string respuesta = readN(cli, totalLen);
            if (respuesta == "ver") {
                lock_guard<mutex> lock(mtx);
                spectators.push_back(cli);
            }
            sendView(cli);
        }
        else if (type == 'P') {
            // Jugada: P Position Symbol
            int lenPos = stoi(readN(cli, 5));
            string posStr = readN(cli, lenPos);
            int pos = stoi(posStr);
            string symStr = readN(cli, 1);
            char sym = symStr[0];
            lock_guard<mutex> lock(mtx);
            if (!game_active || cli != turn_sock || pos < 0 || pos >= 9 || board[pos] != '_') {
                sendError(cli, 1, "Invalid move");
            } else {
                board[pos] = sym;
                // alternar turno
                turn_sock = (turn_sock == player1_sock ? player2_sock : player1_sock);
                // enviar view a jugadores y espectadores
                sendView(player1_sock);
                sendView(player2_sock);
                for (int s : spectators) sendView(s);
                // comprobar fin
                if (check_winner(sym)) {
                    // ganador
                    sendOutcome(cli, 'W');
                    int loser = (cli == player1_sock ? player2_sock : player1_sock);
                    sendOutcome(loser, 'L');
                    game_active = false;
                    player1_sock = player2_sock = -1;
                    spectators.clear();
                } else {
                    bool draw = true;
                    for (char c : board) if (c == '_') { draw = false; break; }
                    if (draw) {
                        sendOutcome(player1_sock, 'D');
                        sendOutcome(player2_sock, 'D');
                        game_active = false;
                        player1_sock = player2_sock = -1;
                        spectators.clear();
                    }
                }
            }
        }
        else {
            // Otros protocolos: chat y archivo
            if (type == 'm' || type == 'l' || type == 'b' || type == 'F' || type == 'q') {
                // Aquí irá tu código original para m, l, b, F, q
                // broadcast(...); enviarMensaje(...); enviarLista(...); forwardFile(...); etc.
            }
        }
    }
    shutdown(cli, SHUT_RDWR);
    close(cli);
}

int main() {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SocketFD < 0) { perror("cannot create socket"); exit(EXIT_FAILURE); }
    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
        perror("bind failed"); close(SocketFD); exit(EXIT_FAILURE);
    }
    if (listen(SocketFD, 10) < 0) {
        perror("listen failed"); close(SocketFD); exit(EXIT_FAILURE);
    }
    while (true) {
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if (ConnectFD < 0) {
            perror("accept failed"); close(SocketFD); exit(EXIT_FAILURE);
        }
        thread(readSocketThread, ConnectFD).detach();
    }
    close(SocketFD);
    return 0;
}

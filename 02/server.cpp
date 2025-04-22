// server.cpp
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <iostream>
#include <map>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <vector>
#include <fstream>

using namespace std;

// ——— Estado global ———
map<int,string> clientes;
mutex            mtx;

// Tic‑Tac‑Toe state
bool        game_active   = false;
char        board[9];            // 'x', 'o' o '_'
int         player1_sock  = -1;
int         player2_sock  = -1;
int         turn_sock     = -1;
vector<int> spectators;

// ——— Básicos de E/S ———
ssize_t writeN(int sockfd, const char *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t sent = write(sockfd, buf + total, n - total);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

string readN(int sock, int n) {
    string result;
    result.reserve(n);
    char c;
    int readBytes;
    while (n > 0 && (readBytes = read(sock, &c, 1)) > 0) {
        result.push_back(c);
        n -= readBytes;
    }
    return result;
}

// ——— Chat y envío de archivos ———
void broadcast(const string &msg, int fromSock) {
    lock_guard<mutex> lock(mtx);
    string sender = clientes[fromSock];
    int total = 1 + 5 + 5 + sender.size() + msg.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'b'
       << setw(5) << setfill('0') << msg.size()
       << setw(5) << setfill('0') << sender.size()
       << sender
       << msg;
    string data = ss.str();
    for (auto &[sock, name] : clientes)
        if (sock != fromSock)
            writeN(sock, data.data(), data.size());
}

void enviarMensaje(const string &msg, const string &dest, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[sock, name] : clientes)
        if (name == dest) { destSock = sock; break; }
    if (destSock < 0) return;
    string sender = clientes[fromSock];
    int total = 1 + 5 + msg.size() + 5 + sender.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'M'
       << setw(5) << setfill('0') << msg.size()
       << msg
       << setw(5) << setfill('0') << sender.size()
       << sender;
    string data = ss.str();
    writeN(destSock, data.data(), data.size());
}

void enviarLista(int cliSock) {
    lock_guard<mutex> lock(mtx);
    stringstream ls;
    for (auto &[sock, name] : clientes)
        ls << name << " ";
    string lista = ls.str();
    int total = 1 + lista.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'L'
       << lista;
    string data = ss.str();
    writeN(cliSock, data.data(), data.size());
}

void forwardFile(const string &dest, const string &fname, const vector<char> &content, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[sock, name] : clientes)
        if (name == dest) { destSock = sock; break; }
    if (destSock < 0) return;
    long long cSz = content.size();
    long long total = 1 + 5 + dest.size() + 5 + fname.size() + 18 + cSz;
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'f'
       << setw(5) << setfill('0') << dest.size() << dest
       << setw(5) << setfill('0') << fname.size() << fname
       << setw(18) << setfill('0') << cSz;
    string header = ss.str();
    writeN(destSock, header.data(), header.size());
    writeN(destSock, content.data(), cSz);
}

// ——— Tic‑Tac‑Toe auxiliares ———
void sendJoin(int sock) {
    writeN(sock, "00001J", 6);
}

void sendView(int sock) {
    string bs(board, board+9);
    int len = bs.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << (1 + 5 + len)
       << 'X'
       << setw(5) << setfill('0') << len
       << bs;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

void sendError(int sock, int code, const string &desc) {
    int len = desc.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << (1 + 5 + len)
       << 'E'
       << setw(5) << setfill('0') << len
       << desc;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

void sendOutcome(int sock, char res) {
    stringstream ss;
    ss << setw(5) << setfill('0') << 2
       << 'O'
       << res;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

bool check_winner(char s) {
    int wins[8][3] = {
      {0,1,2},{3,4,5},{6,7,8},
      {0,3,6},{1,4,7},{2,5,8},
      {0,4,8},{2,4,6}
    };
    for (auto &w : wins)
        if (board[w[0

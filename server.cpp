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
#include <algorithm>
#include <array>
#include <set>

using namespace std;

constexpr int WLEN   = 10;
constexpr int WFIELD = 10;
constexpr int WCONT  = 18;

static const int WINS[8][3] = {
    {0,1,2}, {3,4,5}, {6,7,8},
    {0,3,6}, {1,4,7}, {2,5,8},
    {0,4,8}, {2,4,6}
};


mutex mtx;
map<int,string> clientes;

struct Game {
    array<char,9> board{ {'_','_','_','_','_','_','_','_','_'} };
    int turnSock = -1;
    char turnSymbol = 'X';
    set<int> players;
    set<int> spectators;
};
static Game game;
static vector<int> waiting;

ssize_t safeRead(int sock, void* buf, size_t len) {
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t r = read(sock, (char*)buf + total, len - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}
string readN(int sock, int n) {
    string s(n, '\0');
    if (safeRead(sock, &s[0], n) != n) return "";
    return s;
}
ssize_t safeWrite(int sock, const void* buf, size_t len) {
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t w = write(sock, (char*)buf + total, len - total);
        if (w <= 0) return w;
        total += w;
    }
    return total;
}

void broadcast(const string &msg, int fromSock) {
    lock_guard<mutex> lock(mtx);
    string sender = clientes[fromSock];
    long long total = 1 + WFIELD + WFIELD + sender.size() + msg.size();
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf;
    out += 'b';
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, msg.size());   out += buf;
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, sender.size()); out += buf;
    out += sender + msg;
    for (auto &[sock,name] : clientes) {
        if (sock != fromSock) safeWrite(sock, out.c_str(), out.size());
    }
}

void enviarMensaje(const string &msg, const string &dst, int fromSock) {
    lock_guard<mutex> lock(mtx);
    auto it = find_if(clientes.begin(), clientes.end(),
                      [&](auto &p){ return p.second == dst; });
    if (it == clientes.end()) return;
    int destSock = it->first;
    long long total = 1 + WFIELD + msg.size() + WFIELD + clientes[fromSock].size();
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf;
    out += 'm';
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, msg.size()); out += buf;
    out += msg;
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, clientes[fromSock].size()); out += buf;
    out += clientes[fromSock];
    safeWrite(destSock, out.c_str(), out.size());
}

void enviarLista(int sock) {
    lock_guard<mutex> lock(mtx);
    string lst;
    for (auto &[s,name] : clientes) lst += name + " ";
    long long total = 1 + lst.size();
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf;
    out += 'L';
    out += lst;
    safeWrite(sock, out.c_str(), out.size());
}

void forwardFile(const string &dst, const string &fname, const vector<char> &content) {
    auto it = find_if(clientes.begin(), clientes.end(),
                      [&](auto &p){ return p.second == dst; });
    if (it == clientes.end()) return;
    int destSock = it->first;
    size_t sz = content.size();
    long long total = 1 + WFIELD + dst.size() + WFIELD + fname.size() + WCONT + sz;
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf;
    out += 'F';
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, dst.size());   out += buf; out += dst;
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, fname.size()); out += buf; out += fname;
    snprintf(buf, sizeof(buf), "%0*zu", WCONT, sz);            out += buf;
    safeWrite(destSock, out.c_str(), out.size());
    safeWrite(destSock, content.data(), sz);
}

void startGame() {
    game.players.insert(waiting[0]);
    game.players.insert(waiting[1]);
    game.turnSock = waiting[0];
    game.turnSymbol = 'X';
    waiting.clear();
    string bstate(game.board.begin(), game.board.end());
    char buf[64];
    long long tot = 1 + WFIELD + WFIELD + bstate.size();
    snprintf(buf,sizeof(buf),"%0*lld",WLEN,tot);
    string out = buf; out += 'X';
    snprintf(buf,64, "%0*zu", WFIELD, bstate.size()); out += buf;
    snprintf(buf,64, "%0*zu", WFIELD, (size_t)0); out += buf;
    out += bstate;
    for (int s: game.players) safeWrite(s,out.c_str(),out.size());
}
int checkWin() {
    static int wins[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for (int i=0;i<8;i++) {
        char a=game.board[wins[i][0]], b=game.board[wins[i][1]], c=game.board[wins[i][2]];
        if (a!='_' && a==b && b==c) return i+1;
    }
    return 0;
}

void readSocketThread(int cli) {
    while (true) {
        string hdr = readN(cli, WLEN);
        if (hdr.empty()) break;
        size_t totalLen = stoull(hdr);
        string payload = readN(cli, totalLen);
        if (payload.size() < totalLen) break;
        char type = payload[0];
        size_t off = 1;

        if (type == 'n') {
            size_t ln = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string name = payload.substr(off, ln);
            lock_guard<mutex> lock(mtx);
            clientes[cli] = name;
        }
        else if (type == 'm') {
            size_t lm = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string msg = payload.substr(off, lm); off += lm;
            size_t ls = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string dst = payload.substr(off, ls);
            enviarMensaje(msg, dst, cli);
        }
        else if (type == 'b') {
            size_t lm = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string msg = payload.substr(off, lm);
            broadcast(msg, cli);
        }
        else if (type == 'L') {
            enviarLista(cli);
        }
        else if (type == 'F') {
            size_t ld = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string dst = payload.substr(off, ld); off += ld;
            size_t fn = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string fname = payload.substr(off, fn); off += fn;
            size_t sz = stoull(payload.substr(off, WCONT)); off += WCONT;
            vector<char> data(sz);
            memcpy(data.data(), payload.data()+off, sz);
            forwardFile(dst, fname, data);
        }
        else if (type == 'J') {
            if (game.players.size() < 2) {
                waiting.push_back(cli);
                if (waiting.size() == 2) startGame();
            } else {
                safeWrite(cli, "00001V", 6);
            }
        }
        else if (type == 'V') {
            game.spectators.insert(cli);
        }
        else if (type == 'P') {
            char posChar = payload[off++];  
            char sym = game.turnSymbol;     

            int pos = posChar - '1';
            if (!game.players.count(cli)) {
            }
            else if (cli != game.turnSock) {
                string msg = "Turno equivocado";
                char buf2[32]; string out;
                long long tot2 = 1 + WFIELD + 1 + WFIELD + msg.size();
                snprintf(buf2,sizeof(buf2),"%0*lld",WLEN,tot2);
                out += buf2; out += 'E'; out += '1';
                snprintf(buf2,sizeof(buf2), "%0*zu", WFIELD, msg.size()); out += buf2; out += msg;
                safeWrite(cli, out.c_str(), out.size());
            }
            else if (pos<0 || pos>8 || game.board[pos] != '_') {
                string msg = "Casilla invalida";
                char buf2[32]; string out;
                long long tot2 = 1 + WFIELD + 1 + WFIELD + msg.size();
                snprintf(buf2,sizeof(buf2),"%0*lld",WLEN,tot2);
                out += buf2; out += 'E'; out += '2';
                snprintf(buf2,sizeof(buf2), "%0*zu", WFIELD, msg.size()); out += buf2; out += msg;
                safeWrite(cli, out.c_str(), out.size());
            }
            else {
                game.board[pos] = sym;
                int other = 0;
                for (int p : game.players) if (p != cli) other = p;
                game.turnSock = other;
                game.turnSymbol = (sym=='X'?'O':'X');
                string bstate2(game.board.begin(), game.board.end());
                char buf3[64];
                long long tot3 = 1 + WFIELD + WFIELD + bstate2.size();
                snprintf(buf3,sizeof(buf3),"%0*lld",WLEN,tot3);
                string out2 = buf3; out2 += 'X';
                snprintf(buf3,64, "%0*zu", WFIELD, bstate2.size()); out2 += buf3;
                snprintf(buf3,64, "%0*zu", WFIELD, (size_t)0); out2 += buf3;
                out2 += bstate2;
                for (int s: game.players) safeWrite(s,out2.c_str(),out2.size());
                for (int s: game.spectators) safeWrite(s,out2.c_str(),out2.size());
                int w = checkWin();
                if (w) {
                    for (int p: game.players) {
                        char result = (game.board[WINS[w-1][0]]=='X'?'W':'L');
                        string out3 = string("00003O") + (p==game.turnSock ? (result=='W'?"L":"W") : string(1,result));
                        safeWrite(p, out3.c_str(), out3.size());
                    }
                    game = Game();
                    waiting.clear();
                }
            }
        }
    }
    close(cli);
}

int main() {
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(45000);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(sockfd, 10) < 0) { perror("listen"); return 1; }
    while (true) {
        int cli = accept(sockfd, nullptr, nullptr);
        if (cli < 0) break;
        thread(readSocketThread, cli).detach();
    }
    close(sockfd);
    return 0;
}

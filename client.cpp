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
#include <fstream>
#include <vector>
#include <iterator>
#include <set>

using namespace std;

constexpr int WLEN   = 10;
constexpr int WFIELD = 10;
constexpr int WCONT  = 18;

int mySock;
set<int> gamePlayers;

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

string formatMessage(const string &msg, const string &dst) {
    long long total = 1 + WFIELD + msg.size() + WFIELD + dst.size();
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf;
    out += 'm';
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, msg.size()); out += buf;
    out += msg;
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, dst.size()); out += buf;
    out += dst;
    return out;
}
string formatBroadcast(const string& msg) {
    long long total = 1 + WFIELD + msg.size();
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf; out += 'b';
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, msg.size()); out += buf;
    out += msg;
    return out;
}
string formatFile(const string& fname, const string& dst) {
    ifstream in(fname, ios::binary);
    if (!in) { perror("open file"); return ""; }
    vector<char> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    size_t sz = data.size();
    long long total = 1 + WFIELD + dst.size() + WFIELD + fname.size() + WCONT + sz;
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", WLEN, total);
    string out = buf; out += 'F';
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, dst.size()); out += buf; out += dst;
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, fname.size()); out += buf; out += fname;
    snprintf(buf, sizeof(buf), "%0*zu", WCONT, sz); out += buf;
    out.append(data.begin(), data.end());
    return out;
}

void reader(int sock) {
    while (true) {
        string hdr = readN(sock, WLEN);
        if (hdr.empty()) break;
        size_t total = stoull(hdr);
        string p = readN(sock, total);
        if (p.size() < total) break;

        char type = p[0]; size_t off = 1;
        if (type == 'm') {
            size_t lm = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string msg = p.substr(off, lm); off += lm;
            size_t ls = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string sender = p.substr(off, ls);
            printf("<%s> %s\n", sender.c_str(), msg.c_str());
        }
        else if (type == 'b') {
            size_t lm = stoull(p.substr(off, WFIELD)); off += WFIELD;
            size_t ls = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string sender = p.substr(off, ls); off += ls;
            string msg = p.substr(off, lm);
            printf("[BROADCAST %s] %s\n", sender.c_str(), msg.c_str());
        }
        else if (type == 'L') {
            string lst = p.substr(off);
            printf("[USUARIOS] %s\n", lst.c_str());
        }
        else if (type == 'F') {
            size_t ld = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string dst = p.substr(off, ld); off += ld;
            size_t fn = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string orig = p.substr(off, fn); off += fn;
            size_t sz = stoull(p.substr(off, WCONT)); off += WCONT;
            vector<char> data(sz);
            memcpy(data.data(), p.data() + off, sz);

            string copia;
            size_t pos = orig.find_last_of('.');
            if (pos != string::npos) {
                copia = orig.substr(0, pos) + "_copia" + orig.substr(pos);
            } else {
                copia = orig + "_copia";
            }

            FILE* out = fopen(copia.c_str(), "wb");
            if (out) {
                fwrite(data.data(), 1, sz, out);
                fclose(out);
                printf("\n[ARCHIVO recibido: %s (de %s)]\n", copia.c_str(), dst.c_str());
            } else {
                perror("Error al guardar");
            }
        }
        else if (type == 'X') {
            size_t lm = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string board = p.substr(off, lm);
            printf("\nTablero TicTacToe:\n");
            for (int i = 0; i < 9; i++) {
                printf(" %c", board[i]);
                if (i % 3 == 2) printf("\n");
            }
            if (gamePlayers.count(sock)) {
                printf("Tu turno (1-9): "); fflush(stdout);
                char c = getchar(); getchar();
                if (c >= '1' && c <= '9') {
                    char msg[6] = {'0','0','0','0','3','P'};
                    msg[5] = c;
                    safeWrite(sock, msg, 6);
                }
            }
        }
        else if (type == 'E') {
            off += 1;  
            size_t lm = stoull(p.substr(off, WFIELD)); off += WFIELD;
            string em = p.substr(off, lm);
            printf("Error TicTacToe: %s\n", em.c_str());
        }
        else if (type == 'O') {
            char res = p[1];
            printf(res == 'W' ? "¡Ganaste!\n" : "Perdiste.\n");
        }
    }
}

int main() {
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) { perror("socket"); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    mySock = sock;

    string user;
    printf("Usuario: "); fflush(stdout);
    getline(cin, user);
    {
        long long tot = 1 + WFIELD + user.size();
        char b[32]; snprintf(b, sizeof(b), "%0*lld", WLEN, tot);
        string m = b; m += 'n';
        snprintf(b, sizeof(b), "%0*zu", WFIELD, user.size());
        m += b; m += user;
        write(sock, m.c_str(), m.size());
    }

    thread(reader, sock).detach();

    while (true) {
        printf("\nCmd (mensaje,broadcast,lista,archivo,t,chau): "); fflush(stdout);
        string cmd; getline(cin, cmd);

        if (cmd == "chau") {
            char b[32]; snprintf(b, sizeof(b), "%0*lld", WLEN, 1);
            string q = b; q += 'q';
            write(sock, q.c_str(), q.size());
            break;
        }
        else if (cmd == "lista") {
            char b[32]; snprintf(b, sizeof(b), "%0*lld", WLEN, 1);
            string m = b; m += 'L';
            write(sock, m.c_str(), m.size());
        }
        else if (cmd == "broadcast") {
            printf("Msg broadcast: "); fflush(stdout);
            string msg; getline(cin, msg);
            string m = formatBroadcast(msg);
            write(sock, m.c_str(), m.size());
        }
        else if (cmd == "archivo") {
            printf("Archivo (ruta): "); fflush(stdout);
            string ruta; getline(cin, ruta);
            printf("Destinatario: "); fflush(stdout);
            string dst; getline(cin, dst);
            string m = formatFile(ruta, dst);
            if (!m.empty()) write(sock, m.c_str(), m.size());
        }
        else if (cmd == "t") {
            safeWrite(sock, "00001J", 6);
            gamePlayers.insert(mySock);
        }
        else {
            printf("Destinatario: "); fflush(stdout);
            string dst; getline(cin, dst);
            string m = formatMessage(cmd, dst);
            write(sock, m.c_str(), m.size());
        }
    }

    close(sock);
    return 0;
}

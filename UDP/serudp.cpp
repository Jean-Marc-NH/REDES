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
#include <sstream>
#include <iomanip>

using namespace std;

int puerto = 45001;

map<int, string> clientes;
mutex mtx;
int psize{ 5 }, fsize{ 5 };

// Lectura y escritura
ssize_t safeRead(int sock, void* buf, size_t len);
ssize_t write(int sock, const void* buf, size_t len);
string readN(int, int);
void readSocketThread(int);

// Protocolos
void formatBroadcast(string, int);
void formatMenssage(string, string, int);
void formatList(int);
void formatFile(string, string, vector<char>);

int main()
{
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(puerto);
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
ssize_t writeN(int sock, const void* buf, size_t len) {
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t w = write(sock, (char*)buf + total, len - total);
        if (w <= 0) return w;
        total += w;
    }
    return total;
}

void formatBroadcast(string msg, int fromSock) 
{
    lock_guard<mutex> lock(mtx);
    string sender = clientes[fromSock];
    int total = 1 + psize + msg.size() + psize + sender.size();
    stringstream ss;
    ss << setw(psize) << setfill('0') << total
        << 'b'
        << setw(psize) << setfill('0') << msg.size()
        << setw(psize) << setfill('0') << sender.size()
        << sender
        << msg;
    string data = ss.str();
    for (auto& [sock, name] : clientes)
        if (sock != fromSock)
            writeN(sock, data.data(), data.size());
}

void formatMenssage(string msg, string dest, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto& [sock, name] : clientes)
        if (name == dest) { destSock = sock; break; }
    if (destSock < 0) return;
    string sender = clientes[fromSock];
    int total = 1 + psize + msg.size() + psize + sender.size();
    stringstream ss;
    ss << setw(psize) << setfill('0') << total
        << 'm'
        << setw(psize) << setfill('0') << msg.size()
        << msg
        << setw(psize) << setfill('0') << sender.size()
        << sender;
    string data = ss.str();
    writeN(destSock, data.data(), data.size());
}

void formatList(int cliSock) {
    lock_guard<mutex> lock(mtx);
    stringstream ls;
    for (auto& [sock, name] : clientes)
        ls << name << " ";
    string lista = ls.str();
    int total = 1 + lista.size();
    stringstream ss;
    ss << setw(psize) << setfill('0') << total
        << 'l'
        << lista;
    writeN(cliSock, ss.str().data(), ss.str().size());
}

void formatFile(string dst, string fname, vector<char> content) {
    auto it = find_if(clientes.begin(), clientes.end(),
        [&](auto& p) { return p.second == dst; });
    if (it == clientes.end()) return;
    int destSock = it->first;
    size_t sz = content.size();
    long long total = 1 + fsize + dst.size() + fsize + fname.size() + 18 + sz;
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*lld", fsize, total);
    string out = buf;
    out += 'f';
    snprintf(buf, sizeof(buf), "%0*zu", fsize, dst.size());   out += buf; out += dst;
    snprintf(buf, sizeof(buf), "%0*zu", fsize, fname.size()); out += buf; out += fname;
    snprintf(buf, sizeof(buf), "%0*zu", 18, sz);            out += buf;
    writeN(destSock, out.c_str(), out.size());
    writeN(destSock, content.data(), sz);
}

void readSocketThread(int cli) {
    while (true) {

        string hdr = readN(cli, psize);
        if (hdr.empty()) break;
        size_t total = stoi(hdr);
        string p = readN(cli, total);
        if (p.size() < total) break;

        char type = p[0]; size_t off = 1;

        if (type == 'N') {
            size_t ln = stoi(p.substr(off, psize)); 
            off += psize;
            string name = p.substr(off, ln);
            lock_guard<mutex> lock(mtx);
            clientes[cli] = name;
            cout << "Nuevo usuario: " << name << "\n" << flush;
        }
        else if (type == 'M') {
            size_t lm = stoi(p.substr(off, psize)); 
            off += psize;
            string msg = p.substr(off, lm); 
            off += lm;
            size_t ls = stoi(p.substr(off, psize)); 
            off += psize;
            string dst = p.substr(off, ls);
            formatMenssage(msg, dst, cli);
        }
        else if (type == 'B') {
            size_t lm = stoi(p.substr(off, psize)); 
            off += psize;
            string msg = p.substr(off, lm);
            formatBroadcast(msg, cli);
        }
        else if (type == 'L') {
            formatList(cli);
        }
        else if (type == 'F') {

            size_t ld = stoull(p.substr(off, fsize)); 
            off += fsize;
            string dst = p.substr(off, ld); 
            off += ld;
            size_t fn = stoull(p.substr(off, fsize)); 
            off += fsize;
            string fname = p.substr(off, fn); 
            off += fn;
            size_t sz = stoull(p.substr(off, 18)); 
            off += 18;
            vector<char> data(sz);
            memcpy(data.data(), p.data() + off, sz);
            formatFile(dst, fname, data);
        }
    }
    close(cli);
}
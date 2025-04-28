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

using namespace std;

constexpr int WLEN   = 10;
constexpr int WFIELD = 10;
constexpr int WCONT  = 18;

mutex mtx;
map<int,string> clientes;

ssize_t safeRead(int sock, void* buf, size_t len) {
    size_t total = 0;
    char *ptr = (char*)buf;
    while (total < len) {
        ssize_t r = read(sock, ptr + total, len - total);
        if (r < 0) { perror("read error"); return -1; }
        if (r == 0) return 0;
        total += r;
    }
    return total;
}

ssize_t safeWrite(int sock, const void* buf, size_t len) {
    size_t total = 0;
    const char *ptr = (const char*)buf;
    while (total < len) {
        ssize_t w = write(sock, ptr + total, len - total);
        if (w < 0) { perror("write error"); return -1; }
        total += w;
    }
    return total;
}

string readN(int sock, int n) {
    string s(n, '\0');
    if (safeRead(sock, &s[0], n) <= 0) return "";
    return s;
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
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, msg.size());   out += buf;
    out += msg;
    snprintf(buf, sizeof(buf), "%0*zu", WFIELD, clientes[fromSock].size()); out += buf;
    out += clientes[fromSock];
    safeWrite(destSock, out.c_str(), out.size());
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
        } else if (type == 'm') {
            size_t lm = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string msg = payload.substr(off, lm); off += lm;
            size_t ls = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string dst = payload.substr(off, ls);
            enviarMensaje(msg, dst, cli);
        } else if (type == 'b') {
            size_t lm = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string msg = payload.substr(off, lm);
            broadcast(msg, cli);
        } else if (type == 'L') {
            enviarLista(cli);
        } else if (type == 'F') {
            size_t ld = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string dst = payload.substr(off, ld); off += ld;
            size_t fn = stoull(payload.substr(off, WFIELD)); off += WFIELD;
            string fname = payload.substr(off, fn); off += fn;
            size_t sz = stoull(payload.substr(off, WCONT)); off += WCONT;
            vector<char> content(sz);
            memcpy(content.data(), payload.data() + off, sz);
            forwardFile(dst, fname, content);
        } else if (type == 'q') {
            lock_guard<mutex> lock(mtx);
            clientes.erase(cli);
            break;
        }
    }
    close(cli);
}

int main() {
    int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
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


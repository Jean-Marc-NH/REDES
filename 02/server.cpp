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

map<int, string> clientes;
mutex mtx;

// --- Estado del juego Tic‑Tac‑Toe ---
bool    game_active = false;
char    board[9];               // 'x', 'o' o '_'
int     player1_sock = -1;
int     player2_sock = -1;
int     turn_sock = -1;         // socket del jugador que debe mover
vector<int> spectators;

// --- Prototipos de utilidades ---
ssize_t writeN(int sockfd, const char *buf, size_t n);
string  readN(int sock, int n);

// Funciones originales de chat y archivos
void broadcast(const string &mensaje, int fromSock);
void enviarMensaje(const string &mensaje, const string &destino, int fromSock);
void enviarLista(int cliSock);
void forwardFile(const string &dest, const string &filename, const vector<char> &content, int fromSock);

// Funciones auxiliares para Tic‑Tac‑Toe
void sendJoin(int sock);
void sendView(int sock);
void sendError(int sock, int code, const string &desc);
void sendOutcome(int sock, char result);
bool check_winner(char sym);

// Hilo que atiende a cada cliente
void readSocketThread(int cli);

int main() {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SocketFD < 0) { perror("cannot create socket"); exit(EXIT_FAILURE); }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port   = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
        perror("bind failed"); close(SocketFD); exit(EXIT_FAILURE);
    }
    if (listen(SocketFD, 10) < 0) {
        perror("listen failed"); close(SocketDFD); exit(EXIT_FAILURE);
    }

    while (true) {
        int ConnectFD = accept(SocketFD, nullptr, nullptr);
        if (ConnectFD < 0) {
            perror("accept failed"); close(SocketFD); exit(EXIT_FAILURE);
        }
        thread(readSocketThread, ConnectFD).detach();
    }

    close(SocketFD);
    return 0;
}

// --- Definición de utilidades básicas ---

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
    char buffer[1];
    int bytesRead;
    while (n > 0 && (bytesRead = read(sock, buffer, 1)) > 0) {
        result.append(buffer, bytesRead);
        n -= bytesRead;
    }
    return result;
}

// --- Chat y archivos ---

void broadcast(const string &mensaje, int fromSock) {
    lock_guard<mutex> lock(mtx);
    string sender = clientes[fromSock];
    int total = 1 + 5 + 5 + sender.size() + mensaje.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'b'
       << setw(5) << setfill('0') << mensaje.size()
       << setw(5) << setfill('0') << sender.size()
       << sender
       << mensaje;
    string data = ss.str();
    for (auto &[cliSock, name] : clientes) {
        if (cliSock != fromSock)
            writeN(cliSock, data.data(), data.size());
    }
}

void enviarMensaje(const string &mensaje, const string &destino, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[cliSock, name] : clientes) {
        if (name == destino) { destSock = cliSock; break; }
    }
    if (destSock < 0) return;
    string sender = clientes[fromSock];
    int total = 1 + 5 + mensaje.size() + 5 + sender.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total;
    ss << 'M'
       << setw(5) << setfill('0') << mensaje.size()
       << mensaje
       << setw(5) << setfill('0') << sender.size()
       << sender;
    string data = ss.str();
    writeN(destSock, data.data(), data.size());
}

void enviarLista(int cliSock) {
    lock_guard<mutex> lock(mtx);
    stringstream ls;
    for (auto &[s, name] : clientes) ls << name << " ";
    string lista = ls.str();
    int total = 1 + lista.size();
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'L'
       << lista;
    string data = ss.str();
    writeN(cliSock, data.data(), data.size());
}

void forwardFile(const string &dest, const string &filename, const vector<char> &content, int fromSock) {
    lock_guard<mutex> lock(mtx);
    int destSock = -1;
    for (auto &[s, name] : clientes) {
        if (name == dest) { destSock = s; break; }
    }
    if (destSock < 0) return;
    long long cSz = content.size();
    long long total = 1 + 5 + dest.size() + 5 + filename.size() + 18 + cSz;
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'f'
       << setw(5) << setfill('0') << dest.size() << dest
       << setw(5) << setfill('0') << filename.size() << filename
       << setw(18) << setfill('0') << cSz;
    string header = ss.str();
    writeN(destSock, header.data(), header.size());
    writeN(destSock, content.data(), cSz);
}

// --- Tic‑Tac‑Toe ---

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

void sendOutcome(int sock, char result) {
    stringstream ss;
    ss << setw(5) << setfill('0') << 2
       << 'O'
       << result;
    string data = ss.str();
    writeN(sock, data.data(), data.size());
}

bool check_winner(char s) {
    int wins[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    for (auto &w : wins) {
        if (board[w[0]]==s && board[w[1]]==s && board[w[2]]==s)
            return true;
    }
    return false;
}

void readSocketThread(int cli) {
    while (true) {
        string h = readN(cli,5);
        if (h.size()<5) break;
        int totalLen = stoi(h);
        char type = readN(cli,1)[0];

        if (type=='n') {
            string nombre = readN(cli,totalLen);
            {
                lock_guard<mutex> lock(mtx);
                clientes[cli]=nombre;
            }
            sendJoin(cli);
            lock_guard<mutex> lock(mtx);
            if (player1_sock<0) {
                player1_sock=cli;
                enviarMensaje("wait", nombre, cli);
            }
            else if (player2_sock<0) {
                player2_sock=cli;
                game_active=true;
                turn_sock=player1_sock;
                for(int i=0;i<9;++i) board[i]='_';
                enviarMensaje("start", nombre, cli);
                sendView(player1_sock);
                sendView(player2_sock);
            }
            else {
                enviarMensaje("quieres ver", nombre, cli);
            }
        }
        else if (type=='V') {
            string resp = readN(cli,totalLen);
            if (resp=="ver") {
                lock_guard<mutex> lock(mtx);
                spectators.push_back(cli);
            }
            sendView(cli);
        }
        else if (type=='P') {
            int lp = stoi(readN(cli,5));
            int pos = stoi(readN(cli,lp));
            char sym = readN(cli,1)[0];
            lock_guard<mutex> lock(mtx);
            if (!game_active || cli!=turn_sock || pos<0||pos>=9||board[pos]!='_') {
                sendError(cli,1,"Invalid move");
            } else {
                board[pos]=sym;
                turn_sock=(turn_sock==player1_sock?player2_sock:player1_sock);
                sendView(player1_sock);
                sendView(player2_sock);
                for(int s: spectators) sendView(s);
                if (check_winner(sym)) {
                    sendOutcome(cli,'W');
                    sendOutcome((cli==player1_sock?player2_sock:player1_sock),'L');
                    game_active=false;
                    player1_sock=player2_sock=-1;
                    spectators.clear();
                } else {
                    bool draw=true;
                    for(char c:board) if(c=='_'){draw=false;break;}
                    if (draw) {
                        sendOutcome(player1_sock,'D');
                        sendOutcome(player2_sock,'D');
                        game_active=false;
                        player1_sock=player2_sock=-1;
                        spectators.clear();
                    }
                }
            }
        }
        else {
            // Chat y archivos
            if (type=='m') {
                int lm=stoi(readN(cli,5));
                string msg=readN(cli,lm);
                int ld=stoi(readN(cli,5));
                string dest=readN(cli,ld);
                enviarMensaje(msg,dest,cli);
            }
            else if(type=='b') {
                int lm=stoi(readN(cli,5));
                string msg=readN(cli,lm);
                broadcast(msg,cli);
            }
            else if(type=='l') {
                enviarLista(cli);
            }
            else if(type=='F') {
                int ld=stoi(readN(cli,5));
                string dest=readN(cli,ld);
                int ln=stoi(readN(cli,5));
                string fname=readN(cli,ln);
                int lc=stoi(readN(cli,18));
                vector<char> buf(lc);
                int r=0;
                while(r<lc) {
                    int k=read(cli,buf.data()+r,lc-r);
                    if(k<=0) break;
                    r+=k;
                }
                forwardFile(dest,fname,buf,cli);
            }
            else if(type=='q') {
                lock_guard<mutex> lock(mtx);
                cout<<clientes[cli]<<" se desconectó."<<endl;
                clientes.erase(cli);
                break;
            }
        }
    }
    shutdown(cli,SHUT_RDWR);
    close(cli);
}

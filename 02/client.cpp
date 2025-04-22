// cliente.cpp
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
    result.reserve(n);
    char c; int r;
    while (n>0 && (r=read(sock,&c,1))>0) {
        result.push_back(c);
        n -= r;
    }
    return result;
}

string formatMessage(const string &msg, const string &dst) {
    stringstream ss;
    int total = 1 + 5 + msg.size() + 5 + dst.size();
    ss << setw(5) << setfill('0') << total
       << 'M'
       << setw(5) << setfill('0') << msg.size()
       << msg
       << setw(5) << setfill('0') << dst.size()
       << dst;
    return ss.str();
}

string formatBroadcast(const string &msg) {
    stringstream ss;
    int total = 1 + 5 + msg.size();
    ss << setw(5) << setfill('0') << total
       << 'b'
       << setw(5) << setfill('0') << msg.size()
       << msg;
    return ss.str();
}

string formatFile(const string &fn, const string &dst) {
    ifstream in(fn, ios::binary);
    vector<char> buf((istreambuf_iterator<char>(in)), {});
    in.close();
    long long sz = buf.size();
    long long total = 1 + 5 + dst.size() + 5 + fn.size() + 18 + sz;
    stringstream ss;
    ss << setw(5) << setfill('0') << total
       << 'F'
       << setw(5) << setfill('0') << dst.size() << dst
       << setw(5) << setfill('0') << fn.size() << fn
       << setw(18) << setfill('0') << sz;
    string hdr = ss.str();
    hdr.insert(hdr.end(), buf.begin(), buf.end());
    return hdr;
}

string formatSpectator() {
    // payload "ver"
    string p = "ver";
    stringstream ss;
    ss << setw(5) << setfill('0') << p.size()
       << 'V'
       << p;
    return ss.str();
}

string formatJoinReq() {
    // no payload
    return string("00000j");
}

string formatPlay(int pos, char sym) {
    string ps = to_string(pos);
    stringstream ss;
    int total = 1 + 5 + ps.size() + 1;
    ss << setw(5) << setfill('0') << total
       << 'P'
       << setw(5) << setfill('0') << ps.size()
       << ps
       << sym;
    return ss.str();
}

void readSocketThread(int sock) {
    while (true) {
        string h = readN(sock,5);
        if (h.size()<5) break;
        int totalLen = stoi(h);
        char type = readN(sock,1)[0];

        if (type=='J') {
            cout<<"[Sistema] JOIN confirmado, esperando…”"<<endl;
        }
        else if (type=='M') {
            int lm = stoi(readN(sock,5));
            string msg = readN(sock,lm);
            int ls = stoi(readN(sock,5));
            string snd = readN(sock,ls);
            cout<<"\nMensaje de "<<snd<<": "<<msg<<endl;
            if (msg=="wait") {
                in_client_game=true; my_symbol='x';
                cout<<"[TTT] Eres X, esperando rival…"<<endl;
            }
            else if (msg=="start") {
                in_client_game=true; my_symbol='o';
                cout<<"[TTT] Ya puedes jugar, eres O."<<endl;
            }
            else if (msg=="quieres ver") {
                spectator_request=true;
                cout<<"[TTT] Oferta espectador: escribe 'ver'."<<endl;
            }
        }
        else if (type=='X') {
            int ln = stoi(readN(sock,5));
            string bs = readN(sock,ln);
            cout<<"\n[TTT] Tablero:"<<endl;
            for(int i=0;i<9;i++){
                cout<<(bs[i]=='_'?'.':bs[i])<<' ';
                if(i%3==2) cout<<endl;
            }
            if(in_client_game){
                int cx=count(bs.begin(),bs.end(),'x');
                int co=count(bs.begin(),bs.end(),'o');
                char turn = (cx<=co?'x':'o');
                if(turn==my_symbol){
                    my_turn=true;
                    cout<<"[TTT] Tu turno: escribe 'play'."<<endl;
                }
            }
        }
        else if(type=='E') {
            int le=stoi(readN(sock,5));
            string d=readN(sock,le);
            cout<<"[TTT] Error: "<<d<<endl;
        }
        else if(type=='O') {
            char r = readN(sock,1)[0];
            if(r=='W') cout<<"[TTT] ¡Ganaste!"<<endl;
            else if(r=='L') cout<<"[TTT] Perdiste."<<endl;
            else if(r=='D') cout<<"[TTT] Empate."<<endl;
            in_client_game=false; my_turn=false; my_symbol='_';
        }
        else if(type=='L') {
            string ls = readN(sock,totalLen-1);
            cout<<"\nUsuarios: "<<ls<<endl;
        }
        else if(type=='b') {
            int lm=stoi(readN(sock,5));
            int ls=stoi(readN(sock,5));
            string snd=readN(sock,ls);
            string msg=readN(sock,lm);
            cout<<"\nBroadcast de "<<snd<<": "<<msg<<endl;
        }
        else if(type=='f') {
            int ld=stoi(readN(sock,5));
            readN(sock,ld); //dest
            int ln=stoi(readN(sock,5));
            string fn=readN(sock,ln);
            int lc=stoi(readN(sock,18));
            vector<char> buf(lc);
            int r=0;
            while(r<lc){int k=read(sock,buf.data()+r,lc-r); if(k<=0)break; r+=k;}
            size_t p=fn.find_last_of('.');
            string out = (p==string::npos? fn+"_copia": fn.substr(0,p)+"_copia"+fn.substr(p));
            ofstream o(out,ios::binary);
            o.write(buf.data(),lc);
            o.close();
            cout<<"\nArchivo recibido: "<<out<<endl;
        }
        else if(type=='q') {
            cout<<"\nServidor cerró conexión."<<endl;
            break;
        }
    }
    shutdown(sock,SHUT_RDWR);
    close(sock);
    exit(0);
}

int main(){
    sockaddr_in addr{};
    int sock = socket(PF_INET,SOCK_STREAM,0);
    addr.sin_family=AF_INET;
    addr.sin_port=htons(45000);
    inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
    if(connect(sock,(sockaddr*)&addr,sizeof(addr))<0){
        perror("connect"); exit(1);
    }
    mainSock=sock;

    cout<<"Usuario: "; getline(cin,usuario);
    stringstream ss;
    ss<<setw(5)<<setfill('0')<<usuario.size()
      <<'n'
      <<usuario;
    write(sock,ss.str().c_str(),ss.str().size());

    thread(readSocketThread,sock).detach();

    while(true){
        cout<<"\nComando (mensaje, broadcast, lista, archivo, jugar, play, ver, chau): ";
        string e; getline(cin,e);

        if(e=="chau"){ write(sock,"00001q",6); break; }
        if(e=="jugar"){ write(sock,formatJoinReq().c_str(),5+1); continue; }
        if(e=="ver" && spectator_request){
            write(sock,formatSpectator().c_str(),formatSpectator().size());
            spectator_request=false;
            continue;
        }
        if(e=="play"){
            if(in_client_game && my_turn){
                cout<<"Pos(0-8): "; string p; getline(cin,p);
                write(sock,formatPlay(stoi(p),my_symbol).c_str(),formatPlay(stoi(p),my_symbol).size());
                my_turn=false;
            } else cout<<"[TTT] No es tu turno o no estás en juego.\n";
            continue;
        }
        if(e=="lista"){ write(sock,"00001l",6); continue; }
        if(e=="broadcast"){
            cout<<"Msg: "; string m; getline(cin,m);
            write(sock,formatBroadcast(m).c_str(),formatBroadcast(m).size());
            continue;
        }
        if(e=="archivo"){
            cout<<"Fichero: "; string fn; getline(cin,fn);
            cout<<"Para: ";    string dst; getline(cin,dst);
            write(sock,formatFile(fn,dst).c_str(),formatFile(fn,dst).size());
            continue;
        }
        // si no es ninguno, es mensaje directo
        cout<<"Para: "; string dst; getline(cin,dst);
        write(sock,formatMessage(e,dst).c_str(),formatMessage(e,dst).size());
    }

    shutdown(sock,SHUT_RDWR);
    close(sock);
    return 0;
}

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <thread>
#include <iterator>
#include <set>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>

using namespace std;

int puerto = 45001;

int psize{ 5 }, fsize{ 5 };
int mySock;

// Lectura por header
string readN(int, int);

// Protocolos
string formatMessage(string, string);
string formatBroadcast(string);
string formatList();
string formatChau();
string formatFile(string, string);
string formatUser(string);

// Lectura de protocolos
void reader(int);

int main()
{
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) { perror("socket"); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(puerto);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    mySock = sock;

    string user;
    printf("Usuario: "); 
    fflush(stdout);
    getline(cin, user);
    string m = formatUser(user);
    write(sock, m.c_str(), m.size());
    thread(reader, sock).detach();

    while (true) {
        printf("\nCmd [mensaje,broadcast,lista,archivo,t,chau]: "); 
        fflush(stdout);
        string cmd; getline(cin, cmd);

        if (cmd == "chau") {
            string q = formatChau();
            write(sock, q.c_str(), q.size());
            break;
        }
        else if (cmd == "lista") {
            string m = formatList();
            write(sock, m.c_str(), m.size());
        }
        else if (cmd == "broadcast") {
            printf("Msg broadcast: "); 
            fflush(stdout);
            string msg; 
            getline(cin, msg);
            string m = formatBroadcast(msg);
            write(sock, m.c_str(), m.size());
        }
        else if (cmd == "archivo") {
            printf("Archivo (ruta): "); 
            fflush(stdout);
            string ruta; 
            getline(cin, ruta);
            printf("Destinatario: "); 
            fflush(stdout);
            string dst; 
            getline(cin, dst);
            string m = formatFile(ruta, dst);
            if (!m.empty()) write(sock, m.c_str(), m.size());
        }
        else {
            printf("Destinatario: "); 
            fflush(stdout);
            string dst; 
            getline(cin, dst);
            string m = formatMessage(cmd, dst);
            write(sock, m.c_str(), m.size());
        }
    }

    close(sock);
    return 0;
}

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

string formatMessage(string msg, string dst)
{
	int total = 1 + psize + msg.length() + psize + dst.length();

	stringstream ss;
	ss << setw(psize) << setfill('0') << total
		<< 'M'
		<< setw(psize) << setfill('0') << msg.length()
		<< msg
		<< setw(psize) << setfill('0') << dst.length()
		<< dst;
	return ss.str();
}

string formatBroadcast(string msg)
{
	int total = 1 + psize + msg.length();

	stringstream ss;
	ss << setw(psize) << setfill('0') << total
		<< 'B'
		<< setw(psize) << setfill('0') << msg.length()
		<< msg;

	return ss.str();
}

string formatList()
{
	return "00001L";
}

string formatChau()
{
	return "00001Q";
}

string formatFile(string fname, string dst) 
{
	ifstream in(fname, ios::binary);
	if (!in) {
		perror("open file");
		return "";
	}

	vector<char> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
	size_t sz = data.size();

	long long total = 1 + fsize + dst.size() + fsize + fname.size() + 18 + sz;

	ostringstream out;
	out << setw(fsize) << setfill('0') << total;
	out << 'F';
	out << setw(fsize) << setfill('0') << dst.size();
	out << dst;
	out << setw(fsize) << setfill('0') << fname.size();
	out << fname;
	out << setw(18) << setfill('0') << sz;
	out.write(data.data(), data.size());

	return out.str();
}

string formatUser(string name)
{
	int total = 1 + psize + name.length();

	stringstream ss;
	ss << setw(psize) << setfill('0') << total
		<< 'N'
		<< setw(psize) << setfill('0') << name.length()
		<< name;

	return ss.str();
}

void reader(int sock) 
{
    while (true) 
    {
        string hdr = readN(sock, psize);
        if (hdr.empty()) break;
        size_t total = stoi(hdr);
        string p = readN(sock, total);
        if (p.size() < total) break;

        char type = p[0]; size_t off = 1;

        if (type == 'm') 
        {
            size_t lm = stoi(p.substr(off, psize)); off += psize;
            string msg = p.substr(off, lm); off += lm;
            size_t ls = stoi(p.substr(off, psize)); off += psize;
            string sender = p.substr(off, ls);
            printf("<%s> %s\n", sender.c_str(), msg.c_str());
        }
        else if (type == 'b') 
        {
            size_t lm = stoi(p.substr(off, psize)); off += psize;
            size_t ls = stoi(p.substr(off, psize)); off += psize;
            string sender = p.substr(off, ls); off += ls;
            string msg = p.substr(off, lm);
            printf("[BROADCAST %s] %s\n", sender.c_str(), msg.c_str());
        }
        else if (type == 'l') 
        {
            string lst = p.substr(off);
            printf("[USUARIOS] %s\n", lst.c_str());
        }
        else if (type == 'f') 
        {
            size_t ld = stoull(p.substr(off, fsize)); off += fsize;
            string dst = p.substr(off, ld); off += ld;
            size_t fn = stoull(p.substr(off, fsize)); off += fsize;
            string orig = p.substr(off, fn); off += fn;
            size_t sz = stoull(p.substr(off, 18)); off += 18;
            vector<char> data(sz);
            memcpy(data.data(), p.data() + off, sz);

            string copia;
            size_t pos = orig.find_last_of('.');
            if (pos != string::npos) 
            {
                copia = orig.substr(0, pos) + "_copia" + orig.substr(pos);
            }
            else 
            {
                copia = orig + "_copia";
            }

            FILE* out = fopen(copia.c_str(), "wb");
            if (out) 
            {
                fwrite(data.data(), 1, sz, out);
                fclose(out);
                printf("\n[ARCHIVO recibido: %s (de %s)]\n", copia.c_str(), dst.c_str());
            }
            else 
            {
                perror("Error al guardar");
            }
        }
    }
}
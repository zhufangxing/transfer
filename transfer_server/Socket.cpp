/* 
 * File:   Socket.cpp
 */
#include "Socket.h"
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#define BUFSIZE 128
#define TIMEOUT 5000


Socket::Socket() {
    this->id = socket(PF_INET, SOCK_STREAM, 0);
    if (this->id < 0) {
        Eccezione("socket",errno);
    }
    settings();
}

Socket::Socket(int s) {
    if (s>=0){
        this->id = s;
        settings();
    }
}

void Socket::settings(){
    poll.events=POLLIN|POLLOUT;
    int optval=1;
    // Permette ad un socket di riutilizzare un numero di porta
    // associato ad un socket in TIME-WAIT.
    setsockopt(this->id, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
    //setsockopt(this->id, IPPROTO_TCP, TCP_CORK, &optval, sizeof optval);
}

// Timeout è un valore in millisecondi 
int Socket::checkPollIn(int timeout){
    poll.events=POLLIN;
    return checkPoll(timeout);
}
int Socket::checkPollOut(int timeout){
    poll.events=POLLOUT;
    return checkPoll(timeout);
}
int Socket::checkPoll(int timeout){
    poll.fd=this->id;
    int out=::poll(&poll, 1, timeout);
    switch(out){
        case(-1): Eccezione("poll", errno); break;
        case(0):  Eccezione("poll", -2); break;
        default: break;
    }
    return out;
}

int Socket::getId(){
    // Restituisce l'id (ovvero il file descriptor)
    // del socket
    return this->id;
}

int Socket::getPort(){
    // Restituisce la porta a cui il socket è associato
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(this->id, (struct sockaddr *)&sin, &len)==-1)
        Eccezione("getsockname", errno);
    int out = ntohs(sin.sin_port);
    return out;
}

void Socket::close(){
    if(::close(this->id)==-1)
        Eccezione("close", errno);
}

void Socket::bind(const string p) {
    // Associa il socket ad una porta
    struct addrinfo settings, *myInfo;
    memset(&settings, 0, sizeof settings);
    settings.ai_family = PF_INET;
    settings.ai_socktype = SOCK_STREAM;    // TCP
    settings.ai_flags = AI_PASSIVE;        // Il SO inserisce l'indirizzo IP dell'host
    int out;
    if (out=getaddrinfo(NULL, p.c_str(), &settings, &myInfo)<0)
        Eccezione("getaddrinfo", out);
    if (::bind(this->id, myInfo->ai_addr, myInfo->ai_addrlen) < 0)
        Eccezione("bind", errno);
    freeaddrinfo(myInfo);
}

void Socket::bind(const int p) {
    stringstream s;
    s<<p;
    this->bind(s.str());
}

void Socket::connect(string host, int p) {
    stringstream s_p;
    s_p<<htons(p);
    connect(host,s_p.str());
}

void Socket::connect(string host, string port) {
    // Connessione a host:port
    int out;
    struct addrinfo settings, *serverInfo;
    memset(&settings, 0, sizeof (settings));
    settings.ai_family = PF_INET;
    settings.ai_socktype = SOCK_STREAM;
    // Ottenimento informazioni su "host". Il SO effettua
    // una query DNS, quindi host può essere un IP o un
    // nome di dominio.
    if (out = getaddrinfo(host.c_str(), port.c_str(), &settings, &serverInfo) != 0) {
        Eccezione("getaddrinfo", out);
    }
    if (::connect(this->id, serverInfo->ai_addr, serverInfo->ai_addrlen)== -1)
        Eccezione("connect", errno);
    freeaddrinfo(serverInfo);
}

void Socket::connect(string host, string port, int timeout){
    // Se la connessione non viene stabilita entro
    // "timeout" secondi, viene lanciata un'eccezione
    fcntl(id, F_SETFL, O_NONBLOCK);
    int flags = fcntl(id, F_GETFL, 0);
    try{connect(host,port);}
    catch(string e){}
    try{checkPollOut(timeout);}
    catch(string e){
        fcntl(id, F_SETFL, flags & ~O_NONBLOCK);
        throw(e);
    }
    fcntl(id, F_SETFL, flags & ~O_NONBLOCK);
}

void Socket::send(std::string s){
    // Invio di una stringa
    checkPollOut(TIMEOUT);
    int out;
    int size=s.size();
    int tot=out=0;
    do{
        out=::send(this->id, s.substr(tot, size).c_str(), size-tot, MSG_NOSIGNAL);
        if (out<0)
            Eccezione("send", errno);
        tot += out;
    } while (tot < size);
}

void Socket::listen(int max){
    if( ::listen(this->id, max) < 0)
        Eccezione("listen", errno);
}

Socket Socket::Accept(int timeout){
    poll.events=POLLIN|POLLOUT;
    checkPoll(timeout);
    return Accept();
}

Socket Socket::Accept(){
    int out= ::accept(this->id, NULL,0);
    if  (out < 0)
        Eccezione("accept", errno);
    Socket s(out);
    return s;
}

/* receive() esegue una singola recv() sul socket.
 * recv è bloccante, ma viene eseguita solo se checkPollIn
 * non genera un'eccezione, cioè se ci sono dati in arrivo.
 */
string Socket::receive(){
    unsigned char * buf = new unsigned char[BUFSIZE];
    int len=0;
    string out;
    checkPollIn(TIMEOUT);
    len=recv(this->id, buf, sizeof(buf), 0);
    if(len<0)
        Eccezione("recv", errno);
    out.append((char*) buf, len);
    delete [] buf;
    return out;
}

/* Ricezione di una stringa di lunghezza sconosciuta.
 */
string Socket::receiveString(){
    string out="";
    string b="";
    do{
        b=receive();
        out+=b;
    } while (out.find("\r\n\r\n")==string::npos && b.size()>0);
    // Se recv()=0 ma non è stata ricevuta la sequenza \r\n\r\n,
    // genero un'eccezione.
    if(out.find("\r\n\r\n")==string::npos)
        Eccezione("receive", -2);
    // In caso contrario, rimuovo \r\n\r\n.
    else
        out.resize(out.size()-4);
    return out;
}

/* Ricezione di una sequenza binaria di dati.
 */
string Socket::receiveBinary(unsigned long size){
    unsigned long tot=0;
    string out;
    string b=out="";
    do{
        b=receive();
        out+=b;
        tot+=b.size();
    } while (tot<size && b.size()>0);
    return out;
}

/* Classe per generare eccezioni
 * (una stringa con l'errore riportato dal SO)
 */
void Socket::Eccezione(string f, int err){
    stringstream out;
    out << f << ": ";
    if (err == -3)
        out << "Connessione chiusa inaspettatamente";
    if (err == -2)
        out << "Timeout";
    else{
        if (f == "getaddrinfo")
            out << gai_strerror(err) << " ";
        else 
            out << strerror(err);
    }
    throw out.str();
}

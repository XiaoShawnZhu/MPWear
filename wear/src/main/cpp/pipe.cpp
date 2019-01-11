
#include "pipe.h"
#include "proxy.h"
#include "proxy_setting.h"
#include "tools.h"
#include "connections.h"
#include "kernel_info.h"

extern struct PIPE pipes;

// set up pipe listener in c++
int PIPE::Setup(){

    this->n = 1;

    localListenFD = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(sockaddr_in));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PIPE_LISTEN_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int optval = 1;
    int r = setsockopt(localListenFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    MyAssert(r == 0, 1762);
    if (bind(localListenFD, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) return R_FAIL;
    if (listen(localListenFD, 32) != 0) return R_FAIL;

    Accept();

    return R_SUCC;

}

void * AcceptThread(void * arg) {

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    pipes.fd[0] = accept(pipes.localListenFD, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (pipes.fd[0] == -1) return R_FAIL;
    pipes.SetCongestionControl(pipes.fd[0], PROXY_SETTINGS::pipeProtocol[0].c_str());
    SetSocketNoDelay_TCP(pipes.fd[0]);
    SetNonBlockIO(pipes.fd[0]);
    int localPort = (int)ntohs(clientAddr.sin_port);
    LOGD("Pipe connection established. IP=%s, port=%d, TCP=%s, fd=%d",
                inet_ntoa(clientAddr.sin_addr), localPort,
                PROXY_SETTINGS::pipeProtocol[0].c_str(), pipes.fd[0]
    );

    return NULL;
}

void PIPE::Accept(){
    // bStarted = 0;
    pthread_t accept_thread;
    int r = pthread_create(&accept_thread, NULL, AcceptThread, this);

    MyAssert(r == 0, 2459);
    // while (bStarted != 2) {pthread_yield();}
    LOGD("Accept threads started.");
}


void PIPE::SetCongestionControl(int fd, const char * tcpVar) {
    if (!strcmp(tcpVar, "default") || !strcmp(tcpVar, "DEFAULT")) return;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, tcpVar, (int)strlen(tcpVar));
    MyAssert(r == 0, 1815);
}
#include <thread>
#include <chrono>         // std::chrono::seconds
#include "pipe.h"
#include "proxy.h"
#include "proxy_setting.h"
#include "tools.h"
#include "connections.h"
#include "kernel_info.h"

extern struct PIPE pipes;

int bufTime = 0;
int netRTT = 0;

const char * rIP;

// set up pipe listener in c++
int PIPE::Setup(const char * localIP, const char * remoteIP){

    rIP = remoteIP;
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


    LOGD("Setting up pipe listener.");

    localListenFDSide = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serverAddrSide;
    memset(&serverAddrSide, 0, sizeof(sockaddr_in));
    serverAddrSide.sin_family = AF_INET;
    serverAddrSide.sin_port = htons(PIPE_LISTEN_PORT_SIDE);
    serverAddrSide.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rr = setsockopt(localListenFDSide, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    MyAssert(rr == 0, 1762);
    if (bind(localListenFDSide, (struct sockaddr *)&serverAddrSide, sizeof(serverAddrSide)) != 0) return R_FAIL;
    if (listen(localListenFDSide, 32) != 0) return R_FAIL;

//    // Set up UDP socket for feedback over secondary subflow
//    feedbackFD = socket(AF_INET, SOCK_DGRAM, 0);
//    if (feedbackFD == -1) {
//        LOGD("Could not create feedback socket");
//    }
//    SetNonBlockIO(feedbackFD);
//
//    int udpoptval = 1;
//    int rrr = setsockopt(feedbackFD, SOL_SOCKET, SO_REUSEADDR, &udpoptval, sizeof(udpoptval));
//    MyAssert(rrr == 0, 1762);
//    // Using WiFi as the subflow
//    struct sockaddr_in udpClientAddr;
//    memset(&udpClientAddr, 0, sizeof(struct sockaddr_in));
//    udpClientAddr.sin_family = AF_INET;
//    inet_pton(AF_INET, localIP, &udpClientAddr.sin_addr);
//    if (bind(feedbackFD, (struct sockaddr *)&udpClientAddr, sizeof(udpClientAddr)) != 0) {
//        LOGD("Feedback cannot bind to %s", localIP);
//    }
//    else{
//        LOGD("feedbackFD binded to %s", localIP);
//    }

//    if (bind(udpLocalListenFD, (struct sockaddr *)&udpServerAddr, sizeof(udpServerAddr))) return R_FAIL;
    Accept();
    return R_SUCC;
}

void PIPE::Feedback() {

    struct sockaddr_in udpServerAddr;
    memset(&udpServerAddr, 0, sizeof(sockaddr_in));
    udpServerAddr.sin_family = AF_INET;
    udpServerAddr.sin_port = htons(4399);
    inet_pton(AF_INET, rIP, &udpServerAddr.sin_addr);
    BYTE buf[9];
    buf[8] = 0;
    *((int *) buf)= netRTT;
    *((int *)(buf + 4)) = bufTime;
    if(sendto(feedbackFD, buf, 8, 0, (sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
        LOGD("Send feedback failed");
    }
    else{
//        LOGD("Feeding back");
    }
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
//
//    while(1){
//        std::this_thread::sleep_for (std::chrono::milliseconds(100));
////        LOGD("In Thread.");
//        pipes.Feedback();
//    }

    return NULL;
}

void * AcceptThreadSide(void * arg) {

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    pipes.fd[1] = accept(pipes.localListenFDSide, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (pipes.fd[1] == -1) return R_FAIL;
    pipes.SetCongestionControl(pipes.fd[1], PROXY_SETTINGS::pipeProtocol[1].c_str());
    SetSocketNoDelay_TCP(pipes.fd[1]);
    //SetNonBlockIO(pipes.fd[1]);
    int localPort = (int)ntohs(clientAddr.sin_port);
    LOGD("Pipe connection established. IP=%s, port=%d, TCP=%s, fd=%d",
         inet_ntoa(clientAddr.sin_addr), localPort,
         PROXY_SETTINGS::pipeProtocol[1].c_str(), pipes.fd[1]
    );

    bool running = true;
    BYTE buf[13];
    buf[12] = 0;
    while (running) {
        if (buf[12] == 12){
            int throughput = *((int *) buf);
            int RTT = *((int *)(buf + 4));
            int timedif = *((int *)(buf + 8));
            buf[12] = 0;
//            LOGI("throughput is %d, RTT is %d, timediff is %d",throughput,RTT,timedif);
            bufTime = timedif;
            netRTT = RTT;

        } else{
            int nLeft = 12 - buf[12];
            int r = read(pipes.fd[1], buf + buf[12], nLeft);
            buf[12] += r;
        }
    }

    return NULL;
}


void PIPE::Accept(){
    // bStarted = 0;
    pthread_t accept_thread;
    int r = pthread_create(&accept_thread, NULL, AcceptThread, this);
    MyAssert(r == 0, 2459);

    pthread_t accept_thread_side;
    int rr = pthread_create(&accept_thread_side, NULL, AcceptThreadSide, this);
    MyAssert(rr == 0, 2459);

    LOGD("Accept threads started.");

}


void PIPE::SetCongestionControl(int fd, const char * tcpVar) {
    if (!strcmp(tcpVar, "default") || !strcmp(tcpVar, "DEFAULT")) return;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, tcpVar, (int)strlen(tcpVar));
    MyAssert(r == 0, 1815);
}


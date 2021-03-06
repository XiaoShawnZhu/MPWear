#include "proxy.h"
#include "connections.h"
#include "subflow.h"
#include "pipe.h"
#include "tools.h"
#include "hints.h"
#include <jni.h>


struct CONNECTIONS conns;
struct SUBFLOW subflows;
struct PIPE pipes;
struct BUFFER_SUBFLOW subflowOutput;  //the buffer for writing to subflows
struct BUFFER_TCP tcpOutput;  //the buffer for writing to tcp connections
struct KERNEL_INFO kernelInfo;
struct HINTS hintRules;
FILE * ofsIODump = NULL;
int keepRunning = 1;
int tickCount;
DWORD rpIPAddress = 0;
int lastPipeActivityTick;
unsigned long highResTimestampBase;

extern "C" {
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP, jstring rpIP);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_WearProxy_proxyFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz);
};

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP, jstring rpIP) {

    const char *subIp = env->GetStringUTFChars(subflowIP, 0);
    const char *rIp = env->GetStringUTFChars(rpIP, 0);
    LOGD("[subflow] subflowIP = %s, rpIP = %s", subIp, rIp);
    if(subflows.Setup(subIp, rIp))
        return env->NewStringUTF("SubflowSetupSucc");

    return env->NewStringUTF("Subflow failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz) {
    // Just for simplicity, we do this right away; correct way would do it in
    // another thread...s

    if(pipes.Setup())
        return env->NewStringUTF("Pipe listener setup successfully.");

    return env->NewStringUTF("Pipe failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz) {
    // Just for simplicity, we do this right away; correct way would do it in
    // another thread...s

    if(conns.Setup())
        return env->NewStringUTF("Conn setup successfully.");

    return env->NewStringUTF("Conn failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_WearProxy_proxyFromJNI(JNIEnv *env, jobject thiz){

    LOGD("About to start proxy.");
//    sleep(5);
    PROXY_SETTINGS::ApplyRawSettings();
    PROXY_SETTINGS::AdjustSettings();

    subflowOutput.Setup();
    tcpOutput.Setup();

    ProxyMain();

    return env->NewStringUTF("Proxy running.");

}

int ProxyMain(){

    while (keepRunning) {
        // local proxy polling
        //LOGD("stuck here?");
        int nReady = poll(conns.peers, conns.maxIdx + 1, -1);
        LOGD("nReady=%d", nReady);

        if (nReady == 0) {
             LOGD("Polled nothing.");
            continue;
        }

        for (int j=0;j<conns.maxIdx+1;j++){
            LOGD("revent[%d] is %d",j,conns.peers[j].revents);
        }

        //LOGD("test: %d",conns.maxIdx);
        if (conns.peers[0].revents & POLLRDNORM) { //new connection
            LOGD("New incoming TCP connection");
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            int clientFD = accept(conns.localListenFD, (struct sockaddr *) &clientAddr,
                                  &clientAddrLen);
            LOGD("connection accepted.");
            //good here
            //need to check if an error happens for accept (clientFD == -1),
            //since the listen socket uses non-block IO
            LOGD("client=%d", clientFD);
            if (clientFD != -1) {
                SetNonBlockIO(clientFD);
                SetSocketNoDelay_TCP(clientFD);
                SetSocketBuffer(clientFD, PROXY_SETTINGS::pipeReadBufLocalProxy,
                                PROXY_SETTINGS::pipeWriteBufLocalProxy);

                int newPollPos = conns.AddTCPConnection(clientFD,
                                                        clientAddr.sin_addr.s_addr,
                                                        ntohs(clientAddr.sin_port),
                                                        0, 0, 0
                );
                //try to send a SYN message to the subflow immediately, since bSentSYNToPipe is 0
                LOGD("About to send SYN");
                conns.TransferFromTCPToSubflows(newPollPos, clientFD); // sending SYN
            }
            if (--nReady <= 0) continue;
        }


        /////////// for scheduling////////////////
        unsigned long cur_r_clock = 0;
        if (PROXY_SETTINGS::bScheduling) cur_r_clock = GetHighResTimestamp();
        int bursty_class = 0;
        unsigned long long min_l_clock = (unsigned long long) 1 << 60;
        int best_pollPos = -1;
        //////////////////////////////////////////

        for (int i=1; i<=conns.maxIdx; i++) {

            int peerFD = conns.peers[i].fd;
            if (peerFD < 0) continue;

            int bPipe = i<=subflows.n;
            int bMarked = 0;


            if ((conns.peers[i].revents & (POLLRDNORM | POLLERR | POLLHUP)) ||
                (/*!bPipe &&*/ !conns.peersExt[i].bSentSYNToPipe) ||
                (/*!bPipe &&*/  conns.peersExt[i].establishStatus == POLLFD_EXT::EST_FAIL) ||
                (/*!bPipe &&*/  conns.peersExt[i].bToSendSYNACK)
                    ) {
                //conditions of !bSentSYNToPipe, establish failure, and to-send-SYNACK are triggered by
                //last time the when the corresponding pipe msg was not sent due to buffer full

                bMarked = 1;
                MyAssert(conns.peersExt[i].establishStatus != POLLFD_EXT::EST_NOT_CONNECTED, 1704);

                if (i == 2) {
                    LOGD("BT Pipe Message.");
                    if (conns.TransferFromSubflowsToTCP(i, peerFD) > 0) { // here subflow is pipe.
                        conns.TransferDelayedFINsToSubflows();
                    }
                    continue;
                    //reserve for real pipe//
                }

                if (bPipe) {

                    if(i<=1) { //tcp subflow msg
                        LOGD("TCP Subflow Message.");
                        if (conns.TransferFromSubflowsToTCP(i, peerFD) > 0) {
                            conns.TransferDelayedFINsToSubflows();
                        }
                    } else { //udp pipe msg
                        //udpOutput.TransferFromPipesToUDP(i);
                    }
                    //conns.CheckOWDMeasurement();
                } else {
                    //Apply scheduling algorithm here (for RP)
                    if (PROXY_SETTINGS::bScheduling) {
                        WORD connID = conns.peersExt[i].connID;
                        MyAssert(connID>0, 2037);
                        struct CONN_INFO & c = conns.connTab[connID];
                        if (cur_r_clock - c.r_clock > (unsigned long)PROXY_SETTINGS::burstDurationTh) c.accuBurstyBytes = 0;
                        int bBursty = c.accuBurstyBytes < PROXY_SETTINGS::burstSizeTh;
                        if (bBursty) {
                            if (!bursty_class) {
                                bursty_class = 1;
                                min_l_clock = c.l_clock;
                                best_pollPos = i;
                            } else {
                                if (c.l_clock < min_l_clock) {
                                    min_l_clock = c.l_clock;
                                    best_pollPos = i;
                                }
                            }
                        } else {
                            if (!bursty_class) {
                                if (c.l_clock < min_l_clock) {
                                    min_l_clock = c.l_clock;
                                    best_pollPos = i;
                                }
                            }
                        }
                    } else {
                        //when no scheduling (i.e., round robin)
                        // InfoMessage("No scheduling, use %d", i);
                        LOGD("Local Connection Peer!");
                        conns.TransferFromTCPToSubflows(i, peerFD);
                    }
                }
            }

            if (conns.peers[i].revents & POLLWRNORM) {
                bMarked = 1;
                MyAssert(conns.peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC, 1705);
                if (bPipe) {
                    LOGD("Check");
                     /////////bPipe means it is from Pipe which is the downlink. WAITING FOR CONSTRUCTION/////////////
                    if (subflowOutput.TransferFromTCPToSubflows(i, peerFD) > 0) {
                        conns.TransferDelayedFINsToSubflows();
                    }
                     ////////////////////////////////////////////////////////////////////////////////////////////////
                } else {
                    tcpOutput.TransferFromSubflowsToTCP(i);
                }

            }


           // SLOT_FINISHED:	///////////////
            if (bMarked)
                if (--nReady <= 0) break;
        }
        //MyAssert(conns.maxIdx >= pipes.n, 1620);
    }
    return 0;
}

#include "proxy.h"
#include "subflow.h"
#include <jni.h>
#include "connections.h"
#include "proxy_setting.h"
#include "hints.h"
#include "tools.h"
#include "pipe.h"

struct CONNECTIONS conns;
struct SUBFLOW subflows;
struct PIPE pipes;
struct BUFFER_SUBFLOW subflowOutput;  //the buffer for writing to subflows
struct BUFFER_TCP tcpOutput;  //the buffer for writing to tcp connections
struct BUFFER_PIPE pipeOutput;
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
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_PhoneProxy_proxyFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP, jstring rpIP);
};

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP, jstring rpIP) {

    const char *subIp = env->GetStringUTFChars(subflowIP, 0);
    const char *rIp = env->GetStringUTFChars(rpIP, 0);
//    LOGD("[subflow] subflowIP = %s, rpIP = %s", subIp, rIp);
    if(subflows.Setup(subIp, rIp))
        return env->NewStringUTF("SubflowSetupSucc");
    return env->NewStringUTF("Subflow setup failed.");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP, jstring rpIP) {

    const char *subIp = env->GetStringUTFChars(subflowIP, 0);
    const char *rIp = env->GetStringUTFChars(rpIP, 0);
    LOGD("[subflow] subflowIP = %s, rpIP = %s", subIp, rIp);
    if(pipes.Setup(subIp, rIp))
        return env->NewStringUTF("Pipe listener setup successfully.");

    return env->NewStringUTF("Pipe failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_helper_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz) {

    if(conns.Setup())// should be pipe conns , need to be modified
        return env->NewStringUTF("Conn setup successfully.");
    return env->NewStringUTF("Conn failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpwear_PhoneProxy_proxyFromJNI(JNIEnv *env, jobject thiz){

    LOGD("About to start proxy.");
//    sleep(5);
    PROXY_SETTINGS::ApplyRawSettings();
    PROXY_SETTINGS::AdjustSettings();

    subflowOutput.Setup();
    tcpOutput.Setup();
    pipeOutput.Setup();

    ProxyMain();

    return env->NewStringUTF("Proxy running.");
}

int ProxyMain(){

    while (keepRunning) {
        // local proxy polling
        //LOGD("polling");
        //LOGD("test_before: %d",conns.maxIdx);
//        LOGD("stuck here?");
        int nReady = poll(conns.peers, conns.maxIdx + 1, -1);
//        LOGD("nReady=%d", nReady);

        if (nReady == 0) {
            LOGD("Polled nothing.");
            continue;
        }

//        for (int j=0;j<conns.maxIdx+1;j++){
//            LOGD("revent[%d] is %d",j,conns.peers[j].revents);
//        }

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

        //kernelInfo.UpdatePipeInfo(1);//pipe is subflow here
        //kernelInfo.UpdatePipeInfo(2);//pipe is subflow here
        /////////// for scheduling////////////////
        unsigned long cur_r_clock = 0;
        if (PROXY_SETTINGS::bScheduling) cur_r_clock = GetHighResTimestamp();
        int bursty_class = 0;
        unsigned long long min_l_clock = (unsigned long long) 1 << 60;
        int best_pollPos = -1;
        //////////////////////////////////////////

        for (int i=1; i<=conns.maxIdx; i++) {
            if (i == 2) {
                continue;
                //reserve for real pipe///
            }
            //filter out UDP
            //if(i==5) continue;
            // Xiao addded for enabling tinyCam BT later
            //if((i==1)&&(btDown)) continue;

            // // Before on-demand WiFi setups
            //if((i==2)&&(WiFiPipe==0)&&(onDemand==1)) continue;

            int peerFD = conns.peers[i].fd;
            if (peerFD < 0) continue;

            int bPipe = i<=subflows.n;
            int bMarked = 0;

            /*   ////////// since it is for remote proxy //////////////////
            if ((conns.peersExt[i].establishStatus == POLLFD_EXT::EST_NOT_CONNECTED) &&
                (conns.peers[i].revents & (POLLRDNORM | POLLWRNORM))
                    ) {
                MyAssert((proxyMode == PROXY_MODE_REMOTE) && (!bPipe), 1700);

                bMarked = 1;
                //a new connection established (remote proxy mode only)
                int err;
                socklen_t len = sizeof(err);
                if (getsockopt(peerFD, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
                    conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_ERROR);
                } else if (err) {
                    conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_ERROR);
                } else {
                    conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_SUCC);
                }
                goto SLOT_FINISHED;
            }
            */  ///////////////////////////////////////////////////////////////



            if ((conns.peers[i].revents & (POLLRDNORM | POLLERR | POLLHUP)) ||
                (/*!bPipe &&*/ !conns.peersExt[i].bSentSYNToPipe) ||
                (/*!bPipe &&*/  conns.peersExt[i].establishStatus == POLLFD_EXT::EST_FAIL) ||
                (/*!bPipe &&*/  conns.peersExt[i].bToSendSYNACK)
                    ) {
                //conditions of !bSentSYNToPipe, establish failure, and to-send-SYNACK are triggered by
                //last time the when the corresponding pipe msg was not sent due to buffer full

                bMarked = 1;
                MyAssert(conns.peersExt[i].establishStatus != POLLFD_EXT::EST_NOT_CONNECTED, 1704);
                if (bPipe) {

                    if(i<=1) { //tcp subflow msg
//                        LOGD("TCP Subflow Message.");
                        if (conns.TransferFromSubflowsToPipe(i, peerFD) > 0) {
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

//#define _GNU_SOURCE
#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "proxy.h"
#include "tools.h"
#include "subflows.h"
#include "connections.h"
#include "meta_buffer.h"
#include "kernel_info.h"
#include "scheduler.h"

struct SUBFLOWS subflows;
struct CONNECTIONS conns;
struct BUFFER_SUBFLOWS subflowOutput;	//the buffer for writing to subflows
struct BUFFER_TCP tcpOutput;	//the buffer for writing to tcp connections
struct META_BUFFER metaBuffer; // meta buffer for multipath sender side above multiplexed connections
struct DELAYED_FINS delayedFins;	//delayed fin
struct KERNEL_INFO kernelInfo;
struct SCHEDULER scheduler;

#define DEBUG_META_BUF
#define DEBUG_UNA

int kfd;

int proxyMode;

int tickCount;
int lastSubflowActivityTick;

static DWORD metaSeq; // Subflow message SEQ across all subflows

FILE * ofsSubflowDump = NULL;
FILE * ofsIODump = NULL;
FILE * ofsAccDump = NULL;
FILE * ofsLatency = NULL;
FILE * ofsDebug = NULL;
FILE * ofsOWD = NULL;

char * debugFilename = "debug.txt";
const char * prefix = "/home/shawnzhu/MPWear/log/";
char filename1[64];
char filename2[64];
char filename3[64];
unsigned long highResTimestampBase;

DWORD rpIPAddress = 0;

unsigned int firstSeq1, firstSeq2;
int bMetaSendBufFull = 0;

int rnjData;
unsigned long tBlock = 0;
unsigned long tLastFull = 0;

extern DWORD subflowLastAck1, subflowLastAck2;

int keepRunning = 1;
int tmpCount = 0;

void intHandler(int dummy) {
	keepRunning = 0;
}

void ProxyMain() {
	int pollTimeout = PROXY_SETTINGS::pollTimeout;
	bMetaSendBufFull = 0;
	rnjData = 0;
	tBlock = 0;
	metaSeq = 0;
	strcpy(filename1, prefix);
	strcat(filename1, "latency.txt");
	ofsLatency = fopen(filename1, "w");
	if (ofsLatency == NULL) {
            printf("Error opening file!\n");
            exit(1);
	}
	strcpy(filename2, prefix);
	strcat(filename2, debugFilename);
    strcat(filename2, "_debug.txt");
	ofsDebug = fopen(filename2, "w");
    if (ofsDebug == NULL) {
        printf("Error opening debug file!\n");
        exit(1);
    }

    strcpy(filename3, prefix);
	strcat(filename3, debugFilename);
    strcat(filename3, "_owd.txt");
	ofsOWD = fopen(filename3, "w");
    if (ofsOWD == NULL) {
        printf("Error opening owd file!\n");
        exit(1);
    }
	
	signal(SIGINT, intHandler);

	int space[5];
	int decision, bytes, allBytes, schedulerNo;
    int nTCP = PROXY_SETTINGS::nTCPSubflows;
    uint64_t startTx;
    //uint64_t startTime;

	while (keepRunning) {
		VerboseMessage("Polling");
		tmpCount += 1;
		// Reinjection check
                // Active reinjection: check if this subflow has packets to reinject to another subflow
                // Only add reinjected packets when the MsgBuf is empty
                //InfoMessage("Subflow %d reinjection check.", i);
		//if (PROXY_SETTINGS::reinjectionMechanism) {
		//InfoMessage("start");

#ifdef REINJECTION
			int h1 = 0, h2 = 0;
			if (subflowOutput.pMsgHead[1] != NULL) {
				h1 = 1;
			}
			if (subflowOutput.pMsgHead[2] != NULL) {
                                h2 = 1;
                        }
			for (int i = 1; i <= 2; i++) {
				//InfoMessage("%d", i);
				// Maintain the Unacked queue and decide which packet to reinject
        	        	conns.Reinjection(i);
				//InfoMessage("reinject %d", i);
				if (subflowOutput.bActiveReinject[i] > 0) {
					MyAssert(conns.peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC, 1705);
        	        if (subflowOutput.TransferFromTCPToSubflows(i, conns.peers[i].fd, subflowOutput.bActiveReinject[i]) > 0) {
                	    conns.TransferDelayedFINsToSubflows();
                    }
				}
				//InfoMessage("%d end.", i);
			}
/*
			if (subflowOutput.pMsgUnaHead[1] != NULL && subflowOutput.pMsgUnaHead[2] != NULL
			&& (subflowOutput.pMsgUnaHead[1]->seq != firstSeq1 ||
				subflowOutput.pMsgUnaHead[2]->seq != firstSeq2)) {
				firstSeq1 = subflowOutput.pMsgUnaHead[1]->seq;
				firstSeq2 = subflowOutput.pMsgUnaHead[2]->seq;
				InfoMessage("Current Una head: 1: (%u, %d); 2: (%u, %d)",
					firstSeq1, subflowOutput.pMsgUnaHead[1]->bReinjected, firstSeq2, subflowOutput.pMsgUnaHead[2]->bReinjected);
			}
*/
		//InfoMessage("End");;
			if (subflowOutput.bActiveReinject[1] != 0 || subflowOutput.bActiveReinject[2] != 0) {
				MyAssert(((subflowOutput.bActiveReinject[1] != 0) == (subflowOutput.pMsgHead[1] != NULL)) && ((subflowOutput.bActiveReinject[2] != 0) == (subflowOutput.pMsgHead[2] != NULL)), 9006);
				//InfoMessage("More reinjected");
				continue;
			}
		//}
		
		SUBFLOW_MSG * una1 = subflowOutput.pMsgUnaHead[1];
		SUBFLOW_MSG * una2 = subflowOutput.pMsgUnaHead[2];

		// Cap on meta send buffer
		if (una1 != NULL && una2 != NULL) {
			MyAssert(una1->subflowSeq > una2->subflowSeq? (una2->bSubflowAcked == 0): (una1->bSubflowAcked == 0), 9001);
		} else if (una1 != NULL && una2 == NULL) {
			// una2 empty
			MyAssert(una1->bSubflowAcked == 0, 9002);
		} else if (una1 == NULL && una2 != NULL) {
			// una1 empty
			MyAssert(una2->bSubflowAcked == 0, 9003);
		}
		int unaAll = subflowOutput.unaBytes[1] + subflowOutput.unaBytes[2];
		//unsigned long t = GetHighResTimestamp();
		if (unaAll > PROXY_SETTINGS::unackAllBufCapacity) {
#ifdef DEBUG_META_BUF
			if (!bMetaSendBufFull) {
				//InfoMessage("Meta send buffer full: %dB, %lu", unaAll, t);
				bMetaSendBufFull = 1;
				tLastFull = GetHighResTimestamp();
#ifdef DEBUG_UNA
				//InfoMessage("Current ACK: subflow 1 [%u], subflow 2 [%u]", subflowLastAck1, subflowLastAck2);
                                //conns.PrintUnaQueueSimple(1);
	                        //conns.PrintUnaQueueSimple(2);
#endif
			}
#endif
			//InfoMessage("full");
			continue;
		} 
#ifdef DEBUG_META_BUF
		else {
			if (bMetaSendBufFull) {
				tBlock += (GetHighResTimestamp() - tLastFull);
				//InfoMessage("Meta send buffer ok: %dB, %lu, %lu", unaAll, t, tBlock);
                                bMetaSendBufFull = 0;
			}
		}
#endif

#endif // #ifdef REINJECTION
        
        //startTime = get_current_microsecond();
		int nReady = poll(conns.peers, conns.maxIdx + 1, pollTimeout);
		MyAssert(nReady >= 0, 1699);
		//startTime = get_current_microsecond() - startTime;
        //InfoMessage("--- Poll %lluus", startTime);

		// Check clock tick
		//if (tmpCount % 100000 == 0)
		//	InfoMessage("Clock = %d, %d", tickCount, tmpCount);
/*
		//check remote proxy -> server connection timeout (for remote proxy only)
		if (proxyMode == PROXY_MODE_REMOTE) {
			for (int i=subflows.n+1; i<=conns.maxIdx; i++) {
				if (conns.peers[i].fd < 0) continue;
				if (conns.peersExt[i].establishStatus == POLLFD_EXT::EST_NOT_CONNECTED && 
					!(conns.peers[i].revents & (POLLRDNORM | POLLWRNORM)) &&
					(tickCount - (int)conns.peersExt[i].connectTime > PROXY_SETTINGS::connectTimeout)
				) {
					conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_TIMEOUT);
				}
			}
		}

		//delayed FIN reaches deadline
		if (delayedFins.n > 0 && tickCount >= delayedFins.deadline) {
			conns.TransferDelayedFINsToSubflows();
		}
*/
		//GetCongestionWinSize(conns.peers[1].fd);
		//GetCongestionWinSize(conns.peers[2].fd);

		//int delay1, delay2;
		//ioctl(kfd, CMAT_IOCTL_GET_FD1_RTT, &delay1);
		//ioctl(kfd, CMAT_IOCTL_GET_FD2_RTT, &delay2);
		//InfoMessage("Subflow %d: %d", 1, GetCongestionWinSize(conns.peers[1].fd));
		//InfoMessage("Subflow %d: %d", 2, GetCongestionWinSize(conns.peers[2].fd));
		//InfoMessage("Check nReady: %d", nReady);

		for (int i = 0; i < 2; i++) {
			if (conns.peers[i].revents & POLLRDHUP) {
				InfoMessage("Subflow closed.");
				MyAssert(0, 1698);
			}
		}

		if (nReady > 0) {
			//InfoMessage("****** Read data from subflows or connections.");
			//any new incoming connection (peers[0], for local proxy only)?
			if (proxyMode == PROXY_MODE_LOCAL) {
				if (conns.peers[0].revents & POLLRDNORM) { //new connection
					//InfoMessage("New incoming connection");

					struct sockaddr_in clientAddr;
					socklen_t clientAddrLen = sizeof(clientAddr);	
					
					int clientFD = accept(conns.localListenFD, (struct sockaddr *)&clientAddr, &clientAddrLen);
					//need to check if an error happens for accept (clientFD == -1), 
					//since the listen socket uses non-block IO
					if (clientFD != -1) {
						SetNonBlockIO(clientFD);
						SetSocketNoDelay_TCP(clientFD);

						int newPollPos = conns.AddTCPConnection(clientFD, 
							clientAddr.sin_addr.s_addr, ntohs(clientAddr.sin_port),
							0, 0, 0
						);

						//try to send a SYN message to the subflow immediately, since bSentSYNToSubflow is 0
						conns.TransferFromTCPToMetaBuffer(newPollPos, clientFD, 0);
					}

					if (--nReady <= 0) continue;
				}
			}
			
			MyAssert(conns.maxIdx >= subflows.n, 1620);

			//any new data from SUBFLOWS (peers[1..nSubflows]) and/or TCP PEERS (peers[nSubflows+1..maxIdx])?		
			for (int i=1; i<=conns.maxIdx; i++) {
				int peerFD = conns.peers[i].fd;
				if (peerFD < 0) continue;

				int bSubflow = i<=subflows.n;
				int bMarked = 0;
				
				// New connections
				if ((conns.peersExt[i].establishStatus == POLLFD_EXT::EST_NOT_CONNECTED) && 
					(conns.peers[i].revents & (POLLRDNORM | POLLWRNORM))
				) {
					MyAssert((proxyMode == PROXY_MODE_REMOTE) && (!bSubflow), 1700);

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

				// There is data to read from the subflow or the connection
				if ((conns.peers[i].revents & (POLLRDNORM | POLLERR | POLLHUP)) || 
					(/*!bSubflow &&*/ !conns.peersExt[i].bSentSYNToSubflow) ||
					(/*!bSubflow &&*/  conns.peersExt[i].establishStatus == POLLFD_EXT::EST_FAIL) ||
					(/*!bSubflow &&*/  conns.peersExt[i].bToSendSYNACK)
				) {				
					//conditions of !bSentSYNToSubflow, establish failure, and to-send-SYNACK are triggered by 
					//last time the when the corresponding subflow msg was not sent due to buffer full
									
					bMarked = 1;
					MyAssert(conns.peersExt[i].establishStatus != POLLFD_EXT::EST_NOT_CONNECTED, 1704);
					// There is data from Subflow
					if (bSubflow) {
						if (conns.TransferFromSubflowsToTCP(i, peerFD) > 0) {
							conns.TransferDelayedFINsToSubflows();
						}
					}
					// There is data from TCP connection
					else {
                        //for (int msgNo = 0; msgNo < 5; msgNo++) {
						//InfoMessage("Read message");

						if (metaBuffer.GetSize(MAX_PRIORITY) > 100000) {
                            conns.TransferFromTCPToMetaBuffer(i, peerFD, 0);
                        } else {
                            startTx = get_current_microsecond();
                            while (conns.TransferFromTCPToMetaBuffer(i, peerFD, startTx) == 0) {
                               continue;
                            }
                        }


                        //InfoMessage("Read message done.");
                        //}
                        //conns.TransferFromTCPToMetaBuffer(i, peerFD);
					}
				}
	/* move to the beginning
				if (i == 1 || i == 2) {
					// Active reinjection: check if this subflow has packets to reinject to another subflow
					// Only add reinjected packets when the MsgBuf is empty
					InfoMessage("Subflow %d reinjection check.", i);
					conns.Reinjection(i);
				}
	*/
				// Writing is now possible
				if (conns.peers[i].revents & POLLWRNORM) {
					//if (subflowOutput.bActiveReinject[i])
					//	InfoMessage("Subflow %d writing possible, reinject: %d (%d messages)", i, subflowOutput.bActiveReinject[i], subflowOutput.msgBufSize[i]);
					bMarked = 1;
					MyAssert(conns.peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC, 1705); 

					if (bSubflow) {
						// disable old way of transmission
						//if (subflowOutput.TransferFromTCPToSubflows(i, peerFD, subflowOutput.bActiveReinject[i]) > 0) {
						//	conns.TransferDelayedFINsToSubflows();
						//}
					} else {
						tcpOutput.TransferFromSubflowsToTCP(i);				
					}
				}
				SLOT_FINISHED:	///////////////
				if (bMarked)
					if (--nReady <= 0) break;
			}
		}
		
		//InfoMessage("Update ACK");
		subflowOutput.UpdateACK();
        metaBuffer.MarkACKedMsg();
		metaBuffer.UpdateAfterTransmit();
        //metaBuffer.CheckACK();

		//metaBuffer.UpdateAfterTransmit(0);
		//metaBuffer.UpdateAfterTransmit(1);
		// Check meta buffer and subflow congestion window space
		// Do scheduling if there is available space
		if (metaBuffer.HasPackets()) {
			//InfoMessage("Meta buffer has packets: %d", metaBuffer.GetSize(MAX_PRIORITY));
			kernelInfo.UpdateSubflowInfo(0);
			kernelInfo.UpdateTCPAvailableSpace();
			for (int i = 1; i <= nTCP; i++) {
				space[i] = kernelInfo.GetTCPAvailableSpace(i);
                //InfoMessage("space: %d %d %d %d", space[1], space[2], space[3], space[4]);
			}
			//InfoMessage("Space: [1] %d, [2] %d", space[1], space[2]);
			//while (space[1] > 0 || space[2] > 0) {
			if (kernelInfo.HaveSpace() > 0) {
                //startTime = get_current_microsecond();

                // check previously partial transmitted message
                schedulerNo = scheduler.getNextSchedNo();
                SUBFLOW_MSG * msg = NULL;
                

				// then check control messages
                //
                if (metaBuffer.hasPartial() == 0) {
			        msg = scheduler.SelectSubflow_ControlMsg();
                }

				// then data messages

				if (msg == NULL) {
					//schedulerNo = scheduler.getNextSchedNo();
					msg = (scheduler.*scheduler.SelectSubflow[schedulerNo])();
					//startTime = get_current_microsecond() - startTime;
					//InfoMessage("--- Make scheduling decision %d -us", msg);
				}


				if (msg != NULL) {
					decision = msg->schedDecision;
					allBytes = msg->bytesOnWire;
/*
                    InfoMessage("Transmit pakcet: seq=%u (prio=%d subflow=%d(%d)) [%u, %u]",
                            msg->seq, msg->priority, msg->schedDecision,
                            msg->oldDecision,
                            metaBuffer.pMetaMsgHead[msg->priority]->seq,
                            metaBuffer.GetDecrementMsgPointer(metaBuffer.pMetaMsgTail[msg->priority], msg->priority)->seq);
                    msg->Print();
*/
                    /*
                    if (msg->connID == 3 && msg->seq == 496) {
                        msg->Print();
                        InfoMessage("----");
                    }
                    */
/*

					InfoMessage("Transmit packet: %u[index=%d] (prio=%d) [%u{0},%u{%d}) subflow=%d (old=%d)\n",
						msg, 
						metaBuffer.ConvertMsgPointerToIndex(msg),
						msg->priority,
						metaBuffer.pMetaMsgHead[msg->priority],
						metaBuffer.pMetaMsgTail[msg->priority],
						metaBuffer.pMetaMsgTail[msg->priority] - metaBuffer.pMetaMsgHead[msg->priority],
						decision, msg->oldDecision);
*/
					MyAssert((decision >= 1) && (decision <= nTCP), 9210);
                    if (space[decision] <= 0) {
                        //InfoMessage("space1: %d, space2: %d",
                        //        space[1], space[2]);
                        //msg->Print();
                    }
					//MyAssert(space[decision] > 0, 9211);
                    if (msg->bytesLeft[decision] == 0)
                        msg->Print();
                    MyAssert(msg->bytesLeft[decision] > 0, 9212);
					/*
					if (msg->pMsgStart != msg->pMsgData) {
						InfoMessage("Pointer mismatch (start=%u p=%u delta=%u connID=%u seq=%u sched=%d old=%d "
							"prio=%d",
							msg->pMsgStart, msg->pMsgData, msg->pMsgData - msg->pMsgStart,  msg->connID, msg->seq,
							msg->schedDecision, msg->oldDecision, msg->priority);
					}
					*/
					bytes = metaBuffer.TransmitFromMetaBuffer(msg, 0);
                    /*
					if (msg->bTransmitted == 0) {
						InfoMessage("Msg not transmitted! space1=%d space2=%d space3=%d space4=%d", 
							space[1], space[2], space[3], space[4]);
						msg->Print();
					}
                    */
					space[decision] -= bytes;
					kernelInfo.DecreaseTCPAvailableSpace(decision, bytes);
					if (bytes < allBytes) { 
					//InfoMessage("Can't write more."); 
						//break; 
					}
				} else {
					//break;
				}
			}
		}

	}

	MyAssert(0, 1030);
}


void DumpProxyStatus() {
	fprintf(stderr, "\n");
	fprintf(stderr, "                                           P R O X Y    S T A T U S                                      \n");
	fprintf(stderr, "**********************************************************************************************************\n");
	fprintf(stderr, "SUBFLOWS: (%d)\n", subflows.n);
	fprintf(stderr, "dataBuf  msgBuf   readNotify  writeNotify\n");
	fprintf(stderr, "------------------------------------------\n");
	for (int i=1; i<=subflows.n; i++) {
		fprintf(stderr, "%d  %d  %s  %s\n", 
			subflowOutput.dataBufSize[i], 
			subflowOutput.msgBufSize[i], 
			conns.peers[i].events & POLLRDNORM ? "YES" : "NO",
			conns.peers[i].events & POLLWRNORM ? "YES" : "NO"
			);
	}
	fprintf(stderr, "------------------------------------------\n");

	static const char * estStr[] = {"NOT_CONN", "SUCC", "FAIL"};

	fprintf(stderr, "TCP Connections: (maxIdx = %d)\n", conns.maxIdx);
	fprintf(stderr, "connID  status  pollPos  fd  buf  IP  rPort  lPort  R-Notify  W-Notify  sentSYN  estStatus  #msgLists  headers  pCurMsgs\n");
	fprintf(stderr, "----------------------------------------------------------------------------------------------------------\n");
	for (int i=1; i<=65535; i++) {
		int s = conns.connTab[i].GetInUseStatus(tickCount);
		int pollPos = conns.connTab[i].pollPos;
		if (s == CONN_INFO::CONN_EMPTY) continue;
		fprintf(stderr, "%d  %s  %d  %d  %d  %s  %d  %d  %s  %s  %s  %s  %d  %d  %s\n", 
			i, 
			s==CONN_INFO::CONN_INUSE ? "INUSE" : "CLOSED", 
			pollPos, 
			conns.peers[pollPos].fd, 
			conns.connTab[i].bytesInTCPBuffer,
			ConvertDWORDToIP(conns.connTab[i].serverIP),
			conns.connTab[i].serverPort,
			conns.connTab[i].clientPort,
			conns.peers[pollPos].events & POLLRDNORM ? "YES" : "NO",
			conns.peers[pollPos].events & POLLWRNORM ? "YES" : "NO",
			conns.peersExt[pollPos].bSentSYNToSubflow ? "YES" : "NO",
			estStr[conns.peersExt[pollPos].establishStatus],
			tcpOutput.GetMsgListSize(pollPos),
			(int)tcpOutput.headers[pollPos][8],
			tcpOutput.pCurMsgs[pollPos] == NULL ? "NUL" : "NOT-NUL"
		);
	}
	fprintf(stderr, "----------------------------------------------------------------------------------------------------------\n");

	fprintf(stderr, "# Msgs to be abandoned after fully read (MAX_FDS):  %d\n", tcpOutput.GetMsgListSize(MAX_FDS));
	fprintf(stderr, "# Msgs to be included when SYN arrives (MAX_FDS+1): %d\n", tcpOutput.GetMsgListSize(MAX_FDS+1));

	fprintf(stderr, "TCP Data buffer: Capacity=%d  Size=%d  Tail=%d\n", PROXY_SETTINGS::tcpOverallBufDataCapacity, tcpOutput.dataCacheSize, tcpOutput.dataCacheEnd);
	fprintf(stderr, "TCP Msg  buffer: Capacity=%d  Size=%d  Tail=%d\n", PROXY_SETTINGS::tcpOverallBufMsgCapacity, tcpOutput.msgCacheSize, tcpOutput.msgCacheEnd);
	
	/*
	fprintf(stderr, "Total subflow msgs:%u   Total bytes on subflows:%u   Protocol efficiency:%.3lf%%", 
		conns.statTotalMsgs, conns.statTotalBytesOnWire, 
		conns.statTotalMsgs*8/(double)conns.statTotalBytesOnWire * 100.0f);
	*/

	fprintf(stderr, "**********************************************************************************************************\n");
	fprintf(stderr, "\n");
}

int TestFileFlag(const char * filename) {
	FILE * ifsFlag = fopen(filename, "rb");	
	if (ifsFlag == NULL) {
		return 0;
	} else {		
		fclose(ifsFlag);
		return 1;
	}
}

#ifndef VS_SIMULATION

pthread_mutex_t subflowDumpLock = PTHREAD_MUTEX_INITIALIZER;

void * TickCountThread(void * arg) {
	struct timeval tv;
	int r = gettimeofday(&tv, NULL);
	MyAssert(r == 0, 1725);
	time_t tBase = tv.tv_sec;

	int nTicks = 0;

	int g = (proxyMode == PROXY_MODE_LOCAL) ? 
		TICK_COUNT_GRANULARITY_LOCAL_PROXY : TICK_COUNT_GRANULARITY_REMOTE_PROXY;

	//int gDebug = (proxyMode == PROXY_MODE_LOCAL) ? 3 : 30;
	int gDebug = 30;

	while (1) {		
#ifdef FEATURE_DUMP_SUBFLOW_MSG
		if (TestFileFlag("/run/start.dump")) {
			pthread_mutex_lock(&subflowDumpLock);
			if (ofsSubflowDump != NULL) {
				ErrorMessage("!!!! Subflow dump already started !!!!");
			} else {
				ofsSubflowDump = fopen("/home/fengqian/km/subflows.dump", "wb");
				MyAssert(ofsSubflowDump != NULL, 1840);
				int nSubflows = subflows.n;
				fwrite(&nSubflows, sizeof(int), 1, ofsSubflowDump);
				InfoMessage("#### Subflow dump started ####");
			}
			pthread_mutex_unlock(&subflowDumpLock);
			int r = remove("/run/start.dump");
			MyAssert(!r, 1873);
		}

		if (TestFileFlag("/run/stop.dump")) {
			pthread_mutex_lock(&subflowDumpLock);
			if (ofsSubflowDump == NULL) {
				ErrorMessage("!!!! Subflow dump already stopped !!!!");
			} else {
				fclose(ofsSubflowDump);
				ofsSubflowDump = NULL;
				InfoMessage("#### Subflow dump stopped ####");
			}
			pthread_mutex_unlock(&subflowDumpLock);
			int r = remove("/run/stop.dump");
			MyAssert(!r, 1874);
		}
#endif

		int r = gettimeofday(&tv, NULL);
		MyAssert(r == 0, 1726);
		tickCount = tv.tv_sec - tBase + 1 + PROXY_SETTINGS::connIDReuseTimeout;
		//if (PROXY_SETTINGS::bLateBindingRP) KERNEL_INTERFACE::SetTickCount(tickCount);

		sleep(g);
		
		/*
		if (PROXY_SETTINGS::bLateBindingRP) {
			int o = KERNEL_INTERFACE::GetBufferOccupancy();
			if (o > 0) {
				InfoMessage("### Buffer Occupancy: %d ###", o);
			}
		}
		*/
		
		/*
		if (proxyMode == PROXY_MODE_REMOTE)
			InfoMessage("### Overall send buf occupancy: %d ###", subflows.GetTotalSendBufferOccupancy());
		*/

		if (PROXY_SETTINGS::bDumpIO) {
			fflush(ofsIODump);
		}

		/*
		{
			for (int i=1; i<=65535; i++) {
				int s = conns.connTab[i].bytesInSubflowBuffer;
				if (s > 0) {
					InfoMessage("Conn %d: subflow buf bytes = %d", i, s);
				}
			}
		}
		*/

		if (++nTicks % gDebug == 0) {
			#ifdef PERIODICALLY_DUMP_PROXY_STATUS
			DumpProxyStatus();
			#endif
		}
	}

	return NULL;
}

void StartTickCountThread() {
	tickCount = -1;
	pthread_t tick_count_thread;	
	int r = pthread_create(&tick_count_thread, NULL, TickCountThread, NULL);
	MyAssert(r == 0, 1724);
		
	while (tickCount == -1) {pthread_yield();}
	InfoMessage("Tick thread started.");
}

#else

void StartTickCountThread() {
	MyAssert(0, 1727);
}

#endif

int main(int argc, char * * argv) {

	InfoMessage("MPWear Version: %s [USERLEVEL MULTIPATH]", MY_VERSION);
	srand((DWORD)time(NULL));

#ifndef VS_SIMULATION
	struct timeval tv;
	gettimeofday(&tv, NULL);
	highResTimestampBase = tv.tv_sec * 1000000 + tv.tv_usec;
#endif

	char remoteIP[32];

#ifdef FEATURE_DUMP_SUBFLOW_MSG
	remove("/run/start.dump");
	remove("/run/stop.dump");
#endif

	int subflowSelectionPolicy = -1;
	int reinjectionPolicy = -1;

	if (argc == 4 || argc == 5) {
		// Remote Proxy Mode
		InfoMessage("Remote Proxy Mode");
		proxyMode = PROXY_MODE_REMOTE;
		// Get the number of subflows from command-line arguments
		// Given "n", there are 2*n subflows: n TCP subflows and n UDP subflows
		// For each interface, there are exactly one TCP subflow and one UDP subflow
        PROXY_SETTINGS::nTCPSubflows = atoi(argv[1]);
        PROXY_SETTINGS::nUDPSubflows = 2;
		PROXY_SETTINGS::nSubflows = PROXY_SETTINGS::nTCPSubflows + PROXY_SETTINGS::nUDPSubflows;
		// Get the scheduler (subflow selection policy) to use
		subflowSelectionPolicy = atoi(argv[2]);
		reinjectionPolicy = atoi(argv[3]);
		if (argc == 5) {
			debugFilename = argv[4];
		}
		switch (subflowSelectionPolicy) {
			//case SUBFLOW_SELECTION_RANDOM:
			//case SUBFLOW_SELECTION_ROUNDROBIN:
			//case SUBFLOW_SELECTION_MINBUF:
			//case SUBFLOW_SELECTION_FIXED:
			//case SUBFLOW_SELECTION_MINRTT:
			case SUBFLOW_SELECTION_TWOWAY:
			case SUBFLOW_SELECTION_NEWTXDELAY:
			case SUBFLOW_SELECTION_TWOWAY_NAIVE:
			case SUBFLOW_SELECTION_TWOWAY_BALANCE:
			case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
			case SUBFLOW_SELECTION_MINRTT_KERNEL:
			case SUBFLOW_SELECTION_TXDELAY:
			case SUBFLOW_SELECTION_WIFIONLY:
			case SUBFLOW_SELECTION_BTONLY:
			//case SUBFLOW_SELECTION_BBS:
			case SUBFLOW_SELECTION_BBS_MRT:
			//case SUBFLOW_SELECTION_CBS:
			case SUBFLOW_SELECTION_EMPTCP:
			case SUBFLOW_SELECTION_BLOCK:
			case SUBFLOW_SELECTION_UDP:
			case SUBFLOW_SELECTION_ROUNDROBIN:
			case SUBFLOW_SELECTION_REMP:
				break;
			default:
				goto SHOW_USAGE;
		}

	} else if (argc >= 6) {
		InfoMessage("Local Proxy Mode");
		proxyMode = PROXY_MODE_LOCAL;
		strcpy(remoteIP, argv[1]);
		rpIPAddress = ConvertIPToDWORD(remoteIP);
		PROXY_SETTINGS::nSubflows = atoi(argv[2]);
		
		if (argc != 3 + PROXY_SETTINGS::nSubflows) goto SHOW_USAGE;

	} else {
		SHOW_USAGE:
		InfoMessage("Usage for local proxy:  %s [remote_proxy_IP] [number_of_subflows] [subflow1_if] [subflow2_if] ...", argv[0]);
		InfoMessage("Usage for remote proxy: %s [number_of_subflows] [subflow selection policy]", argv[0]);
		InfoMessage("Subflow selection policy:");
		InfoMessage("1 = Random");
		InfoMessage("2 = Round Robin");
		InfoMessage("3 = Round Robin / Minimum buffer");
		InfoMessage("5 = Only use Subflow 1");
		InfoMessage("6 = Minimum RTT (user)");
		InfoMessage("7 = Minimum RTT (kernel)");
		return 0;
	}
	
	// Apply default settings
	PROXY_SETTINGS::ApplyRawSettings();
	// Adjust the settings based on parameters and proxy location (RP/LP)
	PROXY_SETTINGS::AdjustSettings();
	
	// Set remote proxy UDP listening ports
	subflows.udpPeerPort[0] = REMOTE_PROXY_UDP1_PORT;
        subflows.udpPeerPort[1] = REMOTE_PROXY_UDP2_PORT;
	
	if (PROXY_SETTINGS::nSubflows < 1 || PROXY_SETTINGS::nSubflows > 16) {
		ErrorMessage("Invalid number of subflows");
		return -1;
	}

	if (proxyMode == PROXY_MODE_LOCAL) {
		// interface name
		for (int i=0; i<PROXY_SETTINGS::nSubflows; i++) {
			PROXY_SETTINGS::subflowInterfaces[i] = argv[3+i];
		}

		PROXY_SETTINGS::subflowSelectionAlgorithm = SUBFLOW_SELECTION_MINRTT_KERNEL;
	} else {
		PROXY_SETTINGS::subflowSelectionAlgorithm = subflowSelectionPolicy;
		PROXY_SETTINGS::reinjectionMechanism = reinjectionPolicy;
		InfoMessage("Reinjection mechanism: %d",
			PROXY_SETTINGS::reinjectionMechanism);	
	}

	//if (PROXY_SETTINGS::bLateBindingRP) KERNEL_INTERFACE::Init();

	StartTickCountThread();

	//Setup subflows (TCP and UDP handshake between RP and LP)
	int r;
	if (proxyMode == PROXY_MODE_REMOTE) {	
		r = subflows.Setup(NULL);			
	} else { 
		r = subflows.Setup(remoteIP);
	}
	if (r != R_SUCC) {
		ErrorMessage("Error setting up subflows");
		return -1;
	}

	if (conns.Setup() != R_SUCC) {
		ErrorMessage("Error setting up connections");
		return -1;
	}

	subflowOutput.Setup();
	tcpOutput.Setup();
	metaBuffer.Setup();
	kernelInfo.Setup();
	delayedFins.Setup(PROXY_SETTINGS::delayedFinD1, PROXY_SETTINGS::delayedFinD2);
	scheduler.Setup();

	lastSubflowActivityTick = tickCount;

	if (proxyMode == PROXY_MODE_REMOTE) {
		SUBFLOW_MONITOR::StartListen();
		
		#ifndef VS_SIMULATION
		kfd = open("/dev/cmatbuf", O_RDWR);
		MyAssert(kfd>=0, 2024);

		ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD1, subflows.fd[0]);
		ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD2, subflows.fd[1]);
        ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD3, subflows.fd[2]);
        ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD4, subflows.fd[3]);

		MyAssert(subflows.fd[0] == conns.peers[1].fd && subflows.fd[1] == conns.peers[2].fd, 2460);
        MyAssert(subflows.fd[2] == conns.peers[3].fd && subflows.fd[3] == conns.peers[4].fd, 2460);

		#endif
	}

	// Start running proxy
	ProxyMain();
	InfoMessage(" ************** Proxy Stopped. ***************");
	return 0;
}

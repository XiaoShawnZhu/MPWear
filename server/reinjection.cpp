#include "stdafx.h"

#include "kernel_info.h"
#include "connections.h"
#include "meta_buffer.h"
#include "tools.h"

#ifdef VS_SIMULATION
#include <errno.h>
#endif

//#define DEBUG_REINJECT
//#define DEBUG_META_SEQ
//#define DEBUG_STALL
#define DEBUG_UNA

extern int proxyMode;
extern struct SUBFLOWS subflows;
extern struct CONNECTIONS conns;
extern struct BUFFER_SUBFLOWS subflowOutput;	
extern struct BUFFER_TCP tcpOutput;
extern struct META_BUFFER metaBuffer;
extern struct DELAYED_FINS delayedFins;
extern struct KERNEL_INFO kernelInfo;

extern int tickCount;
extern int lastSubflowActivityTick;

extern int keepRunning;
extern int statThreadRunning;

extern FILE * ofsIODump;
extern FILE * ofsLatency;
extern FILE * ofsDebug;

extern int kfd;
extern unsigned long tBlock;
extern int rnjData;

#ifdef REINJECTION
static DWORD metaSeq;
#endif

#ifdef DEBUG_UNA
int bFlag;
#endif

#ifdef REINJECTION

void CONNECTIONS::Reinjection(int toPollPos) {
	// #################################################
        // Reinjection
        int otherPollPos = (toPollPos == 1) ? 2 : 1;
        // For test: same subflow reinject
        //otherPollPos = toPollPos;

	//MaintainUnaQueue(toPollPos, otherPollPos);
	//MaintainUnaQueue(otherPollPos, toPollPos);
	MaintainUnaQueue(1, 2);
	MaintainUnaQueue(2, 1);	

	if (subflowOutput.pMsgHead[toPollPos]) {
                //InfoMessage("subflow %d pMsgHead has data: %d.", toPollPos, subflowOutput.msgBufSize[toPollPos]);
                return;
        }
	
        int rnj = AddReinjectPacket(otherPollPos, toPollPos);

	
#ifdef DEBUG_REINJECT
       	//InfoMessage("Reinject %d messages (subflow %d to %d).", rnj, otherPollPos, toPollPos);
#endif
        if (rnj > 0) {
		// the reinjection queue holds messages to be reinjected on this subflow from the other
		if (subflowOutput.pRnjUna[toPollPos] == NULL) {
			subflowOutput.pRnjUna[toPollPos] = subflowOutput.pMsgRnjHead[toPollPos];
			MyAssert(subflowOutput.pRnjCurr[toPollPos] == NULL, 4858);
			subflowOutput.pRnjCurr[toPollPos] = subflowOutput.pMsgRnjHead[toPollPos];
		}
		if (!subflowOutput.pRnjCurr[toPollPos]) {
			subflowOutput.pRnjCurr[toPollPos] = subflowOutput.pMsgRnjHead[toPollPos];
		}
                CopyFromRnjToMsgBuf(toPollPos, toPollPos);
                //InfoMessage("Reinject %d messages (subflow %d[%dus] to %d[%dus]) at %lu (una %dB).", rnj, otherPollPos, (otherPollPos==1)?rtt1:rtt2, toPollPos, (toPollPos==1)?rtt1:rtt2, GetHighResTimestamp(), subflowOutput.unaBytes[1] + subflowOutput.unaBytes[2]);
		//PrintMsgQueueSimple(toPollPos);
		
		//PrintUnaQueueSimple(1);
		//PrintUnaQueueSimple(2);
		//InfoMessage("Subflow %d Una: %d", otherPollPos, subflowOutput.unaSize[otherPollPos]);
		subflowOutput.bActiveReinject[toPollPos] = 1;
		//EnableSubflowWriteNotification(toPollPos, 1);
                //MyAssert(3 == 2, 1249);
        }
}

void CONNECTIONS::MaintainUnaQueue(int originPollPos, int rnjPollPos) {

	SUBFLOW_MSG * pMsgUnacked = subflowOutput.pMsgUnacked[originPollPos];
        SUBFLOW_MSG * & pMsgUnaHead = subflowOutput.pMsgUnaHead[originPollPos];
	SUBFLOW_MSG * pIterHead = subflowOutput.pMsgUnaHead[originPollPos];
        SUBFLOW_MSG * & pMsgUnaTail = subflowOutput.pMsgUnaTail[originPollPos];
        int & unaSize = subflowOutput.unaSize[originPollPos];
	int & unaBytes = subflowOutput.unaBytes[originPollPos];

	SUBFLOW_MSG * pMsgUnackedOther = subflowOutput.pMsgUnacked[rnjPollPos];
	SUBFLOW_MSG * & pMsgUnaOtherHead = subflowOutput.pMsgUnaHead[rnjPollPos];
	SUBFLOW_MSG * & pMsgUnaOtherTail = subflowOutput.pMsgUnaTail[rnjPollPos];
	int & unaOtherSize = subflowOutput.unaSize[rnjPollPos];
	int & unaOtherBytes = subflowOutput.unaBytes[rnjPollPos];

        const SUBFLOW_MSG * pMsgUnaEnd = pMsgUnacked + PROXY_SETTINGS::subflowBufMsgCapacity;
	const SUBFLOW_MSG * pMsgUnaOtherEnd = pMsgUnackedOther + PROXY_SETTINGS::subflowBufMsgCapacity;

        //SUBFLOW_MSG * pMsgReinject = subflowOutput.pMsgReinject[rnjPollPos];
        //SUBFLOW_MSG * & pMsgRnjHead = subflowOutput.pMsgRnjHead[rnjPollPos];
        //SUBFLOW_MSG * & pMsgRnjTail = subflowOutput.pMsgRnjTail[rnjPollPos];
        //int & rnjSize = subflowOutput.rnjSize[rnjPollPos];

        //const SUBFLOW_MSG * pMsgRnjEnd = pMsgReinject + PROXY_SETTINGS::subflowBufMsgCapacity;
	
	unsigned long long r = -1;
	if (originPollPos == 1) {
        ioctl(kfd, CMAT_IOCTL_GET_FD1_ACK, &r);
	} else if (originPollPos == 2) {
		ioctl(kfd, CMAT_IOCTL_GET_FD2_ACK, &r);
	} else {
		return;
	}

	DWORD lastACK = (DWORD)(r & 0xFFFFFFFF);
	int bDupACK = (int)((r >> 32) & 0xFFFFFFFF);

	if (!pMsgUnaHead) {
		MyAssert(unaSize == 0, 9005);
                //InfoMessage("------ lastACK=%u, bDupACK=%d, Remove: ", lastACK, bDupACK);
                return;
        }
	//InfoMessage("------ lastACK=%u, bDupACK=%d, Remove: ", lastACK, bDupACK);
#ifdef DEBUG_REINJECT
	InfoMessageHead("------ lastACK=%u, bDupACK=%d, Remove: ", lastACK, bDupACK);
	InfoMessageContinue("\n");
#endif
//#ifdef DEBUG_UNA
                if (originPollPos == 1) {
                        if (subflowLastAck1 != lastACK) {
                                subflowLastAck1 = lastACK;
				//InfoMessage("------ subflow 1 lastACK=%u, bDupACK=%d, Remove: ", lastACK, bDupACK);
                        }
			subflowDupACK1 = bDupACK;
                } else {
                        if (subflowLastAck2 != lastACK) {
                                subflowLastAck2 = lastACK;
				//InfoMessage("------ subflow 2 lastACK=%u, bDupACK=%d, Remove: ", lastACK, bDupACK);
                        }
			subflowDupACK2 = bDupACK;
                }
        //}
//#endif
#ifdef DEBUG_META_SEQ
	InfoMessageHead("------ lastACK(%d)=%u, bDupACK=%d, Remove: ", originPollPos, lastACK, bDupACK);
        InfoMessageContinue("\n");
	InfoMessage("Enter: 8888888888888888888888888888888");
	PrintUnaQueueSimple(1);
	PrintUnaQueueSimple(2);
	InfoMessage("---------------------------");
#endif
	unsigned long currT = GetHighResTimestamp();
	// Remove ACKed messages in the UnaBuffer of this subflow
	while (pIterHead != pMsgUnaTail) {
		if (pIterHead->tcpSeq + pIterHead->bytesOnWire <= lastACK) {
#ifdef DEBUG_REINJECT
			InfoMessageContinue("(%u, %u, %u) ", pIterHead->connID, pIterHead->seq, pIterHead->tcpSeq);
#endif
			// Acked by this subflow but not the meta level
			if (pMsgUnaOtherHead &&
				pIterHead->subflowSeq > pMsgUnaOtherHead->subflowSeq) {
				pIterHead->bSubflowAcked = 1;
				pIterHead++;
			} else {
				MyAssert(pIterHead == pMsgUnaHead, 8000);
				fprintf(ofsLatency, "%d\t%u\t%lu\t%lu\n", originPollPos, pMsgUnaHead->subflowSeq, pMsgUnaHead->timestamp, currT);
				unaBytes -= pMsgUnaHead->bytesOnWire;
				pIterHead++;
				pMsgUnaHead++;
				unaSize--;
			}
			if (pMsgUnaHead >= pMsgUnaEnd) pMsgUnaHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
			if (pIterHead >= pMsgUnaEnd) pIterHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
		} else {
			if (pIterHead->bReinjected && !pIterHead->bSubflowAcked) {
				//InfoMessage("Reinjected pkt in Una: %u %u %u %u (%d %d)", pIterHead->connID, pIterHead->seq, pIterHead->tcpSeq, pIterHead->subflowSeq, pIterHead->bReinjected, pIterHead->bSubflowAcked);
				if (isRnjAcked(pIterHead, rnjPollPos)) {
					//InfoMessage("Reinjected packet ACKed");
					pIterHead->bSubflowAcked = 1;
				}
				pIterHead++;
				if (pIterHead >= pMsgUnaEnd) pIterHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
			} else {
				break;
			}
#ifdef DEBUG_REINJECT
                        InfoMessageContinue("{%u, %u, %u} (%d %d)", pMsgUnaHead->connID, pMsgUnaHead->seq, pMsgUnaHead->tcpSeq, bDupACK, pMsgUnaHead->bReinjected);
#endif
			break;
		} 
	}
#ifdef DEBUG_REINJECT
        InfoMessageContinue("\n");
#endif
	if (pMsgUnaHead == pMsgUnaTail) {
                MyAssert(unaSize == 0, 8203);
                pMsgUnaHead = NULL;
        }
#ifdef DEBUG_META_SEQ
	InfoMessage("After ACK: 8888888888888888888888888888888");
        PrintUnaQueueSimple(1);
        PrintUnaQueueSimple(2);
        InfoMessage("---------------------------");
#endif
	// Maintain both Unacked queue
	int bChanged = 1;
	while (bChanged) {
		bChanged = 0;
		// start from the other una queue
		if (pMsgUnaOtherHead) {
			while (pMsgUnaOtherHead != pMsgUnaOtherTail) {
				if (pMsgUnaOtherHead->bSubflowAcked) {
					if (!pMsgUnaHead || pMsgUnaOtherHead->subflowSeq < pMsgUnaHead->subflowSeq) {
						fprintf(ofsLatency, "%d\t%u\t%lu\t%lu\n", rnjPollPos, pMsgUnaOtherHead->subflowSeq, pMsgUnaOtherHead->timestamp, currT);
						unaOtherBytes -= pMsgUnaOtherHead->bytesOnWire;
						pMsgUnaOtherHead++;
						unaOtherSize--;
						if (pMsgUnaOtherHead >= pMsgUnaOtherEnd) pMsgUnaOtherHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
						bChanged = 1;
					} else {
						break;
					}
				}
				else {
					break;
				}
			}
			if (pMsgUnaOtherHead == pMsgUnaOtherTail) {
				MyAssert(unaOtherSize == 0, 8034);
				pMsgUnaOtherHead = NULL;
			}
		}
                  
		// then this una queue
		if (pMsgUnaHead) {
			while (pMsgUnaHead != pMsgUnaTail) {
                        	if (pMsgUnaHead->bSubflowAcked) {
                                	if (!pMsgUnaOtherHead || pMsgUnaHead->subflowSeq < pMsgUnaOtherHead->subflowSeq) {
						fprintf(ofsLatency, "%d\t%u\t%lu\t%lu\n", originPollPos, pMsgUnaHead->subflowSeq, pMsgUnaHead->timestamp, currT);
                                        	unaBytes -= pMsgUnaHead->bytesOnWire;
	                                        pMsgUnaHead++;
        	                                unaSize--;
                	                        if (pMsgUnaHead >= pMsgUnaEnd) pMsgUnaHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
                        	                bChanged = 1;
                                	} else {
                                        	break;
	                                }
        	                }
                	        else {
                        	        break;
	                        }
        	        }
			if (pMsgUnaHead == pMsgUnaTail) {
                        	MyAssert(unaSize == 0, 8035);
                        	pMsgUnaHead = NULL;
                	}
		}
	}

#ifdef DEBUG_META_SEQ
        InfoMessage("After maintain: 8888888888888888888888888888888");
        PrintUnaQueueSimple(1);
        PrintUnaQueueSimple(2);
        InfoMessage("---------------------------");
#endif

	// Set acked reinjected packets
	SUBFLOW_MSG * pOriginRnjUna = subflowOutput.pRnjUna[originPollPos];
	SUBFLOW_MSG * & pOriginRnjTail = subflowOutput.pMsgRnjTail[originPollPos];
	SUBFLOW_MSG * pOriginRnjEnd = subflowOutput.pMsgReinject[originPollPos] + PROXY_SETTINGS::subflowBufMsgCapacity;
	while (pOriginRnjUna && pOriginRnjUna != pOriginRnjTail) {
		if (pOriginRnjUna->tcpSeq + pOriginRnjUna->bytesOnWire <= lastACK) {
			pOriginRnjUna->bRnjAcked = 1;
			pOriginRnjUna++;
			if (pOriginRnjUna >= pOriginRnjEnd)  pOriginRnjUna -= PROXY_SETTINGS::subflowBufMsgCapacity;
		} else {
			break;
		}
	}
	if (pOriginRnjUna == pOriginRnjTail) pOriginRnjUna = NULL;

	MyAssert(unaSize >= 0, 2637);
        MyAssert((unaSize == 0) == (unaBytes == 0), 2638);
	if (unaSize == 0) {
                pMsgUnaHead = NULL;
        }
}

int CONNECTIONS::AddReinjectPacket(int originPollPos, int rnjPollPos) {
	SUBFLOW_MSG * pMsgUnacked = subflowOutput.pMsgUnacked[originPollPos];
        SUBFLOW_MSG * & pMsgUnaHead = subflowOutput.pMsgUnaHead[originPollPos];
        SUBFLOW_MSG * & pMsgUnaTail = subflowOutput.pMsgUnaTail[originPollPos];

        const SUBFLOW_MSG * pMsgUnaEnd = pMsgUnacked + PROXY_SETTINGS::subflowBufMsgCapacity;

        SUBFLOW_MSG * pMsgReinject = subflowOutput.pMsgReinject[rnjPollPos];
        SUBFLOW_MSG * & pMsgRnjHead = subflowOutput.pMsgRnjHead[rnjPollPos];
        SUBFLOW_MSG * & pMsgRnjTail = subflowOutput.pMsgRnjTail[rnjPollPos];
        int & rnjSize = subflowOutput.rnjSize[rnjPollPos];

        const SUBFLOW_MSG * pMsgRnjEnd = pMsgReinject + PROXY_SETTINGS::subflowBufMsgCapacity;

	int bDupACK = 0;
	DWORD lastACK;
	if (originPollPos == 1) {
                bDupACK = subflowDupACK1;
		lastACK = subflowLastAck1;
        } else {
                bDupACK = subflowDupACK2;
		lastACK = subflowLastAck2;
        }

	// Reinjection Algorithm

	int unaAllBytes;
	DWORD otherSeq;
	int bOtherAcked;
	SUBFLOW_MSG * head;
	unsigned long otherTs;

        ioctl(kfd, CMAT_IOCTL_GET_FD1_RTT, &rtt1);
        ioctl(kfd, CMAT_IOCTL_GET_FD2_RTT, &rtt2);

	int originRTT, rnjRTT;
	if (originPollPos == 1) {
		originRTT = rtt1;
	} else {
		originRTT = rtt2;
	}

	if (rnjPollPos == 1) {
		rnjRTT = rtt1;
	} else {
		rnjRTT = rtt2;
	}

	unsigned long currT = GetHighResTimestamp();
	int timeElaps = 0;
	switch (PROXY_SETTINGS::reinjectionMechanism) {
	case REINJECTION_PACKET_LOSS:
		if (pMsgUnaHead && !pMsgUnaHead->bReinjected && !pMsgUnaHead->bSubflowAcked) {
			if (bDupACK) {
			pMsgRnjTail->CopyPointerFrom(pMsgUnaHead);
			if (pMsgRnjHead == NULL) pMsgRnjHead = pMsgRnjTail;
                        pMsgRnjTail++;
                        if (pMsgRnjTail >= pMsgRnjEnd) pMsgRnjTail -= PROXY_SETTINGS::subflowBufMsgCapacity;
			rnjSize++;
			pMsgUnaHead->bReinjected = 1;
			}

			else if ((int)(currT - pMsgUnaHead->timestamp) > 2 * originRTT && originRTT > rnjRTT) {
				pMsgRnjTail->CopyPointerFrom(pMsgUnaHead);
	                        if (pMsgRnjHead == NULL) pMsgRnjHead = pMsgRnjTail;
        	                pMsgRnjTail++;
                	        if (pMsgRnjTail >= pMsgRnjEnd) pMsgRnjTail -= PROXY_SETTINGS::subflowBufMsgCapacity;
                        	rnjSize++;
	                        pMsgUnaHead->bReinjected = 1;
			}
		}
		break;
	case REINJECTION_EARLY_TIMER:
		break;
	case REINJECTION_UNACK_BYTES:
		if (!pMsgUnaHead)
			break;
		//if (originPollPos == 1)
		//	break;
		unaAllBytes = subflowOutput.unaBytes[1] + subflowOutput.unaBytes[2];
		otherSeq = subflowOutput.pMsgUnaHead[rnjPollPos]? subflowOutput.pMsgUnaHead[rnjPollPos]->subflowSeq: 0;
		bOtherAcked = subflowOutput.pMsgUnaHead[rnjPollPos]? subflowOutput.pMsgUnaHead[rnjPollPos]->bSubflowAcked: 0; 
		otherTs = subflowOutput.pMsgUnaHead[rnjPollPos]? subflowOutput.pMsgUnaHead[rnjPollPos]->timestamp: 0;
		head = pMsgUnaHead;
		//InfoMessage("UnaAllBytes: %d, %u, %u", unaAllBytes, head->seq, otherSeq);
		if (head->bReinjected)
			break;
		while (head != pMsgUnaTail) {
			unaAllBytes -= head->bytesOnWire;
			if (head->bSubflowAcked) {
				head++;
				if (head >= pMsgUnaEnd) head -= PROXY_SETTINGS::subflowBufMsgCapacity;
				continue;
			}
			if (head->bReinjected || head->timestamp == 0) {
				if (unaAllBytes > PROXY_SETTINGS::unackBytesThreshold)
					break;
				head++;
				if (head >= pMsgUnaEnd) head -= PROXY_SETTINGS::subflowBufMsgCapacity;
				continue;
			}
			timeElaps = (int)(currT - head->timestamp);
			int rttvar = 0;
		        if (originPollPos == 1) {
		                ioctl(kfd, CMAT_IOCTL_GET_FD1_RTTVAR, &rttvar);
		        } else if (originPollPos == 2) {
		                ioctl(kfd, CMAT_IOCTL_GET_FD2_RTTVAR, &rttvar);
		        }

//			if (head->subflowSeq < otherSeq /*&&
//				otherTs - head->timestamp > 100000*/ &&
//				unaAllBytes <= PROXY_SETTINGS::unackBytesThreshold) {
			//if (rnjPollPos == 2) {
			//InfoMessage("(%d->%d) time=%lu, head=%lu, seq=%u, otherseq=%u", originPollPos, rnjPollPos, currT, head->timestamp, head->seq, otherSeq);
			//} 

//				if (timeElaps < originRTT - rnjRTT && timeElaps > rnjRTT /*timeElaps > 3 * originRTT or timeElaps > 500000*/ /* || (bDupACK && head->tcpSeq <= lastACK)*/) {
					//if (bDupACK) InfoMessage("Packet loss %u; reinject: %u time [%d, %d]",lastACK, head->tcpSeq, timeElaps, originRTT);
/*					pMsgRnjTail->CopyPointerFrom(head);
	                                if (pMsgRnjHead == NULL) pMsgRnjHead = pMsgRnjTail;
        	                        pMsgRnjTail++;
                	                if (pMsgRnjTail >= pMsgRnjEnd) pMsgRnjTail -= PROXY_SETTINGS::subflowBufMsgCapacity;
                        	        rnjSize++;
                                	head->bReinjected = 1;
				}
			}
*/
			//if (head->seq >= otherSeq) break;
			// RTO with max of 500ms
			int thres = originRTT + rttvar > 500000? 500000: originRTT + rttvar;
			if (head->subflowSeq < otherSeq &&
//				(timeElaps > rnjRTT) &&
				originRTT > rnjRTT && 
				((unaAllBytes > PROXY_SETTINGS::unackBytesThreshold && bOtherAcked) ||
				timeElaps > thres)) {
				pMsgRnjTail->CopyPointerFrom(head);
				if (pMsgRnjHead == NULL) pMsgRnjHead = pMsgRnjTail;
                        	pMsgRnjTail++;
                        	if (pMsgRnjTail >= pMsgRnjEnd) pMsgRnjTail -= PROXY_SETTINGS::subflowBufMsgCapacity;
                        	rnjSize++;
                        	head->bReinjected = 1;
				break;
			} else {
				break;
			}	

			if (rnjSize >= 10) break;
			head++;
			if (head >= pMsgUnaEnd) head -= PROXY_SETTINGS::subflowBufMsgCapacity;
		}
		break;
	default:
		break;
	}

/*
	// Test case (reinject the message [connID=1, seq=3])
	SUBFLOW_MSG * head = subflowOutput.pMsgUnaHead[originPollPos];

	InfoMessage("AddReinjectPacket, head %u", head);
	if (!head)
		return 0;
	if (testCounter > 0)
		return 0;

	while (head != pMsgUnaTail) {
		if (head->connID == 1 && head->seq == 3) {
			pMsgRnjTail->CopyPointerFrom(head);
			if (pMsgRnjHead == NULL) pMsgRnjHead = pMsgRnjTail;
                	pMsgRnjTail++;
                	if (pMsgRnjTail >= pMsgRnjEnd) pMsgRnjTail -= PROXY_SETTINGS::subflowBufMsgCapacity;

			rnjSize++;
			testCounter++;
			break;
		}
		head++;
		if (head >= pMsgUnaEnd) head -= PROXY_SETTINGS::subflowBufMsgCapacity;
	}
*/

#ifdef DEBUG_REINJECT
	if (rnjSize > 0)
		PrintRnjQueue(rnjPollPos);
#endif
	return rnjSize;
}


int CONNECTIONS::isRnjAcked(SUBFLOW_MSG * pMsgUnaHead, int rnjPollPos) {
	SUBFLOW_MSG * & head = subflowOutput.pRnjUna[rnjPollPos];
	SUBFLOW_MSG * tail = subflowOutput.pMsgRnjTail[rnjPollPos];
	SUBFLOW_MSG * end = subflowOutput.pMsgReinject[rnjPollPos] + PROXY_SETTINGS::subflowBufMsgCapacity;

	MyAssert(head != NULL, 4680);
	int r = 0;
	while (head != tail) {
		if (head->connID == pMsgUnaHead->connID &&
			head->seq == pMsgUnaHead->seq) {
			r = head->bRnjAcked;
			if (r == 0)
				return 0;
			head++;
	                if (head >= end) head -= PROXY_SETTINGS::subflowBufMsgCapacity;
			if (head == tail) head = NULL;
			return r;
		}
		head++;
		if (head >= end) head -= PROXY_SETTINGS::subflowBufMsgCapacity;
	}
	// Error: reinjected packet not found
	MyAssert(3==2, 4681);
	return 0;
}

void CONNECTIONS::CopyFromRnjToMsgBuf(int rnjPollPos, int toPollPos) {
	SUBFLOW_MSG * pMsgBuffer = subflowOutput.pMsgBuffer[toPollPos];
        SUBFLOW_MSG * & pMsgHead = subflowOutput.pMsgHead[toPollPos];
        SUBFLOW_MSG * & pMsgTail = subflowOutput.pMsgTail[toPollPos];
        int & msgBufSize = subflowOutput.msgBufSize[toPollPos];

        const SUBFLOW_MSG * pMsgEnd = pMsgBuffer + PROXY_SETTINGS::subflowBufMsgCapacity;

	SUBFLOW_MSG * pMsgReinject = subflowOutput.pMsgReinject[rnjPollPos];
        SUBFLOW_MSG * & pMsgRnjHead = subflowOutput.pMsgRnjHead[rnjPollPos];
        SUBFLOW_MSG * & pMsgRnjTail = subflowOutput.pMsgRnjTail[rnjPollPos];
        int & rnjSize = subflowOutput.rnjSize[rnjPollPos];

        const SUBFLOW_MSG * pMsgRnjEnd = pMsgReinject + PROXY_SETTINGS::subflowBufMsgCapacity;

	while (rnjSize > 0) {
		pMsgTail->CopyPointerFrom(pMsgRnjHead);
		rnjData += pMsgRnjHead->bytesOnWire;
		pMsgRnjHead++;
		if (pMsgRnjHead >= pMsgRnjEnd) pMsgRnjHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
		if (pMsgRnjHead == pMsgRnjTail) pMsgRnjHead = NULL;		

		if (pMsgHead == NULL) pMsgHead = pMsgTail;
        	pMsgTail++;
        	if (pMsgTail >= pMsgEnd) pMsgTail -= PROXY_SETTINGS::subflowBufMsgCapacity;

		rnjSize--;
		msgBufSize++;
	}	
}

void CONNECTIONS::PrintUnaQueueSimple(int toPollPos) {
        SUBFLOW_MSG * pMsgUnacked = subflowOutput.pMsgUnacked[toPollPos];
        SUBFLOW_MSG * pMsgUnaHead = subflowOutput.pMsgUnaHead[toPollPos];
        SUBFLOW_MSG * pMsgUnaTail = subflowOutput.pMsgUnaTail[toPollPos];

        const SUBFLOW_MSG * pMsgUnaEnd = pMsgUnacked + PROXY_SETTINGS::subflowBufMsgCapacity;

        InfoMessageHead("Subflow %d Unacked Queue: ", toPollPos);
        if (!pMsgUnaHead) {
                InfoMessageContinue("NULL -> EMPTY\n");
                return;
        }

	int showCount = 0;
        while (pMsgUnaHead != pMsgUnaTail) {
                InfoMessageContinue("%u(%d, %u, %lu)-> ", pMsgUnaHead->subflowSeq, pMsgUnaHead->bSubflowAcked, pMsgUnaHead->tcpSeq, pMsgUnaHead->timestamp);
                pMsgUnaHead++;
                if (pMsgUnaHead >= pMsgUnaEnd) pMsgUnaHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
		showCount++;
		if (showCount >= 4) {
			InfoMessageContinue(" ... -> ");
			break;
		}
        }
        InfoMessageContinue("EMPTY\n");
}

void CONNECTIONS::PrintUnaQueue(int toPollPos) {
	SUBFLOW_MSG * pMsgUnacked = subflowOutput.pMsgUnacked[toPollPos];
        SUBFLOW_MSG * pMsgUnaHead = subflowOutput.pMsgUnaHead[toPollPos];
        SUBFLOW_MSG * pMsgUnaTail = subflowOutput.pMsgUnaTail[toPollPos];

	const SUBFLOW_MSG * pMsgUnaEnd = pMsgUnacked + PROXY_SETTINGS::subflowBufMsgCapacity;

	InfoMessageHead("Subflow %d Unacked Queue: ", toPollPos);
	if (!pMsgUnaHead) {
		InfoMessageContinue("NULL -> EMPTY\n");
		return;
	}

	while (pMsgUnaHead != pMsgUnaTail) {
		InfoMessageContinue("(%d, %u, %u): %u [%u] ", pMsgUnaHead->msgType, pMsgUnaHead->connID, pMsgUnaHead->seq, pMsgUnaHead->payloadLen, pMsgUnaHead->tcpSeq);
		InfoMessageContinue("[");
		for (int i = 0; i < 12; i++)
			InfoMessageContinue("%d ", pMsgUnaHead->pMsgData[i]);
		InfoMessageContinue("] -> ");
		pMsgUnaHead++;
		if (pMsgUnaHead >= pMsgUnaEnd) pMsgUnaHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
	}
	InfoMessageContinue("EMPTY\n");
}

void CONNECTIONS::PrintRnjQueue(int toPollPos) {
	SUBFLOW_MSG * pMsgReinject = subflowOutput.pMsgReinject[toPollPos];
        SUBFLOW_MSG * pMsgRnjHead = subflowOutput.pMsgRnjHead[toPollPos];
        SUBFLOW_MSG * pMsgRnjTail = subflowOutput.pMsgRnjTail[toPollPos];

        const SUBFLOW_MSG * pMsgRnjEnd = pMsgReinject + PROXY_SETTINGS::subflowBufMsgCapacity;

        InfoMessageHead("Subflow %d Reinject Queue: ", toPollPos);
        if (!pMsgRnjHead) {
                InfoMessageContinue("NULL -> EMPTY\n");
                return;
        }

        while (pMsgRnjHead != pMsgRnjTail) {
                InfoMessageContinue("(%d, %u, %u): %u [%u] -> ", pMsgRnjHead->msgType, pMsgRnjHead->connID, pMsgRnjHead->seq, pMsgRnjHead->payloadLen, pMsgRnjHead->tcpSeq);
                pMsgRnjHead++;
                if (pMsgRnjHead >= pMsgRnjEnd) pMsgRnjHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
        }
        InfoMessageContinue("EMPTY\n");
}

void CONNECTIONS::PrintMsgQueue(int toPollPos) {
        SUBFLOW_MSG * pMsgBuffer = subflowOutput.pMsgBuffer[toPollPos];
        SUBFLOW_MSG * pMsgHead = subflowOutput.pMsgHead[toPollPos];
        SUBFLOW_MSG * pMsgTail = subflowOutput.pMsgTail[toPollPos];

        const SUBFLOW_MSG * pMsgEnd = pMsgBuffer + PROXY_SETTINGS::subflowBufMsgCapacity;

        InfoMessageHead("Subflow %d Message Queue: ", toPollPos);
        if (!pMsgHead) {
                InfoMessageContinue("NULL -> EMPTY\n");
                return;
        }

        while (pMsgHead != pMsgTail) {
                InfoMessageContinue("(%d, %u, %u): %u [%u] -> ", pMsgHead->msgType, pMsgHead->connID, pMsgHead->seq, pMsgHead->payloadLen, pMsgHead->tcpSeq);
                pMsgHead++;
                if (pMsgHead >= pMsgEnd) pMsgHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
        }
        InfoMessageContinue("EMPTY\n");
}

void CONNECTIONS::PrintMsgQueueSimple(int toPollPos) {
        SUBFLOW_MSG * pMsgBuffer = subflowOutput.pMsgBuffer[toPollPos];
        SUBFLOW_MSG * pMsgHead = subflowOutput.pMsgHead[toPollPos];
        SUBFLOW_MSG * pMsgTail = subflowOutput.pMsgTail[toPollPos];

        const SUBFLOW_MSG * pMsgEnd = pMsgBuffer + PROXY_SETTINGS::subflowBufMsgCapacity;

        InfoMessageHead("Subflow %d Message Queue: ", toPollPos);
        if (!pMsgHead) {
                InfoMessageContinue("NULL -> EMPTY\n");
                return;
        }

	int showCount = 0;

        while (pMsgHead != pMsgTail) {
                InfoMessageContinue("(%u, %u[%lu]) -> ", pMsgHead->connID, pMsgHead->seq, pMsgHead->timestamp);
                pMsgHead++;
                if (pMsgHead >= pMsgEnd) pMsgHead -= PROXY_SETTINGS::subflowBufMsgCapacity;
		showCount++;
		if (showCount >= 4) {
			InfoMessageContinue(" ... -> ");
			break;
		}
        }
        InfoMessageContinue("EMPTY\n");
}

SUBFLOW_MSG * CONNECTIONS::CopyFromMsgBufToUna(int toPollPos, SUBFLOW_MSG * pMsg) {
	SUBFLOW_MSG * pMsgUnacked = subflowOutput.pMsgUnacked[toPollPos];
        SUBFLOW_MSG * & pMsgUnaHead = subflowOutput.pMsgUnaHead[toPollPos];
        SUBFLOW_MSG * & pMsgUnaTail = subflowOutput.pMsgUnaTail[toPollPos];
	SUBFLOW_MSG * & pMsgTimestamp = subflowOutput.pMsgTimestamp[toPollPos];
	SUBFLOW_MSG * returnP = subflowOutput.pMsgUnaTail[toPollPos];	

        int & unaSize = subflowOutput.unaSize[toPollPos];
	int & unaBytes = subflowOutput.unaBytes[toPollPos];

	const SUBFLOW_MSG * pMsgUnaEnd = pMsgUnacked + PROXY_SETTINGS::subflowBufMsgCapacity;
	
	if (pMsg->bReinjected) {
		ErrorMessage("Reinjected packet should be put into the unacked queue: connID=%u, seq=%u.", pMsg->connID, pMsg->seq);
		MyAssert(3 == 2, 2354);
#ifdef DEBUG_REINJECTION
		InfoMessage("pMsg (%u, %u, %u) reinjected, should not put into Una %d", pMsg->connID, pMsg->seq, pMsg->tcpSeq, toPollPos);
#endif
		return NULL;
	}
	// Unacked queue full
	if (pMsgUnaTail == pMsgUnaHead) {
		ErrorMessage("Subflow %d Unacked queue full!", toPollPos);
		return NULL;
	}

	if (subflowOutput.lastUnaSeq[toPollPos] >= pMsg->subflowSeq) {
		// May still happen due the limit of SEQ bits
		InfoMessage("### Warning ###: lastUnaSeq (%u) -> (%u)",
			subflowOutput.lastUnaSeq[toPollPos], pMsg->subflowSeq);
		InfoMessage("Subflow %d info: msg=%d, una=%d", toPollPos, subflowOutput.msgBufSize[toPollPos], subflowOutput.unaSize[toPollPos]);
		//PrintUnaQueueSimple(toPollPos);
	} else {
		//InfoMessage("... Normal Subflow %d info: %d", toPollPos, subflowOutput.msgBufSize[toPollPos]);
	}
	subflowOutput.lastUnaSeq[toPollPos] = pMsg->subflowSeq;
	pMsgUnaTail->CopyDataFrom(pMsg);

	if (pMsgUnaHead == NULL) {
		pMsgUnaHead = pMsgUnaTail;
		pMsgTimestamp = pMsgUnaHead;
	}
        unaSize++;
	unaBytes += pMsg->bytesOnWire;
        pMsgUnaTail++;
        if (pMsgUnaTail >= pMsgUnaEnd) pMsgUnaTail -= PROXY_SETTINGS::subflowBufMsgCapacity;   	
	return returnP;
}

#endif

// Only called once for each outgoing packets on subflows
void CONNECTIONS::GetTCPSeqForUna(SUBFLOW_MSG * pCurrMsg, int nBytesWritten) {
	int subflowNo = pCurrMsg->schedDecision;
	static int disabled = 0;
	uint64_t timestamp = get_current_microsecond();
    //MyAssert(subflowNo > 0, 6767);

	if (pCurrMsg->bytesLeft[subflowNo] == 0) {
		pCurrMsg->txTime[subflowNo] = timestamp;
	}

	if (pCurrMsg->transportSeq[subflowNo] > 0) {
		/*
		InfoMessage("Packet already with transportSeq!! subflowNo=%u, connID=%u, seq=%u, tcpSeq=%u, bytesOnWire=%d, left=%d",
			subflowNo, pCurrMsg->connID, pCurrMsg->seq, pCurrMsg->transportSeq[subflowNo],
			pCurrMsg->bytesOnWire, pCurrMsg->bytesLeft);
			*/
        if (nBytesWritten > 0) {
        	tmp_bif1 = kernelInfo.GetInFlightSize(1);
        	tmp_bif2 = kernelInfo.GetInFlightSize(2);
        	tmp_owd1 = kernelInfo.owdMapping.mapping[1].GetOWD(tmp_bif1);
        	tmp_owd2 = kernelInfo.owdMapping.mapping[2].GetOWD(tmp_bif2);
        	tmp_var1 = kernelInfo.owdMapping.mapping[1].GetVar();
        	tmp_var2 = kernelInfo.owdMapping.mapping[2].GetVar();
        fprintf(ofsDebug, "%d\t%u\t%u\t%u\t%u\t%d\t%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%f\t%d\t%f\t%f\n", subflowNo,
                pCurrMsg->connID, pCurrMsg->seq, pCurrMsg->bytesOnWire,
                pCurrMsg->transportSeq[subflowNo], pCurrMsg->bytesLeft[subflowNo],
                nBytesWritten, timestamp, 
                kernelInfo.GetSRTT(1), kernelInfo.GetSRTT(2),
                kernelInfo.GetTCPAvailableSpace(1),
                kernelInfo.GetTCPAvailableSpace(2),
                tmp_bif1, tmp_bif2,
                kernelInfo.owdMapping.mapping[1].isOWD, tmp_owd1, tmp_var1,
                kernelInfo.owdMapping.mapping[2].isOWD, tmp_owd2, tmp_var2);
        }
		return;
	}

	unsigned long long r = 0;

	if (subflowOutput.transportSeq[subflowNo] == 0) {
		unsigned long long param = ((unsigned long long)subflowNo << 48) + 
			(((unsigned long long)pCurrMsg->connID) << 32) + ((unsigned long long)pCurrMsg->seq);
		ioctl(kfd, CMAT_IOCTL_SET_SEQ_MAPPING, param);
		ioctl(kfd, CMAT_IOCTL_GET_SEQ_MAPPING, &r);
		InfoMessage("Get mapping: subflowNo=%u, connID=%u, seq=%u", subflowNo, pCurrMsg->connID, pCurrMsg->seq);

		if (r == (unsigned long long)1 << 48) {
			//InfoMessage("Mapping not found: (%d, %u, %u). Use cache: %u", subflowNo, pCurrMsg->connID, pCurrMsg->seq, subflowOutput.tcpSeq[subflowNo]);
			pCurrMsg->transportSeq[subflowNo] = subflowOutput.transportSeq[subflowNo];
			pCurrMsg->bValidTSeq[subflowNo] = 1;
			subflowOutput.transportSeq[subflowNo] = (DWORD)(
				((unsigned long long)(subflowOutput.transportSeq[subflowNo]) + 
					pCurrMsg->bytesOnWire) % ((unsigned long long)1 << 32));
			return;
		}

		pCurrMsg->transportSeq[subflowNo] = (DWORD)r;
		pCurrMsg->bValidTSeq[subflowNo] = 1;

		if (subflowOutput.transportSeq[subflowNo] != pCurrMsg->transportSeq[subflowNo]) {
			InfoMessage("Update subflow %d tcpSeq: %u -> %u (%u, %u)", subflowNo,
				subflowOutput.transportSeq[subflowNo], pCurrMsg->transportSeq[subflowNo], pCurrMsg->connID, pCurrMsg->seq);
		} else {
			//InfoMessage("Update correct!");
		}
		subflowOutput.transportSeq[subflowNo] = (DWORD)((r + pCurrMsg->bytesOnWire) % ((unsigned long long)1 << 32));
		InfoMessage("Update next: %u", subflowOutput.transportSeq[subflowNo]);
	} else {
		if (!disabled) {
            int all = 1;
            for (int i = 1; i <= PROXY_SETTINGS::nTCPSubflows; i++) {
                if (subflowOutput.transportSeq[i] == 0) {
                    all = 0;
                    break;
                }
            }
			if (all > 0) {
				ioctl(kfd, CMAT_IOCTL_SET_DISABLE_MAPPING, 0);
				InfoMessage("Disable kernel mapping");
				disabled = 1;
			}
		}
		pCurrMsg->transportSeq[subflowNo] = subflowOutput.transportSeq[subflowNo];
		pCurrMsg->bValidTSeq[subflowNo] = 1;
		int tmp = 0;
		if (subflowOutput.transportSeq[subflowNo] > 4294960000) {
			tmp = 1;
			InfoMessage("Update: %u", subflowOutput.transportSeq[subflowNo]);
		}
		subflowOutput.transportSeq[subflowNo] = (DWORD)(
				((unsigned long long)(subflowOutput.transportSeq[subflowNo]) + 
					pCurrMsg->bytesOnWire) % ((unsigned long long)1 << 32));
		if (tmp)
			InfoMessage("After update: %u", subflowOutput.transportSeq[subflowNo]);
	}
    if (nBytesWritten > 0) {
    	tmp_bif1 = kernelInfo.GetInFlightSize(1);
        	tmp_bif2 = kernelInfo.GetInFlightSize(2);
        	tmp_owd1 = kernelInfo.owdMapping.mapping[1].GetOWD(tmp_bif1);
        	tmp_owd2 = kernelInfo.owdMapping.mapping[2].GetOWD(tmp_bif2);
        	tmp_var1 = kernelInfo.owdMapping.mapping[1].GetVar();
        	tmp_var2 = kernelInfo.owdMapping.mapping[2].GetVar();
        fprintf(ofsDebug, "%d\t%u\t%u\t%u\t%u\t%d\t%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%f\t%d\t%f\t%f\n", subflowNo,
                pCurrMsg->connID, pCurrMsg->seq, pCurrMsg->bytesOnWire,
                pCurrMsg->transportSeq[subflowNo], pCurrMsg->bytesLeft[subflowNo],
                nBytesWritten, timestamp, 
                kernelInfo.GetSRTT(1), kernelInfo.GetSRTT(2),
                kernelInfo.GetTCPAvailableSpace(1),
                kernelInfo.GetTCPAvailableSpace(2),
                tmp_bif1, tmp_bif2,
                kernelInfo.owdMapping.mapping[1].isOWD, tmp_owd1, tmp_var1,
                kernelInfo.owdMapping.mapping[2].isOWD, tmp_owd2, tmp_var2);
                //kernelInfo.GetOWD(1, tmp_bif1, tmp_bif2));
    }
    metaBuffer.AddMsgToMapping(pCurrMsg);
}

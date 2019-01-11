#include "stdafx.h"

#include "kernel_info.h"
#include "meta_buffer.h"
#include "tools.h"
#include "scheduler.h"

extern int proxyMode;
extern struct SUBFLOWS subflows;
extern struct CONNECTIONS conns;
extern struct BUFFER_SUBFLOWS subflowOutput;
extern struct BUFFER_TCP tcpOutput;
extern struct META_BUFFER metaBuffer;
extern struct DELAYED_FINS delayedFins;
extern struct SCHEDULER scheduler;
extern struct KERNEL_INFO kernelInfo;

extern int tickCount;
extern int lastSubflowActivityTick;

extern FILE * ofsDebug;

void META_BUFFER::Setup() {

	memset(pMetaMsgBuffer, 0, sizeof(pMetaMsgBuffer));
	memset(pMetaMsgHead, 0, sizeof(pMetaMsgHead));
	memset(pMetaMsgTail, 0, sizeof(pMetaMsgTail));
	memset(pMetaMsgEnd, 0, sizeof(pMetaMsgEnd));
	memset(pSubMsgHead, 0, sizeof(pSubMsgHead));
	memset(metaMsgSize, 0, sizeof(metaMsgSize));

	memset(pMetaDataBuffer, 0, sizeof(pMetaDataBuffer));
	memset(pMetaDataHead, 0, sizeof(pMetaDataHead));
	memset(pMetaDataTail, 0, sizeof(pMetaDataTail));
	memset(pMetaDataEnd, 0, sizeof(pMetaDataEnd));
	memset(metaDataSize, 0, sizeof(metaDataSize));
	memset(untransSize, 0, sizeof(untransSize));

    memset(pMappingBuffer, 0, sizeof(pMappingBuffer));
    memset(pMappingHead, 0, sizeof(pMappingHead));
    memset(pMappingTail, 0, sizeof(pMappingTail));
    memset(pMappingEnd, 0, sizeof(pMappingEnd));

    memset(pPartial, 0, sizeof(pPartial));
    memset(bPartial, 0, sizeof(bPartial));

	for (int i = 0; i < MAX_PRIORITY; i++) {
		pMetaMsgBuffer[i] = new SUBFLOW_MSG[PROXY_SETTINGS::metaBufMsgCapacity];
		pMetaDataBuffer[i] = new BYTE[PROXY_SETTINGS::metaBufDataCapacity];
		MyAssert(pMetaMsgBuffer[i] != NULL && pMetaDataBuffer[i] != NULL, 7000);

		pMetaMsgHead[i] = NULL; pMetaMsgTail[i] = pMetaMsgBuffer[i];
		pMetaMsgEnd[i] = pMetaMsgBuffer[i] + PROXY_SETTINGS::metaBufMsgCapacity;

		pMetaDataHead[i] = NULL; pMetaDataTail[i] = pMetaDataBuffer[i];
		pMetaDataEnd[i] = pMetaDataBuffer[i] + PROXY_SETTINGS::metaBufDataCapacity;
		
		for (int j = 0; j < MAX_SUBFLOWS; j++) {
			pSubMsgHead[i][j] = NULL;
		}
	}

    memset(pMappingSize, 0, sizeof(pMappingSize));

    for (int i = 0; i < 6; i++) {
        pMappingBuffer[i] = new SUBFLOW_MSG_MAPPING[PROXY_SETTINGS::metaBufMsgCapacity * 2];
        pMappingHead[i] = NULL; pMappingTail[i] = pMappingBuffer[i];
        pMappingEnd[i] = pMappingBuffer[i] + PROXY_SETTINGS::metaBufMsgCapacity * 2;
    }
}

void META_BUFFER::CheckACK() {
    int subflowNo = 0;
    for (int i = 0; i < MAX_PRIORITY; i++) {
        SUBFLOW_MSG * iter = pMetaMsgHead[i];
        if (iter == NULL) continue;
        if (iter != pMetaMsgTail[i]) {
            if (iter->schedDecision > 0) {
                if (iter->transportSeq[iter->schedDecision] + iter->bytesOnWire <= subflowOutput.transportACK[iter->schedDecision] && iter->bSubflowAcked == 0) {
                    InfoMessage("Error ACK:");
                    iter->Print();
                }
            }
            if (iter->oldDecision > 0) {
                subflowNo = iter->oldDecision;
                if (iter->transportSeq[subflowNo] + iter->bytesOnWire <= subflowOutput.transportACK[subflowNo] && iter->bSubflowAcked == 0) {
                    InfoMessage("Error ACK:");
                    iter->Print();
                }
            }
        }
        IncrementMsgPointer(iter, i);
    }
}

int META_BUFFER::hasPartial() {
    for (int i = 0; i < MAX_SUBFLOWS; i++) {
        if (bPartial[i] > 0) return 1;
    }
    return 0;
}

void META_BUFFER::IncrementDataPointer(BYTE * & p, int delta, int prio) {
	p += delta;
	if (p >= pMetaDataEnd[prio]) p -= PROXY_SETTINGS::metaBufDataCapacity;
}

BYTE * META_BUFFER::GetIncrementDataPointer(BYTE * p, int delta, int prio) {
	BYTE * r = p + delta;
	if (r >= pMetaDataEnd[prio]) r -= PROXY_SETTINGS::metaBufDataCapacity;
        return r;
}

void META_BUFFER::IncrementMsgPointer(SUBFLOW_MSG * & p, int prio) {
	p++;
	if (p >= pMetaMsgEnd[prio]) p -= PROXY_SETTINGS::metaBufMsgCapacity;
}

void META_BUFFER::DecrementMsgPointer(SUBFLOW_MSG * & p, int prio) {
	p--;
	if (p < pMetaMsgBuffer[prio]) p += PROXY_SETTINGS::metaBufMsgCapacity;
}

SUBFLOW_MSG * META_BUFFER::GetDecrementMsgPointer(SUBFLOW_MSG * p, int prio) {
	SUBFLOW_MSG * r = p - 1;
	if (r < pMetaMsgBuffer[prio]) r += PROXY_SETTINGS::metaBufMsgCapacity;
	return r;
}

int META_BUFFER::ConvertMsgPointerToIndex(SUBFLOW_MSG * p) {
	SUBFLOW_MSG * head = pMetaMsgHead[p->priority];
	if (p >= head) {
		//InfoMessage("p >= head: %u %u", p, head);
		return (p - head);
	}
	//InfoMessage("p < head: %u %u", p, head);
	return PROXY_SETTINGS::metaBufMsgCapacity - (head - p);
}

void META_BUFFER::UpdateTail(int prio) {
	if (pMetaDataHead[prio] == NULL) pMetaDataHead[prio] = pMetaDataTail[prio];
	metaDataSize[prio] += pMetaMsgTail[prio]->bytesOnWire;
	IncrementDataPointer(pMetaDataTail[prio], pMetaMsgTail[prio]->bytesOnWire, prio);

	if (pMetaMsgHead[prio] == NULL) pMetaMsgHead[prio] = pMetaMsgTail[prio];
	metaMsgSize[prio] += 1;
	IncrementMsgPointer(pMetaMsgTail[prio], prio);
}

void META_BUFFER::AddMsgToMapping(SUBFLOW_MSG * msg) {
    int subflowNo = msg->schedDecision;
    pMappingTail[subflowNo]->msg = msg;
    pMappingTail[subflowNo]->tcpSeq = msg->transportSeq[subflowNo];
    /*
    fprintf(ofsDebug, "map\t%d\t%u\t%u\t%u\t%d\t%d\t%u\t%d\n",
            subflowNo, msg->connID, msg->seq, msg->transportSeq[subflowNo],
            msg->bytesLeft[subflowNo],
            msg->oldDecision, msg->transportSeq[msg->oldDecision],
            msg->bytesLeft[msg->oldDecision]);
            */
    if (pMappingSize[subflowNo] >= PROXY_SETTINGS::metaBufMsgCapacity * 2) {
        InfoMessage("!!!!!!!!!!!!!!!!! pMapping Full: %d, %d msgs !!!!!!!!!!!!!!!!", subflowNo, pMappingSize[subflowNo]);
    }
    if (pMappingHead[subflowNo] == NULL) pMappingHead[subflowNo] = pMappingTail[subflowNo];
    IncrementMappingPointer(pMappingTail[subflowNo], subflowNo);
    pMappingSize[subflowNo] += 1;
}

void META_BUFFER::IncrementMappingPointer(SUBFLOW_MSG_MAPPING * & p, int subflowNo) {
    p++;
    if (p >= pMappingEnd[subflowNo]) p -= PROXY_SETTINGS::metaBufMsgCapacity * 2;
}
/*
void BUFFER_META::Increment(int prio) {
	if (pMetaDataHead[prio] == NULL) pMetaDataHead[prio] = pMetaDataTail[prio];
	metaMsgSize[prio] += 1;
}
*/
int META_BUFFER::GetPriority(int connID, int bControl) {
	// Control messages -> 0
	// Data messages according to the scheduler
	if (bControl) return 0;

	CONN_INFO ci = conns.connTab[connID];

	switch (ci.hints.sched) {
	case SUBFLOW_SELECTION_TWOWAY:
    case SUBFLOW_SELECTION_TWOWAY_NAIVE:
    case SUBFLOW_SELECTION_TWOWAY_BALANCE:
    case SUBFLOW_SELECTION_NEWTXDELAY:
    case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
	case SUBFLOW_SELECTION_MINRTT_KERNEL:
	case SUBFLOW_SELECTION_EMPTCP:
	case SUBFLOW_SELECTION_WIFIONLY:
	case SUBFLOW_SELECTION_BTONLY:
	case SUBFLOW_SELECTION_ROUNDROBIN:
	case SUBFLOW_SELECTION_REMP:
		return scheduler.getSchedNormalQueue(ci.hints.sched);
	case SUBFLOW_SELECTION_DEFAULT:
		return scheduler.getSchedNormalQueue(scheduler.defaultScheduler);
	default:
		MyAssert(0, 8999);
	}
	return -1;
}

/*
int META_BUFFER::Enqueue(SUBFLOW_MSG * msg, int prio) {
	// Scheduling decision here
	return 0;
}

SUBFLOW_MSG * META_BUFFER::Dequeue(int subflowNo, int prio) {
	return NULL;
}
*/
void META_BUFFER::RescheduleAll() {
}

void META_BUFFER::PrintMetaBuffer(int count) {
	int size;
	for (int i = 0; i < MAX_PRIORITY; i++) {
		size = GetSize(i);
		if (size > 0) {
			printf("Queue %d (%d B): ", i, size);
			PrintMetaBuffer(i, count);
		}
	}
}

void META_BUFFER::PrintMetaBuffer(int prio, int count) {
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
	int i = 0;
	if (count < 0) count = 8000000;
	if (iter == NULL) return;
	printf("[Meta prio:%d] ", prio);
	while (iter != pMetaMsgTail[prio]) {
		if (i >= count) {
			printf("...\n");
			break;
		}
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			printf("[%d]sched=%d;old=%d;trans=%d(%d);acked=%d;conn=%u;seq=%u;tcpSeq=%u(%u) ",
				i, iter->schedDecision, iter->oldDecision, iter->bTransmitted[iter->schedDecision],
                iter->bTransmitted[iter->oldDecision], 
                iter->bSubflowAcked, iter->connID, iter->seq, iter->transportSeq[iter->schedDecision],
                iter->transportSeq[iter->oldDecision]);
		}
		IncrementMsgPointer(iter, prio);
		i++;
	}
	printf("\n");
}

int META_BUFFER::HasPackets() {
	return HasPackets(-1, -1);
}

int META_BUFFER::HasPackets(int start, int end) {
	if (start < 0) start = 0;
	if (end < 0) end = MAX_PRIORITY - 1;

	for (int i = start; i <= end; i++) {
		if (metaMsgSize[i] > 0)
			return 1;
	}
	return 0;
}

SUBFLOW_MSG * META_BUFFER::GetMsgByIndex(int prio, int index) {
	if (index >= metaMsgSize[prio]) return NULL;
	SUBFLOW_MSG * r = pMetaMsgHead[prio];
	r += index;
	if (r >= pMetaMsgEnd[prio]) r -= PROXY_SETTINGS::metaBufMsgCapacity;
	return r;
}

int META_BUFFER::ComputeMsgDataSize(int prio, int start, int end) {
	SUBFLOW_MSG * iter = GetMsgByIndex(prio, start), * iter_end = GetMsgByIndex(prio, end);
	int size = 0;
	while (iter != iter_end) {
		if (iter->isTransmitted() == 0)
			size += iter->bytesOnWire;
        //else if (iter->bSubflowAcked == 0) {
        //    subflowOutput.UpdateACKStatus(iter);
        //}
		IncrementMsgPointer(iter, prio);
	}
	return size;
}

SUBFLOW_MSG * META_BUFFER::GetNextUntransmittedMsg(int schedDecision, int queueNo) {
	return GetNextUntransmittedMsg(schedDecision, queueNo, queueNo);
}

SUBFLOW_MSG * META_BUFFER::GetNextUntransmittedMsg(int schedDecision, int startQueue, int endQueue) {
	for (int i = startQueue; i <= endQueue; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& iter->isTransmitted() == 0
                && iter->schedDecision == 0) {
				iter->schedDecision = schedDecision;
                if (iter->bytesLeft[schedDecision] == 0) {
                    iter->bytesLeft[schedDecision] = iter->bytesOnWire;
                }
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}

SUBFLOW_MSG * META_BUFFER::GetNextRempMsg(int schedDecision, int startQueue, int endQueue) {
	for (int i = startQueue; i <= endQueue; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& iter->isTransmitted(schedDecision) == 0) {
				if (iter->schedDecision == 0) {
					iter->schedDecision = schedDecision;
				} else {
					iter->oldDecision = iter->schedDecision;
					iter->schedDecision = schedDecision;
				}
                if (iter->bytesLeft[schedDecision] == 0) {
                    iter->bytesLeft[schedDecision] = iter->bytesOnWire;
                }
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}

/*
SUBFLOW_MSG * META_BUFFER::GetNextUntransmittedMsg(int schedDecision) {
	for (int i = 0; i < MAX_PRIORITY; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& iter->bTransmitted == 0) {
				iter->schedDecision = schedDecision;
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}*/
//subflowOutput.UpdateACKStatus(iter);
SUBFLOW_MSG * META_BUFFER::GetNextMsgNew(int schedDecision) {
	for (int i = 1; i < MAX_PRIORITY; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
				iter->schedDecision = schedDecision;
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}

// return 1: can transmit
// return 0: cannot transmit
int META_BUFFER::CanTransmit(SUBFLOW_MSG * msg, int otherSubflowNo, int direction) {
    SUBFLOW_MSG * iter = msg;
    int space = kernelInfo.GetTCPAvailableSpace(otherSubflowNo);
    int prio = msg->priority, size = 0;
    // direction 0: head->tail, 1: tail->head
    if (direction == 0) {
        while (iter != pMetaMsgTail[prio]) {
            if (iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            } else {
                break;
            }
            if (size > space) return 1;
            IncrementMsgPointer(iter, prio);
        }
    } else {
        while (iter != GetDecrementMsgPointer(pMetaMsgHead[prio], prio)) {
            if (iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            } else {
                break;
            }
            if (size > space) return 1;
            DecrementMsgPointer(iter, prio);
        }
    }

    if (size > space) return 1; 
    return 0;
}

// For TwoWay
// return 1: can transmit
// return 0: cannot transmit
int META_BUFFER::CanLargePathTransmit(int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
            int otherSubflowNo, int direction) {
	if (msg->bTransmitted[otherSubflowNo] > 0) return 0;

	SUBFLOW_MSG * iter = msg;
	double owd = 0.0;
	int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
	if (saveBytes == 2) {
		owd = kernelInfo.GetOWD(otherSubflowNo, bif1, bif2);
	}
	else if (saveBytes == 1) {
		owd = kernelInfo.GetOWD(otherSubflowNo, bif1, bif2);
		owd -= kernelInfo.GetOWDVar();
	}
	if (owd < 0.0) owd = 0.0;
    int space = kernelInfo.GetTCPAvailableSpace(otherSubflowNo) + (int)(
    	kernelInfo.GetBW(otherSubflowNo) * owd / 8000.0);
    int prio = msg->priority, size = 0;

    if (direction == 0) {
        while (iter != pMetaMsgTail[prio]) {
            if (iter->bTransmitted[schedDecision] == 0
                && iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            }/* else {
                break;
            }*/
            if (size > space) return 1;
            IncrementMsgPointer(iter, prio);
        }
    } else {
        while (iter != GetDecrementMsgPointer(pMetaMsgHead[prio], prio)) {
            if (iter->bTransmitted[schedDecision] == 0
                && iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            }/* else {
                break;
            }*/
            if (size > space) return 1;
            DecrementMsgPointer(iter, prio);
        }
    }

    if (size > space) return 1; 
    return 0;
}

int META_BUFFER::CanSmallPathTransmit(
	int saveBytes, SUBFLOW_MSG * msg, int schedDecision, int otherSubflowNo, uint64_t currt) {
	if (msg->bTransmitted[otherSubflowNo] == 0) return 1;

	double delta = (double)(currt - msg->txTime[otherSubflowNo]) / 1000.0;
	// reinject
	if (saveBytes == 1) {
		int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
		double owd = kernelInfo.GetOWD(schedDecision, bif1, bif2);
		owd += kernelInfo.GetOWDVar();
		if (delta <= owd) return 1;
		return 0;
	}

	// only balance
	if (saveBytes == 2) {
		//if (delta <= kernelInfo.GetOWD()) return 1;
		return 0;
	}

	MyAssert(0, 9031);
	return 1;
}

// return 1: can transmit
// return 0: cannot transmit
int META_BUFFER::CheckDelay(SUBFLOW_MSG* msg, int schedDecision, int other, uint64_t currt) {
    MyAssert(msg->txTime[other] > 0, 3848);
    int srtt1 = kernelInfo.GetSRTT(schedDecision);
    int srtt2 = kernelInfo.GetSRTT(other);
    int delta = (int64_t)currt - (int64_t)msg->txTime[other];
    //InfoMessage("srtt: %d %d, %d", srtt1, srtt2, delta);
    if (delta > srtt2 - srtt1) {
        return 0;
    } else {
        return 1;
    }
}

int META_BUFFER::BalanceTransfer(
	int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
	int otherSubflowNo, int isSmallOWD, int direction, uint64_t currt) {

	// saveBytes 0: no balance consider
	if (saveBytes == 0) {
		return 1;
	}
	// saveBytes 1/2: balance transfer
	if (saveBytes == 1 || saveBytes == 2) {
		if (saveBytes == 1) {
            /*
			if (schedDecision == 1) {
				return 1;
			}*/
		}
		if (isSmallOWD == 1) {
			return CanSmallPathTransmit(saveBytes, msg, schedDecision, otherSubflowNo, currt);
		} else {
			return CanLargePathTransmit(saveBytes, msg, schedDecision, otherSubflowNo, direction);
		}
	}
	MyAssert(0, 9032);
	return 1;
}

// return 1: can transmit
// return 0: cannot transmit
// direction: 0: GetFirstUnackedMsg, 1: GetLastUnackedMsg
int META_BUFFER::ControlRedundantTransfer(
	int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
	int otherSubflowNo, int isSmallOWD, int direction, uint64_t currt) {

	// saveBytes 0: reinject any bytes unacknowledged
	if (saveBytes == 0) {
		return 1;
	}
	// saveBytes 2: only balance bytes of subflows, no reinjection
	if (saveBytes == 2) {
		return 0;
	}
	
	// saveBytes 1: reinject bytes based on OWD
	if (saveBytes == 1) {
		// WiFi can anyway transmit
		if (schedDecision == 1) {
			return 1;
		}
		if (isSmallOWD == 1) {
			return CanSmallPathTransmit(saveBytes, msg, schedDecision, otherSubflowNo, currt);
		}
		return 0;
	}
	MyAssert(0, 9033);
	return 1;
}

SUBFLOW_MSG * META_BUFFER::GetFirstUnackedMsg(
	int prio, int schedDecision, int other, int isSmallOWD, int saveBytes, uint64_t currt) {
	//InfoMessage("TwoWay %d", saveBytes);
    //int other = (schedDecision == 1)? 2: 1;
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
    SUBFLOW_MSG * trans_msg = NULL;
    SUBFLOW_MSG * reinj_msg = NULL;
	if (iter == NULL) return NULL;
	while (iter != pMetaMsgTail[prio]) {
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			//if (iter->bSubflowAcked == 0) {
			//	subflowOutput.UpdateACKStatus(iter);
			//}
			if (iter->bTransmitted[schedDecision] > 0) {
				IncrementMsgPointer(iter, prio);
				continue;
			}
            if (iter->bSubflowAcked == 1 && iter->bytesLeft[schedDecision] == 0) {
                IncrementMsgPointer(iter, prio);
                continue;
            }
            // Transmit second time
            MyAssert(iter->bTransmitted[schedDecision] == 0, 9503);

            if (iter->schedDecision == 0) {
            	// new msg
            	if (BalanceTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 0, currt) > 0) {
            		//fprintf(ofsDebug, "Balance: allow\n");
            		trans_msg = iter;
            	} else {
            		//fprintf(ofsDebug, "Balance: do not allow\n");
            	}
            	break;
            } else {
            	// reinject msg
            	// meta-buffer not full: do reinject
            	if (metaBuffer.metaDataSize[prio] < PROXY_SETTINGS::metaBufDataCapacity - 16000) {
	            	if (reinj_msg == NULL) {
	            		if (ControlRedundantTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 0, currt) > 0) {
	            			reinj_msg = iter;
	            			//fprintf(ofsDebug, "Reinject: allow\n");
	            		} else {
	            			//fprintf(ofsDebug, "Reinject: do not allow\n");
	            		}
	            		//break;
	            		//InfoMessage("reinject option %p %dB iter=%p ******************",
	            		//	reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            	} else {
	            		if (reinj_msg->txTime[other] < iter->txTime[other]) {
	            			reinj_msg = iter;
	            			//InfoMessage("reinject update %p %dB iter=%p ******************",
	            			//	reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            		}
	            	}
	            }
            }

		}
		IncrementMsgPointer(iter, prio);
	}

	if (trans_msg == NULL) {
		trans_msg = reinj_msg;
		//InfoMessage("reinject %p %p %dB ******************",
		//	trans_msg, reinj_msg, metaBuffer.metaDataSize[prio]);
	} else {
		//InfoMessage("*********");
	}

	if (trans_msg != NULL) {
		if (trans_msg->schedDecision != schedDecision) {
            if (trans_msg->oldDecision == 0) {
                trans_msg->oldDecision = trans_msg->schedDecision;
                trans_msg->schedDecision = schedDecision;
                trans_msg->bytesLeft[schedDecision] = trans_msg->bytesOnWire;
            } else {
                if (trans_msg->oldDecision == schedDecision) {
                    trans_msg->oldDecision = trans_msg->schedDecision;
                    trans_msg->schedDecision = schedDecision;
                } else {
                    MyAssert(0, 9504);
                }
            }
            //return trans_msg;
        } else {
            //return trans_msg;
        }
    }
	return trans_msg;
}

SUBFLOW_MSG * META_BUFFER::GetLastUnackedMsg(
	int prio, int schedDecision, int other, int isSmallOWD, int saveBytes, uint64_t currt) {
	//InfoMessage("TwoWay %d", saveBytes);
	if (pMetaMsgHead[prio] == NULL) return NULL;
	SUBFLOW_MSG * iter = GetDecrementMsgPointer(pMetaMsgTail[prio], prio);
    SUBFLOW_MSG * trans_msg = NULL;
    SUBFLOW_MSG * reinj_msg = NULL;
	while (iter != GetDecrementMsgPointer(pMetaMsgHead[prio], prio)) {
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			//if (iter->bSubflowAcked == 0) {
			//	subflowOutput.UpdateACKStatus(iter);
			//}
			if (iter->bTransmitted[schedDecision] > 0) {
				DecrementMsgPointer(iter, prio);
				continue;
			}
            if (iter->bSubflowAcked == 1 && iter->bytesLeft[schedDecision] == 0) {
                DecrementMsgPointer(iter, prio);
                continue;
            }
// Transmit second time
            
            MyAssert(iter->bTransmitted[schedDecision] == 0, 9506);
            /*
            if (saveBytes > 0) {
                if (isSmallRTT == 0 && CanTransmit(iter, other, 1) == 0) {
                    return NULL;
                }
                if (isSmallRTT == 1 && iter->bTransmitted[other] > 0 && CheckDelay(iter, schedDecision, other, currt) == 0) {
                    return NULL;
                }
            }
            */
            if (iter->schedDecision == 0) {
            	// new msg
            	if (BalanceTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 1, currt) > 0) {
            		//fprintf(ofsDebug, "Balance: allow\n");
            		trans_msg = iter;
            	} else {
            		//fprintf(ofsDebug, "Balance: do not allow\n");
            	}
            	break;
            } else {
            	// reinject msg
            	// meta-buffer not full: do reinject
            	if (metaBuffer.metaDataSize[prio] < PROXY_SETTINGS::metaBufDataCapacity - 16000) {
	            	if (reinj_msg == NULL) {
	            		if (ControlRedundantTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 1, currt) > 0) {
	            			reinj_msg = iter;
	            			//fprintf(ofsDebug, "Reinject: allow\n");
	            		} else {
	            			//fprintf(ofsDebug, "Reinject: do not allow\n");
	            		}
	            		//break;
	            		//InfoMessage("reinject option %p %dB iter=%p ******************",
	            		//	reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            	} else {
	            		if (reinj_msg->txTime[other] < iter->txTime[other]) {
	            			reinj_msg = iter;
	            		//	InfoMessage("reinject update %p %dB iter=%p ******************",
	            		//		reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            		}
	            	}
	            }
            }
		}
		DecrementMsgPointer(iter, prio);
	}
    
    if (trans_msg == NULL) {
		trans_msg = reinj_msg;
		//InfoMessage("reinject %p %p %dB ******************",
		//	trans_msg, reinj_msg, metaBuffer.metaDataSize[prio]);
	} else {
		//InfoMessage("*********");
	}

    if (trans_msg != NULL) {
		if (trans_msg->schedDecision != schedDecision) {
            if (trans_msg->oldDecision == 0) {
                trans_msg->oldDecision = trans_msg->schedDecision;
                trans_msg->schedDecision = schedDecision;
                trans_msg->bytesLeft[schedDecision] = trans_msg->bytesOnWire;
            } else {
                if (trans_msg->oldDecision == schedDecision) {
                    trans_msg->oldDecision = trans_msg->schedDecision;
                    trans_msg->schedDecision = schedDecision;
                } else {
                    MyAssert(0, 9505);
                }
            }
            //return trans_msg;
        } else {
            //return trans_msg;
        }
    }
    return trans_msg;
}

// Assume use by TxDelay+
SUBFLOW_MSG * META_BUFFER::GetMsgAfterSize(int prio, int schedDecision, int size) {
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
	SUBFLOW_MSG * msg = NULL;
	if (iter == NULL) return NULL;

	int data = 0, after = 0;
	while (iter != pMetaMsgTail[prio]) {
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
			&& iter->isTransmitted() == 0) {
			if (iter->schedDecision == 0) {
				data += iter->bytesOnWire;
				msg = iter;
				after = 0;
			} else {
				if (iter->schedDecision != schedDecision) {
					data += iter->bytesLeft[iter->schedDecision];
					after += iter->bytesLeft[iter->schedDecision];
				}
			}
			if (data > size) {
				if (iter->schedDecision == 0) {
					iter->schedDecision = schedDecision;
					iter->bytesLeft[schedDecision] = iter->bytesOnWire;
					untransSize[prio] = -1;
					return iter;
				}
			}
		}
		IncrementMsgPointer(iter, prio);
	}
	untransSize[prio] = data - after;
	return msg;
}

unsigned int META_BUFFER::GetSize(int prio) {
	int size = 0;
	if (prio < MAX_PRIORITY) {
        /*
		SUBFLOW_MSG * iter = pMetaMsgHead[prio];
		//InfoMessage("GetNextMsg: i: %d", i);
		while (iter && iter != pMetaMsgTail[prio]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
				size2 += iter->bytesOnWire;
			}
			IncrementMsgPointer(iter, prio);
		}

		if (size2 != metaDataSize[prio])
			InfoMessage("Size doesn't match: %d != %d", metaDataSize[prio], size2);
        */
		return metaDataSize[prio];
	}
	
	for (int i = 0; i < MAX_PRIORITY; i++) {
        
		size += metaDataSize[i];
		
        /*
        SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
				size2 += iter->bytesOnWire;
			}
			IncrementMsgPointer(iter, i);
		}
        */
	}
    /*
	if (size != size2) {
		InfoMessage("Size doesn't match: %d != %d", size, size2);
	}
    */
	return size;
}

int META_BUFFER::TransmitFromMetaBuffer(SUBFLOW_MSG * msg, int bReinject) {
	// Transfer already encoded subflow message on the subflow.

	//int nMsgs = msgBufSize[toPollPos];

/*
	if (msg->pMsgStart != msg->pMsgData && msg->oldDecision > 0) {
		InfoMessage("!!! Send content next (start=%u p=%u delta=%u connID=%u seq=%u sched=%d old=%d "
			"prio=%d",
			msg->pMsgStart, msg->pMsgData, msg->pMsgData - msg->pMsgStart,  msg->connID, msg->seq,
			msg->schedDecision, msg->oldDecision, msg->priority);
	}
    */
	int subflowNo = msg->schedDecision;
	int prio = msg->priority;
	int toFD = conns.peers[subflowNo].fd;

	int subflowType = subflows.GetSubflowType(toFD);
	int udpSubflowID = subflows.GetUDPSubflowID(toFD);
/*
    InfoMessage("Header: %x%x%x%x%x%x%x%x", 
            msg->pMsgData[subflowNo][0], msg->pMsgData[subflowNo][1],
            msg->pMsgData[subflowNo][2], msg->pMsgData[subflowNo][3],
            msg->pMsgData[subflowNo][4], msg->pMsgData[subflowNo][5],
            msg->pMsgData[subflowNo][6], msg->pMsgData[subflowNo][7]);
*/
#if TRANS==1 || TRANS==3
	//if (subflowType == SUBFLOWS::TCP_SUBFLOW) {
		static const BYTE zeros[4096] = {0};	//place holder for late binding, always 0
	//}
#endif

	int nBytesWritten = 0;

	//int tmp;
	// tmp change
	int dBufSize = metaBuffer.metaDataSize[prio];

	/*
	BYTE * & pDHead = pDataHead[toPollPos];
	BYTE * & pDTail = pDataTail[toPollPos];
	BYTE * pDBuffer = pDataBuffer[toPollPos];
	int & dBufSize = dataBufSize[toPollPos];
	const BYTE * pDataEnd = pDBuffer + PROXY_SETTINGS::subflowBufDataCapacity;

	SUBFLOW_MSG * & pMHead = pMsgHead[toPollPos];
	SUBFLOW_MSG * & pMTail = pMsgTail[toPollPos];	
	SUBFLOW_MSG * & pMBuffer = pMsgBuffer[toPollPos];
	int & mBufSize = msgBufSize[toPollPos];
	const SUBFLOW_MSG * pMsgEnd = pMBuffer + PROXY_SETTINGS::subflowBufMsgCapacity;
	*/

	/*
	VerboseMessage("pDHead=%8X pDTail=%8X pMHead=%8X pMTail=%8X pMTail->pMsgData=%8X pMTail->bytesLeft=%d",
		pDHead,pDTail,pMHead,pMTail,pMTail->pMsgData,pMTail->bytesLeft
	);
	*/
/*
	if (bReinject)
		MyAssert(pMHead != NULL && pMTail != NULL, 1656);
	else
		MyAssert((pMHead==NULL) == (pDHead==NULL) && pMTail != NULL && pDTail != NULL, 1655);
	if (pMHead == NULL) {
		return nBytesWritten;
	}

	if (!bReinject)
		MyAssert(pMHead->pMsgData == pDHead, 1647);
*/

/*
	MyAssert(pMTail == pMHead + mBufSize || pMTail == pMHead + mBufSize - PROXY_SETTINGS::subflowBufMsgCapacity, 1652);
	if (!bReinject)
		MyAssert(pDTail == pDHead + dBufSize || pDTail == pDHead + dBufSize - PROXY_SETTINGS::subflowBufDataCapacity, 1653);
*/

	// w1: bytes to the end of circular buffer
	int w1, w2;
	//while (mBufSize > 0 /*pMHead < pMTail*/) {
		w1 = msg->bytesLeft[subflowNo];
		//if (!bReinject)
		//	MyAssert(w1 > 0 && pMHead->pMsgData >= pDBuffer && pMHead->pMsgData < pDataEnd, 1648);

		/*
		InfoMessage("*** To Write %d bytes to subflow %d ***", w1, toPollPos);
		*/
				
		if (PROXY_SETTINGS::bLateBindingRP) {			
			w2 = 0;
		} else {
			if (bReinject) {
				w2 = 0;
			} else {
				// consider circular buffer
				w2 = msg->pMsgData[subflowNo] + w1 - metaBuffer.pMetaDataEnd[prio];
				if (w2 > 0) w1 -= w2; else w2 = 0;
			}
		}
		int w = 0;
		switch (subflowType) {
//#if TRANS == 3	//write to subflow
//		int w = sctp_sendmsg(toFD, pMHead->pMsgData, w1, 
//			NULL, 0, 0, 0, pMHead->connID % PROXY_SETTINGS::sctpMaxNumofStreams, 0, 0);
//#elif TRANS == 1
		case SUBFLOWS::TCP_SUBFLOW:
			if (PROXY_SETTINGS::bLateBindingRP) {
				#define EXTRA_BYTES 8
				MyAssert(w1 > 8, 2034);
				w1 += EXTRA_BYTES;		//11/6/14 for late binding we need additional 8 bytes
				w = write(toFD, zeros, w1);
				MyAssert(w == w1 || w < 0, 2032);
				w -= EXTRA_BYTES;
				w1 -= EXTRA_BYTES;
			} else {
				w = write(toFD, msg->pMsgData[subflowNo], w1);
			}
			break;
//#elif TRANS == 2
		case SUBFLOWS::UDP_SUBFLOW:
		//for UDP, we want to make sure the entire subflow message is written once and fit into a single UDP packet
			MyAssert(subflows.udpPeerAddrLen[udpSubflowID] > 0, 2200);
		
			if (w2 > 0) {			
				static BYTE udpBuf[4096];
                // FIXME: here pointer index is wrong for UDP.
				memcpy(udpBuf, msg->pMsgData[udpSubflowID], w1);
				memcpy(udpBuf + w1, metaBuffer.pMetaDataBuffer[prio], w2);
				MyAssert(w1 + w2 < (int)sizeof(udpBuf), 2202);
				w = sendto(toFD, udpBuf, w1+w2, 0, (const struct sockaddr *)&(subflows.udpPeerAddr[udpSubflowID]), subflows.udpPeerAddrLen[udpSubflowID]);
				w1 += w2;
				w2 = 0;
			} else {
				w = sendto(toFD, msg->pMsgData[udpSubflowID], w1, 0, (const struct sockaddr *)&(subflows.udpPeerAddr[udpSubflowID]), subflows.udpPeerAddrLen[udpSubflowID]);
			}
			break;
//#endif
		default:
			break;
		}

#ifdef FEATURE_DUMP_SUBFLOW_MSG
		subflows.DumpSubflowMsg(pMHead->pMsgData[subflowNo], w1);
#endif

#ifdef DEBUG_REINJECT
		InfoMessage("w = %d, w1 = %d, w2 = %d", w, w1, w2);
#endif
		if (w > 0) {
			lastSubflowActivityTick = tickCount;
			nBytesWritten += PROXY_SETTINGS::bLateBindingRP ? w+8 : w;

			conns.connTab[msg->connID].bytesInSubflowBuffer -= w;

			/*
			int rr;
			if (FindRequest(rr, pMHead->pMsgData, w)) {
				DebugMessage("### Request %d: SUBFLOW->SOCKET ###", rr);
			} else if (FindResponse(rr, pMHead->pMsgData, w)) {
				DebugMessage("||| Response %d: SUBFLOW->SOCKET |||", rr);
			}
			DebugMessage("%d bytes to Subflow Socket %d", w, toPollPos);
			*/

			/*
			if (PROXY_SETTINGS::bDumpIO) {
				CONNECTIONS::DumpIO(SUBFLOWBUFFER_2_SUBFLOWSOCKET, w, toPollPos, pMHead->connID, pMHead->seq);
			}
			*/
		}
		
		if (w == w1 && w2 == 0) {	//when late binding is used, this is the only non-error path				
			msg->bytesLeft[subflowNo] = 0;
			dBufSize -= w;

			VerboseMessage("Write %d bytes from TCP %s to subflow %d. Full subflow message written (seq=%u)",
				w, conns.DumpConnection(msg->connID), subflowNo, msg->seq
			);

#ifdef FEATURE_DUMP_SUBFLOW_MSG
			subflows.DumpSubflowMsg_subflowID(toPollPos);
#endif

			goto NEXT_MSG;
		} else if (w == w1 && w2 > 0) {
			// A case where the message encounters the circular wrap
			MyAssert(!PROXY_SETTINGS::bLateBindingRP, 2023);
			msg->pMsgData[subflowNo] = metaBuffer.pMetaDataBuffer[prio]; 
			msg->bytesLeft[subflowNo] -= w;
			dBufSize -= w; 

			VerboseMessage("Write %d bytes from TCP %s to subflow %d.",
				w, conns.DumpConnection(msg->connID), subflowNo
			);

			switch (subflowType) {
//#if TRANS == 3	//write to subflow
//			w = sctp_sendmsg(toFD, pDBuffer, w2, 
//				NULL, 0, 0, 0, pMHead->connID % PROXY_SETTINGS::sctpMaxNumofStreams, 0, 0);
//#elif TRANS == 1
			case SUBFLOWS::TCP_SUBFLOW:
				w = write(toFD, metaBuffer.pMetaDataBuffer[prio], w2);
				break;
//#elif TRANS == 2
			case SUBFLOWS::UDP_SUBFLOW:
				MyAssert(0, 2201);
				break;
			/*
			MyAssert(udpPeerAddrLen > 0, 2201);
			w = sendto(toFD, pDBuffer, w2, 0, (const struct sockaddr *)&udpPeerAddr, udpPeerAddrLen);
			*/
//#endif
			default:
				break;
			}

#ifdef FEATURE_DUMP_SUBFLOW_MSG
			subflows.DumpSubflowMsg(metaBuffer.pMetaDataBuffer[prio], w2);
#endif
			if (w > 0) {
				lastSubflowActivityTick = tickCount;
				nBytesWritten += w;

				/*
				int rr;
				if (FindRequest(rr, pDBuffer, w)) {
					DebugMessage("### Request %d: SUBFLOW->SOCKET ###", rr);
				} else if (FindResponse(rr, pDBuffer, w)) {
					DebugMessage("||| Response %d: SUBFLOW->SOCKET |||", rr);
				}
				DebugMessage("%d bytes to Subflow Socket %d", w, toPollPos);
				*/

				/*
				if (PROXY_SETTINGS::bDumpIO) {
					CONNECTIONS::DumpIO(SUBFLOWBUFFER_2_SUBFLOWSOCKET, w, toPollPos, pMHead->connID, pMHead->seq);
				}
				*/
			}

			if (w == w2) {
				dBufSize -= w;
				msg->bytesLeft[subflowNo] = 0;

				VerboseMessage("Write %d bytes from TCP %s to subflow %d. Full subflow message written (seq=%u)",
					w, conns.DumpConnection(msg->connID), subflowNo, msg->seq
				);

#ifdef FEATURE_DUMP_SUBFLOW_MSG
				subflows.DumpSubflowMsg_subflowID(subflowNo);
#endif

				goto NEXT_MSG;
			} else if (w >= 0 && w < w2) {
				msg->pMsgData[subflowNo] += w;
				msg->bytesLeft[subflowNo] -= w;
                
				dBufSize -= w;

				VerboseMessage("Write %d bytes from TCP %s to subflow %d.",
					w, conns.DumpConnection(msg->connID), subflowNo
				);

				goto FINISH;
			} else if (w < 0 && errno == EWOULDBLOCK) {
				//WarningMessage("Socket buffer full for subflow# %d", subflowNo);
				goto FINISH;
			} else {
				ErrorMessage("Error writing to subflow: %s(%d) Subflow #%d", 
					strerror(errno), errno, (int)subflowNo					
				);
				MyAssert(0, 1650);
			}
		} else if (w >= 0 && w < w1) {
			MyAssert(!PROXY_SETTINGS::bLateBindingRP, 2036);
			msg->pMsgData[subflowNo] += w;
			msg->bytesLeft[subflowNo] -= w;
			dBufSize -= w;//??

			if (w == 0) InfoMessage("No bytes written to subflow %d", subflowNo);
			VerboseMessage("Write %d bytes from TCP %s to subflow %d.",
				w, conns.DumpConnection(msg->connID), subflowNo
			);

			if (subflowType == SUBFLOWS::UDP_SUBFLOW) {
//#if TRANS==2
				MyAssert(0, 2203);
//#endif
			}

			goto FINISH;
		} else if (w < 0 && errno == EWOULDBLOCK) {	
			//WarningMessage
			//InfoMessage("Socket buffer full for subflow# %d", subflowNo);
			goto FINISH;
		} else {
			ErrorMessage("Error writing to subflow #%d: %s(%d)", 
				(int)subflowNo, strerror(errno), errno
			);
			MyAssert(0, 1649);
		}

		NEXT_MSG:
#ifdef DEBUG_REINJECT
		InfoMessage(";;;; Sent out message [connID=%u, seq=%u, len=%d, reinject=%d] on subflow %d", pMHead->connID, pMHead->seq, pMHead->payloadLen, bReinject, toPollPos);
#endif
		// Add timestamp to Una Queue
#ifdef REINJECTION
		SUBFLOW_MSG * & pMsgTs = pMsgTimestamp[toPollPos];
		SUBFLOW_MSG * pMsgUnaEnd = pMsgUnacked[toPollPos] + PROXY_SETTINGS::subflowBufMsgCapacity;
		SUBFLOW_MSG * & pCurr = pRnjCurr[toPollPos];
		SUBFLOW_MSG * pCurrEnd = pMsgReinject[toPollPos] + PROXY_SETTINGS::subflowBufMsgCapacity;
		// keep track of SEQ of reinjected packets
		unsigned long currT = GetHighResTimestamp();
		if (bReinject) {
			/*
			if (pCurr)
				InfoMessage("pCurr: %u (%u[%u %u] <-> %u[%u %u])", pCurr, pMHead->subflowSeq, pMHead->connID, pMHead->seq, pCurr->subflowSeq, pCurr->connID, pCurr->seq);
			else
				InfoMessage("pCurr: NULL");
			*/
#ifdef DEBUG_UNA
			//InfoMessage("#### Reinject packet out %u[%u] on subflow %d at %lu.", pCurr->subflowSeq, tcpSeq[toPollPos], toPollPos, currT);
#endif
			MyAssert(pCurr != NULL && pMHead->connID == pCurr->connID && pMHead->seq == pCurr->seq && pMHead->subflowSeq == pCurr->subflowSeq, 4857);
			pCurr->tcpSeq = tcpSeq[toPollPos];
			pCurr->bRnjAcked = 0;
			pCurr++;
			if (pCurr >= pCurrEnd) pCurr -= PROXY_SETTINGS::subflowBufMsgCapacity;
			if (pCurr == pMsgRnjTail[toPollPos]) pCurr = NULL;
		}
		if (bReinject || pMHead->msgType != SUBFLOW_MSG::MSG_DATA) {
			//InfoMessage("Increment subflowNo %d tcpSeq: (reinject=%d, %d, %d)", toPollPos, bReinject, pMHead->msgType, pMHead->bytesOnWire);
			tcpSeq[toPollPos] = (DWORD)(
			((unsigned long long) tcpSeq[toPollPos] + pMHead->bytesOnWire) %
			((unsigned long long) 1 << 32));
		} else {
			//InfoMessage("pMsgTs: %u, %u, %u", pMsgTs, pMsgTs == NULL? 0: pMsgTs->connID, pMsgTs == NULL? 0: pMsgTs->seq);
			MyAssert(pMsgTs != NULL
				&& pMsgTs->seq == pMHead->seq
				&& pMsgTs->connID == pMHead->connID, 3045);
			pMsgTs->timestamp = GetHighResTimestamp();
			pMsgTs++;
			if (pMsgTs >= pMsgUnaEnd) pMsgTs -= PROXY_SETTINGS::subflowBufMsgCapacity;
		}
		if (pMHead->msgType == SUBFLOW_MSG::MSG_CLOSE ||
			pMHead->msgType == SUBFLOW_MSG::MSG_CLOSE_FIN ||
			pMHead->msgType == SUBFLOW_MSG::MSG_CLOSE_RST) {
                	InfoMessage("Block time: %lu s %lu ms (%lu). Reinjection: %dB", tBlock / 1000000, (tBlock - tBlock/1000000) / 1000, tBlock, rnjData);
			tBlock = 0;
			rnjData = 0;
		}
#endif

		//if (++pMHead == pMsgEnd) pMHead = pMBuffer;
		//mBufSize--;

		//if (nBytesWritten >= PROXY_SETTINGS::subflowTransferUnit) break;
	//}

	FINISH:

	if (msg->bytesLeft[subflowNo] == 0) {
		msg->bTransmitted[subflowNo] = 1;
		//if (msg->pMsgStart != msg->pMsgData) {
			//InfoMessage("*** Mismatch after Transmitted (start=%u p=%u delta=%u connID=%u seq=%u sched=%d old=%d "
			//"prio=%d",
			//	msg->pMsgStart, msg->pMsgData, msg->pMsgData - msg->pMsgStart,  msg->connID, msg->seq,
			//	msg->schedDecision, msg->oldDecision, msg->priority);
		//	msg->pMsgData = msg->pMsgStart;			
		//}
        bPartial[subflowNo] = 0;
        pPartial[subflowNo] = NULL;
        conns.GetTCPSeqForUna(msg, nBytesWritten);
	} else {
        //InfoMessage("Bytes not all transmitted.");
        //msg->Print();
        /*
        if (msg->bytesLeft[subflowNo] == msg->bytesOnWire) {
            msg->ResetSched(subflowNo);
            bPartial[subflowNo] = 0;
            pPartial[subflowNo] = NULL;
        } else {*/
            pPartial[subflowNo] = msg;
            bPartial[subflowNo] = 1;
            conns.GetTCPSeqForUna(msg, nBytesWritten);
        //}
    }
    /*
	conns.GetTCPSeqForUna(msg, nBytesWritten);
	if (msg->transportSeq[subflowNo] == 0) {
		InfoMessage("Problematic msg: valid=%d connID=%d seq=%d", msg->bValidTSeq[subflowNo], msg->connID, msg->seq);
		PrintMetaBuffer(1, -1);
	}
    */
	//UpdateAfterTransmit(prio);

	//MyAssert((metaBuffer.metaMsgSize[prio] == 0) == 
	//	(pMHead == pMTail), 1758);


	// old cleaning
	/*
	if (mBufSize == 0) {
		conns.EnableSubflowWriteNotification(toPollPos, 0);
		ResetSubflow(toPollPos);
		if (bReinject)
			subflowOutput.bActiveReinject[toPollPos] = 0;
		else
			MyAssert(subflowOutput.bActiveReinject[toPollPos] == 0, 9010);
	} else {
		conns.EnableSubflowWriteNotification(toPollPos, 1);
		if (bReinject) {
			subflowOutput.bActiveReinject[toPollPos] = 1;
		} else {
			MyAssert(subflowOutput.bActiveReinject[toPollPos] == 0, 9010);
			pDHead = pMHead->pMsgData;
			MyAssert(pDTail == pDHead + dBufSize || pDTail == pDHead + dBufSize - PROXY_SETTINGS::subflowBufDataCapacity, 1654);
		}
	}
	*/
	////////////////////////////// 08/07/2014 ////////////////////////////// 
	//more free space in subflowOutput? If so, try to enable TCP read ntf
	/*
	if (!subflows.AllSubflowsFull()) {
		conns.EnableTCPReadNotifications(1);
	}
	*/
	////////////////////////////////////////////////////////////////////////

	return nBytesWritten;
}

void META_BUFFER::UpdateAfterTransmit(int prio) {
	SUBFLOW_MSG * & iter = pMetaMsgHead[prio];
    //SUBFLOW_MSG * iter = pMetaMsgHead[prio];
	if (iter == NULL) return;
	while (iter != pMetaMsgTail[prio]) {
		//InfoMessage("head %d end %d: %d", pMetaMsgHead[prio], pMetaMsgEnd[prio], iter);
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			// Currently transmitted packets are removed from meta buffer
			// TODO: keep unACKed packets in meta buffer
			if (iter->isTransmitted()) { /*} && 
				(prio == 0 || (prio == 1 && iter->oldDecision > 0))) {*/
				//if (iter->bSubflowAcked == 0) {
			//		subflowOutput.UpdateACKStatus(iter);
                    /*
                    if (iter == pMetaMsgHead[prio]) {
                        InfoMessage("Update ACK subflow1:%u subflow2:%u.",
                                subflowOutput.transportSeq[1], subflowOutput.transportSeq[2]);
                        iter->Print();
                    }
                    */
				//}
				if (iter->bSubflowAcked) {
                    /*
					if (pMetaDataHead[prio] != iter->pMsgStart) {
						InfoMessage("Msg [%d]: %u != %u", ConvertMsgPointerToIndex(iter),
							pMetaDataHead[prio], iter->pMsgData);
						InfoMessage("Data head: %u, tail: %u, end: %u",
							pMetaDataHead[prio], pMetaDataTail[prio], pMetaDataEnd[prio]);
					}*/
					//MyAssert(iter->bTransmitted, 9566);
					MyAssert(pMetaDataHead[prio] == iter->pMsgStart, 9565);
                    //if (iter == pMetaMsgHead[prio]) {
					metaDataSize[prio] -= iter->bytesOnWire;
					metaMsgSize[prio] -= 1;
					IncrementDataPointer(pMetaDataHead[prio], iter->bytesOnWire, prio);
                    //}
					IncrementMsgPointer(iter, prio);
                    //if (iter == pMetaMsgHead[prio])
                    //    IncrementMsgPointer(pMetaMsgHead[prio], prio);
				} else {
					/*
					printf("[msg not acked]sched=%d;old=%d;trans=%d;acked=%d;conn=%u;seq=%u;tcpSeq=%u(%u) ",
									iter->schedDecision, iter->oldDecision, iter->bTransmitted,
					                iter->bSubflowAcked, iter->connID, iter->seq, iter->transportSeq[iter->schedDecision],
					                iter->transportSeq[iter->oldDecision]);
					                */
                    //IncrementMsgPointer(iter, prio);
					break;
				}
			} else {
                //IncrementMsgPointer(iter, prio);
				break;
			}
		} else {
            //IncrementMsgPointer(iter, prio);
			break;
		}
	}
	if (pMetaMsgHead[prio] == pMetaMsgTail[prio]) {
		pMetaMsgHead[prio] = NULL;
		pMetaDataHead[prio] = NULL;
		MyAssert(metaMsgSize[prio] == 0 && metaDataSize[prio] == 0, 9200);

	}
}

void META_BUFFER::UpdateAfterTransmit() {
	//InfoMessage("Remove ACKed packets");
	for (int i = 0; i < MAX_PRIORITY; i++) {
		UpdateAfterTransmit(i);
	}
}


void META_BUFFER::MarkACKedMsg(int subflowNo) {
    SUBFLOW_MSG_MAPPING * & iter = pMappingHead[subflowNo];
    if (iter == NULL) return;
    while (iter != pMappingTail[subflowNo]) {
        /*
        if (iter->msg->transportSeq[subflowNo] == 0) {
            InfoMessage("Problematic mapping: %u %u",
                    iter->tcpSeq, iter->msg->transportSeq[subflowNo]);
            iter->msg->Print();
        }*/

        //MyAssert(iter->msg->transportSeq[subflowNo] > 0, 6769);
        if (iter->tcpSeq != iter->msg->transportSeq[subflowNo]) {
            IncrementMappingPointer(iter, subflowNo);
            pMappingSize[subflowNo] -= 1;
            continue;
        }
        int flag = 0;
        if (iter->msg->bTransmitted[subflowNo] > 0) {
            flag = subflowOutput.UpdateSubflowACKStatus(iter->msg, subflowNo);
        }
        if (flag == 1) {
            IncrementMappingPointer(iter, subflowNo);
            pMappingSize[subflowNo] -= 1;
        } else {
            break;
        }
    }
    if (iter == pMappingTail[subflowNo]) {
        iter = NULL;
        pMappingTail[subflowNo] = pMappingBuffer[subflowNo];
    }
}

void META_BUFFER::MarkACKedMsg() {
    for (int i = 0; i < 6; i++) {
        MarkACKedMsg(i);
    }
}

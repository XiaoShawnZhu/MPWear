#include "scheduler.h"
#include "kernel_info.h"
#include "meta_buffer.h"
#include "tools.h"
#include <math.h>
#include <sstream>
#include <algorithm>

extern struct KERNEL_INFO kernelInfo;
extern struct META_BUFFER metaBuffer;

extern int kfd;

extern FILE * ofsDebug;

#define DELAY_TOLERANCE 0.0 // ms

void SCHEDULER::Setup() {
	int tmp [] = {SUBFLOW_SELECTION_NEWTXDELAY, SUBFLOW_SELECTION_TWOWAY,
                SUBFLOW_SELECTION_TWOWAY_NAIVE, SUBFLOW_SELECTION_TWOWAY_BALANCE,
                SUBFLOW_SELECTION_NEWTXDELAY_OLD,
				SUBFLOW_SELECTION_MINRTT_KERNEL, SUBFLOW_SELECTION_EMPTCP,
				SUBFLOW_SELECTION_WIFIONLY, SUBFLOW_SELECTION_BTONLY,
                SUBFLOW_SELECTION_ROUNDROBIN, SUBFLOW_SELECTION_REMP
            };
	copy(tmp, tmp + SCHED_SEQ_NUM, schedulerSeq);

    nTCP = PROXY_SETTINGS::nTCPSubflows;
    if (nTCP == 2) {
        twoWayWiFi = 1;
        twoWayBT = 2;
    } else {
        twoWayWiFi = 3;
        twoWayBT = 4;
    }
	// [sched * 2] -> start
	// [sched * 2 + 1] -> end
    int tmp2 [] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	//int tmp2 [] = {5, 6, 3, 4, 13, 14, 15, 16, 17, 18, 1, 2, 7, 8, 9, 10, 11, 12};

    tmp2[SUBFLOW_SELECTION_NEWTXDELAY*2] = 5;
    tmp2[SUBFLOW_SELECTION_NEWTXDELAY*2+1] = 6;

    tmp2[SUBFLOW_SELECTION_TWOWAY*2] = 3;
    tmp2[SUBFLOW_SELECTION_TWOWAY*2+1] = 4;
    tmp2[SUBFLOW_SELECTION_TWOWAY_NAIVE*2] = 13;
    tmp2[SUBFLOW_SELECTION_TWOWAY_NAIVE*2+1] = 14;
    tmp2[SUBFLOW_SELECTION_TWOWAY_BALANCE*2] = 15;
    tmp2[SUBFLOW_SELECTION_TWOWAY_BALANCE*2+1] = 16;

    tmp2[SUBFLOW_SELECTION_NEWTXDELAY_OLD*2] = 17;
    tmp2[SUBFLOW_SELECTION_NEWTXDELAY_OLD*2+1] = 18;

    tmp2[SUBFLOW_SELECTION_MINRTT_KERNEL*2] = 1;
    tmp2[SUBFLOW_SELECTION_MINRTT_KERNEL*2+1] = 2;

    tmp2[SUBFLOW_SELECTION_EMPTCP*2] = 7;
    tmp2[SUBFLOW_SELECTION_EMPTCP*2+1] = 8;

    tmp2[SUBFLOW_SELECTION_WIFIONLY*2] = 9;
    tmp2[SUBFLOW_SELECTION_WIFIONLY*2+1] = 10;

    tmp2[SUBFLOW_SELECTION_BTONLY*2] = 11;
    tmp2[SUBFLOW_SELECTION_BTONLY*2+1] = 12;

    tmp2[SUBFLOW_SELECTION_ROUNDROBIN*2] = 19;
    tmp2[SUBFLOW_SELECTION_ROUNDROBIN*2+1] = 20;

    tmp2[SUBFLOW_SELECTION_REMP*2] = 21;
    tmp2[SUBFLOW_SELECTION_REMP*2+1] = 22;

	copy(tmp2, tmp2 + 16 * 2, schedulerQ);
	indexSeq = -1;

	defaultScheduler = PROXY_SETTINGS::subflowSelectionAlgorithm;

	for (int i = 0; i < MAX_SCHEDULER; i++) {
		switch (i) {
			case SUBFLOW_SELECTION_TWOWAY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_TwoWay;
				break;

			case SUBFLOW_SELECTION_NEWTXDELAY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_NewTxDelay;
				break;

            case SUBFLOW_SELECTION_TWOWAY_NAIVE:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_TwoWay_Naive;
                break;

            case SUBFLOW_SELECTION_TWOWAY_BALANCE:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_TwoWay_Balance;
                break;

            case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_NewTxDelay;
                break;

			case SUBFLOW_SELECTION_MINRTT_KERNEL:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_MinRTT_Kernel;
				break;

			case SUBFLOW_SELECTION_TXDELAY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_TxDelay;
				break;

			case SUBFLOW_SELECTION_WIFIONLY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_Wifi;
				break;

			case SUBFLOW_SELECTION_BTONLY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_BT;
				break;

            /*
			case SUBFLOW_SELECTION_BBS:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_BBS;
				break;

            case SUBFLOW_SELECTION_CBS:
                break;
            */

			case SUBFLOW_SELECTION_BBS_MRT:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_BBS_MRT;
				break;

			case SUBFLOW_SELECTION_EMPTCP:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_EMPTCP;
				break;

			case SUBFLOW_SELECTION_BLOCK:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_Block;
				break;

			case SUBFLOW_SELECTION_UDP:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_UDP;
				break;

            case SUBFLOW_SELECTION_ROUNDROBIN:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_RoundRobin;
                break;

            case SUBFLOW_SELECTION_REMP:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_ReMP;
                break;

			//default:
			//	MyAssert(0, 1979);
		}
	}
    pkt_delay = new double[PROXY_SETTINGS::metaBufMsgCapacity];
}

int SCHEDULER::schedHasPackets(int sched) {
	int start = schedulerQ[sched*2], end = schedulerQ[sched*2+1];
	return metaBuffer.HasPackets(start, end);
}

int SCHEDULER::getSchedNormalQueue(int sched) {
	return schedulerQ[sched*2+1];
}

int SCHEDULER::getSchedFirstQueue(int sched) {
	return schedulerQ[sched*2];
}

int SCHEDULER::getSchedLastQueue(int sched) {
	return schedulerQ[sched*2+1];
}

// Get the next scheduling algorithm to send packet
// currently use round robin based on schedulerSeq
int SCHEDULER::getNextSchedNo() {
	int start = indexSeq;
	if (start == -1) start = SCHED_SEQ_NUM - 1;
	indexSeq++;
	while (indexSeq != start) {
		if (schedHasPackets(schedulerSeq[indexSeq])) break;

		indexSeq++;
		if (indexSeq >= SCHED_SEQ_NUM) indexSeq = 0;
	}
	return schedulerSeq[indexSeq];
}

int SCHEDULER::getNextSchedNo2() {

    if (schedHasPackets(SUBFLOW_SELECTION_NEWTXDELAY)) return SUBFLOW_SELECTION_NEWTXDELAY;

    if (schedHasPackets(SUBFLOW_SELECTION_TWOWAY)) return SUBFLOW_SELECTION_TWOWAY;

    return SUBFLOW_SELECTION_TWOWAY;
}

const char * schedInfo(const char * s, int sched) {
    ostringstream output;
    output << s << " (" << sched << ")";
    return output.str().c_str();
}

const char * SCHEDULER::getSchedulerName(int sched) {
    switch (sched) {
        case SUBFLOW_SELECTION_TWOWAY:
            return schedInfo("TwoWay", sched);

        case SUBFLOW_SELECTION_NEWTXDELAY:
            return schedInfo("TxDelay++", sched);

        case SUBFLOW_SELECTION_TWOWAY_NAIVE:
            return schedInfo("TwoWay Naive", sched);

        case SUBFLOW_SELECTION_TWOWAY_BALANCE:
            return schedInfo("TwoWay Balance", sched);

        case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
            return schedInfo("TxDelay++ Old", sched);

        case SUBFLOW_SELECTION_MINRTT_KERNEL:
            return schedInfo("MinRTT", sched);

        case SUBFLOW_SELECTION_TXDELAY:
            return schedInfo("TxDelay (old)", sched);


        case SUBFLOW_SELECTION_WIFIONLY:
            return schedInfo("WiFi only", sched);

        case SUBFLOW_SELECTION_BTONLY:
            return schedInfo("BT only", sched);
        
        /*
        case SUBFLOW_SELECTION_BBS:
            return schedInfo("BBS", sched);

        case SUBFLOW_SELECTION_CBS:
            return schedInfo("CBS", sched);
            */

        case SUBFLOW_SELECTION_BBS_MRT:
            return schedInfo("BBS MRT", sched);

        case SUBFLOW_SELECTION_EMPTCP:
            return schedInfo("eMPTCP", sched);

        case SUBFLOW_SELECTION_BLOCK:
            return schedInfo("Block (no TX)", sched);
        
        case SUBFLOW_SELECTION_UDP:
            return schedInfo("UDP subflows", sched);

        case SUBFLOW_SELECTION_ROUNDROBIN:
            return schedInfo("Round Robin", sched);

        case SUBFLOW_SELECTION_REMP:
            return schedInfo("ReMP", sched);

        default:
            ostringstream output;
            output << "Default ["
                << getSchedulerName(PROXY_SETTINGS::subflowSelectionAlgorithm)
                << " (" << PROXY_SETTINGS::subflowSelectionAlgorithm << ")]";
            return output.str().c_str();
    }
}

SUBFLOW_MSG * SCHEDULER::GetMessageForSubflow(int subflowNo, int queueNo, int other, int isSmallRTT, int saveBytes, uint64_t currt) {
    SUBFLOW_MSG * msg;
    //static int flag = 0;
    if (metaBuffer.bPartial[subflowNo] > 0) {
        MyAssert(metaBuffer.pPartial[subflowNo] != NULL, 9800);
        metaBuffer.pPartial[subflowNo]->SwitchSched(subflowNo);
        return metaBuffer.pPartial[subflowNo];
    }

    if (subflowNo == twoWayWiFi) {
        msg = metaBuffer.GetFirstUnackedMsg(queueNo, subflowNo, other, isSmallRTT, saveBytes, currt);
    } else if (subflowNo == twoWayBT) {
        msg = metaBuffer.GetLastUnackedMsg(queueNo, subflowNo, other, isSmallRTT, saveBytes, currt);
    } else {
        MyAssert(0, 9888);
    }

    if (msg) {
        /*
        if (flag == 0 && msg->connID == 7) {
            kernelInfo.ChangeCC(1, TCP_CC_BBR);
            kernelInfo.ChangeCC(2, TCP_CC_BBR);
            flag = 1;
        }
        */
        return msg;
    } else {
        return NULL;
    }
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay() {
    return SelectSubflow_TwoWay_Base(1, SUBFLOW_SELECTION_TWOWAY);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay_Naive() {
    return SelectSubflow_TwoWay_Base(0, SUBFLOW_SELECTION_TWOWAY_NAIVE);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay_Balance() {
    return SelectSubflow_TwoWay_Base(2, SUBFLOW_SELECTION_TWOWAY_BALANCE);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay_Base(int saveBytes, int schedNo) {
	int space[5];//, rtt[5];
	int large_i, small_i, this_subflow;
    static int last_subflow = 0;
	SUBFLOW_MSG * msg;
	int queueNo = getSchedNormalQueue(schedNo);
    uint64_t currt = get_current_microsecond();

    //fprintf(ofsDebug, "Enter Decision\n");

    for (int i = 1; i <= nTCP; i++) {
        space[i] = kernelInfo.GetTCPAvailableSpace(i);
        //rtt[i] = kernelInfo.GetSRTT(i);
    }

    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
    double owd = kernelInfo.GetOWD(twoWayWiFi, bif1, bif2);
	if (owd > 0) { // rtt[twoWayWiFi] < rtt[twoWayBT]
		large_i = twoWayBT;
		small_i = twoWayWiFi;
	} else {
		large_i = twoWayWiFi;
		small_i = twoWayBT;
	}
	//InfoMessage("---TwoWay: Get Message.");

    //InfoMessage("--- Space: [1] %d [2] %d", space[1], space[2]);
    //metaBuffer.PrintMetaBuffer(1);

    if (last_subflow == 0) {
	    if (space[small_i] > 0) {
            //fprintf(ofsDebug, "a-path %d\n", small_i);
	        msg = GetMessageForSubflow(small_i, queueNo, large_i, 1, saveBytes, currt);
            if (msg) {
                last_subflow = small_i;
                return msg;
            }
        }
	    if (space[large_i] > 0) {
            //fprintf(ofsDebug, "b-path %d\n", large_i);
	        msg = GetMessageForSubflow(large_i, queueNo, small_i, 0, saveBytes, currt);
            if (msg) {
                last_subflow = large_i;
                return msg;
            }
        }
    } else {
        this_subflow = twoWayBT + twoWayWiFi - last_subflow;
        int this_small, last_small;
        if (this_subflow == small_i) {
            this_small = 1;
            last_small = 0;
        } else {
            last_small = 1;
            this_small = 0;
        }
        //fprintf(ofsDebug, "last: %d, this: %d", last_subflow, this_subflow);
        if (space[this_subflow] > 0) {
            msg = GetMessageForSubflow(this_subflow, queueNo, last_subflow, this_small, saveBytes, currt);
            //fprintf(ofsDebug, "c-path %d\n", this_subflow);
            if (msg) {
                last_subflow = this_subflow;
                return msg;
            }
        }
        if (space[last_subflow] > 0) {
            msg = GetMessageForSubflow(last_subflow, queueNo, this_subflow, last_small, saveBytes, currt);
            //fprintf(ofsDebug, "d-path %d\n", last_subflow);
            if (msg) return msg;
        } 
    }

    //InfoMessage("No i. msg: NULL");
    return NULL;
}

double EstimateAdditionalDelay(int rtt_us, unsigned int cwnd_pkt, unsigned int ssthresh_pkt,
    unsigned int mss_bytes, unsigned int buf_bytes) {
    if (buf_bytes <= cwnd_pkt * mss_bytes)
        return 0.0;

    return ((double)((int)buf_bytes - (int)cwnd_pkt * (int)mss_bytes)
        / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0;
}

// ms in delay
double EstimatePacketDelay(int rtt_us, unsigned int cwnd_pkt, unsigned int ssthresh_pkt,
    unsigned int mss_bytes, unsigned int buf_bytes) {
    if (buf_bytes <= cwnd_pkt * mss_bytes)
        return (double) rtt_us / 1000.0;
    /*
    if (cwnd_pkt < ssthresh_pkt) {
        // Slow start
        return (log2((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) + 1.0) * (double) rtt_us / 1000.0;
    }
    */
    /*
    InfoMessage("Est delay: %f ms (buf=%uB, cwnd=%u, mss=%uB, rtt=%dus)",
    		((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0,
			buf_bytes, cwnd_pkt, mss_bytes, rtt_us);
	*/
    return ((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0;
}

// search range: [left, right]
// adjust the "index" if the subflow message it points to has already been transmitted
// index1: the largest index so that index1 <= index and packet[index1] is not transmitted
// index2: the smallest index so that index2 >= index and packet[index2] is not transmitted
void AdjustIndex(int queueNo, int & index, int & index1, int & index2, int left, int right) {
    SUBFLOW_MSG * iter = metaBuffer.GetMsgByIndex(queueNo, index);
    SUBFLOW_MSG * iter2 = iter;

    if (iter->isTransmitted() == 0) {
        index1 = index;
        index2 = index;
        return;
    }

    index1 = index;
    index2 = index;
    while (iter != metaBuffer.pMetaMsgTail[queueNo] && iter->isTransmitted() > 0
        && index2 <= right) {
        index2++;
        metaBuffer.IncrementMsgPointer(iter, queueNo);
    }

    while (iter2 != metaBuffer.GetDecrementMsgPointer(metaBuffer.pMetaMsgHead[queueNo], queueNo)
        && iter2->isTransmitted() > 0 && index1 >= left) {
        index1--;
        metaBuffer.DecrementMsgPointer(iter2, queueNo);
    }

    if (index1 < left && index2 > right) {
        index = -1;
        index1 = -1;
        index2 = -1;
        return;
    }

    if (index1 < left) {
        index1 = index2;
        index = index2;
        return;
    }

    if (index2 > right) {
        index = index1;
        index2 = index1;
        return;
    }

    index = index1;
}

void SCHEDULER::printPktDelay() {
	//pkt_delay
	printf("PktDelay: ");
	for (int i = 0; i < PROXY_SETTINGS::metaBufMsgCapacity; i++) {
		if (pkt_delay[i] != 0) {
			printf("%d:%fms,", i, pkt_delay[i]);
		}
	}
	printf("\n");
}

void SCHEDULER::clearPktDelay() {
	for (int i = 0; i < PROXY_SETTINGS::metaBufMsgCapacity; i++) {
		pkt_delay[i] = 0;
	}
}

// delay in ms
int ComputeSizeBasedOnDelay(double small_delay, double large_delay, int subflowNo) {
	double r = 0;
	r = large_delay - small_delay;
	if (r <= 0) return 0;
	//r = r / ((double)small_rtt / 1000.0) * (double)small_cwnd;
    r = r * kernelInfo.GetBW(subflowNo) / 8000.0;
	return int(r);
}

SUBFLOW_MSG * SCHEDULER::SearchPacketForLargeRTTPath(int large_rtt_i, int small_rtt_i, int queueNo) {
	// TODO: consider different priority levels
    if (metaBuffer.metaMsgSize[queueNo] == 0) return NULL;

    /*
    //int space[3];
    int rtt[3];
    //unsigned int ssth[3];
    unsigned int cwnd[3], mss[3];


    for (int i = 1; i <= 2; i++) {
        //space[i] = kernelInfo.GetTCPAvailableSpace(i);
        rtt[i] = kernelInfo.GetSRTT(i);
        cwnd[i] = kernelInfo.GetSendCwnd(i);
        //ssth[i] = kernelInfo.GetSndSsthresh(i);
        mss[i] = kernelInfo.GetSndMss(i);
    }
    */

/*
    unsigned int small_rtt_bytes;

    int tmp = (
        (int)(cwnd[small_rtt_i]) * (int)(mss[small_rtt_i]) - space[small_rtt_i]);
    if (tmp <= 0) {
    	InfoMessage("******* Small RTT path (%d): %d cwnd=%d space=%d",
                small_rtt_i,  tmp,
                (int)cwnd[small_rtt_i] * (int)mss[small_rtt_i],
                space[small_rtt_i]);
    	small_rtt_bytes = 0;
    } else {
    	small_rtt_bytes = (unsigned int) tmp;
    }

    unsigned int large_rtt_bytes;

    tmp = (
        (int)(cwnd[large_rtt_i]) * (int)(mss[large_rtt_i]) - space[large_rtt_i]);
    if (tmp <= 0) {
    	//InfoMessage("******* Large RTT path (%d): %d", large_rtt_i,  tmp);
    	large_rtt_bytes = 0;
    } else {
    	large_rtt_bytes = (unsigned int) tmp;
    }

    //double small_rtt_base_delay, large_rtt_base_delay; 
    double small_add_delay, large_add_delay; 
    small_add_delay = EstimateAdditionalDelay(
            rtt[small_rtt_i], cwnd[small_rtt_i], ssth[small_rtt_i], mss[small_rtt_i],
            small_rtt_bytes);
    large_add_delay = EstimateAdditionalDelay(
            rtt[large_rtt_i], cwnd[large_rtt_i], ssth[large_rtt_i], mss[large_rtt_i],
            large_rtt_bytes);
*/
    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
    double owd = kernelInfo.GetOWD(small_rtt_i, bif1, bif2);
    int size = ComputeSizeBasedOnDelay(0.0, owd, small_rtt_i);
    SUBFLOW_MSG * msg = metaBuffer.GetMsgAfterSize(queueNo, large_rtt_i, size);
    if (msg == NULL) return NULL;
    if (metaBuffer.untransSize[queueNo] == -1) {
    	return msg;
    } else {
	    size = ComputeSizeBasedOnDelay(0.0, owd - DELAY_TOLERANCE, small_rtt_i);
	    if (size < metaBuffer.untransSize[queueNo])
	    	return msg;
	    else
	    	return NULL;
	}
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_NewTxDelayBase(int queueNo) {
	int space[3], j;

	for (int i = 1; i <= 2; i++) {
		space[i] = kernelInfo.GetTCPAvailableSpace(i);
	    //rtt[i] = kernelInfo.GetSRTT(i);
	}

	if (space[1] <= 0 && space[2] <= 0)
	    return NULL;

    double owd;
    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);

    // owd for twoWayWiFi
    //if (owd > 0) { // rtt[twoWayWiFi] < rtt[twoWayBT]

	for (int i = 1; i <= 2; i++) {
	    j = 3 - i;
	    if (space[i] > 0) {
            if (metaBuffer.bPartial[i] > 0) {
                MyAssert(metaBuffer.pPartial[i] != NULL, 9800);
                metaBuffer.pPartial[i]->SwitchSched(i);
                return metaBuffer.pPartial[i];
            }
            owd = kernelInfo.GetOWD(i, bif1, bif2);
	        if (owd > 0) {
	            return metaBuffer.GetNextUntransmittedMsg(i, queueNo);
	        } else if (space[j] <= 0) {
	            return SearchPacketForLargeRTTPath(i, j, queueNo);
	        }
	    }
	}

	return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_NewTxDelay() {
    int queueNo = getSchedNormalQueue(SUBFLOW_SELECTION_NEWTXDELAY);
    return SelectSubflow_NewTxDelayBase(queueNo);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_ControlMsg() {
	return SelectSubflow_NewTxDelayBase(0);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_Wifi() {
    int space = kernelInfo.GetTCPAvailableSpace(1);
    int start = getSchedFirstQueue(SUBFLOW_SELECTION_WIFIONLY),
    		end = getSchedLastQueue(SUBFLOW_SELECTION_WIFIONLY);

    if (space <= 0) return NULL;
    if (metaBuffer.bPartial[1] > 0) {
        MyAssert(metaBuffer.pPartial[1] != NULL, 9800);
        metaBuffer.pPartial[1]->SwitchSched(1);
        return metaBuffer.pPartial[1];
    }
    else return metaBuffer.GetNextUntransmittedMsg(1, start, end);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_BT() {
    int space = kernelInfo.GetTCPAvailableSpace(2);
    int start = getSchedFirstQueue(SUBFLOW_SELECTION_BTONLY),
        	end = getSchedLastQueue(SUBFLOW_SELECTION_BTONLY);

    if (space <= 0) return NULL;
    if (metaBuffer.bPartial[2] > 0) {                
        MyAssert(metaBuffer.pPartial[2] != NULL, 9800);
        metaBuffer.pPartial[2]->SwitchSched(2);
        return metaBuffer.pPartial[2];                            
    }
    else return metaBuffer.GetNextUntransmittedMsg(2, start, end);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_UDP() {
    // No congestion control here
    return metaBuffer.GetNextUntransmittedMsg(3, 15);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_EMPTCP() {
    //static WORD currID = 0;
    // if (currID != connID) {
    //    InfoMessage("Reset EMPTCP states. New connID: %d, old: %d.", connID, currID);
    //    ioctl(kfd, CMAT_IOCTL_SET_RESET_EMPTCP, 0);
    //    currID = connID;
    //}
    int r = -1;
    int start = getSchedFirstQueue(SUBFLOW_SELECTION_EMPTCP),
        		end = getSchedLastQueue(SUBFLOW_SELECTION_EMPTCP);
    
    ioctl(kfd, CMAT_IOCTL_GET_SCHED_EMPTCP, &r);
    if (r <= 0) return NULL;
    if (metaBuffer.bPartial[r] > 0) {                
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];                            
    }
    return metaBuffer.GetNextUntransmittedMsg(r, start, end);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_BBS() {
    //int buf1, buf2;
    int r = -1;
    unsigned int buf = metaBuffer.GetSize(MAX_PRIORITY);
    
    ioctl(kfd, CMAT_IOCTL_SET_META_BUFFER, buf);
    ioctl(kfd, CMAT_IOCTL_GET_SCHED_BUFFER, &r);

    //if (conns.connTab[connID].serverPort == 4001) {
    //    InfoMessage("Port 4001 traffic through #%d", r);
        //return 1;
    //}
    if (r <= 0) return NULL;
    if (r > 2) return metaBuffer.GetNextUntransmittedMsg(r-2, 15);
    return metaBuffer.GetNextUntransmittedMsg(r, 15);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_BBS_MRT() {
    int r = -1;
    static int state = 0;
    unsigned int buf = metaBuffer.GetSize(MAX_PRIORITY);

    ioctl(kfd, CMAT_IOCTL_SET_META_BUFFER, buf);
    ioctl(kfd, CMAT_IOCTL_GET_SCHED_BUFFER_MRT, &r);
    
    //if (conns.connTab[connID].serverPort == 4001) {
    //    InfoMessage("Port 4001 traffic through #%d", r);
    //    //return 1;
    //}

    if (r <= 0) return NULL;
    if (r > 2) {
        if (state == 0) {
            InfoMessage("To multipath state.");
            state = 1;
        }
        return metaBuffer.GetNextUntransmittedMsg(r-2, 15);
    }
    if (state == 1) {
        InfoMessage("To WiFi only state.");
        state = 0;
    }
    return metaBuffer.GetNextUntransmittedMsg(r, 15);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_Block() {
    unsigned int buf = metaBuffer.GetSize(MAX_PRIORITY);
    InfoMessage("Buffer size: %d", buf);
    return NULL;
}
SUBFLOW_MSG * SCHEDULER::SelectSubflow_TxDelay() {
    int buf1 = -1, buf2 = -1, rtt1 = -1, rtt2 = -1, delay1 = -1, delay2 = -1;
    double est1 = 0.0, est2 = 0.0;
    int r = -1;

    buf1 = kernelInfo.GetSndBuffer(1);    
    buf2 = kernelInfo.GetSndBuffer(2);

    if (buf1 >= PROXY_SETTINGS::subflowBufDataCapacity) goto DONE;
    if (buf2 >= PROXY_SETTINGS::subflowBufDataCapacity) goto DONE;

    rtt1 = kernelInfo.GetSRTT(1);
    rtt2 = kernelInfo.GetSRTT(2);

    delay1 = rtt1 * 100000 / (kernelInfo.GetSendCwnd(1) * kernelInfo.GetSndMss(1));
    delay2 = rtt2 * 100000 / (kernelInfo.GetSendCwnd(2) * kernelInfo.GetSndMss(2));;
    //ioctl(kfd, CMAT_IOCTL_GET_FD1_DELAY, &delay1);
    //ioctl(kfd, CMAT_IOCTL_GET_FD2_DELAY, &delay2);

    //ioctl(kfd, CMAT_IOCTL_GET_FD1_RTT, &rtt1);
    //ioctl(kfd, CMAT_IOCTL_GET_FD2_RTT, &rtt2);

    est1 = ((double) delay1) / 100000.0 * ((double) buf1) / 1000.0 + ((double) rtt1) / 1000.0;
    est2 = ((double) delay2) / 100000.0 * ((double) buf2) / 1000.0 + ((double) rtt2) / 1000.0;

    //InfoMessage("*** Est delay selection=%d  buf1=%d delay1=%d buf2=%d delay2=%d && est1=%f est2=%f  ***", r, buf1, delay1, buf2, delay2, est1, est2);
    if (est1 < est2) r = 1;
    else {
        if (rtt1 < rtt2) r = 1;
    	else r = 2;
    }
DONE:
	InfoMessage("*** Info: BUF1=%d EST1=%d BUF2=%d EST2=%d", buf1, int(est1), buf2, int(est2));
    InfoMessage("*** Selected=%d   RTT1=%d   RTT2=%d   ***", r, rtt1/1000, rtt2/1000);
    if (r != -1 && SUBFLOW_MONITOR::rtt[r-1] == 0) SUBFLOW_MONITOR::rtt[r-1] = 1;
    if (r <= 0) return NULL;
    return metaBuffer.GetNextUntransmittedMsg(r, 15);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_MinRTT_Kernel() {
    //InfoMessage("MinRTT");
    int r = -1;
    int space1 = kernelInfo.GetTCPAvailableSpace(1), space2 = kernelInfo.GetTCPAvailableSpace(2);
    /*
    InfoMessage("--- Min RTT: [1] %d B, %d us [2] %d B, %d us",
            space1, kernelInfo.GetSRTT(1), space2, kernelInfo.GetSRTT(2));
    */
/*
#ifndef VS_SIMULATION
    ioctl(kfd, CMAT_IOCTL_GET_SCHED, &r);
#endif
*/
    
    if (space1 <= 0 && space2 <= 0)
        return NULL;

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_MINRTT_KERNEL),
            end = getSchedLastQueue(SUBFLOW_SELECTION_MINRTT_KERNEL);

    int both = 0, other = 0;
    if (space1 <= 0) r = 2;
    else if (space2 <= 0) r = 1;
    else {
        both = 1;
        if (kernelInfo.GetSRTT(1) < kernelInfo.GetSRTT(2)) r = 1;
        else r = 2;
    }

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];
    }
    if (both == 1) {
        if (r == 1) other = 2;
        else if (r == 2) other = 1;
        if (metaBuffer.bPartial[other] > 0) {
            MyAssert(metaBuffer.pPartial[other] != NULL, 9800);
            metaBuffer.pPartial[other]->SwitchSched(other);
            return metaBuffer.pPartial[other];
        }
    }

    if (r == 1) {
        return metaBuffer.GetNextUntransmittedMsg(1, start, end);
    }

    if (r == 2) {
        return metaBuffer.GetNextUntransmittedMsg(2, start, end);
    }
    
    MyAssert(0, 2459);
    return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_RoundRobin() {
    static int k = 0;
    int r = -1;
    int space1 = kernelInfo.GetTCPAvailableSpace(1), space2 = kernelInfo.GetTCPAvailableSpace(2);

    if (space1 <= 0 && space2 <= 0)
        return NULL;

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_ROUNDROBIN),
            end = getSchedLastQueue(SUBFLOW_SELECTION_ROUNDROBIN);

    if (space1 <= 0) {
        r = 2;
        k = 2;
    }
    else if (space2 <= 0) {
        r = 1;
        k = 1;
    }
    else {
        /* Round robin when both paths are available */
        if (++k > 2) {
            k = 1;
        }
        r = k;
    }

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];
    }

    if (r == 1) {
        return metaBuffer.GetNextUntransmittedMsg(1, start, end);
    }

    if (r == 2) {
        return metaBuffer.GetNextUntransmittedMsg(2, start, end);
    }
    
    MyAssert(0, 2460);
    return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_ReMP() {
    static int k = 0;
    int r = -1;
    int space1 = kernelInfo.GetTCPAvailableSpace(1), space2 = kernelInfo.GetTCPAvailableSpace(2);

    if (space1 <= 0 && space2 <= 0)
        return NULL;

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_REMP),
            end = getSchedLastQueue(SUBFLOW_SELECTION_REMP);

    if (space1 <= 0) {
        r = 2;
        k = 2;
    }
    else if (space2 <= 0) {
        r = 1;
        k = 1;
    }
    else {
        /* Round robin when both paths are available */
        if (++k > 2) {
            k = 1;
        }
        r = k;
    }

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];
    }

    if (r == 1 || r == 2) {
        return metaBuffer.GetNextRempMsg(r, start, end);
    }
    
    MyAssert(0, 2461);
    return NULL;
}

/*

SUBFLOW_MSG * SCHEDULER::SelectSubflow_RoundRobinChunk() {
    CONN_INFO & c = conns.connTab[connID];

    int delay1, delay2;
        ioctl(kfd, CMAT_IOCTL_GET_FD1_DELAY, &delay1);
        ioctl(kfd, CMAT_IOCTL_GET_FD2_DELAY, &delay2);
    
    if (c.GetInUseStatus(tickCount) != CONN_INFO::CONN_INUSE) {
        return SelectSubflow_RoundRobin(connID);
    } else {
        int & p = c.lastSubflowID;        
        if (c.accuChunkBytes < PROXY_SETTINGS::chunkSizeThreshold && p != -1 && !subflowOutput.IsSubflowFull(p)) {
            return p;
        } else {
            p = SelectSubflow_RoundRobin(connID);
            c.accuChunkBytes = 0;
            return p;
        }
    }
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_Fixed() {
    if (subflowOutput.IsSubflowFull(1))
        return -1;
    else
        return 1;
}


SUBFLOW_MSG * SCHEDULER::SelectSubflow_Random() {
    //InfoMessage("*** random ***");

    static int subflowIDList[MAX_SCHEDULER];
    int nAvailSCHEDULER = 0;

    int i=1;
    while (i<=n) {
        if (!subflowOutput.IsSubflowFull(i)) {
            subflowIDList[nAvailSCHEDULER++] = i;
        }
        i++;
    }

    if (nAvailSCHEDULER == 0) 
        return -1;
    else
        return subflowIDList[rand()%nAvailSCHEDULER];
}
*/

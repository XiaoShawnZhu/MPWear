#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "subflows.h"

#define SCHED_SEQ_NUM 11

struct SCHEDULER;

typedef SUBFLOW_MSG * (SCHEDULER::*SelectSubflowFunc)();

struct SCHEDULER {
	double * pkt_delay;
	int schedulerSeq[SCHED_SEQ_NUM];
	//  mapping between schedNo and meta buffer index for different schedulers
	int schedulerQ[16 * 2];
	int indexSeq;
    int nTCP;
    int twoWayWiFi, twoWayBT;

	void Setup();

	int schedHasPackets(int sched);
	int getNextSchedNo();
	int getNextSchedNo2();

	// SelectSubflowFunc DefaultSelectSubflow;
	int defaultScheduler;

	SelectSubflowFunc SelectSubflow[MAX_SCHEDULER];

	static const char * getSchedulerName(int sched);
	int getSchedNormalQueue(int sched);
	int getSchedFirstQueue(int sched);
	int getSchedLastQueue(int sched);

	void printPktDelay();
	void clearPktDelay();

	SUBFLOW_MSG * SelectSubflow_ControlMsg();

private:
	//subflow selection algorithms
	SUBFLOW_MSG * SelectSubflow_WithBinding();
	//SUBFLOW_MSG * SelectSubflow_MinimumBuf();	//this is the classic algorithm
	//SUBFLOW_MSG * SelectSubflow_RoundRobin();
	//SUBFLOW_MSG * SelectSubflow_Random();
	//SUBFLOW_MSG * SelectSubflow_RoundRobinChunk();
	//SUBFLOW_MSG * SelectSubflow_Fixed();
	//SUBFLOW_MSG * SelectSubflow_MinRTT();

    SUBFLOW_MSG * SelectSubflow_TwoWay_Base(int saveBytes, int schedNo);
    SUBFLOW_MSG * SelectSubflow_TwoWay();
	SUBFLOW_MSG * SelectSubflow_TwoWay_Naive();
	SUBFLOW_MSG * SelectSubflow_TwoWay_Balance();
	SUBFLOW_MSG * SelectSubflow_NewTxDelay();
	SUBFLOW_MSG * SelectSubflow_NewTxDelayBase(int queueNo);
	SUBFLOW_MSG * SelectSubflow_MinRTT_Kernel();
	SUBFLOW_MSG * SelectSubflow_TxDelay();
	SUBFLOW_MSG * SelectSubflow_TxDelayPlus();
	SUBFLOW_MSG * SelectSubflow_RoundRobin();
	SUBFLOW_MSG * SelectSubflow_ReMP();
	SUBFLOW_MSG * SelectSubflow_Wifi();
	SUBFLOW_MSG * SelectSubflow_BT();
	SUBFLOW_MSG * SelectSubflow_BBS();
	SUBFLOW_MSG * SelectSubflow_BBS_MRT();
	SUBFLOW_MSG * SelectSubflow_EMPTCP();
	SUBFLOW_MSG * SelectSubflow_Block();
	SUBFLOW_MSG * SelectSubflow_UDP();

	SUBFLOW_MSG * SearchPacketForLargeRTTPath(int large_rtt_i, int small_rtt_i, int queueNo);
    SUBFLOW_MSG * GetMessageForSubflow(int subflowNo, int queueNo, int other, int isSmallRTT, int saveBytes, uint64_t currt);
};

#endif

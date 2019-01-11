#ifndef _PROXY_SETTING_H_
#define _PROXY_SETTING_H_

#include "proxy.h"

//Show debug message?
//#define DEBUG_MESSAGE

//Output levels
//#define DEBUG_LEVEL_VERBOSE
#define DEBUG_LEVEL_INFO
//#define DEBUG_LEVEL_WARNING
#define DEBUG_ENABLE_ASSERTION

//#define PERIODICALLY_DUMP_PROXY_STATUS

#define MAX_SUBFLOWS 32
#define MAX_FDS 512
#define MAX_PRIORITY 24
#define LOCAL_PROXY_PORT 1202
#define REMOTE_PROXY_PORT 7001
#define REMOTE_PROXY_UDP1_PORT 6002 // for WiFi
#define REMOTE_PROXY_UDP2_PORT 6003 // for cellular
#define MAX_CONN_ID_PLUS_ONE 16381 //max is 65536
#define TICK_COUNT_GRANULARITY_LOCAL_PROXY 1 //sec
#define TICK_COUNT_GRANULARITY_REMOTE_PROXY 1 //sec

#define UPLINK 1		//LP -> RP
#define DOWNLINK 2		//RP -> LP

//features 
//#define FEATURE_DELAYED_FIN	//enable or not


//#define SUBFLOW_SELECTION_RANDOM 1

#define MAX_SCHEDULER 16

#define SUBFLOW_SELECTION_DEFAULT 255

#define SUBFLOW_SELECTION_MINRTT_KERNEL 0
#define SUBFLOW_SELECTION_TWOWAY 1 // Balance + Redundant
#define SUBFLOW_SELECTION_NEWTXDELAY 2
#define SUBFLOW_SELECTION_EMPTCP 3 // eMPTCP
#define SUBFLOW_SELECTION_WIFIONLY 4
#define SUBFLOW_SELECTION_BTONLY 5

//#define SUBFLOW_SELECTION_ROUNDROBIN_CHUNK 4

#define SUBFLOW_SELECTION_FIXED 6
//#define SUBFLOW_SELECTION_MINRTT 6

#define SUBFLOW_SELECTION_ROUNDROBIN 7
//#define SUBFLOW_SELECTION_MINBUF 8
#define SUBFLOW_SELECTION_REMP 8

#define SUBFLOW_SELECTION_TXDELAY 9
#define SUBFLOW_SELECTION_BLOCK 10 // block all data from app servers

#define SUBFLOW_SELECTION_TWOWAY_NAIVE 11 // reinject all unack
#define SUBFLOW_SELECTION_TWOWAY_BALANCE 12 // only Balance
#define SUBFLOW_SELECTION_NEWTXDELAY_OLD 13

//#define SUBFLOW_SELECTION_BBS 11 // buffer-based scheduler
//#define SUBFLOW_SELECTION_CBS 12 // capacity-based scheduler
#define SUBFLOW_SELECTION_BBS_MRT 14 // BBS with micro traffic shaping
#define SUBFLOW_SELECTION_UDP 15 // test UDP subflows

#define REINJECTION_PACKET_LOSS 1 // when subflow observes Dup ACKs
#define REINJECTION_EARLY_TIMER 2
#define REINJECTION_UNACK_BYTES 3
#define REINJECTION_FULL 4

struct PROXY_SETTINGS {
private:
	PROXY_SETTINGS();
	~PROXY_SETTINGS();

public:
		static int bZeroRTTHandshake;
		static int reinjectionMechanism;
		static int unackBytesThreshold; // for REINJECTION_UNACK_BYTES
		static int unackAllBufCapacity;

		static int metaBufMsgCapacity;
		static int metaBufDataCapacity;

		static int subflowReadBufLocalProxy;
		static int subflowReadBufRemoteProxy;
		static int subflowWriteBufLocalProxy; 
		static int subflowWriteBufRemoteProxy;
		static int subflowTransferUnit;
		static int tcpTransferUnit;
		
		static int subflowBufDataCapacity;
		static int subflowBufMsgCapacity;
		static int subflowPerConnBufDataCapacity;

		static int tcpReadBufRemoteProxy;

		static int tcpOverallBufMsgCapacity;
		static int tcpOverallBufDataCapacity;
		static int tcpPerConnBufDataCapacity;
		
		static double tcpBufAlmostFullRatio;
		static double tcpBufFullRatio;
		static double tcpBufReorgRatio;
		static int subflowBufMsgFullLeeway;
		static int subflowBufDataFullLeeway;
		static int pollTimeout;
        static long rcvContTime;// us
		static int connIDReuseTimeout;
		static int connectTimeout;

		static double delayedFinD1;
		static double delayedFinD2;
		static int sctpMaxNumofStreams;
		static int sctpMaxStreamMsgSize;
		
		static int subflowSelectionAlgorithm;
		static int chunkSizeThreshold;

		static int nSubflows;
        static int nTCPSubflows;
        static int nUDPSubflows;
		static string subflowInterfaces[MAX_SUBFLOWS];
		static string subflowProtocol[MAX_SUBFLOWS];
		static string allSubflowInterface;
		static string allSubflowProtocol;

		static int bDumpIO;

		static int bLateBindingRP;
		static int bLateBindingLP;

		static int bUseQuickACK;


		static int maxPayloadPerMsg;
		static int maxPayloadPerMsgFromPeer;

		static int burstDurationTh;
		static int burstSizeTh;


private:
		static int bQuickACK_RP;
		static int bQuickACK_LP;

		static int maxPayloadPerMsgLP;
		static int maxPayloadPerMsgRP;

		static int bUseLateBinding;
		
public:
	static void ApplyRawSettings();
	static void AdjustSettings();
	
	/*
	static int SendFile(const char * filename, const char * remoteIP);
	static int ReceiveFile(const char * filename);
	*/



};

#endif


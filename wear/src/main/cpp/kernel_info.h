
#ifndef MPWEAR_KERNEL_INFO_H
#define MPWEAR_KERNEL_INFO_H

#include "proxy_setting.h"

struct KERNEL_INFO {
    struct tcp_info pipeinfo[MAX_PIPES];
    int space[MAX_PIPES];

    int fd[MAX_PIPES];
    int infoSize;
    int nTCP;

    void Setup();
    void UpdatePipeInfo(int pipeNo);

    int GetTCPAvailableSpace(int pipeNo);
    unsigned int GetSendCwnd(int pipeNo);
    unsigned int GetSndMss(int pipeNo);
    int GetInFlightSize(int pipeNo);
    int GetSRTT(int pipeNo);
};

#endif //MPWEAR_KERNEL_INFO_H

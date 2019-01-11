#ifndef MPWEAR_PIPE_H
#define MPWEAR_PIPE_H

#include "proxy_setting.h"

struct PIPE_MSG {

};

struct PIPE {

    int n;
    int localListenFD;
    int localListenFDSide;
    int feedbackFD;
    int fd[MAX_PIPES];

    int Setup(const char * localIP, const char * remoteIP);
    void Accept();
    void SetCongestionControl(int fd, const char * tcpVar);
    void Feedback();

};

#endif //MPWEAR_PIPE_H

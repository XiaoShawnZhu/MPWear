
#ifndef MPWEAR_PIPE_H
#define MPWEAR_PIPE_H

#include "proxy_setting.h"

struct PIPE_MSG {

};

struct PIPE {

    int n;
    int localListenFD;
    int fd[MAX_PIPES];
    int Setup();
    void Accept();
    void SetCongestionControl(int fd, const char * tcpVar);

};

#endif //MPWEAR_PIPE_H

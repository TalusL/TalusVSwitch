//
// Created by liangzhuohua on 2024/8/28.
//

#ifndef TALUSVSWITCH_VSWITCH_H
#define TALUSVSWITCH_VSWITCH_H

#include "Poller/EventPoller.h"
#include <cstdint>
#include <sys/socket.h>
class VSwitch {
public:
    static void start(const sockaddr_storage& corePeer,uint64_t macLocal,uint8_t sendTtl);
    static void stop();
protected:
    static void setupOnPeerInput(const sockaddr_storage& corePeer,uint64_t macLocal,const toolkit::EventPoller::Ptr& poller);
    static void pollInterface(uint8_t sendTtl,const std::shared_ptr<std::vector<uint8_t>>& buf,const toolkit::EventPoller::Ptr& poller);
    static volatile bool m_running;
    static std::shared_ptr<toolkit::ThreadPool> m_thread;

};


#endif//TALUSVSWITCH_VSWITCH_H

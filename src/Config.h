//
// Created by liangzhuohua on 2024/9/6.
//

#ifndef TALUSVSWITCH_CONFIG_H
#define TALUSVSWITCH_CONFIG_H


namespace Config{
    extern std::string interfaceName;
    extern volatile bool debug;
    extern uint8_t sendTtl;
    extern sockaddr_storage corePeer;
    extern uint64_t macLocal;
    extern  int mtu;
    extern std::string localIp;
    extern int mask;
};


#endif//TALUSVSWITCH_CONFIG_H

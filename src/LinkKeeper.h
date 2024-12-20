//
// Created by liangzhuohua on 2024/8/27.
//

#ifndef TALUSVSWITCH_LINKKEEPER_H
#define TALUSVSWITCH_LINKKEEPER_H

#include "MacMap.h"
#ifdef _WIN32
#include "WinTapInterface.h"
#else
#include "TapInterface.h"
#endif
#include "Transport.h"
#include <Poller/EventPoller.h>
#include "Config.h"

using namespace toolkit;

class LinkKeeper {
public:
    static void start(){
        // 每5S向Mac表内的对端发送垃圾ARP广播（目标IP为0.0.0.0），用于维护链路
        Transport::Instance().getPoller()->doDelayTask(5000,[](){
            MacMap::forEach([](uint64_t mac,sockaddr_storage addr){
            auto port = toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));
                if(port) {
                    sendKeepData(mac, addr, Config::sendTtl);
                }
            });
            return 5000;
        });
    }
    /**
     * 向对端socket发送链接保持数据
     * @param mac 本地mac
     * @param addr 远端地址、端口
     */
    static void sendKeepData(uint64_t mac,sockaddr_storage addr,uint8_t ttl){
        auto buf = std::make_shared<BufferLikeString>();
        // 填充目标MAC
        char *pMac = reinterpret_cast<char*>(&mac)+2;
        buf->append(pMac, 6);
        // 填充来源MAC
        auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
        pMac = reinterpret_cast<char*>(&macLocal)+2;
        buf->append(pMac, 6);

        DebugL<<"Send Link Data to "<<MacMap::uint64ToMacStr(macLocal)<<" -> "<<MacMap::uint64ToMacStr(mac)<<" "
              <<toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr))<<":"
              << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));

        Transport::Instance().send(buf,addr, sizeof(sockaddr_storage), true,ttl);
    }
};

#endif//TALUSVSWITCH_LINKKEEPER_H

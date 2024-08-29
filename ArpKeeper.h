//
// Created by liangzhuohua on 2024/8/27.
//

#ifndef TALUSVSWITCH_ARPKEEPER_H
#define TALUSVSWITCH_ARPKEEPER_H

#include <Poller/EventPoller.h>
#include "MacMap.h"
#include "Transport.h"

using namespace toolkit;

class ArpKeeper {
public:
    static void start(uint8_t ttl){
        // 每5S向Mac表内的对端发送垃圾ARP广播（目标IP为0.0.0.0），用于维护链路
        Transport::Instance().getPoller()->doDelayTask(5000,[ttl](){
            MacMap::forEach([ttl](uint64_t mac,sockaddr_storage addr){
            auto port = toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));
                if(port) {
                    sendJunkArp(mac, addr,ttl);
                }
            });
            return 5000;
        });
    }
    /**
     * 向对端socket发送垃圾ARP广播，目标IP为0.0.0.0
     * @param mac 本地mac
     * @param addr 远端地址、端口
     */
    static void sendJunkArp(uint64_t mac,sockaddr_storage addr,uint8_t ttl){
        // 空白的ARP 二层报文
        char arpData[] = {
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x06,0x00,0x01,
                0x08,0x00,0x06,0x04,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
        };
        // 填充目标MAC
        char *pMac = reinterpret_cast<char*>(&mac)+2;
        memcpy(arpData,pMac,6);
        // 填充来源MAC
        auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
        pMac = reinterpret_cast<char*>(&macLocal)+2;
        memcpy(arpData+6,pMac,6);
        // 发送ARP
        auto buf = std::make_shared<BufferLikeString>();
        buf->append(arpData, sizeof(arpData));
        auto d2 = compress(buf);
        d2->data()[0] = ttl;
        DebugL<<"Send ARP to "<<MacMap::uint64ToMacStr(macLocal)<<" -> "<<MacMap::uint64ToMacStr(mac)<<" "
              <<toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr))<<":"
              << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));

        Transport::Instance().send(d2,addr, sizeof(sockaddr_storage), true);
    }
};

#endif//TALUSVSWITCH_ARPKEEPER_H

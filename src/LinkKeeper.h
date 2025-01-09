/**
 * @file LinkKeeper.h
 * @brief 链路保持器
 * @details 负责维护虚拟网络中各节点间的连接状态
 */

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

/**
 * @class LinkKeeper
 * @brief 链路维护类
 * @details 通过定期发送心跳包来维护网络中各节点间的连接状态
 */
class LinkKeeper {
public:
    /**
     * @brief 启动链路维护服务
     * @details 每5秒向MAC表中的所有对端发送一次ARP广播，用于保持链路活跃
     */
    static void start() {
        // 每5秒执行一次链路维护
        Transport::Instance().getPoller()->doDelayTask(5000, []() {
            MacMap::forEach([](uint64_t mac, sockaddr_storage addr) {
                auto port = toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));
                if(port) {
                    sendKeepData(mac, addr, 0);
                }
            });
            return 5000;  // 返回下次执行的延迟时间(毫秒)
        });
    }

    /**
     * @brief 向指定对端发送链路保持数据
     * @param mac 目标MAC地址
     * @param addr 目标网络地址
     * @param ttl 数据包生存时间
     * @details 发送一个特殊的ARP包(目标IP为0.0.0.0)用于维持链路连接
     */
    static void sendKeepData(uint64_t mac, sockaddr_storage addr, uint8_t ttl) {
        auto buf = std::make_shared<BufferLikeString>();
        
        // 填充目标MAC地址
        char *pMac = reinterpret_cast<char*>(&mac) + 2;
        buf->append(pMac, 6);
        
        // 填充源MAC地址
        auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
        pMac = reinterpret_cast<char*>(&macLocal) + 2;
        buf->append(pMac, 6);

        DebugL << "Send Link Data to " << MacMap::uint64ToMacStr(macLocal) 
               << " -> " << MacMap::uint64ToMacStr(mac) << " "
               << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr)) 
               << ":" << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));

        // 发送保活数据包
        Transport::Instance().send(buf, addr, sizeof(sockaddr_storage), true, ttl);
    }
};

#endif //TALUSVSWITCH_LINKKEEPER_H

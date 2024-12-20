﻿//
// Created by liangzhuohua on 2024/8/28.
//

#include "VSwitch.h"
#include "MacMap.h"
#ifdef _WIN32
#include "WinTapInterface.h"
#else
#include "TapInterface.h"
#endif
#include "Transport.h"
#include "Utils.h"
#include <memory>
#include "Config.h"


namespace Config{
    volatile bool debug = false;

    uint8_t sendTtl = 8;

    sockaddr_storage corePeer;

    uint64_t macLocal;

    std::string interfaceName;

    int mtu;
};

volatile bool VSwitch::m_running = false;
std::shared_ptr<toolkit::ThreadPool> VSwitch::m_thread;

void VSwitch::start() {
    m_running = true;
    // 处理线程
    auto poller = toolkit::EventPollerPool::Instance().getPoller();
    // 分发远程输入
    setupOnPeerInput(Config::corePeer,Config::macLocal);
    // 分发本地输入
    std::shared_ptr<std::vector<uint8_t>> buf = std::make_shared<std::vector<uint8_t>>();
    buf->resize(1024*1024);
    m_thread = std::make_shared<toolkit::ThreadPool>(1, toolkit::ThreadPool::Priority::PRIORITY_HIGHEST, true, true, "PollingInterface");
    m_thread->async([=](){
        while(m_running){
            pollInterface(Config::sendTtl,buf);
        }
    },false);
}
void VSwitch::setupOnPeerInput(const sockaddr_storage &corePeer, uint64_t macLocal) {
    Transport::Instance().setOnRead([macLocal, corePeer](const toolkit::Buffer::Ptr &buf, const sockaddr_storage& pktRecvPeer, int addr_len,uint8_t ttl){

        uint64_t sMac = *(uint64_t*)(buf->data()+6);
        sMac = sMac<<16;
        // 获取目标MAC
        uint64_t dMac = *(uint64_t*)buf->data();
        dMac = dMac<<16;

        if(Config::debug){
            DebugL<<"P:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)
                   <<" - "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<":"
                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<" "
                   << (int)ttl;
        }


        // 符合要求的流量送入虚拟网卡
        if( ( dMac == macLocal || dMac == MAC_BROADCAST ) && sMac != macLocal && buf->size() > 12){

            if(Config::debug) {
                DebugL << "RX:" << MacMap::uint64ToMacStr(sMac) << " - " << MacMap::uint64ToMacStr(dMac) << " "
                       << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << ":"
                       << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer));
            }
            TapInterface::Instance().write(buf->data(),buf->size());
        }


        // TTL为0不转发
        if( dMac != macLocal && ttl ){
            // 从核心节点转发的来的数据(即本节点是客户端)不进行转发
            // 就是发给本节点的数据，不转发
            if( dMac != MAC_BROADCAST ){
                // 常规流量转发，只有核心节点需要
                bool got = false;
                auto forwardPeer = MacMap::getMacPeer(dMac,got);
                if(got){
                    if(Config::debug) {
                        DebugL << "FORWARD:" << MacMap::uint64ToMacStr(sMac) << " -> " << MacMap::uint64ToMacStr(dMac) << " - "
                               << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << ":"
                               << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << " -> "
                               << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&forwardPeer)) << ":"
                               << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&forwardPeer));
                    }
                    // 转发前TTL减一
                    Transport::Instance().send(buf,forwardPeer, sizeof(sockaddr_storage),true,ttl-1);
                }
            }else{
                // 广播流量转发，只有核心节点需要,向子节点转发
                std::shared_ptr<std::list<sockaddr_storage>> sendPeers = std::make_shared<std::list<sockaddr_storage>>();
                MacMap::forEach([ buf,sMac,dMac, corePeer, pktRecvPeer, ttl,sendPeers](uint64_t mac,sockaddr_storage addr){
                    auto iter = std::find_if(sendPeers->begin(), sendPeers->end(), [addr](const sockaddr_storage& addr2){
                        return compareSockAddr(addr, addr2);
                    });
                    if (iter!= sendPeers->end()) {
                        return;
                    }
                    if( mac != MAC_BROADCAST && !compareSockAddr(pktRecvPeer,addr)){

                        if(Config::debug) {
                            DebugL << "BROADCAST:" << MacMap::uint64ToMacStr(sMac) << " -> " << MacMap::uint64ToMacStr(dMac) << " - " << MacMap::uint64ToMacStr(mac) << " "
                                   << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << ":"
                                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << " -> "
                                   << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr)) << ":"
                                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr))<< " " << (int)ttl;
                        }

                        // 转发前TTL减一
                        Transport::Instance().send(buf,addr, sizeof(sockaddr_storage),true,ttl-1);
                        sendPeers->push_back(addr);
                    }
                });
            }
        }

        if( sMac != MAC_BROADCAST && sMac != Config::macLocal ){
            MacMap::addMacPeer(sMac, pktRecvPeer,ttl);
        }
    });
}
void VSwitch::stop() {
    m_running = false;
    Transport::Instance().setOnRead(nullptr);
}
void VSwitch::pollInterface(uint8_t sendTtl,const std::shared_ptr<std::vector<uint8_t>>& buf) {
    // 从虚拟网卡接收数据
    size_t len = buf->size();
    int size = TapInterface::Instance().read(buf->data(),len);
    if(size <= 0){
        return;
    }
    auto data = std::make_shared<toolkit::BufferLikeString>();
    data->assign(reinterpret_cast<const char *>(buf->data()),size);
    // 查询mac表并转发数据
    uint64_t dMac = *(uint64_t*)data->data();
    dMac = dMac<<16;
    uint64_t sMac = *(uint64_t*)(data->data()+6);
    sMac = sMac<<16;
    bool got = false;
    auto peer = MacMap::getMacPeer(dMac,got);

    if(Config::debug) {
        DebugL << "TP:" << MacMap::uint64ToMacStr(sMac) << " -> " << MacMap::uint64ToMacStr(dMac);
    }


    // 远端有效则发送数据，无效则只执行广播
    auto port = toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer));
    if(port){
        if(Config::debug) {
            DebugL << "TX:" << MacMap::uint64ToMacStr(sMac) << " -> " << MacMap::uint64ToMacStr(dMac) << " -> " << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer));
        }
        // 发送数据到远端
        Transport::Instance().send(data, peer, sizeof(sockaddr_storage),true,sendTtl);
        return ;
    }else if( dMac == MAC_BROADCAST ){
        // 远端地址无效，但目标MAC地址是广播地址，转发广播
        std::shared_ptr<std::list<sockaddr_storage>> sendPeers = std::make_shared<std::list<sockaddr_storage>>();
        MacMap::forEach([data, sMac,dMac, sendTtl, sendPeers](uint64_t mac,sockaddr_storage addr){
            auto iter = std::find_if(sendPeers->begin(), sendPeers->end(), [addr](const sockaddr_storage& addr2){
                return compareSockAddr(addr, addr2);
            });
            if (iter!= sendPeers->end()) {
                return;
            }
            if( mac != MAC_BROADCAST ){
                if(Config::debug) {
                    DebugL << "TX BROADCAST:" << MacMap::uint64ToMacStr(sMac) << " -> " << MacMap::uint64ToMacStr(dMac) << " - " << MacMap::uint64ToMacStr(mac) << " "
                           << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr)) << ":"
                           << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));
                }
                Transport::Instance().send(data, addr, sizeof(sockaddr_storage),true,sendTtl);
                sendPeers->push_back(addr);
            }
        });
    }
}

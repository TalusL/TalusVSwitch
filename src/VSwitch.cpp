//
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
            pollInterface(buf);
        }
    },false);
}

void VSwitch::sendBroadcast(const toolkit::Buffer::Ptr& buf,const sockaddr_storage& pktRecvPeer,uint8_t ttl) {
    uint64_t sMac = *(uint64_t*)(buf->data()+6);
    sMac = sMac<<16;
    // 获取目标MAC
    uint64_t dMac = *(uint64_t*)buf->data();
    dMac = dMac<<16;
    // 广播流量转发，向子节点转发
    std::shared_ptr<std::list<sockaddr_storage>> sendPeers = std::make_shared<std::list<sockaddr_storage>>();
    MacMap::forEach([ buf,sMac,dMac, pktRecvPeer, ttl,sendPeers](uint64_t mac,sockaddr_storage addr){
        //去重
        auto iter = std::find_if(sendPeers->begin(), sendPeers->end(), [addr](const sockaddr_storage& addr2){
            return compareSockAddr(addr, addr2);
        });
        if (iter!= sendPeers->end()) {
            return;
        }
        // 忽略数据包来源地址
        if (compareSockAddr(pktRecvPeer,addr)) {
            return;
        }
        if(Config::debug) {
            DebugL << "BROADCAST:" << MacMap::uint64ToMacStr(sMac) << " -> " << MacMap::uint64ToMacStr(dMac) << " - " << MacMap::uint64ToMacStr(mac) << " "
                   << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << ":"
                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer)) << " -> "
                   << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr)) << ":"
                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr))<< " " << (int)ttl;
        }
        if( mac != MAC_BROADCAST ){
            // 向P2P节点转发,ttl置为0
            Transport::Instance().send(buf,addr, sizeof(sockaddr_storage),true,0);
        }else {
            // 向上级节点转发 TTL - 1
            Transport::Instance().send(buf,addr, sizeof(sockaddr_storage),true,ttl-1);
        }
        sendPeers->push_back(addr);
    });
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
                   << (int)ttl<<" size:"<<buf->size();
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
        // 收到合适的MAC地址报文,更新MAC表
        if( sMac != MAC_BROADCAST && sMac != Config::macLocal){
            // 目标是MAC广播，且源MAC已经存在，不更新。MacMap不存在时允许ARP
            MacMap::addMacPeer(sMac, pktRecvPeer,ttl);
        }
        // TTL为0不转发
        if (!ttl) {
            return;
        }
        // 只有二层，不转发
        if (buf->size() <= 12) {
            return;
        }
        if( dMac != macLocal){
            // 从核心节点转发的来的数据(即本节点是客户端)不进行转发
            // 就是发给本节点的数据，不转发
            if( dMac != MAC_BROADCAST ){
                // 常规流量转发
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
                sendBroadcast(buf,pktRecvPeer,ttl);
            }
        }
    });
}
void VSwitch::stop() {
    m_running = false;
    Transport::Instance().setOnRead(nullptr);
}


void VSwitch::pollInterface(const std::shared_ptr<std::vector<uint8_t>>& buf) {
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
        Transport::Instance().send(data, peer, sizeof(sockaddr_storage),true,Config::sendTtl);
        return ;
    }else if( dMac == MAC_BROADCAST ){
        // 远端地址无效，但目标MAC地址是广播地址，转发广播
        sendBroadcast(data,{},Config::sendTtl);
    }
}

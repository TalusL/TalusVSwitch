//
// Created by liangzhuohua on 2024/8/28.
//

#include "VSwitch.h"
#include "MacMap.h"
#include "TapInterface.h"
#include "Transport.h"
#include "Utils.h"
#include <memory>

volatile bool VSwitch::m_running = false;
std::shared_ptr<toolkit::ThreadPool> VSwitch::m_thread;

void VSwitch::start(const sockaddr_storage& corePeer,uint64_t macLocal,uint8_t sendTtl) {
    m_running = true;
    // 处理线程
    auto poller = toolkit::EventPollerPool::Instance().getPoller();
    // 分发远程输入
    setupOnPeerInput(corePeer,macLocal,poller);
    // 分发本地输入
    std::shared_ptr<std::vector<uint8_t>> buf = std::make_shared<std::vector<uint8_t>>();
    buf->resize(1024*1024);
    m_thread = std::make_shared<toolkit::ThreadPool>(1, toolkit::ThreadPool::Priority::PRIORITY_HIGHEST, true, true, "PollingInterface");
    m_thread->async([=](){
        pollInterface(sendTtl,buf,poller);
    },false);
}
void VSwitch::setupOnPeerInput(const sockaddr_storage &corePeer, uint64_t macLocal,const toolkit::EventPoller::Ptr& poller) {
    Transport::Instance().setOnRead([macLocal, corePeer,poller](toolkit::Buffer::Ptr &buf, struct sockaddr *addr, int addr_len){
        // 获取数据包TTL
        uint8_t ttl = buf->data()[0];
        // 解压流量
        auto d2 = decompress(buf);

        uint64_t sMac = *(uint64_t*)(d2->data()+6);
        sMac = sMac<<16;
        // 获取目标MAC
        uint64_t dMac = *(uint64_t*)d2->data();
        dMac = dMac<<16;

        // 符合要求的流量送入虚拟网卡
        if( ( dMac == macLocal || dMac == MAC_BROADCAST ) && sMac != macLocal){

            DebugL<<"RX:"<<MacMap::uint64ToMacStr(sMac)<<" - "<<MacMap::uint64ToMacStr(dMac) <<" "
                   << toolkit::SockUtil::inet_ntoa(addr)<<":"
                   << toolkit::SockUtil::inet_port(addr);
            TapInterface::Instance().write(d2->data(),d2->size());
        }

        sockaddr_storage pktRecvPeer{};
        if (addr) {
            auto addrLen = addr_len ? addr_len : toolkit::SockUtil::get_sock_len(addr);
            memcpy(&pktRecvPeer, addr, addrLen);
        }
        poller->async([macLocal,buf, corePeer, sMac, dMac, pktRecvPeer,ttl](){
            DebugL<<"P:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)
                   <<" - "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<":"
                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<" "
                   << (int)ttl;


            if( sMac != MAC_BROADCAST ){
                MacMap::addMacPeer(sMac, pktRecvPeer,ttl);
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

                        DebugL<<"FORWARD:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "
                               << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<":"
                               << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<" -> "
                               << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&forwardPeer))<<":"
                               << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&forwardPeer));
                        // 转发前TTL减一
                        buf->data()[0] = ttl - 1;
                        Transport::Instance().send(buf, reinterpret_cast<sockaddr *>(&forwardPeer
                                                                                     ), sizeof(sockaddr_storage),true);
                    }
                }else{
                    // 广播流量转发，只有核心节点需要,向子节点转发
                    MacMap::forEach([ buf,sMac,dMac, corePeer, pktRecvPeer, ttl](uint64_t mac,sockaddr_storage addr){
                        if( mac != MAC_BROADCAST && !compareSockAddr(pktRecvPeer,addr)){

                            DebugL<<"BROADCAST:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "<<MacMap::uint64ToMacStr(mac)<<" "
                                   << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<":"
                                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&pktRecvPeer))<<" -> "
                                   << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr))<<":"
                                   << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));

                            // 转发前TTL减一
                            buf->data()[0] = ttl - 1;
                            Transport::Instance().send(buf, reinterpret_cast<sockaddr *>(&addr), sizeof(sockaddr_storage),true);
                        }
                    });
                }
            }
        },false);
    });
}
void VSwitch::stop() {
    m_running = false;
    Transport::Instance().setOnRead(nullptr);
}
void VSwitch::pollInterface(uint8_t sendTtl,const std::shared_ptr<std::vector<uint8_t>>& buf,const toolkit::EventPoller::Ptr &poller) {
    if(m_running){
        m_thread->async([=](){
            pollInterface(sendTtl,buf,poller);
        },false);
    }

    // 从虚拟网卡接收数据
    size_t len = buf->size();
    int size = TapInterface::Instance().read(buf->data(),len);
    if(size <= 0){
        return;
    }
    // 压缩数据
    auto data = std::make_shared<toolkit::BufferLikeString>();
    data->assign(reinterpret_cast<const char *>(buf->data()),size);
    auto d1 = compress(data);
    // 查询mac表并转发数据
    poller->async([data,d1, sendTtl](){
        uint64_t dMac = *(uint64_t*)data->data();
        dMac = dMac<<16;
        uint64_t sMac = *(uint64_t*)(data->data()+6);
        sMac = sMac<<16;
        bool got = false;
        auto peer = MacMap::getMacPeer(dMac,got);

        DebugL<<"TP:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac);

        // 第一Byte定为ttl
        d1->data()[0] = sendTtl;

        // 远端有效则发送数据，无效则只执行广播
        auto port = toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer));
        if(port){
            DebugL<<"TX:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" -> "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer));
            // 发送数据到远端
            Transport::Instance().send(d1, reinterpret_cast<sockaddr *>(&peer), sizeof(sockaddr_storage),true);
            return ;
        }else if( dMac == MAC_BROADCAST ){
            // 远端地址无效，但目标MAC地址是广播地址，转发广播
            MacMap::forEach([d1, sMac,dMac](uint64_t mac,sockaddr_storage addr){
                if( mac != MAC_BROADCAST ){
                    DebugL<<"TX BROADCAST:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "<<MacMap::uint64ToMacStr(mac)<<" "
                           << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr))<<":"
                           << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr));

                    Transport::Instance().send(d1, reinterpret_cast<sockaddr *>(&addr), sizeof(sockaddr_storage),true);
                }
            });
        }
    },false);
}

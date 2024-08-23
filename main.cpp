
#include "TapInterface.h"
#include "MacMap.h"
#include <Network/Socket.h>
#include "Utils.h"

#define MAC_VENDOR "00:0c:01"

int main(int argc, char* argv[]) {
#ifndef DEBUG
    // 守护进程
    startDaemon();
#endif
    CommandLineParser parser(argc,argv);

    // 构建虚拟接口
    auto name = parser.getOptionValue("name");
    name = name.empty()?"tvs0":name;
    TapInterface::Instance().name(name);
    InfoL<<"Interface name "<<name;

    // 获取本地MAC地址
    auto mac = parser.getOptionValue("mac");
    if(mac.empty()){
        mac = getMacAddress();
        mac = std::string().append(MAC_VENDOR).append(mac.substr(8,10));
    }
    TapInterface::Instance().hwaddr(mac);
    TapInterface::Instance().up();
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    InfoL<<"Local mac "<<MacMap::uint64ToMacStr(macLocal);
    // ttl
    auto ttlStr = parser.getOptionValue("ttl");
    auto sendTtl = 8;
    if(!ttlStr.empty()){
        sendTtl = stoi(ttlStr);
    }
    // 远端地址
    auto remoteAddr = parser.getOptionValue("remote_addr");
    // 远端端口
    auto remotePort = 0;
    auto remote_port = parser.getOptionValue("remote_port");
    if(!remote_port.empty()){
        remotePort = stoi(remote_port);
    }
    InfoL<<"Remote "<<remoteAddr<<":"<<remote_port;

    // 处理线程
    auto poller = toolkit::EventPollerPool::Instance().getPoller();
    // 增加默认广播地址到MAC表
    auto corePeer = toolkit::SockUtil::make_sockaddr(remoteAddr.c_str(),remotePort);
    MacMap::addMacPeer(MAC_BROADCAST, corePeer,sendTtl);
    // 监听传输
    toolkit::Socket::Ptr sock = toolkit::Socket::createSocket();
    sock->bindUdpSock(9001);
    sock->setOnRead([macLocal, poller, sock, corePeer](toolkit::Buffer::Ptr &buf, struct sockaddr *addr, int addr_len){
        // 解压流量
        auto d2 = decompress(buf);

        uint64_t sMac = *(uint64_t*)(d2->data()+6);
        sMac = sMac<<16;
        // 获取目标MAC
        uint64_t dMac = *(uint64_t*)d2->data();
        dMac = dMac<<16;

        // 符合要求的流量送入虚拟网卡
        if( ( dMac == macLocal || dMac == MAC_BROADCAST ) && sMac != macLocal){
#ifdef DEBUG
            InfoL<<"RX:"<<MacMap::uint64ToMacStr(sMac)<<" - "<<MacMap::uint64ToMacStr(dMac);
#endif
            TapInterface::Instance().write(d2->data(),d2->size());
        }

        sockaddr_storage pktRecvPeer{};
        if (addr) {
            auto addrLen = addr_len ? addr_len : toolkit::SockUtil::get_sock_len(addr);
            memcpy(&pktRecvPeer, addr, addrLen);
        }
        poller->async([macLocal,buf, sock, corePeer, sMac, dMac, pktRecvPeer](){

#ifdef DEBUG
            InfoL<<"P:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&pktRecvPeer));
#endif
            // 获取数据包TTL
            uint8_t ttl = buf->data()[0];

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
#ifdef DEBUG
                        InfoL<<"FORWARD:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&forwardPeer));
#endif
                        // 转发前TTL减一
                        buf->data()[0] -= 1;
                        sock->send(buf, reinterpret_cast<sockaddr *>(&forwardPeer
                                                                     ), sizeof(sockaddr_storage),true);
                    }
                }else{
                    // 广播流量转发，只有核心节点需要,向子节点转发
                    MacMap::forEach([sock, buf,sMac,dMac, corePeer, pktRecvPeer](uint64_t mac,sockaddr_storage addr){
                        if( mac != MAC_BROADCAST && !compareSockAddr(corePeer,addr) && !compareSockAddr(pktRecvPeer,addr)){
#ifdef DEBUG
                            InfoL<<"BROADCAST:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "<<MacMap::uint64ToMacStr(mac)<<" "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr));
#endif
                            // 转发前TTL减一
                            buf->data()[0] -= 1;
                            sock->send(buf, reinterpret_cast<sockaddr *>(&addr), sizeof(sockaddr_storage),true);
                        }
                    });
                }
            }


        });
    });
    // 从端口接收数据
    std::vector<uint8_t> buf;
    buf.resize(65536);
    while(true){
        size_t len = buf.size();
        int size = TapInterface::Instance().read(buf.data(),len);
        // 压缩数据
        auto data = std::make_shared<toolkit::BufferLikeString>();
        data->assign(reinterpret_cast<const char *>(buf.data()),size);
        auto d1 = compress(data);
        // 第一Byte定为ttl
        d1->data()[0] = sendTtl;
        // 查询mac表并转发数据
        poller->async([data, sock,d1](){
            uint64_t dMac = *(uint64_t*)data->data();
            dMac = dMac<<16;
            uint64_t sMac = *(uint64_t*)(data->data()+6);
            sMac = sMac<<16;
            bool got = false;
            auto peer = MacMap::getMacPeer(dMac,got);
#ifdef DEBUG
            InfoL<<"TX:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" -> "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer));
#endif
            sock->send(d1, reinterpret_cast<sockaddr *>(&peer), sizeof(sockaddr_storage),true);
        });
    }
}

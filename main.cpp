
#include "TapInterface.h"
#include "MacMap.h"
#include <Network/Socket.h>
#include "Utils.h"
#include "ArpKeeper.h"
#include "Transport.h"

#define MAC_VENDOR "00:0c:01"

int main(int argc, char* argv[]) {

    CommandLineParser parser(argc,argv);

    bool debug = atoi(parser.getOptionValue("debug").c_str());

    if(!debug){
        // 守护进程
        startDaemon();
        // 日志级别
        Logger::Instance().add(std::make_shared<ConsoleChannel>("ConsoleChannel", LogLevel::LInfo));
    }else{
        // 日志级别
        Logger::Instance().add(std::make_shared<ConsoleChannel>("ConsoleChannel", LogLevel::LTrace));
    }

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

    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    InfoL<<"Local mac "<<MacMap::uint64ToMacStr(macLocal);

    // mtu
    auto mtuStr = parser.getOptionValue("mtu");
    int mtu = 1450;
    if(!mtuStr.empty()){
        mtu = stoi(mtuStr);
    }
    TapInterface::Instance().mtu(mtu);
    InfoL<<"MTU "<<TapInterface::Instance().mtu();


    // ttl
    auto ttlStr = parser.getOptionValue("ttl");
    uint8_t sendTtl = 8;
    if(!ttlStr.empty()){
        sendTtl = stoi(ttlStr);
    }
    // 远端地址
    auto remoteAddr = parser.getOptionValue("remote_addr");
    if(remoteAddr.empty()){
        remoteAddr = "0.0.0.0";
    }
    // 远端端口
    auto remotePort = 0;
    auto remote_port = parser.getOptionValue("remote_port");
    if(!remote_port.empty()){
        remotePort = stoi(remote_port);
    }
    InfoL<<"Remote "<<remoteAddr<<":"<<remotePort;
    // 远端端口
    auto localPort = 9001;
    auto local_port = parser.getOptionValue("local_port");
    if(!local_port.empty()){
        localPort = stoi(local_port);
    }
    InfoL<<"Local port "<<localPort;

    TapInterface::Instance().up();

    // ARP保持链路
    ArpKeeper::Instance().start();

    // 处理线程
    auto poller = toolkit::EventPollerPool::Instance().getPoller();
    // 增加默认广播地址到MAC表
    auto corePeer = toolkit::SockUtil::make_sockaddr(remoteAddr.c_str(),remotePort);
    MacMap::addMacPeer(MAC_BROADCAST, corePeer,sendTtl);
    // 监听传输
    Transport::Instance().start(localPort);
    Transport::Instance().setOnRead([macLocal, poller, corePeer](toolkit::Buffer::Ptr &buf, struct sockaddr *addr, int addr_len){
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

                        DebugL<<"P:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" - "
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
        // 查询mac表并转发数据
        poller->async([data,d1, sendTtl](){
            uint64_t dMac = *(uint64_t*)data->data();
            dMac = dMac<<16;
            uint64_t sMac = *(uint64_t*)(data->data()+6);
            sMac = sMac<<16;
            bool got = false;
            auto peer = MacMap::getMacPeer(dMac,got);

            DebugL<<"TX:"<<MacMap::uint64ToMacStr(sMac)<<" -> "<<MacMap::uint64ToMacStr(dMac)<<" -> "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer));
            // 第一Byte定为ttl
            d1->data()[0] = sendTtl;
            Transport::Instance().send(d1, reinterpret_cast<sockaddr *>(&peer), sizeof(sockaddr_storage),true);
        });
    }
}


#include "LinkKeeper.h"
#include "MacMap.h"
#include "TapInterface.h"
#include "Transport.h"
#include "Utils.h"
#include "VSwitch.h"
#include <Network/Socket.h>

#define MAC_VENDOR "00:0c:01"

#define INVALID_REMOTE "0.0.0.0"

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
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

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
        remoteAddr = INVALID_REMOTE;
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


    // 远端地址
    auto addr = parser.getOptionValue("addr");
    auto netMaskStr = parser.getOptionValue("mask");
    auto netMask = 24;
    if(!netMaskStr.empty()){
        netMask = atoi(netMaskStr.c_str());
    }
    if(!addr.empty()){
        TapInterface::Instance().ip(addr,netMask);
        InfoL<<"Interface Addr:"<<addr<<"/"<<netMask;
    }

    TapInterface::Instance().up();


    // 增加默认广播地址到MAC表
    auto corePeer = toolkit::SockUtil::make_sockaddr(remoteAddr.c_str(),remotePort);
    MacMap::addMacPeer(MAC_BROADCAST, corePeer,sendTtl);

    // 监听传输
    Transport::Instance().start(localPort);

    // 启动交换
    VSwitch::start(corePeer,macLocal,sendTtl);

    // ARP保持链路
    LinkKeeper::start(sendTtl);

    // 设置退出信号处理函数
    static semaphore sem;
    signal(SIGINT, [](int) {
        InfoL << "SIGINT:exit";
        signal(SIGINT, SIG_IGN); // 设置退出信号
        sem.post();
    }); // 设置退出信号
    sem.wait();


}

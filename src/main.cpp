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
    // 解析命令行参数
    CommandLineParser parser(argc,argv);
    
    // 设置调试模式
    Config::debug = atoi(parser.getOptionValue("debug").c_str());

    // 配置日志系统
    if(!Config::debug){
        // 非调试模式下运行为守护进程
        startDaemon();
        Logger::Instance().add(std::make_shared<ConsoleChannel>("ConsoleChannel", LogLevel::LInfo));
    }else{
        // 调试模式下显示更详细的日志
        Logger::Instance().add(std::make_shared<ConsoleChannel>("ConsoleChannel", LogLevel::LTrace));
    }
    // 使用异步日志写入器
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    // 配置虚拟网络接口
    Config::interfaceName = parser.getOptionValue("name");
    Config::interfaceName = Config::interfaceName.empty()?"tvs0":Config::interfaceName;
    TapInterface::Instance().name(Config::interfaceName);
    InfoL<<"Interface name "<<Config::interfaceName;

    // 配置MAC地址
    auto mac = parser.getOptionValue("mac");
    if(mac.empty()){
        // 如果未指定MAC地址，则自动生成
        mac = getMacAddress();
        mac = std::string().append(MAC_VENDOR).append(mac.substr(8,10));
    }
    TapInterface::Instance().hwaddr(mac);
    
    // 存储本地MAC地址
    Config::macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    InfoL<<"Local mac "<<MacMap::uint64ToMacStr(Config::macLocal);

    // 配置MTU大小
    auto mtuStr = parser.getOptionValue("mtu");
    Config::mtu = 1400;  // 默认MTU值
    if(!mtuStr.empty()){
        Config::mtu = stoi(mtuStr);
    }
    TapInterface::Instance().mtu(Config::mtu);
    InfoL<<"MTU "<<TapInterface::Instance().mtu();

    auto autoUpStr= parser.getOptionValue("auto_up");
    bool autoUp = true;
    if(!autoUpStr.empty()){
        autoUp = stoi(autoUpStr);
    }

    auto enableP2PStr= parser.getOptionValue("p2p");
    if(!enableP2PStr.empty()){
        Config::enableP2p = stoi(enableP2PStr);
    }


    // ttl
    auto ttlStr = parser.getOptionValue("ttl");
    Config::sendTtl = 8;
    if(!ttlStr.empty()){
        Config::sendTtl = stoi(ttlStr);
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

    Config::localIp = addr;
    Config::mask = netMask;

    if(autoUp) {
        TapInterface::Instance().up();
    }


    // 增加默认广播地址到MAC表
    Config::corePeer = toolkit::SockUtil::make_sockaddr(remoteAddr.c_str(),remotePort);
    MacMap::addMacPeer(MAC_BROADCAST, Config::corePeer,Config::sendTtl);

    // 启动各个组件
    Transport::Instance().start(localPort);    // 启动传输层
    VSwitch::start();                         // 启动虚拟交换机
    LinkKeeper::start();                      // 启动链路保持
    VSCtrlHelper::Instance().Start();         // 启动控制助手

    // 设置信号处理，优雅退出
    static semaphore sem;
    signal(SIGINT, [](int) {
        InfoL << "SIGINT:exit";
        signal(SIGINT, SIG_IGN);
        sem.post();
    });
    sem.wait();
}

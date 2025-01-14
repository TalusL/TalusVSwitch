/**
 * @file VSwitch.cpp
 * @brief 虚拟交换机核心实现
 * @details 实现了虚拟交换机的数据转发和处理功能
 */

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

/**
 * @namespace Config
 * @brief 全局配置参数
 */
namespace Config{
    volatile bool debug = false;         ///< 调试模式标志
    uint8_t sendTtl = 8;                ///< 发送TTL值
    sockaddr_storage corePeer;          ///< 核心节点地址
    uint64_t macLocal;                  ///< 本地MAC地址
    uint64_t macCore;                   ///< 核心节点MAC地址
    std::string interfaceName;          ///< 接口名称
    std::string localIp;                ///< 本地IP地址
    std::string coreIp;                ///< 远端IP地址
    int mask = 24;                      ///< 子网掩码
    int mtu;                            ///< MTU大小
    bool enableP2p = true;              ///< P2P功能开关
};

// 静态成员初始化
volatile bool VSwitch::m_running = false;
std::shared_ptr<toolkit::ThreadPool> VSwitch::m_thread;

/**
 * @brief 启动虚拟交换机
 * @details 
 * 1. 初始化运行状态
 * 2. 设置网络事件处理
 * 3. 启动接口轮询线程
 */
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

/**
 * @brief 处理广播数据包
 * @param buf 数据包内容
 * @param pktRecvPeer 数据包来源地址
 * @param ttl 生存时间
 * @details 
 * 1. 解析源和目标MAC地址
 * 2. 维护已发送节点列表，避免重复发送
 * 3. 根据节点类型设置不同的TTL
 */
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

/**
 * @brief 设置网络数据接收回调
 * @param corePeer 核心节点地址
 * @param macLocal 本地MAC地址
 * @details 处理接收到的数据包：
 * 1. 解析MAC地址
 * 2. 处理ARP请求
 * 3. 转发数据包
 * 4. 更新MAC表
 */
void VSwitch::setupOnPeerInput(const sockaddr_storage &corePeer, uint64_t macLocal) {
    Transport::Instance().setOnRead([macLocal, corePeer](const toolkit::Buffer::Ptr &buf,
        const sockaddr_storage& pktRecvPeer, int addr_len,uint8_t ttl,bool isTvsCmd){

        // 获取来源MAC
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

        // ARP检查
        ArpMap::checkArp(buf,pktRecvPeer,addr_len,ttl);

        // TVS命令流量不写入网卡，只参与在各节点内部转发
        if (!isTvsCmd) {
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
                MacMap::addMacPeer(sMac, pktRecvPeer,ttl);
            }
        }
        // 发给本节点的流量，不转发
        if( dMac == macLocal) {
            return;
        }
        // TTL为0不转发
        if (!ttl) {
            return;
        }
        // 只有二层，不转发
        if (buf->size() <= 12) {
            return;
        }

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
            // 广播流量转发
            sendBroadcast(buf,pktRecvPeer,ttl);
        }
    });
}

/**
 * @brief 停止虚拟交换机
 * @details 清理资源并停止数据处理
 */
void VSwitch::stop() {
    if (m_running) {
        m_running = false;
        Transport::Instance().setOnRead(nullptr);
    }
}

/**
 * @brief 轮询TAP接口数据
 * @param buf 数据缓冲区
 * @details 
 * 1. 从TAP接口读取数据
 * 2. 解析MAC地址
 * 3. 查找目标节点
 * 4. 转发数据包
 */
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

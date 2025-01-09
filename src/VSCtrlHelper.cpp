/**
 * @file VSCtrlHelper.cpp
 * @brief 虚拟交换机控制助手实现
 * @details 实现了虚拟交换机的控制命令处理，包括节点发现和信息查询
 */

#include "VSCtrlHelper.h"
#include "MacMap.h"
#include "Transport.h"
#ifdef _WIN32
#include "WinTapInterface.h"
#else
#include "TapInterface.h"
#endif
#include <algorithm>

#include "LinkKeeper.h"
#include "Config.h"

// 命令字定义
#define TVS_CMD_QUERY_PEERS TVS_CMD_PREFIX"QueryPeers"              ///< 查询对端列表命令
#define TVS_CMD_QUERY_PEERS_RESPONSE TVS_CMD_PREFIX"ReQueryPeers"   ///< 查询对端列表响应
#define TVS_CMD_QUERY_PEER_INFO TVS_CMD_PREFIX"QueryPeerInfo"       ///< 查询对端信息命令
#define TVS_CMD_QUERY_PEER_INFO_RESPONSE TVS_CMD_PREFIX"ReQueryPeerInfo" ///< 查询对端信息响应

/**
 * @brief 处理接收到的命令
 * @param buf 命令数据
 * @param peer 发送方地址
 * @param addr_len 地址长度
 * @param ttl 生存时间
 */
void VSCtrlHelper::handleCmd(const toolkit::Buffer::Ptr &buf, 
                           const sockaddr_storage& peer, 
                           int addr_len,
                           uint8_t ttl) {
    auto parts = toolkit::split(buf->data() + 12, ",");
    using request_handler = void (VSCtrlHelper::*)(const toolkit::Buffer::Ptr &buf, 
                                                  const sockaddr_storage& peer, 
                                                  int addr_len,
                                                  uint8_t ttl);
    static std::unordered_map<std::string, request_handler> s_cmd_functions;
    static toolkit::onceToken token([]() {
        s_cmd_functions.emplace(TVS_CMD_QUERY_PEERS, &VSCtrlHelper::OnQueryPeers);
        s_cmd_functions.emplace(TVS_CMD_QUERY_PEERS_RESPONSE, &VSCtrlHelper::OnQueryPeersResponse);
        s_cmd_functions.emplace(TVS_CMD_QUERY_PEER_INFO, &VSCtrlHelper::OnQueryPeerInfo);
        s_cmd_functions.emplace(TVS_CMD_QUERY_PEER_INFO_RESPONSE, &VSCtrlHelper::OnQueryPeerInfoResponse);
    });

    auto it = s_cmd_functions.find(parts.front());
    if (it == s_cmd_functions.end()) {
        return;
    }
    (this->*(it->second))(buf, peer, addr_len, ttl);
}

/**
 * @brief 处理查询对端列表请求
 * @details 返回本地MAC表中记录的所有对端信息
 */
void VSCtrlHelper::OnQueryPeers(const toolkit::Buffer::Ptr &buf, 
                               const sockaddr_storage& peer, 
                               int addr_len,
                               uint8_t ttl) {
    // 拷贝表，防止加锁影响正常交换
    auto macMap = MacMap::macMap();
    std::shared_ptr<toolkit::BufferLikeString> resp = std::make_shared<toolkit::BufferLikeString>();
    
    for (const auto &item: macMap) {
        if (!item.first || item.first == MAC_BROADCAST) {
            continue;
        }
        
        if (resp->empty()) {
            // 填充目标MAC
            uint64_t mac = 0;
            char *pMac = reinterpret_cast<char*>(&mac) + 2;
            resp->append(pMac, 6);
            
            // 填充来源MAC
            auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
            pMac = reinterpret_cast<char*>(&macLocal) + 2;
            resp->append(pMac, 6);
            
            // 填充返回命令字
            resp->append(TVS_CMD_QUERY_PEERS_RESPONSE",");
        }

        // 填充MAC-IP-端口信息
        std::string peerStr = StrPrinter << MacMap::uint64ToMacStr(item.first) << "-"
            << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&item.second.sock)) << "-"
            << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&item.second.sock)) << ",";
        resp->append(peerStr);

        // 分包发送，避免数据包过大
        if (resp->size() > 1000) {
            InfoL << "response mac map.";
            Transport::Instance().send(resp, peer, addr_len, true, ttl);
            resp = std::make_shared<toolkit::BufferLikeString>();
        }
    }
    Transport::Instance().send(resp, peer, addr_len, true, ttl);
}

/**
 * @brief 处理查询对端列表响应
 * @details 处理返回的对端信息，尝试建立P2P连接
 */
void VSCtrlHelper::OnQueryPeersResponse(const toolkit::Buffer::Ptr &buf, 
                                      const sockaddr_storage &peer, 
                                      int addr_len, 
                                      uint8_t ttl) {
    auto parts = toolkit::split(buf->toString(), ",");
    std::for_each(parts.begin() + 1, parts.end(), [&](const auto &item) {
        auto parts = toolkit::split(item, "-");
        if (parts.size() != 3) {
            return;
        }

        auto mac = MacMap::macToUint64(parts[0]);
        if (mac == MAC_BROADCAST) {
            return;
        }

        auto addr = parts[1];
        auto port = atoi(parts[2].c_str());
        auto peer = toolkit::SockUtil::make_sockaddr(addr.c_str(), port);
        bool gotPeer = false;
        auto macMapPeer = MacMap::getMacPeer(mac, gotPeer);

        // 检查是否需要建立P2P连接
        if (Config::macLocal != mac && compareSockAddr(macMapPeer, Config::corePeer)) {
            InfoL << "got mac peer " << parts[0] << " " 
                 << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer)) << ":"
                 << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer)) << " current "
                 << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&macMapPeer)) << ":"
                 << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&macMapPeer));

            // 尝试向远程返回地址表发送数据，打通P2P
            std::shared_ptr<int> retry = std::make_shared<int>();
            *retry = 10;
            EventPollerPool::Instance().getPoller()->doDelayTask(1000, [=]() {
                LinkKeeper::sendKeepData(mac, peer, 0);
                if (*retry) {
                    (*retry)--;
                    return 1000;
                }
                return 0;
            });
        } else {
            WarnL << "ignore mac peer " << parts[0] << " "
                 << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer)) << ":"
                 << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer)) << " current "
                 << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&macMapPeer)) << ":"
                 << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&macMapPeer));
        }
    });
}

/**
 * @brief 发送查询对端信息请求
 * @details 向核心节点查询其基本信息
 */
void VSCtrlHelper::SendQueryPeerInfo() {
    std::shared_ptr<toolkit::BufferLikeString> req = std::make_shared<toolkit::BufferLikeString>();
    
    // 填充目标MAC
    uint64_t mac = 0;
    char *pMac = reinterpret_cast<char*>(&mac) + 2;
    req->append(pMac, 6);
    
    // 填充来源MAC
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    pMac = reinterpret_cast<char*>(&macLocal) + 2;
    req->append(pMac, 6);
    
    // 填充查询命令字
    req->append(TVS_CMD_QUERY_PEER_INFO",");

    InfoL << "SendQueryPeerInfo to " 
          << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&Config::corePeer)) << ":"
          << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&Config::corePeer));

    Transport::Instance().send(req, Config::corePeer, sizeof(sockaddr_storage), true, Config::sendTtl);
}

/**
 * @brief 处理查询对端信息请求
 * @details 返回本节点的IP和MAC地址信息
 */
void VSCtrlHelper::OnQueryPeerInfo(const toolkit::Buffer::Ptr &buf, 
                                 const sockaddr_storage &peer, 
                                 int addr_len,
                                 uint8_t ttl) {
    std::shared_ptr<toolkit::BufferLikeString> resp = std::make_shared<toolkit::BufferLikeString>();
    
    // 填充目标MAC
    uint64_t mac = 0;
    char *pMac = reinterpret_cast<char*>(&mac) + 2;
    resp->append(pMac, 6);
    
    // 填充来源MAC
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    pMac = reinterpret_cast<char*>(&macLocal) + 2;
    resp->append(pMac, 6);
    
    // 填充返回命令字和本机信息
    resp->append(TVS_CMD_QUERY_PEER_INFO_RESPONSE",");
    resp->append(Config::localIp);
    resp->append(",");
    resp->append(MacMap::uint64ToMacStr(MacMap::macToUint64(TapInterface::Instance().hwaddr())));

    Transport::Instance().send(resp, peer, addr_len, true, Config::sendTtl);
}

/**
 * @brief 处理查询对端信息响应
 * @details 保存核心节点的IP和MAC地址信息
 */
void VSCtrlHelper::OnQueryPeerInfoResponse(const toolkit::Buffer::Ptr &buf, 
                                        const sockaddr_storage &peer, 
                                        int addr_len,
                                        uint8_t ttl) {
    auto parts = toolkit::split(buf->toString(), ",");
    auto corePeerIp = parts[1];
    auto corePeerMac = parts[2];
    InfoL << "CorePeer " << corePeerIp << " " << corePeerMac;
    Config::macCore = MacMap::macToUint64(corePeerMac);
}

/**
 * @brief 启动控制服务
 * @details 启动P2P发现和信息更新定时任务
 */
void VSCtrlHelper::Start() {
    // P2P 远端轮询
    if (Config::enableP2p) {
        VSCtrlHelper::Instance().SendQueryPeers();
        EventPollerPool::Instance().getPoller()->doDelayTask(60 * 1000, []() {
            VSCtrlHelper::Instance().SendQueryPeers();
            return 60 * 1000;
        });
    }

    // 定期刷新远程信息
    VSCtrlHelper::Instance().SendQueryPeerInfo();
    EventPollerPool::Instance().getPoller()->doDelayTask(30 * 1000, []() {
        VSCtrlHelper::Instance().SendQueryPeerInfo();
        return 30 * 1000;
    });
}

/**
 * @brief 发送查询对端列表请求
 * @details 向核心节点查询所有已知的对端信息
 */
void VSCtrlHelper::SendQueryPeers() {
    std::shared_ptr<toolkit::BufferLikeString> req = std::make_shared<toolkit::BufferLikeString>();
    
    // 填充目标MAC
    uint64_t mac = 0;
    char *pMac = reinterpret_cast<char*>(&mac) + 2;
    req->append(pMac, 6);
    
    // 填充来源MAC
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    pMac = reinterpret_cast<char*>(&macLocal) + 2;
    req->append(pMac, 6);
    
    // 填充查询命令字
    req->append(TVS_CMD_QUERY_PEERS",");

    InfoL << "send QueryPeers to " 
          << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&Config::corePeer)) << ":"
          << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&Config::corePeer));

    Transport::Instance().send(req, Config::corePeer, sizeof(sockaddr_storage), true, Config::sendTtl);
}

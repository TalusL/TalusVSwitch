//
// Created by liangzhuohua on 2024/9/6.
//
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

// 命令字列表
#define TVS_CMD_QUERY_PEERS TVS_CMD_PREFIX"QueryPeers"
#define TVS_CMD_QUERY_PEERS_RESPONSE TVS_CMD_PREFIX"ReQueryPeers"
#define TVS_CMD_QUERY_PEER_INFO TVS_CMD_PREFIX"QueryPeerInfo"
#define TVS_CMD_QUERY_PEER_INFO_RESPONSE TVS_CMD_PREFIX"ReQueryPeerInfo"


void VSCtrlHelper::handleCmd(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl){
    auto parts = toolkit::split(buf->data()+12,",");
    using request_handler = void (VSCtrlHelper::*)(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl);
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
    (this->*(it->second))(buf,peer,addr_len,ttl);
}
// 收到查询远端地址表请求
void VSCtrlHelper::OnQueryPeers(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl){
    // 拷贝表，防止加锁影响正常交换
    auto macMap = MacMap::macMap();
    std::shared_ptr<toolkit::BufferLikeString> resp = std::make_shared<toolkit::BufferLikeString>();
    for (const auto &item: macMap){
        if(!item.first||item.first==MAC_BROADCAST){
            continue;
        }
        if(resp->empty()){
            // 填充目标MAC
            uint64_t mac = 0;
            char *pMac = reinterpret_cast<char*>(&mac)+2;
            resp->append(pMac, 6);
            // 填充来源MAC
            auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
            pMac = reinterpret_cast<char*>(&macLocal)+2;
            resp->append(pMac, 6);
            // 填充返回命令字
            resp->append(TVS_CMD_QUERY_PEERS_RESPONSE",");
        }
        std::string peerStr = StrPrinter<<MacMap::uint64ToMacStr(item.first)<<"-"
                                         << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&item.second.sock))<< "-"
                                         << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&item.second.sock))<<",";
        //填充MAC-IP-端口，信息
        resp->append(peerStr);
        if(resp->size()>1000){
            InfoL<<"response mac map.";
            Transport::Instance().send(resp,peer,addr_len, true,ttl);
            resp = std::make_shared<toolkit::BufferLikeString>();
        }
    }
    Transport::Instance().send(resp,peer,addr_len, true,ttl);
}
// 收到查询远端地址表返回
void VSCtrlHelper::OnQueryPeersResponse(const toolkit::Buffer::Ptr &buf, const sockaddr_storage &peer, int addr_len, uint8_t ttl) {
    auto parts = toolkit::split(buf->toString(),",");
    std::for_each(parts.begin()+1, parts.end(), [&](const auto &item) {
        auto parts = toolkit::split(item,"-");
        if(parts.size()!=3){
            return;
        }
        auto mac = MacMap::macToUint64(parts[0]);
        if (mac == MAC_BROADCAST) {
            return;
        }
        auto addr = parts[1];
        auto port = atoi(parts[2].c_str());
        auto peer = toolkit::SockUtil::make_sockaddr(addr.c_str(),port);
        bool gotPeer = false;
        auto macMapPeer = MacMap::getMacPeer(mac,gotPeer);
        if(Config::macLocal!=mac&&compareSockAddr(macMapPeer,Config::corePeer)) {
            InfoL<<"got mac peer "<< parts[0] <<" " <<toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer)) << ":"
            << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer)) <<" current "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&macMapPeer)) << ":"
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
        }else {
            WarnL<<"ignore mac peer "<< parts[0] <<" " << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer)) << ":"
            << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer)) <<" current "
            << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&macMapPeer)) << ":"
            << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&macMapPeer));
        }
    });
}

void VSCtrlHelper::SendQueryPeerInfo() {
    std::shared_ptr<toolkit::BufferLikeString> req = std::make_shared<toolkit::BufferLikeString>();
    // 填充目标MAC
    uint64_t mac = 0;
    char *pMac = reinterpret_cast<char*>(&mac)+2;
    req->append(pMac, 6);
    // 填充来源MAC
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    pMac = reinterpret_cast<char*>(&macLocal)+2;
    req->append(pMac, 6);
    // 填充查询命令字
    req->append(TVS_CMD_QUERY_PEER_INFO",");
    // 发送查询指令
    InfoL<<"SendQueryPeerInfo to "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&Config::corePeer)) << ":"
        << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&Config::corePeer));
    Transport::Instance().send(req,Config::corePeer, sizeof(sockaddr_storage), true,Config::sendTtl);
}

void VSCtrlHelper::OnQueryPeerInfo(const toolkit::Buffer::Ptr &buf, const sockaddr_storage &peer, int addr_len,
                                   uint8_t ttl) {
    std::shared_ptr<toolkit::BufferLikeString> resp = std::make_shared<toolkit::BufferLikeString>();
    // 填充目标MAC
    uint64_t mac = 0;
    char *pMac = reinterpret_cast<char*>(&mac)+2;
    resp->append(pMac, 6);
    // 填充来源MAC
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    pMac = reinterpret_cast<char*>(&macLocal)+2;
    resp->append(pMac, 6);
    // 填充返回命令字
    resp->append(TVS_CMD_QUERY_PEERS_RESPONSE",");
    // 填充本机信息
    resp->append(Config::localIp);
    resp->append(",");
    resp->append(TapInterface::Instance().hwaddr());
    // 返回信息
    Transport::Instance().send(resp,peer,addr_len, true,Config::sendTtl);
}

void VSCtrlHelper::OnQueryPeerInfoResponse(const toolkit::Buffer::Ptr &buf, const sockaddr_storage &peer, int addr_len,
    uint8_t ttl) {
    auto parts = toolkit::split(buf->toString(),",");
    auto corePeerIp = parts[1];
    auto corePeerMac = parts[2];
    DebugL<<"CorePeer "<<corePeerIp<<" "<<corePeerMac;
    Config::macCore = MacMap::macToUint64(corePeerMac);
}

void VSCtrlHelper::Start() {
    // P2P 远端轮询
    if(Config::enableP2p) {
        VSCtrlHelper::Instance().SendQueryPeers();
        EventPollerPool::Instance().getPoller()->doDelayTask(60*1000,[](){
            VSCtrlHelper::Instance().SendQueryPeers();
            return 60*1000;
        });
    }
    // 定期刷新远程信息
    VSCtrlHelper::Instance().SendQueryPeerInfo();
    EventPollerPool::Instance().getPoller()->doDelayTask(30*1000,[](){
        VSCtrlHelper::Instance().SendQueryPeerInfo();
        return 30*1000;
    });
}

void VSCtrlHelper::SendQueryPeers() {
    std::shared_ptr<toolkit::BufferLikeString> req = std::make_shared<toolkit::BufferLikeString>();
    // 填充目标MAC
    uint64_t mac = 0;
    char *pMac = reinterpret_cast<char*>(&mac)+2;
    req->append(pMac, 6);
    // 填充来源MAC
    auto macLocal = MacMap::macToUint64(TapInterface::Instance().hwaddr());
    pMac = reinterpret_cast<char*>(&macLocal)+2;
    req->append(pMac, 6);
    // 填充查询命令字
    req->append(TVS_CMD_QUERY_PEERS",");
    // 发送查询指令
    InfoL<<"send QueryPeers to "<< toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&Config::corePeer)) << ":"
        << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&Config::corePeer));
    Transport::Instance().send(req,Config::corePeer, sizeof(sockaddr_storage), true,Config::sendTtl);
}

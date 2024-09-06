//
// Created by liangzhuohua on 2024/9/6.
//
#include "VSCtrlHelper.h"
#include "MacMap.h"
#include "Transport.h"
#include "TapInterface.h"

void VSCtrlHelper::handleCmd(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl){
    auto parts = toolkit::split(buf->toString(),",");
    using request_handler = void (VSCtrlHelper::*)(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl);
    static std::unordered_map<std::string, request_handler> s_cmd_functions;
    static toolkit::onceToken token([]() {
        s_cmd_functions.emplace("QueryPeers", &VSCtrlHelper::QueryPeers);
    });
    auto it = s_cmd_functions.find(parts.front());
    if (it == s_cmd_functions.end()) {
        return;
    }
    (this->*(it->second))(buf,peer,addr_len,ttl);
}
// 收到查询远端地址表请求
void VSCtrlHelper::QueryPeers(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl){
    // 拷贝表，防止加锁影响正常交换
    auto macMap = MacMap::macMap();
    std::shared_ptr<toolkit::BufferLikeString> resp = std::make_shared<toolkit::BufferLikeString>();
    for (const auto &item: macMap){
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
            resp->append("ReQueryPeers,");
        }
        std::string peerStr = StrPrinter<<MacMap::uint64ToMacStr(item.first)<<"-"
                                         << toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer))<< "-"
                                         << toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer))<<",";
        //填充MAC-IP-端口，信息
        resp->append(peerStr);
        if(resp->size()>1000){
            Transport::Instance().send(resp,peer,addr_len, true,ttl);
        }
    }
}
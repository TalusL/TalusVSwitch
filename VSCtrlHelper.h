//
// Created by liangzhuohua on 2024/9/5.
//

#ifndef TALUSVSWITCH_P2PHELPER_H
#define TALUSVSWITCH_P2PHELPER_H

#include <Network/Buffer.h>
#include <Network/Socket.h>

class VSCtrlHelper{
public:
    static VSCtrlHelper& Instance(){
        static VSCtrlHelper vsCtrlHelper;
        return vsCtrlHelper;
    }
    // 命令分发
    void handleCmd(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl);
    // 发送查询地址表请求
    void SendQueryPeers();
    // 收到查询远端地址表请求
    void QueryPeers(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl);
    // 收到查询远端地址表返回
    void ReQueryPeers(const toolkit::Buffer::Ptr &buf, const sockaddr_storage& peer, int addr_len,uint8_t ttl);
    // 尝试启用P2P
    void EnableP2P();
};

#endif//TALUSVSWITCH_P2PHELPER_H

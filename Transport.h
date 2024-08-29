//
// Created by liangzhuohua on 2024/8/27.
//

#ifndef TALUSVSWITCH_TRANSPORT_H
#define TALUSVSWITCH_TRANSPORT_H

#include <Network/Socket.h>

class Transport{
public:
    static Transport & Instance(){
        static Transport transport;
        return transport;
    }
    void start(uint16_t port, const std::string &local_ip = "::", bool enable_reuse = true){
        _sock = toolkit::Socket::createSocket();
        _sock->bindUdpSock(port,local_ip,enable_reuse);
    }
    void setOnRead(const toolkit::Socket::onReadCB& cb){
        _sock->setOnRead(cb);
    }
    void send(const toolkit::Buffer::Ptr& buf,const sockaddr_storage& addr, socklen_t addr_len = 0, bool try_flush = true){
        getPoller()->async([=](){
            Instance()._sock->send(buf, reinterpret_cast<sockaddr*>(const_cast<sockaddr_storage*>(&addr)),addr_len,try_flush);
        });
    }
    toolkit::EventPoller::Ptr getPoller(){
        return _sock->getPoller();
    }
protected:
    toolkit::Socket::Ptr _sock;
};

#endif//TALUSVSWITCH_TRANSPORT_H

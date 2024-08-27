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
    template <typename... ArgsType>
    ssize_t send(ArgsType &&...args){
        return _sock->send(std::forward<ArgsType>(args)...);
    }
protected:
    toolkit::Socket::Ptr _sock;
};

#endif//TALUSVSWITCH_TRANSPORT_H

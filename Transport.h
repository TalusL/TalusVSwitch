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
    void setOnRead(const std::function<void(const toolkit::Buffer::Ptr &buf,const sockaddr_storage& pktRecvPeer, int addr_len,uint8_t ttl)>& cb){
        auto poller = getPoller();
        _sock->setOnRead([cb,poller](toolkit::Buffer::Ptr &buf, struct sockaddr *addr, int addr_len){
            sockaddr_storage pktRecvPeer{};
            if (addr) {
                auto addrLen = addr_len ? addr_len : toolkit::SockUtil::get_sock_len(addr);
                memcpy(&pktRecvPeer, addr, addrLen);
            }
            uint8_t ttl = buf->data()[0] ^ buf->data()[buf->size()-1];
            auto dd = decompress(buf);
            if(cb){
                cb(dd,pktRecvPeer,addr_len,ttl);
            }
        });
    }
    void send(const toolkit::Buffer::Ptr& buf,const sockaddr_storage& addr, socklen_t addr_len, bool try_flush ,uint8_t ttl){
        auto poller = getPoller();
        toolkit::EventPollerPool::Instance().getPoller()->async([poller, buf, ttl, addr, addr_len, try_flush](){
            auto cd = compress(buf);
            cd->data()[0] = (char)(ttl^cd->data()[cd->size()-1]);
            cd->data()[1] = cd->data()[cd->size()-2];
            poller->async([=](){
                Instance()._sock->send(cd, reinterpret_cast<sockaddr*>(const_cast<sockaddr_storage*>(&addr)),addr_len,try_flush);
            },false);
        },false);
    }
    toolkit::EventPoller::Ptr getPoller(){
        return _sock->getPoller();
    }
protected:
    toolkit::Socket::Ptr _sock;
};

#endif//TALUSVSWITCH_TRANSPORT_H

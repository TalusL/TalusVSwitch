//
// Created by liangzhuohua on 2024/8/20.
//

#ifndef TUNNEL_MACMAP_H
#define TUNNEL_MACMAP_H

#include <functional>
#include <iomanip>
#include <sstream>
#include <sys/socket.h>
#include <unordered_map>
#include <Util/util.h>
#include <Util/TimeTicker.h>
#include "Utils.h"

#define MAC_BROADCAST (uint64_t)(0xFFFFFFFFFFFFFFFF << 16)

class MacMap{
public:
    class MacPeer{
    public:
        sockaddr_storage sock;
        uint8_t ttl{};
        toolkit::Ticker ticker;
    };
    static uint64_t macToUint64(const std::string& macAddress) {
        uint64_t addr = 0;
        auto *a = reinterpret_cast<uint8_t *>(&addr);
        auto parts = toolkit::split(macAddress,":");
        if(parts.size()!=6){
            return addr;
        }
        int i = 2;
        for (auto &item: parts){
            *(a+i) = std::stoi(item, nullptr, 16);;
            i++;
        }
        return addr;
    }
    static std::string uint64ToMacStr(uint64_t mac) {
        std::ostringstream oss;
        auto * p = reinterpret_cast<uint8_t *>(&mac);
        for (int i = 2; i <= 7; i++) {
            oss <<  std::setw(2) << std::setfill('0')<< std::hex << (int)*(p+i);
            if (i!= 7) oss << ':';
        }
        return oss.str();
    }
    static void addMacPeer(const std::string& mac,const sockaddr_storage& peer,uint8_t ttl){
        addMacPeer(macToUint64(mac),peer,ttl);
    }
    static void addMacPeer(uint64_t mac,const sockaddr_storage& peer,uint8_t ttl){
        auto peerInfo = macMap()[mac];
        if(!compareSockAddr(peerInfo.sock,peer)){
            if( peerInfo.ttl <= ttl ){
                peerInfo.sock = peer;
                peerInfo.ticker.resetTime();
                peerInfo.ttl = ttl;

                DebugL<<"Peer:"<<MacMap::uint64ToMacStr(mac)
                      <<" - "
                      <<toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&peer))
                      <<":"
                      <<toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&peer));
            }
        }else{
            peerInfo.ticker.resetTime();
        }
    }
    static sockaddr_storage getMacPeer(uint64_t mac,bool& got){
        if(macMap().find(mac) != macMap().end()){
            got = true;
            return macMap()[mac].sock;
        }
        got = false;
        return macMap()[MAC_BROADCAST].sock;
    }
    static sockaddr_storage getMacPeer(const std::string& mac,bool& got){
        return getMacPeer(macToUint64(mac),got);
    }

    static std::unordered_map<uint64_t,MacPeer>& macMap(){
        static std::unordered_map<uint64_t,MacPeer> _macMap;
        return _macMap;
    }
    static void forEach(const std::function<void(uint64_t mac,sockaddr_storage addr)>& cb){
        for (auto & it : macMap()) {
            cb(it.first,it.second.sock);
        }
    }
};

#endif//TUNNEL_MACMAP_H

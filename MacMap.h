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

#define MAC_BROADCAST (uint64_t)(0xFFFFFFFFFFFFFFFF << 16)

class MacMap{
public:
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
    static void addMacPeer(const std::string& mac,const sockaddr_storage& peer){
        macMap()[macToUint64(mac)] = peer;
    }
    static void addMacPeer(uint64_t mac,const sockaddr_storage& peer){
        macMap()[mac] = peer;
    }
    static sockaddr_storage getMacPeer(uint64_t mac,bool& got){
        if(macMap().find(mac) != macMap().end()){
            got = true;
            return macMap()[mac];
        }
        got = false;
        return macMap()[MAC_BROADCAST];
    }
    static sockaddr_storage getMacPeer(const std::string& mac,bool& got){
        if(macMap().find(macToUint64(mac)) != macMap().end()){
            got = true;
            return macMap()[macToUint64(mac)];
        }
        got = false;
        return macMap()[MAC_BROADCAST];
    }

    static std::unordered_map<uint64_t,sockaddr_storage>& macMap(){
        static std::unordered_map<uint64_t,sockaddr_storage> _macMap;
        return _macMap;
    }
    static void forEach(const std::function<void(uint64_t mac,sockaddr_storage addr)>& cb){
        for (auto & it : macMap()) {
            cb(it.first,it.second);
        }
    }
};

#endif//TUNNEL_MACMAP_H

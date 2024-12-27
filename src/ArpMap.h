//
// Created by liangzhuohua on 2024/12/27.
//

#ifndef ARPMAP_H
#define ARPMAP_H

#include <MacMap.h>
#include <Network/Buffer.h>
#include <Thread/ThreadPool.h>

// 定义ARP请求/响应的结构
#pragma pack(push, 1)
struct ARPPacket {
    uint16_t hardwareType; // 硬件类型
    uint16_t protocolType; // 协议类型
    uint8_t hardwareSize; // 硬件地址大小
    uint8_t protocolSize; // 协议地址大小
    uint16_t operation; // 操作码：请求=1, 响应=2
    uint8_t senderMAC[6]; // 发送者 MAC 地址
    uint32_t senderIP; // 发送者 IP 地址
    uint8_t targetMAC[6]; // 目标 MAC 地址
    uint32_t targetIP; // 目标 IP 地址
};
#pragma pack(pop)

class ArpMap {
public:
    static void checkArp(const toolkit::Buffer::Ptr &buf,
                         const sockaddr_storage &pktRecvPeer, int addr_len, uint8_t ttl) {
        static toolkit::ThreadPool arpCheckTh = toolkit::ThreadPool(1, toolkit::ThreadPool::PRIORITY_NORMAL);
        arpCheckTh.async([buf]() {
            if (buf->size() < 14 + sizeof(ARPPacket)) {
                return;
            }
            auto type = htons(*(uint16_t *) ((uint8_t *) buf->data() + 12));
            // 过滤非arp
            if (type != 0x0806) {
                return;
            }
            ARPPacket arpPacket{};
            memcpy(&arpPacket, buf->data() + 14, sizeof(ARPPacket));

            // 检查请求类型
            auto opcode = ntohs(arpPacket.operation);
            if (opcode != 0x0001 && opcode != 0x0002) {
                return;
            }
            auto sendIp = arpPacket.senderIP;
            uint64_t sendMac{};
            memcpy(((uint8_t *) &sendMac) + 2, arpPacket.senderMAC, 6);
            auto targetIP = arpPacket.targetIP;
            uint64_t targetMAC{};
            memcpy(((uint8_t *) &targetMAC) + 2, arpPacket.targetMAC, 6);
            // 检查ARP并记录
            if (sendIp && sendMac && sendIp != 0xffffffff && sendMac != MAC_BROADCAST) {
                addArp(sendIp, sendMac);
            }
            if (targetIP && targetMAC && targetIP != 0xffffffff && targetMAC != MAC_BROADCAST) {
                addArp(sendIp, sendMac);
            }
        });
    }

    static uint64_t getMac(uint32_t ip) {
        std::lock_guard<std::mutex> lck(arpMutex());
        return arpMap().find(ip)==arpMap().end() ? 0 : arpMap()[ip];
    }
    static void delMac(uint64_t mac) {
        std::lock_guard<std::mutex> lck(arpMutex());
        erase_if(arpMap(), [mac](const auto& pair) {
            return pair.second == mac;
        });
    }
    static void addArp(uint32_t ip, uint64_t mac) {
        std::lock_guard<std::mutex> lck(arpMutex());
        arpMap()[ip] = mac;
    }
    static void delArp(uint32_t ip) {
        std::lock_guard<std::mutex> lck(arpMutex());
        arpMap().erase(ip);
    }
protected:
    static std::unordered_map<uint32_t, uint64_t> &arpMap() {
        static std::unordered_map<uint32_t, uint64_t> _arpMap;
        return _arpMap;
    }

    static std::mutex &arpMutex() {
        static std::mutex mtx;
        return mtx;
    }
};

#endif //ARPMAP_H

/**
 * @file Transport.h
 * @brief 网络传输层封装
 * @details 提供可靠的网络数据传输功能，处理数据的压缩和解压缩
 */

#ifndef TALUSVSWITCH_TRANSPORT_H
#define TALUSVSWITCH_TRANSPORT_H

#include "Config.h"
#include "VSCtrlHelper.h"
#include <Network/Socket.h>
#include "ArpMap.h"

/**
 * @class Transport
 * @brief 网络传输管理类
 * @details 负责网络数据的发送和接收，包括：
 * - UDP Socket的创建和管理
 * - 数据的压缩和解压缩
 * - 命令数据的识别和处理
 */
class Transport {
public:
    /**
     * @brief 获取Transport单例
     * @return Transport& 单例引用
     */
    static Transport& Instance() {
        static Transport transport;
        return transport;
    }

    /**
     * @brief 启动传输服务
     * @param port 监听端口
     * @param local_ip 本地IP地址
     * @param enable_reuse 是否允许端口重用
     * @details 创建并初始化UDP Socket，开始监听指定端口
     */
    void start(uint16_t port, const std::string& local_ip = "::", bool enable_reuse = true) {
        _sock = toolkit::Socket::createSocket();
        _sock->bindUdpSock(port, local_ip, enable_reuse);
    }

    /**
     * @brief 设置数据接收回调
     * @param cb 回调函数
     * @details 回调函数处理接收到的数据，包括：
     * - 数据包内容
     * - 发送方地址
     * - 地址长度
     * - TTL值
     * - 是否为TVS命令
     */
    void setOnRead(const std::function<void(const toolkit::Buffer::Ptr& buf,
        const sockaddr_storage& pktRecvPeer, int addr_len, uint8_t ttl, bool isTvsCmd)>& cb) {
        auto poller = getPoller();
        _sock->setOnRead([cb, poller](toolkit::Buffer::Ptr& buf, struct sockaddr* addr, int addr_len) {
            sockaddr_storage pktRecvPeer{};
            if (addr) {
                auto addrLen = addr_len ? addr_len : toolkit::SockUtil::get_sock_len(addr);
                memcpy(&pktRecvPeer, addr, addrLen);
            }
            uint8_t ttl = buf->data()[0] ^ buf->data()[buf->size()-1];
            auto dd = decompress(buf);
            uint64_t dMac = *(uint64_t*)dd->data();
            dMac = dMac << 16;
            auto isTvsCmd = strncmp(dd->data() + 12, TVS_CMD_PREFIX, strlen(TVS_CMD_PREFIX)) == 0;
            
            if (cb && dMac) {
                cb(dd, pktRecvPeer, addr_len, ttl, isTvsCmd);
            }
            
            if (isTvsCmd) {
                // 执行命令处理
                toolkit::EventPollerPool::Instance().getPoller()->async([dd, pktRecvPeer, addr_len, ttl, dMac]() {
                    VSCtrlHelper::Instance().handleCmd(dd, pktRecvPeer, addr_len, ttl);
                }, false);
            }
        });
    }

    /**
     * @brief 发送数据
     * @param buf 要发送的数据
     * @param addr 目标地址
     * @param addr_len 地址长度
     * @param try_flush 是否尝试立即发送
     * @param ttl 生存时间
     * @details 压缩数据并通过UDP发送
     */
    void send(const toolkit::Buffer::Ptr& buf, const sockaddr_storage& addr, 
             socklen_t addr_len, bool try_flush, uint8_t ttl) {
        auto poller = getPoller();
        toolkit::EventPollerPool::Instance().getPoller()->async([poller, buf, ttl, addr, addr_len, try_flush]() {
            auto cd = compress(buf);
            if (cd->size() > Config::mtu) {
                WarnL << "WTF! compressedData is bigger than mtu " << cd->size() << " -> " << buf->size();
            }
            cd->data()[0] = (char)(ttl ^ cd->data()[cd->size()-1]);
            cd->data()[1] = cd->data()[cd->size()-2];
            poller->async([=]() {
                Instance()._sock->send(cd, reinterpret_cast<sockaddr*>(const_cast<sockaddr_storage*>(&addr)), 
                                     addr_len, try_flush);
            }, false);
        }, false);
    }

    /**
     * @brief 获取事件轮询器
     * @return toolkit::EventPoller::Ptr 事件轮询器指针
     */
    toolkit::EventPoller::Ptr getPoller() {
        return _sock->getPoller();
    }

protected:
    toolkit::Socket::Ptr _sock;  ///< UDP Socket指针
};

#endif //TALUSVSWITCH_TRANSPORT_H

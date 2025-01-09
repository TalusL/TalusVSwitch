/**
 * @file VSwitch.h
 * @brief 虚拟交换机核心类定义
 * @details 定义了虚拟交换机的核心功能接口，包括：
 * - 数据包的转发和处理
 * - MAC地址学习
 * - 广播包处理
 * - TAP接口数据轮询
 */

#ifndef TALUSVSWITCH_VSWITCH_H
#define TALUSVSWITCH_VSWITCH_H

#include "Poller/EventPoller.h"
#include <cstdint>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <ws2def.h>
#else
#include <sys/socket.h>
#endif

/**
 * @class VSwitch
 * @brief 虚拟交换机类
 * @details 实现了虚拟交换机的核心功能，包括：
 * - 数据包的接收和转发
 * - MAC地址表的维护
 * - 广播包的处理
 * - TAP接口的数据处理
 */
class VSwitch {
public:
    /**
     * @brief 启动虚拟交换机
     * @details 初始化并启动：
     * - 数据包处理线程
     * - 网络事件回调
     * - TAP接口轮询
     */
    static void start();

    /**
     * @brief 停止虚拟交换机
     * @details 停止所有处理线程和数据转发
     */
    static void stop();

protected:
    /**
     * @brief 设置网络数据接收回调
     * @param corePeer 核心节点地址
     * @param macLocal 本地MAC地址
     * @details 处理从网络接收到的数据包：
     * - MAC地址学习
     * - ARP包处理
     * - 数据包转发
     */
    static void setupOnPeerInput(const sockaddr_storage& corePeer, uint64_t macLocal);

    /**
     * @brief 轮询TAP接口数据
     * @param buf 数据缓冲区
     * @details 持续读取TAP接口数据并处理：
     * - 解析目标MAC地址
     * - 查找目标节点
     * - 转发数据包
     */
    static void pollInterface(const std::shared_ptr<std::vector<uint8_t>>& buf);

    /**
     * @brief 处理广播数据包
     * @param buf 数据包内容
     * @param pktRecvPeer 数据包来源地址
     * @param ttl 生存时间
     * @details 将广播包转发给所有已知节点，除了发送者
     */
    static void sendBroadcast(const toolkit::Buffer::Ptr& buf,
                            const sockaddr_storage& pktRecvPeer,
                            uint8_t ttl);

    static volatile bool m_running;                    ///< 运行状态标志
    static std::shared_ptr<toolkit::ThreadPool> m_thread;  ///< 数据处理线程池
};

#endif //TALUSVSWITCH_VSWITCH_H

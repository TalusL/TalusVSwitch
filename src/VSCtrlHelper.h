/**
 * @file VSCtrlHelper.h
 * @brief 虚拟交换机控制助手
 * @details 提供虚拟交换机的控制功能，包括：
 * - 节点发现和P2P连接建立
 * - 对端信息查询和更新
 * - 命令处理和响应
 */

#ifndef TALUSVSWITCH_P2PHELPER_H
#define TALUSVSWITCH_P2PHELPER_H

#include <Network/Buffer.h>
#include <Network/Socket.h>

/// TVS命令前缀，用于识别控制命令
#define TVS_CMD_PREFIX "TVS_"

/**
 * @class VSCtrlHelper
 * @brief 虚拟交换机控制类
 * @details 实现虚拟交换机的控制功能，采用单例模式
 */
class VSCtrlHelper {
public:
    /**
     * @brief 获取VSCtrlHelper单例
     * @return VSCtrlHelper& 单例引用
     */
    static VSCtrlHelper& Instance() {
        static VSCtrlHelper vsCtrlHelper;
        return vsCtrlHelper;
    }

    /**
     * @brief 命令分发处理
     * @param buf 命令数据
     * @param peer 发送方地址
     * @param addr_len 地址长度
     * @param ttl 生存时间
     * @details 根据命令类型分发到对应的处理函数
     */
    void handleCmd(const toolkit::Buffer::Ptr &buf, 
                  const sockaddr_storage& peer, 
                  int addr_len,
                  uint8_t ttl);

    /**
     * @brief 发送查询对端列表请求
     * @details 向核心节点发送查询请求，获取所有已知的对端信息
     */
    void SendQueryPeers();

    /**
     * @brief 处理查询对端列表请求
     * @param buf 请求数据
     * @param peer 发送方地址
     * @param addr_len 地址长度
     * @param ttl 生存时间
     * @details 返回本地MAC表中记录的所有对端信息
     */
    void OnQueryPeers(const toolkit::Buffer::Ptr &buf, 
                     const sockaddr_storage& peer, 
                     int addr_len,
                     uint8_t ttl);

    /**
     * @brief 处理查询对端列表响应
     * @param buf 响应数据
     * @param peer 发送方地址
     * @param addr_len 地址长度
     * @param ttl 生存时间
     * @details 处理返回的对端信息，尝试建立P2P连接
     */
    void OnQueryPeersResponse(const toolkit::Buffer::Ptr &buf, 
                            const sockaddr_storage& peer, 
                            int addr_len,
                            uint8_t ttl);

    /**
     * @brief 发送查询对端信息请求
     * @details 向核心节点查询其基本信息
     */
    void SendQueryPeerInfo();

    /**
     * @brief 处理查询对端信息请求
     * @param buf 请求数据
     * @param peer 发送方地址
     * @param addr_len 地址长度
     * @param ttl 生存时间
     * @details 返回本节点的IP和MAC地址信息
     */
    void OnQueryPeerInfo(const toolkit::Buffer::Ptr &buf, 
                        const sockaddr_storage& peer, 
                        int addr_len,
                        uint8_t ttl);

    /**
     * @brief 处理查询对端信息响应
     * @param buf 响应数据
     * @param peer 发送方地址
     * @param addr_len 地址长度
     * @param ttl 生存时间
     * @details 保存核心节点的IP和MAC地址信息
     */
    void OnQueryPeerInfoResponse(const toolkit::Buffer::Ptr &buf, 
                               const sockaddr_storage& peer, 
                               int addr_len,
                               uint8_t ttl);

    /**
     * @brief 启动控制服务
     * @details 启动P2P发现和信息更新定时任务：
     * - 如果启用P2P，每60秒查询一次对端列表
     * - 每30秒更新一次核心节点信息
     */
    void Start();
};

#endif //TALUSVSWITCH_P2PHELPER_H

/**
 * @file Config.h
 * @brief 全局配置参数定义
 * @details 定义了虚拟交换机的全局配置参数
 */

#ifndef TALUSVSWITCH_CONFIG_H
#define TALUSVSWITCH_CONFIG_H

/**
 * @namespace Config
 * @brief 全局配置命名空间
 * @details 包含所有虚拟交换机运行所需的全局配置参数
 */
namespace Config {
    extern std::string interfaceName;  ///< 网络接口名称
    extern volatile bool debug;        ///< 调试模式开关
    extern uint8_t sendTtl;           ///< 发送数据包的TTL值
    extern sockaddr_storage corePeer;  ///< 核心节点地址
    extern uint64_t macLocal;         ///< 本地MAC地址
    extern uint64_t macCore;          ///< 核心节点MAC地址
    extern int mtu;                   ///< 最大传输单元
    extern std::string localIp;       ///< 本地IP地址
    extern int mask;                  ///< 子网掩码
    extern bool enableP2p;            ///< 是否启用P2P功能
};

#endif //TALUSVSWITCH_CONFIG_H

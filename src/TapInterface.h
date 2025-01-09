/**
 * @file TapInterface.h
 * @brief TAP网络接口基类
 * @details 定义了TAP虚拟网络接口的基本接口，作为跨平台实现的基类
 */

#ifndef TUNNEL_TAPINTERFACE_H
#define TUNNEL_TAPINTERFACE_H

#include "tuntap++.hh"

/**
 * @class TapInterface
 * @brief TAP接口管理类
 * @details 继承自tuntap::tap，提供TAP设备的基本操作接口
 * 
 * TapInterface类是对TAP设备的高层封装，提供了：
 * - 设备的创建和销毁
 * - 网络配置(IP、掩码等)
 * - 数据的收发
 * 
 * 该类采用单例模式，确保系统中只有一个TAP设备实例
 */
class TapInterface : public tuntap::tap {
public:
    /**
     * @brief 获取TapInterface单例
     * @return TapInterface& 单例引用
     * @details 确保整个系统只有一个TAP设备实例
     */
    static TapInterface& Instance() {
        static TapInterface tapInterface;
        return tapInterface;
    }
};

#endif //TUNNEL_TAPINTERFACE_H

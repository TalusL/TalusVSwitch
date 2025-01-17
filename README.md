# TalusVSwitch

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20|%20Linux-lightgrey.svg)

TalusVSwitch 是一个高性能的虚拟二层交换机实现，专为构建大规模虚拟网络设计。它提供了类似物理交换机的功能，但具有更强的灵活性和可编程性。

## 🌟 核心特性

### 🚀 高性能网络引擎
- **异步I/O处理**: 采用事件驱动模型，高效处理网络数据
- **智能数据压缩**: 自动压缩传输数据，降低网络带宽占用
- **线程池调度**: 优化的线程池设计，提供稳定的性能表现
- **零拷贝优化**: 减少数据复制，提升处理效率

### 🌐 智能交换功能
- **MAC地址学习**: 自动学习和维护MAC地址表
- **ARP协议支持**: 内置ARP协议处理，优化地址解析
- **P2P直连优化**: 支持节点间直接通信，降低延迟
- **广播控制**: 内置TTL机制，有效防止广播风暴

### 🛡️ 可靠性保障
- **链路保持**: 自动维护节点间连接状态
- **故障恢复**: 快速检测和处理链路故障
- **状态监控**: 实时监控网络状态和性能指标
- **日志追踪**: 详细的日志记录，便于问题诊断

### 🔌 跨平台支持
- **Windows支持**: 完整支持Windows TAP设备
- **Linux支持**: 支持Linux TAP/TUN设备
- **统一接口**: 跨平台统一的API设计

## 📦 安装使用

### 依赖环境
- CMake 3.10+
- C++11 兼容的编译器
- Windows: TAP-Windows驱动
- Linux: TAP/TUN内核模块

### 编译安装
```bash
#克隆仓库
git clone https://github.com/your-username/TalusVSwitch.git
cd TalusVSwitch
#创建构建目录
mkdir build && cd build
#配置并编译
cmake ..
make
安装
sudo make install
```

### 基本使用
```bash
#启动虚拟交换机
talusvswitch -name tvs0 -addr 10.0.0.1 -mask 24 -remote 192.168.1.100 -port 9001
```

## ⚙️ 配置参数

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| name | 接口名称 | tvs0 | -name tvs0 |
| addr | 本地IP地址 | - | -addr 10.0.0.1 |
| mask | 子网掩码 | 24 | -mask 24 |
| remote | 远程节点地址 | - | -remote 192.168.1.100 |
| port | 监听端口 | 9001 | -port 9001 |
| mtu | MTU大小 | 1400 | -mtu 1400 |
| ttl | TTL值 | 8 | -ttl 8 |
| p2p | 启用P2P | true | -p2p 1 |
| debug | 调试模式 | false | -debug 1 |

## 🎯 应用场景

### 数据中心互联
- 构建安全的跨数据中心网络
- 优化跨区域网络传输
- 简化网络管理和配置

### 云原生网络
- 为容器提供网络互联
- 支持微服务通信
- 构建服务网格基础设施

### 开发测试环境
- 提供隔离的网络环境
- 支持网络性能测试
- 便于故障模拟和调试

## 📊 性能指标

- **吞吐量**: 单节点转发性能 1Gbps+
- **延迟**: 本地转发延迟 < 1ms
- **资源占用**:
    - CPU: 正常负载 < 5%
    - 内存: 稳定运行 < 50MB
- **可扩展性**: 支持100+节点规模部署

## 🤝 参与贡献

欢迎提交Issue和Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交变更 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交Pull Request

## 📄 开源协议

本项目采用 MIT 协议开源，详见 [LICENSE](LICENSE) 文件。

## 🙏 致谢

感谢以下开源项目的支持：
- [ZLToolKit](https://github.com/ZLMediaKit/ZLToolKit)
- [TAP-Windows](https://github.com/OpenVPN/tap-windows6)

## 📚 文档

更多详细信息，请参考我们的[在线文档](docs/index.md)。

## 📞 联系我们

如有问题或建议，欢迎通过以下方式联系：
- 提交 Issue
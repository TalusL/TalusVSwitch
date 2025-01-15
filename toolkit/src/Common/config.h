/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include "Util/NoticeCenter.h"
#include "Util/mini.h"
#include "Util/onceToken.h"
#include "macros.h"
#include <functional>

namespace toolkit {

// 加载配置文件，如果配置文件不存在，那么会导出默认配置并生成配置文件
// 加载配置文件成功后会触发kBroadcastUpdateConfig广播
// 如果指定的文件名(ini_path)为空，那么会加载默认配置文件
// 默认配置文件名为 /path/to/your/exe.ini
// 加载配置文件成功后返回true，否则返回false
bool loadIniConfig(const char *ini_path = nullptr);

////////////广播名称///////////
namespace Broadcast {

// 收到http api请求广播
extern const std::string kBroadcastHttpRequest;
#define BroadcastHttpRequestArgs const Parser &parser, const HttpSession::HttpResponseInvoker &invoker, bool &consumed, SockInfo &sender

// 在http文件服务器中,收到http访问文件或目录的广播,通过该事件控制访问http目录的权限
extern const std::string kBroadcastHttpAccess;
#define BroadcastHttpAccessArgs const Parser &parser, const std::string &path, const bool &is_dir, const HttpSession::HttpAccessPathInvoker &invoker, SockInfo &sender

// 在http文件服务器中,收到http访问文件或目录前的广播,通过该事件可以控制http url到文件路径的映射
// 在该事件中通过自行覆盖path参数，可以做到譬如根据虚拟主机或者app选择不同http根目录的目的
extern const std::string kBroadcastHttpBeforeAccess;
#define BroadcastHttpBeforeAccessArgs const Parser &parser, std::string &path, SockInfo &sender

// 更新配置文件事件广播,执行loadIniConfig函数加载配置文件成功后会触发该广播
extern const std::string kBroadcastReloadConfig;
#define BroadcastReloadConfigArgs void

#define ReloadConfigTag ((void *)(0xFF))
#define RELOAD_KEY(arg, key)                                                                                           \
    do {                                                                                                               \
        decltype(arg) arg##_tmp = ::toolkit::mINI::Instance()[key];                                                    \
        if (arg == arg##_tmp) {                                                                                        \
            return;                                                                                                    \
        }                                                                                                              \
        arg = arg##_tmp;                                                                                               \
        InfoL << "reload config:" << key << "=" << arg;                                                                \
    } while (0)

// 监听某个配置发送变更
#define LISTEN_RELOAD_KEY(arg, key, ...)                                                                               \
    do {                                                                                                               \
        static ::toolkit::onceToken s_token_listen([]() {                                                              \
            ::toolkit::NoticeCenter::Instance().addListener(                                                           \
                ReloadConfigTag, Broadcast::kBroadcastReloadConfig, [](BroadcastReloadConfigArgs) { __VA_ARGS__; });   \
        });                                                                                                            \
    } while (0)

#define GET_CONFIG(type, arg, key)                                                                                     \
    static type arg = ::toolkit::mINI::Instance()[key];                                                                \
    LISTEN_RELOAD_KEY(arg, key, { RELOAD_KEY(arg, key); });

#define GET_CONFIG_FUNC(type, arg, key, ...)                                                                           \
    static type arg;                                                                                                   \
    do {                                                                                                               \
        static ::toolkit::onceToken s_token_set([]() {                                                                 \
            static auto lam = __VA_ARGS__;                                                                             \
            static auto arg##_str = ::toolkit::mINI::Instance()[key];                                                  \
            arg = lam(arg##_str);                                                                                      \
            LISTEN_RELOAD_KEY(arg, key, {                                                                              \
                RELOAD_KEY(arg##_str, key);                                                                            \
                arg = lam(arg##_str);                                                                                  \
            });                                                                                                        \
        });                                                                                                            \
    } while (0)

} // namespace Broadcast

////////////通用配置///////////
namespace General {
// 是否启动虚拟主机
extern const std::string kEnableVhost;
// 合并写缓存大小(单位毫秒)，合并写指服务器缓存一定的数据后才会一次性写入socket，这样能提高性能，但是会提高延时
// 开启后会同时关闭TCP_NODELAY并开启MSG_MORE
extern const std::string kMergeWriteMS;
} // namespace General

////////////HTTP配置///////////
namespace Http {
// http 端口
extern const std::string kPort;
// http 文件发送缓存大小
extern const std::string kSendBufSize;
// http 最大请求字节数
extern const std::string kMaxReqSize;
// http keep-alive秒数
extern const std::string kKeepAliveSecond;
// http 字符编码
extern const std::string kCharSet;
// http 服务器根目录
extern const std::string kRootPath;
// http 服务器虚拟目录 虚拟目录名和文件路径使用","隔开，多个配置路径间用";"隔开，例如  path_d,d:/record;path_e,e:/record
extern const std::string kVirtualPath;
// http 404错误提示内容
extern const std::string kNotFound;
// 是否显示文件夹菜单
extern const std::string kDirMenu;
// 禁止缓存文件的后缀
extern const std::string kForbidCacheSuffix;
// 可以把http代理前真实客户端ip放在http头中：https://github.com/ZLMediaKit/ZLMediaKit/issues/1388
extern const std::string kForwardedIpHeader;
// 是否允许所有跨域请求
extern const std::string kAllowCrossDomains;
// 允许访问http api和http文件索引的ip地址范围白名单，置空情况下不做限制
extern const std::string kAllowIPRange;
} // namespace Http

} // namespace mediakit

#endif /* COMMON_CONFIG_H */

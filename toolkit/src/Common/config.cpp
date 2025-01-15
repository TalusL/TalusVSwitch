/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Common/config.h"
#include "Util/NoticeCenter.h"
#include "Util/logger.h"
#include "Util/onceToken.h"
#include "Util/util.h"
#include <assert.h>
#include <stdio.h>

using namespace std;
using namespace toolkit;

namespace toolkit {

bool loadIniConfig(const char *ini_path) {
    string ini;
    if (ini_path && ini_path[0] != '\0') {
        ini = ini_path;
    } else {
        ini = exePath() + ".ini";
    }
    try {
        mINI::Instance().parseFile(ini);
        NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastReloadConfig);
        return true;
    } catch (std::exception &) {
        InfoL << "dump ini file to:" << ini;
        mINI::Instance().dumpFile(ini);
        return false;
    }
}
////////////广播名称///////////
namespace Broadcast {
const string kBroadcastHttpRequest = "kBroadcastHttpRequest";
const string kBroadcastHttpAccess = "kBroadcastHttpAccess";
const string kBroadcastReloadConfig = "kBroadcastReloadConfig";
const string kBroadcastHttpBeforeAccess = "kBroadcastHttpBeforeAccess";

} // namespace Broadcast

// 通用配置项目
namespace General {
#define GENERAL_FIELD "general."




const string kEnableVhost = GENERAL_FIELD "enableVhost";
const string kMergeWriteMS = GENERAL_FIELD "mergeWriteMS";

static onceToken token([]() {
    mINI::Instance()[kEnableVhost] = 0;
    mINI::Instance()[kMergeWriteMS] = 0;
});

} // namespace General

////////////HTTP配置///////////
namespace Http {
#define HTTP_FIELD "http."
const string kSendBufSize = HTTP_FIELD "sendBufSize";
const string kMaxReqSize = HTTP_FIELD "maxReqSize";
const string kKeepAliveSecond = HTTP_FIELD "keepAliveSecond";
const string kCharSet = HTTP_FIELD "charSet";
const string kRootPath = HTTP_FIELD "rootPath";
const string kVirtualPath = HTTP_FIELD "virtualPath";
const string kNotFound = HTTP_FIELD "notFound";
const string kDirMenu = HTTP_FIELD "dirMenu";
const string kForbidCacheSuffix = HTTP_FIELD "forbidCacheSuffix";
const string kForwardedIpHeader = HTTP_FIELD "forwarded_ip_header";
const string kAllowCrossDomains = HTTP_FIELD "allow_cross_domains";
const string kAllowIPRange = HTTP_FIELD "allow_ip_range";
const string kPort = HTTP_FIELD "port";

static onceToken token([]() {
    mINI::Instance()[kSendBufSize] = 64 * 1024;
    mINI::Instance()[kMaxReqSize] = 10 * 1024 * 1024;
    mINI::Instance()[kKeepAliveSecond] = 15;
    mINI::Instance()[kDirMenu] = true;
    mINI::Instance()[kVirtualPath] = "";
    mINI::Instance()[kPort] = 8081;

#if defined(_WIN32)
    mINI::Instance()[kCharSet] = "gb2312";
#else
    mINI::Instance()[kCharSet] = "utf-8";
#endif

    mINI::Instance()[kRootPath] = "./www";
    mINI::Instance()[kNotFound] = StrPrinter << "<html>"
                                                "<head><title>404 Not Found</title></head>"
                                                "<body bgcolor=\"white\">"
                                                "<center><h1>您访问的资源不存在！</h1></center>"
                                                "<hr><center>"
                                             << kServerName
                                             << "</center>"
                                                "</body>"
                                                "</html>"
                                             << endl;
    mINI::Instance()[kForbidCacheSuffix] = "";
    mINI::Instance()[kForwardedIpHeader] = "";
    mINI::Instance()[kAllowCrossDomains] = 1;
    mINI::Instance()[kAllowIPRange] = "127.0.0.1,172.16.0.0-172.31.255.255,192.168.0.0-192.168.255.255,10.0.0.0-10.255.255.255";
});

} // namespace Http


} // namespace mediakit
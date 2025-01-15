﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <sys/stat.h>
#if !defined(_WIN32)
#include <dirent.h>
#endif //!defined(_WIN32)
#include <iomanip>
#include "HttpFileManager.h"
#include "Util/File.h"
#include "HttpConst.h"
#include "HttpSession.h"
#include "Common/Parser.h"
#include "Common/config.h"
#include "Common/strCoding.h"

using namespace std;
using namespace toolkit;

namespace toolkit {

// hls的播放cookie缓存时间默认60秒，
// 每次访问一次该cookie，那么将重新刷新cookie有效期
// 假如播放器在60秒内都未访问该cookie，那么将重新触发hls播放鉴权
static int kHlsCookieSecond = 60;
static const string kCookieName = "ZL_COOKIE";
static const string kHlsSuffix = "/hls.m3u8";
static const string kHlsFMP4Suffix = "/hls.fmp4.m3u8";

struct HttpCookieAttachment {
    //是否已经查找到过MediaSource
    bool _find_src = false;
    //cookie生效作用域，本cookie只对该目录下的文件生效
    string _path;
    //上次鉴权失败信息,为空则上次鉴权成功
    string _err_msg;
};

const string &HttpFileManager::getContentType(const char *name) {
    return HttpConst::getHttpContentType(name);
}

#ifndef ntohll
static uint64_t ntohll(uint64_t val) {
    return (((uint64_t)ntohl(val)) << 32) + ntohl(val >> 32);
}
#endif

static uint64_t get_ip_uint64(const std::string &ip) {
    try {
        auto storage = SockUtil::make_sockaddr(ip.data(), 0);
        if (storage.ss_family == AF_INET) {
            return ntohl(reinterpret_cast<uint32_t &>(reinterpret_cast<struct sockaddr_in &>(storage).sin_addr));
        }
        if (storage.ss_family == AF_INET6) {
            return ntohll(reinterpret_cast<uint64_t &>(reinterpret_cast<struct sockaddr_in6 &>(storage).sin6_addr));
        }
    } catch (std::exception &ex) {
        WarnL << ex.what();
    }
    return 0;
}

bool HttpFileManager::isIPAllowed(const std::string &ip) {
    using IPRangs = std::vector<std::pair<uint64_t /*min_ip*/, uint64_t /*max_ip*/>>;
    GET_CONFIG_FUNC(IPRangs, allow_ip_range, Http::kAllowIPRange, [](const string &str) -> IPRangs {
        IPRangs ret;
        auto vec = split(str, ",");
        for (auto &item : vec) {
            if (trim(item).empty()) {
                continue;
            }
            auto range = split(item, "-");
            if (range.size() == 2) {
                auto ip_min = get_ip_uint64(trim(range[0]));
                auto ip_max = get_ip_uint64(trim(range[1]));
                if (ip_min && ip_max) {
                    ret.emplace_back(ip_min, ip_max);
                }
            } else if (range.size() == 1) {
                auto ip = get_ip_uint64(trim(range[0]));
                if (ip) {
                    ret.emplace_back(ip, ip);
                }
            } else {
                WarnL << "Invalid ip range: " << item;
            }
        }
        return ret;
    });

    if (allow_ip_range.empty()) {
        return true;
    }
    auto ip_int = get_ip_uint64(ip);
    for (auto &range : allow_ip_range) {
        if (ip_int >= range.first && ip_int <= range.second) {
            return true;
        }
    }
    return false;
}

static string searchIndexFile(const string &dir){
    DIR *pDir;
    dirent *pDirent;
    if ((pDir = opendir(dir.data())) == NULL) {
        return "";
    }
    set<string> setFile;
    while ((pDirent = readdir(pDir)) != NULL) {
        static set<const char *, StrCaseCompare> indexSet = {"index.html", "index.htm"};
        if (indexSet.find(pDirent->d_name) != indexSet.end()) {
            string ret = pDirent->d_name;
            closedir(pDir);
            return ret;
        }
    }
    closedir(pDir);
    return "";
}

static bool makeFolderMenu(const string &httpPath, const string &strFullPath, string &strRet) {
    GET_CONFIG(bool, dirMenu, Http::kDirMenu);
    if (!dirMenu) {
        //不允许浏览文件夹
        return false;
    }
    string strPathPrefix(strFullPath);
    //url后缀有没有'/'访问文件夹，处理逻辑不一致
    string last_dir_name;
    if (strPathPrefix.back() == '/') {
        strPathPrefix.pop_back();
    } else {
        last_dir_name = split(strPathPrefix, "/").back();
    }

    if (!File::is_dir(strPathPrefix.data())) {
        return false;
    }
    stringstream ss;
    ss << "<html>\r\n"
          "<head>\r\n"
          "<title>文件索引</title>\r\n"
          "</head>\r\n"
          "<body>\r\n"
          "<h1>文件索引:";

    ss << httpPath;
    ss << "</h1>\r\n";
    if (httpPath != "/") {
        ss << "<li><a href=\"";
        ss << "/";
        ss << "\">";
        ss << "根目录";
        ss << "</a></li>\r\n";

        ss << "<li><a href=\"";
        if (!last_dir_name.empty()) {
            ss << "./";
        } else {
            ss << "../";
        }
        ss << "\">";
        ss << "上级目录";
        ss << "</a></li>\r\n";
    }

    DIR *pDir;
    dirent *pDirent;
    if ((pDir = opendir(strPathPrefix.data())) == NULL) {
        return false;
    }
    multimap<string/*url name*/, std::pair<string/*note name*/, string/*file path*/> > file_map;
    while ((pDirent = readdir(pDir)) != NULL) {
        if (File::is_special_dir(pDirent->d_name)) {
            continue;
        }
        if (pDirent->d_name[0] == '.') {
            continue;
        }
        file_map.emplace(strCoding::UrlEncode(pDirent->d_name), std::make_pair(pDirent->d_name, strPathPrefix + "/" + pDirent->d_name));
    }
    //如果是root目录，添加虚拟目录
    if (httpPath == "/") {
        GET_CONFIG_FUNC(StrCaseMap, virtualPathMap, Http::kVirtualPath, [](const string &str) {
            return Parser::parseArgs(str, ";", ",");
        });
        for (auto &pr : virtualPathMap) {
            file_map.emplace(pr.first, std::make_pair(string("虚拟目录:") + pr.first, File::absolutePath("", pr.second)));
        }
    }
    int i = 0;
    for (auto &pr :file_map) {
        auto &strAbsolutePath = pr.second.second;
        bool isDir = File::is_dir(strAbsolutePath.data());
        ss << "<li><span>" << i++ << "</span>\t";
        ss << "<a href=\"";
        //路径链接地址
        if (!last_dir_name.empty()) {
            ss << last_dir_name << "/" << pr.first;
        } else {
            ss << pr.first;
        }

        if (isDir) {
            ss << "/";
        }
        ss << "\">";
        //路径名称
        ss << pr.second.first;
        if (isDir) {
            ss << "/</a></li>\r\n";
            continue;
        }
        //是文件
        auto fileSize = File::fileSize(strAbsolutePath.data());
        if (fileSize < 1024) {
            ss << " (" << fileSize << "B)" << endl;
        } else if (fileSize < 1024 * 1024) {
            ss << fixed << setprecision(2) << " (" << fileSize / 1024.0 << "KB)";
        } else if (fileSize < 1024 * 1024 * 1024) {
            ss << fixed << setprecision(2) << " (" << fileSize / 1024 / 1024.0 << "MB)";
        } else {
            ss << fixed << setprecision(2) << " (" << fileSize / 1024 / 1024 / 1024.0 << "GB)";
        }
        ss << "</a></li>\r\n";
    }
    closedir(pDir);
    ss << "<ul>\r\n";
    ss << "</ul>\r\n</body></html>";
    ss.str().swap(strRet);
    return true;
}


class SockInfoImp : public SockInfo{
public:
    using Ptr = std::shared_ptr<SockInfoImp>;
    SockInfoImp() = default;
    ~SockInfoImp() override = default;

    string get_local_ip() override {
        return _local_ip;
    }

    uint16_t get_local_port() override {
        return _local_port;
    }

    string get_peer_ip() override {
        return _peer_ip;
    }

    uint16_t get_peer_port() override {
        return _peer_port;
    }

    string getIdentifier() const override {
        return _identifier;
    }

    string _local_ip;
    string _peer_ip;
    string _identifier;
    uint16_t _local_port;
    uint16_t _peer_port;
};

/**
 * 判断http客户端是否有权限访问文件的逻辑步骤
 * 1、根据http请求头查找cookie，找到进入步骤3
 * 2、根据http url参数查找cookie，如果还是未找到cookie则进入步骤5
 * 3、cookie标记是否有权限访问文件，如果有权限，直接返回文件
 * 4、cookie中记录的url参数是否跟本次url参数一致，如果一致直接返回客户端错误码
 * 5、触发kBroadcastHttpAccess事件
 */
static void canAccessPath(Session &sender, const Parser &parser, bool is_dir,
                          const function<void(const string &err_msg, const HttpServerCookie::Ptr &cookie)> &callback) {
    //获取用户唯一id
    auto uid = parser.params();
    auto path = parser.url();

    //先根据http头中的cookie字段获取cookie
    HttpServerCookie::Ptr cookie = HttpCookieManager::Instance().getCookie(kCookieName, parser.getHeader());
    //是否需要更新cookie
    bool update_cookie = false;
    if (!cookie && !uid.empty()) {
        //客户端请求中无cookie,再根据该用户的用户id获取cookie
        cookie = HttpCookieManager::Instance().getCookieByUid(kCookieName, uid);
        update_cookie = true;
    }

    if (cookie) {
        auto& attach = cookie->getAttach<HttpCookieAttachment>();
        if (path.find(attach._path) == 0) {
            //上次cookie是限定本目录
            if (attach._err_msg.empty()) {
                callback("", update_cookie ? cookie : nullptr);
                return;
            }
            //上次鉴权失败，但是如果url参数发生变更，那么也重新鉴权下
            if (parser.params().empty() || parser.params() == cookie->getUid()) {
                //url参数未变，或者本来就没有url参数，那么判断本次请求为重复请求，无访问权限
                callback(attach._err_msg, update_cookie ? cookie : nullptr);
                return;
            }
        }
        //如果url参数变了或者不是限定本目录，那么旧cookie失效，重新鉴权
        HttpCookieManager::Instance().delCookie(cookie);
    }


    SockInfoImp::Ptr info = std::make_shared<SockInfoImp>();
    info->_identifier = sender.getIdentifier();
    info->_peer_ip = sender.get_peer_ip();
    info->_peer_port = sender.get_peer_port();
    info->_local_ip = sender.get_local_ip();
    info->_local_port = sender.get_local_port();

    //该用户从来未获取过cookie，这个时候我们广播是否允许该用户访问该http目录
    HttpSession::HttpAccessPathInvoker accessPathInvoker = [callback, uid, path, is_dir, info]
            (const string &err_msg, const string &cookie_path_in, int life_second) {
        HttpServerCookie::Ptr cookie;
        if (life_second) {
            //本次鉴权设置了有效期，我们把鉴权结果缓存在cookie中
            string cookie_path = cookie_path_in;
            if (cookie_path.empty()) {
                //如果未设置鉴权目录，那么我们采用当前目录
                cookie_path = is_dir ? path : path.substr(0, path.rfind("/") + 1);
            }

            auto attach = std::make_shared<HttpCookieAttachment>();
            //记录用户能访问的路径
            attach->_path = cookie_path;
            //记录能否访问
            attach->_err_msg = err_msg;
            callback(err_msg, HttpCookieManager::Instance().addCookie(kCookieName, uid, life_second, attach));
        } else {
            callback(err_msg, nullptr);
        }
    };

    // 事件未被拦截，则认为是http下载请求
    bool flag = NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastHttpAccess, parser, path, is_dir, accessPathInvoker, static_cast<SockInfo &>(sender));
    if (!flag) {
        // 此事件无人监听，我们默认都有权限访问
        callback("", nullptr);
    }
}

/**
 * 发送404 Not Found
 */
static void sendNotFound(const HttpFileManager::invoker &cb) {
    GET_CONFIG(string, notFound, Http::kNotFound);
    cb(404, "text/html", StrCaseMap(), std::make_shared<HttpStringBody>(notFound));
}

/**
 * 拼接文件路径
 */
static string pathCat(const string &a, const string &b){
    if (a.back() == '/') {
        return a + b;
    }
    return a + '/' + b;
}

/**
 * 访问文件
 * @param sender 事件触发者
 * @param parser http请求
 * @param media_info http url信息
 * @param file_path 文件绝对路径
 * @param cb 回调对象
 */
static void accessFile(Session &sender, const Parser &parser,  const string &file_path, const HttpFileManager::invoker &cb) {
    if (!File::fileExist(file_path.data())) {
        //文件不存在且不是hls,那么直接返回404
        sendNotFound(cb);
        return;
    }

    weak_ptr<Session> weakSession = static_pointer_cast<Session>(sender.shared_from_this());
    //判断是否有权限访问该文件
    canAccessPath(sender, parser, false, [cb, file_path, parser, weakSession](const string &err_msg, const HttpServerCookie::Ptr &cookie) {
        auto strongSession = weakSession.lock();
        if (!strongSession) {
            // http客户端已经断开，不需要回复
            return;
        }
        if (!err_msg.empty()) {
            //文件鉴权失败
            StrCaseMap headerOut;
            if (cookie) {
                headerOut["Set-Cookie"] = cookie->getCookie(cookie->getAttach<HttpCookieAttachment>()._path);
            }
            cb(401, "text/html", headerOut, std::make_shared<HttpStringBody>(err_msg));
            return;
        }

        auto response_file = [](const HttpServerCookie::Ptr &cookie, const HttpFileManager::invoker &cb, const string &file_path, const Parser &parser, const string &file_content = "") {
            StrCaseMap httpHeader;
            if (cookie) {
                httpHeader["Set-Cookie"] = cookie->getCookie(cookie->getAttach<HttpCookieAttachment>()._path);
            }
            HttpSession::HttpResponseInvoker invoker = [&](int code, const StrCaseMap &headerOut, const HttpBody::Ptr &body) {
                if (cookie && body) {
                    auto& attach = cookie->getAttach<HttpCookieAttachment>();
                }
                cb(code, HttpFileManager::getContentType(file_path.data()), headerOut, body);
            };
            GET_CONFIG_FUNC(vector<string>, forbidCacheSuffix, Http::kForbidCacheSuffix, [](const string &str) {
                return split(str, ",");
            });
            bool is_forbid_cache = false;
            for (auto &suffix : forbidCacheSuffix) {
                if (suffix != "" && end_with(file_path, suffix)) {
                    is_forbid_cache = true;
                    break;
                }
            }
            invoker.responseFile(parser.getHeader(), httpHeader, file_content.empty() ? file_path : file_content,  !is_forbid_cache, file_content.empty());
        };

        if (!cookie) {
            // 直接回复文件或404
            response_file(cookie, cb, file_path, parser);
            return;
        }

    });
}

static string getFilePath(const Parser &parser, Session &sender){
    GET_CONFIG(bool, enableVhost, General::kEnableVhost);
    GET_CONFIG(string, rootPath, Http::kRootPath);
    GET_CONFIG_FUNC(StrCaseMap, virtualPathMap, Http::kVirtualPath, [](const string &str) {
        return Parser::parseArgs(str, ";", ",");
    });

    string url, path;

    //访问的是rootPath
    path = rootPath;
    url = parser.url();

    for (auto &ch : url) {
        if (ch == '\\') {
            //如果url中存在"\"，这种目录是Windows样式的；需要批量转换为标准的"/"; 防止访问目录权限外的文件
            ch = '/';
        }
    }
    auto ret = File::absolutePath(url, path);
    NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastHttpBeforeAccess, parser, ret, static_cast<SockInfo &>(sender));
    return ret;
}

/**
 * 访问文件或文件夹
 * @param sender 事件触发者
 * @param parser http请求
 * @param cb 回调对象
 */
void HttpFileManager::onAccessPath(Session &sender, Parser &parser, const HttpFileManager::invoker &cb) {
    auto file_path = getFilePath(parser, sender);
    if (file_path.size() == 0) {
        sendNotFound(cb);
        return;
    }
    //访问的是文件夹
    if (File::is_dir(file_path.data())) {
        auto indexFile = searchIndexFile(file_path);
        if (!indexFile.empty()) {
            // 发现该文件夹下有index文件
            file_path = pathCat(file_path, indexFile);
            if (!File::is_dir(file_path.data())) {
                // 不是文件夹
                parser.setUrl(pathCat(parser.url(), indexFile));
                accessFile(sender, parser, file_path, cb);
                return;
            }
        }
        string strMenu;
        //生成文件夹菜单索引
        if (!makeFolderMenu(parser.url(), file_path, strMenu)) {
            //文件夹不存在
            sendNotFound(cb);
            return;
        }
        //判断是否有权限访问该目录
        canAccessPath(sender, parser, true, [strMenu, cb](const string &err_msg, const HttpServerCookie::Ptr &cookie) mutable{
            if (!err_msg.empty()) {
                strMenu = err_msg;
            }
            StrCaseMap headerOut;
            if (cookie) {
                headerOut["Set-Cookie"] = cookie->getCookie(cookie->getAttach<HttpCookieAttachment>()._path);
            }
            cb(err_msg.empty() ? 200 : 401, "text/html", headerOut, std::make_shared<HttpStringBody>(strMenu));
        });
        return;
    }

    //访问的是文件
    accessFile(sender, parser, file_path, cb);
};


////////////////////////////////////HttpResponseInvokerImp//////////////////////////////////////

void HttpResponseInvokerImp::operator()(int code, const StrCaseMap &headerOut, const Buffer::Ptr &body) const {
    return operator()(code, headerOut, std::make_shared<HttpBufferBody>(body));
}

void HttpResponseInvokerImp::operator()(int code, const StrCaseMap &headerOut, const HttpBody::Ptr &body) const{
    if (_lambad) {
        _lambad(code, headerOut, body);
    }
}

void HttpResponseInvokerImp::operator()(int code, const StrCaseMap &headerOut, const string &body) const{
    this->operator()(code, headerOut, std::make_shared<HttpStringBody>(body));
}

HttpResponseInvokerImp::HttpResponseInvokerImp(const HttpResponseInvokerImp::HttpResponseInvokerLambda0 &lambda){
    _lambad = lambda;
}

HttpResponseInvokerImp::HttpResponseInvokerImp(const HttpResponseInvokerImp::HttpResponseInvokerLambda1 &lambda){
    if (!lambda) {
        _lambad = nullptr;
        return;
    }
    _lambad = [lambda](int code, const StrCaseMap &headerOut, const HttpBody::Ptr &body) {
        string str;
        if (body && body->remainSize()) {
            str = body->readData(body->remainSize())->toString();
        }
        lambda(code, headerOut, str);
    };
}

void HttpResponseInvokerImp::responseFile(const StrCaseMap &requestHeader,
                                          const StrCaseMap &responseHeader,
                                          const string &file,
                                          bool use_mmap,
                                          bool is_path) const {
    if (!is_path) {
        //file是文件内容
        (*this)(200, responseHeader, std::make_shared<HttpStringBody>(file));
        return;
    }

    //file是文件路径
    StrCaseMap &httpHeader = const_cast<StrCaseMap &>(responseHeader);
    auto fileBody = std::make_shared<HttpFileBody>(file, use_mmap);
    if (fileBody->remainSize() < 0) {
        //打开文件失败
        GET_CONFIG(string, notFound, Http::kNotFound);
        GET_CONFIG(string, charSet, Http::kCharSet);

        auto strContentType = StrPrinter << "text/html; charset=" << charSet << endl;
        httpHeader["Content-Type"] = strContentType;
        (*this)(404, httpHeader, notFound);
        return;
    }

    auto &strRange = const_cast<StrCaseMap &>(requestHeader)["Range"];
    int code = 200;
    if (!strRange.empty()) {
        //分节下载
        code = 206;
        auto iRangeStart = atoll(findSubString(strRange.data(), "bytes=", "-").data());
        auto iRangeEnd = atoll(findSubString(strRange.data(), "-", nullptr).data());
        auto fileSize = fileBody->remainSize();
        if (iRangeEnd == 0) {
            iRangeEnd = fileSize - 1;
        }
        //设置文件范围
        fileBody->setRange(iRangeStart, iRangeEnd - iRangeStart + 1);
        //分节下载返回Content-Range头
        httpHeader.emplace("Content-Range", StrPrinter << "bytes " << iRangeStart << "-" << iRangeEnd << "/" << fileSize << endl);
    }

    //回复文件
    (*this)(code, httpHeader, fileBody);
}

HttpResponseInvokerImp::operator bool(){
    return _lambad.operator bool();
}


}//namespace mediakit

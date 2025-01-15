/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <algorithm>
#include "Common/config.h"
#include "Common/strCoding.h"
#include "HttpSession.h"
#include "HttpConst.h"
#include "Util/base64.h"
#include "Util/SHA1.h"

using namespace std;
using namespace toolkit;

namespace toolkit {

HttpSession::HttpSession(const Socket::Ptr &pSock) : Session(pSock) {
    GET_CONFIG(uint32_t, keep_alive_sec, Http::kKeepAliveSecond);
    pSock->setSendTimeOutSecond(keep_alive_sec);
}

HttpSession::~HttpSession() = default;

void HttpSession::onHttpRequest_HEAD() {
    // 暂时全部返回200 OK，因为HTTP GET存在按需生成流的操作，所以不能按照HTTP GET的流程返回
    // 如果直接返回404，那么又会导致按需生成流的逻辑失效，所以HTTP HEAD在静态文件或者已存在资源时才有效
    // 对于按需生成流的直播场景并不适用
    sendResponse(200, false);
}

void HttpSession::onHttpRequest_OPTIONS() {
    KeyValue header;
    header.emplace("Allow", "GET, POST, HEAD, OPTIONS");
    GET_CONFIG(bool, allow_cross_domains, Http::kAllowCrossDomains);
    if (allow_cross_domains) {
        header.emplace("Access-Control-Allow-Origin", "*");
        header.emplace("Access-Control-Allow-Headers", "*");
        header.emplace("Access-Control-Allow-Methods", "GET, POST, HEAD, OPTIONS");
    }
    header.emplace("Access-Control-Allow-Credentials", "true");
    header.emplace("Access-Control-Request-Methods", "GET, POST, OPTIONS");
    header.emplace("Access-Control-Request-Headers", "Accept,Accept-Language,Content-Language,Content-Type");
    sendResponse(200, true, nullptr, header);
}

ssize_t HttpSession::onRecvHeader(const char *header, size_t len) {
    using func_type = void (HttpSession::*)();
    static unordered_map<string, func_type> s_func_map;
    static onceToken token([]() {
        s_func_map.emplace("GET", &HttpSession::onHttpRequest_GET);
        s_func_map.emplace("POST", &HttpSession::onHttpRequest_POST);
        // DELETE命令用于whip/whep用，只用于触发http api
        s_func_map.emplace("DELETE", &HttpSession::onHttpRequest_POST);
        s_func_map.emplace("HEAD", &HttpSession::onHttpRequest_HEAD);
        s_func_map.emplace("OPTIONS", &HttpSession::onHttpRequest_OPTIONS);
    });

    _parser.parse(header, len);
    CHECK(_parser.url()[0] == '/');

    urlDecode(_parser);
    auto &cmd = _parser.method();
    auto it = s_func_map.find(cmd);
    if (it == s_func_map.end()) {
        WarnP(this) << "Http method not supported: " << cmd;
        sendResponse(405, true);
        return 0;
    }

    size_t content_len;
    auto &content_len_str = _parser["Content-Length"];
    if (content_len_str.empty()) {
        if (it->first == "POST") {
            // Http post未指定长度，我们认为是不定长的body
            WarnL << "Received http post request without content-length, consider it to be unlimited length";
            content_len = SIZE_MAX;
        } else {
            content_len = 0;
        }
    } else {
        // 已经指定长度
        content_len = atoll(content_len_str.data());
    }

    if (content_len == 0) {
        //// 没有body的情况，直接触发回调 ////
        (this->*(it->second))();
        _parser.clear();
        // 如果设置了_on_recv_body, 那么说明后续要处理body
        return _on_recv_body ? -1 : 0;
    }

    GET_CONFIG(size_t, maxReqSize, Http::kMaxReqSize);
    if (content_len > maxReqSize) {
        //// 不定长body或超大body ////
        if (content_len != SIZE_MAX) {
            WarnL << "Http body size is too huge: " << content_len << " > " << maxReqSize
                  << ", please set " << Http::kMaxReqSize << " in config.ini file.";
        }

        size_t received = 0;
        auto parser = std::move(_parser);
        _on_recv_body = [this, parser, received, content_len](const char *data, size_t len) mutable {
            received += len;
            onRecvUnlimitedContent(parser, data, len, content_len, received);
            if (received < content_len) {
                // 还没收满
                return true;
            }

            // 收满了
            setContentLen(0);
            return false;
        };
        // 声明后续都是body；Http body在本对象缓冲，不通过HttpRequestSplitter保存
        return -1;
    }

    //// body size明确指定且小于最大值的情况 ////
    auto body = std::make_shared<std::string>();
    // 预留一定的内存buffer，防止频繁的内存拷贝
    body->reserve(content_len);

    _on_recv_body = [this, body, content_len, it](const char *data, size_t len) mutable {
        body->append(data, len);
        if (body->size() < content_len) {
            // 未收满数据
            return true;
        }

        // 收集body完毕
        _parser.setContent(std::move(*body));
        (this->*(it->second))();
        _parser.clear();

        // 后续是header
        setContentLen(0);
        return false;
    };

    // 声明后续都是body；Http body在本对象缓冲，不通过HttpRequestSplitter保存
    return -1;
}

void HttpSession::onRecvContent(const char *data, size_t len) {
    if (_on_recv_body && !_on_recv_body(data, len)) {
        _on_recv_body = nullptr;
    }
}

void HttpSession::onRecv(const Buffer::Ptr &pBuf) {
    _ticker.resetTime();
    input(pBuf->data(), pBuf->size());
}

void HttpSession::onError(const SockException &err) {
}

void HttpSession::onManager() {
    GET_CONFIG(uint32_t, keepAliveSec, Http::kKeepAliveSecond);

    if (_ticker.elapsedTime() > keepAliveSec * 1000) {
        // 1分钟超时
        shutdown(SockException(Err_timeout, "session timeout"));
    }
}

bool HttpSession::checkWebSocket() {
    auto Sec_WebSocket_Key = _parser["Sec-WebSocket-Key"];
    if (Sec_WebSocket_Key.empty()) {
        return false;
    }
    auto Sec_WebSocket_Accept = encodeBase64(SHA1::encode_bin(Sec_WebSocket_Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));

    KeyValue headerOut;
    headerOut["Upgrade"] = "websocket";
    headerOut["Connection"] = "Upgrade";
    headerOut["Sec-WebSocket-Accept"] = Sec_WebSocket_Accept;
    if (!_parser["Sec-WebSocket-Protocol"].empty()) {
        headerOut["Sec-WebSocket-Protocol"] = _parser["Sec-WebSocket-Protocol"];
    }

    // 这是普通的websocket连接
    if (!onWebSocketConnect(_parser)) {
        sendResponse(501, true, nullptr, headerOut);
        return true;
    }
    sendResponse(101, false, nullptr, headerOut, nullptr, true);
    return true;
}


void HttpSession::onHttpRequest_GET() {
    // 先看看是否为WebSocket请求
    if (checkWebSocket()) {
        // 后续都是websocket body数据
        _on_recv_body = [this](const char *data, size_t len) {
            WebSocketSplitter::decode((uint8_t *)data, len);
            // _contentCallBack是可持续的，后面还要处理后续数据
            return true;
        };
        return;
    }

    if (emitHttpEvent(false)) {
        // 拦截http api事件
        return;
    }


    bool bClose = !strcasecmp(_parser["Connection"].data(), "close");
    weak_ptr<HttpSession> weak_self = static_pointer_cast<HttpSession>(shared_from_this());
    HttpFileManager::onAccessPath(*this, _parser, [weak_self, bClose](int code, const string &content_type,
                                                                      const StrCaseMap &responseHeader, const HttpBody::Ptr &body) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->async([weak_self, bClose, code, content_type, responseHeader, body]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            strong_self->sendResponse(code, bClose, content_type.data(), responseHeader, body);
        });
    });
}

static string dateStr() {
    char buf[64];
    time_t tt = time(NULL);
    strftime(buf, sizeof buf, "%a, %b %d %Y %H:%M:%S GMT", gmtime(&tt));
    return buf;
}

class AsyncSenderData {
public:
    friend class AsyncSender;
    using Ptr = std::shared_ptr<AsyncSenderData>;
    AsyncSenderData(HttpSession::Ptr session, const HttpBody::Ptr &body, bool close_when_complete) {
        _session = std::move(session);
        _body = body;
        _close_when_complete = close_when_complete;
    }
    ~AsyncSenderData() = default;

private:
    std::weak_ptr<HttpSession> _session;
    HttpBody::Ptr _body;
    bool _close_when_complete;
    bool _read_complete = false;
};

class AsyncSender {
public:
    using Ptr = std::shared_ptr<AsyncSender>;
    static bool onSocketFlushed(const AsyncSenderData::Ptr &data) {
        if (data->_read_complete) {
            if (data->_close_when_complete) {
                // 发送完毕需要关闭socket
                shutdown(data->_session.lock());
            }
            return false;
        }

        GET_CONFIG(uint32_t, sendBufSize, Http::kSendBufSize);
        data->_body->readDataAsync(sendBufSize, [data](const Buffer::Ptr &sendBuf) {
            auto session = data->_session.lock();
            if (!session) {
                // 本对象已经销毁
                return;
            }
            session->async([data, sendBuf]() {
                auto session = data->_session.lock();
                if (!session) {
                    // 本对象已经销毁
                    return;
                }
                onRequestData(data, session, sendBuf);
            }, false);
        });
        return true;
    }

private:
    static void onRequestData(const AsyncSenderData::Ptr &data, const std::shared_ptr<HttpSession> &session, const Buffer::Ptr &sendBuf) {
        session->_ticker.resetTime();
        if (sendBuf && session->send(sendBuf) != -1) {
            // 文件还未读完，还需要继续发送
            if (!session->isSocketBusy()) {
                // socket还可写，继续请求数据
                onSocketFlushed(data);
            }
            return;
        }
        // 文件写完了
        data->_read_complete = true;
        if (!session->isSocketBusy() && data->_close_when_complete) {
            shutdown(session);
        }
    }

    static void shutdown(const std::shared_ptr<HttpSession> &session) {
        if (session) {
            session->shutdown(SockException(Err_shutdown, StrPrinter << "close connection after send http body completed."));
        }
    }
};

void HttpSession::sendResponse(int code,
                               bool bClose,
                               const char *pcContentType,
                               const HttpSession::KeyValue &header,
                               const HttpBody::Ptr &body,
                               bool no_content_length) {
    GET_CONFIG(string, charSet, Http::kCharSet);
    GET_CONFIG(uint32_t, keepAliveSec, Http::kKeepAliveSecond);

    // body默认为空
    int64_t size = 0;
    if (body && body->remainSize()) {
        // 有body，获取body大小
        size = body->remainSize();
    }

    //    if (no_content_length) {
    //        // http-flv直播是Keep-Alive类型
    //        bClose = false;
    //    } else if ((size_t)size >= SIZE_MAX || size < 0) {
    //        // 不固定长度的body，那么发送完body后应该关闭socket，以便浏览器做下载完毕的判断
    bClose = true;
    //    }

    HttpSession::KeyValue &headerOut = const_cast<HttpSession::KeyValue &>(header);
    headerOut.emplace("Date", dateStr());
    headerOut.emplace("Server", kServerName);
    headerOut.emplace("Connection", bClose ? "close" : "keep-alive");

    GET_CONFIG(bool, allow_cross_domains, Http::kAllowCrossDomains);
    if (allow_cross_domains) {
        headerOut.emplace("Access-Control-Allow-Origin", "*");
        headerOut.emplace("Access-Control-Allow-Credentials", "true");
    }

    if (!bClose) {
        string keepAliveString = "timeout=";
        keepAliveString += to_string(keepAliveSec);
        keepAliveString += ", max=100";
        headerOut.emplace("Keep-Alive", std::move(keepAliveString));
    }

    if (!no_content_length && size >= 0 && (size_t)size < SIZE_MAX) {
        // 文件长度为固定值,且不是http-flv强制设置Content-Length
        headerOut["Content-Length"] = to_string(size);
    }

    if (size && !pcContentType) {
        // 有body时，设置缺省类型
        pcContentType = "text/plain";
    }

    if ((size || no_content_length) && pcContentType) {
        // 有body时，设置文件类型
        string strContentType = pcContentType;
        strContentType += "; charset=";
        strContentType += charSet;
        headerOut.emplace("Content-Type", std::move(strContentType));
    }

    // 发送http头
    string str;
    str.reserve(256);
    str += "HTTP/1.1 ";
    str += to_string(code);
    str += ' ';
    str += HttpConst::getHttpStatusMessage(code);
    str += "\r\n";
    for (auto &pr : header) {
        str += pr.first;
        str += ": ";
        str += pr.second;
        str += "\r\n";
    }
    str += "\r\n";
    SockSender::send(std::move(str));
    _ticker.resetTime();

    if (!size) {
        // 没有body
        if (bClose) {
            shutdown(SockException(Err_shutdown, StrPrinter << "close connection after send http header completed with status code:" << code));
        }
        return;
    }

#if 0
    //sendfile跟共享mmap相比并没有性能上的优势，相反，sendfile还有功能上的缺陷，先屏蔽
    if (typeid(*this) == typeid(HttpSession) && !body->sendFile(getSock()->rawFD())) {
        // http支持sendfile优化
        return;
    }
#endif

    GET_CONFIG(uint32_t, sendBufSize, Http::kSendBufSize);
    if (body->remainSize() > sendBufSize) {
        // 文件下载提升发送性能
        setSocketFlags();
    }

    // 发送http body
    AsyncSenderData::Ptr data = std::make_shared<AsyncSenderData>(static_pointer_cast<HttpSession>(shared_from_this()), body, bClose);
    getSock()->setOnFlush([data]() { return AsyncSender::onSocketFlushed(data); });
    AsyncSender::onSocketFlushed(data);
}

string HttpSession::urlDecode(const string &str) {
    auto ret = strCoding::UrlDecode(str);
#ifdef _WIN32
    GET_CONFIG(string, charSet, Http::kCharSet);
    bool isGb2312 = !strcasecmp(charSet.data(), "gb2312");
    if (isGb2312) {
        ret = strCoding::UTF8ToGB2312(ret);
    }
#endif // _WIN32
    return ret;
}

void HttpSession::urlDecode(Parser &parser) {
    parser.setUrl(urlDecode(parser.url()));
    for (auto &pr : _parser.getUrlArgs()) {
        const_cast<string &>(pr.second) = urlDecode(pr.second);
    }
}

bool HttpSession::emitHttpEvent(bool doInvoke) {
    bool bClose = !strcasecmp(_parser["Connection"].data(), "close");
    /////////////////////异步回复Invoker///////////////////////////////
    weak_ptr<HttpSession> weak_self = static_pointer_cast<HttpSession>(shared_from_this());
    HttpResponseInvoker invoker = [weak_self, bClose](int code, const KeyValue &headerOut, const HttpBody::Ptr &body) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->async([weak_self, bClose, code, headerOut, body]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                // 本对象已经销毁
                return;
            }
            strong_self->sendResponse(code, bClose, nullptr, headerOut, body);
        });
    };
    ///////////////////广播HTTP事件///////////////////////////
    bool consumed = false; // 该事件是否被消费
    NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastHttpRequest, _parser, invoker, consumed, static_cast<SockInfo &>(*this));
    if (!consumed && doInvoke) {
        // 该事件无人消费，所以返回404
        invoker(404, KeyValue(), HttpBody::Ptr());
    }
    return consumed;
}

std::string HttpSession::get_peer_ip() {
    GET_CONFIG(string, forwarded_ip_header, Http::kForwardedIpHeader);
    if (!forwarded_ip_header.empty() && !_parser.getHeader()[forwarded_ip_header].empty()) {
        return _parser.getHeader()[forwarded_ip_header];
    }
    return Session::get_peer_ip();
}

void HttpSession::onHttpRequest_POST() {
    emitHttpEvent(true);
}

void HttpSession::sendNotFound(bool bClose) {
    GET_CONFIG(string, notFound, Http::kNotFound);
    sendResponse(404, bClose, "text/html", KeyValue(), std::make_shared<HttpStringBody>(notFound));
}

void HttpSession::setSocketFlags() {
    GET_CONFIG(int, mergeWriteMS, General::kMergeWriteMS);
    if (mergeWriteMS > 0) {
        // 推流模式下，关闭TCP_NODELAY会增加推流端的延时，但是服务器性能将提高
        SockUtil::setNoDelay(getSock()->rawFD(), false);
        // 播放模式下，开启MSG_MORE会增加延时，但是能提高发送性能
        setSendFlags(SOCKET_DEFAULE_FLAGS | FLAG_MORE);
    }
}


void HttpSession::onWebSocketEncodeData(Buffer::Ptr buffer) {
    _total_bytes_usage += buffer->size();
    send(std::move(buffer));
}

void HttpSession::onWebSocketDecodeComplete(const WebSocketHeader &header_in) {
    WebSocketHeader &header = const_cast<WebSocketHeader &>(header_in);
    header._mask_flag = false;

    switch (header._opcode) {
        case WebSocketHeader::CLOSE: {
            encode(header, nullptr);
            shutdown(SockException(Err_shutdown, "recv close request from client"));
            break;
        }

        default: break;
    }
}

} /* namespace mediakit */

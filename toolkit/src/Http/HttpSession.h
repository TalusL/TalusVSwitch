﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_HTTP_HTTPSESSION_H_
#define SRC_HTTP_HTTPSESSION_H_

#include <functional>
#include "Network/Session.h"
#include "HttpRequestSplitter.h"
#include "WebSocketSplitter.h"
#include "HttpCookieManager.h"
#include "HttpFileManager.h"

namespace toolkit {

class HttpSession: public toolkit::Session,
                   public HttpRequestSplitter,
                   public WebSocketSplitter {
public:
    using Ptr = std::shared_ptr<HttpSession>;
    using KeyValue = StrCaseMap;
    using HttpResponseInvoker = HttpResponseInvokerImp ;
    friend class AsyncSender;
    /**
     * @param errMsg 如果为空，则代表鉴权通过，否则为错误提示
     * @param accessPath 运行或禁止访问的根目录
     * @param cookieLifeSecond 鉴权cookie有效期
     **/
    using HttpAccessPathInvoker = std::function<void(const std::string &errMsg,const std::string &accessPath, int cookieLifeSecond)>;

    HttpSession(const toolkit::Socket::Ptr &pSock);
    ~HttpSession() override;

    void onRecv(const toolkit::Buffer::Ptr &) override;
    void onError(const toolkit::SockException &err) override;
    void onManager() override;
    static std::string urlDecode(const std::string &str);

protected:

    //HttpRequestSplitter override
    ssize_t onRecvHeader(const char *data,size_t len) override;
    void onRecvContent(const char *data,size_t len) override;

    /**
     * 重载之用于处理不定长度的content
     * 这个函数可用于处理大文件上传、http-flv推流
     * @param header http请求头
     * @param data content分片数据
     * @param len content分片数据大小
     * @param totalSize content总大小,如果为0则是不限长度content
     * @param recvedSize 已收数据大小
     */
    virtual void onRecvUnlimitedContent(const Parser &header,
                                        const char *data,
                                        size_t len,
                                        size_t totalSize,
                                        size_t recvedSize){
        shutdown(toolkit::SockException(toolkit::Err_shutdown,"http post content is too huge,default closed"));
    }

    /**
     * websocket客户端连接上事件
     * @param header http头
     * @return true代表允许websocket连接，否则拒绝
     */
    virtual bool onWebSocketConnect(const Parser &header){
        WarnP(this) << "http server do not support websocket default";
        return false;
    }

    //WebSocketSplitter override
    /**
     * 发送数据进行websocket协议打包后回调
     * @param buffer websocket协议数据
     */
    void onWebSocketEncodeData(toolkit::Buffer::Ptr buffer) override;

    /**
     * 接收到完整的一个webSocket数据包后回调
     * @param header 数据包包头
     */
    void onWebSocketDecodeComplete(const WebSocketHeader &header_in) override;

    //重载获取客户端ip
    std::string get_peer_ip() override;

private:
    void onHttpRequest_GET();
    void onHttpRequest_POST();
    void onHttpRequest_HEAD();
    void onHttpRequest_OPTIONS();


    bool checkWebSocket();
    bool emitHttpEvent(bool doInvoke);
    void urlDecode(Parser &parser);
    void sendNotFound(bool bClose);
    void sendResponse(int code, bool bClose, const char *pcContentType = nullptr,
                      const HttpSession::KeyValue &header = HttpSession::KeyValue(),
                      const HttpBody::Ptr &body = nullptr, bool no_content_length = false);

    //设置socket标志
    void setSocketFlags();


private:
    bool _is_live_stream = false;
    bool _live_over_websocket = false;
    //消耗的总流量
    uint64_t _total_bytes_usage = 0;
    Parser _parser;
    toolkit::Ticker _ticker;
    //处理content数据的callback
    std::function<bool (const char *data,size_t len) > _on_recv_body;
};

using HttpsSession = toolkit::SessionWithSSL<HttpSession>;

} /* namespace mediakit */

#endif /* SRC_HTTP_HTTPSESSION_H_ */

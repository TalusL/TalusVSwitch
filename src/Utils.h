//
// Created by liangzhuohua on 2024/8/20.
//

#ifndef TUNNEL_UTILS_H
#define TUNNEL_UTILS_H

#include <csignal>
#include <fstream>
#include <string>
#include <vector>
#include <Network/Buffer.h>
#include <Network/sockutil.h>

#include <zlib.h>

#ifdef _WIN32
#else
#include <dirent.h>
#include <sys/wait.h>
#endif




class CommandLineParser {
public:
    CommandLineParser(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.size() > 2 && arg[0] == '-' && arg[1]!= '-') {
                std::string value;
                if (i + 1 < argc && argv[i + 1][0]!= '-') {
                    value = argv[i + 1];
                    ++i;
                }
                options[arg.substr(1)] = value;
            }
        }
    }

    std::string getOptionValue(const std::string& option) const {
        auto it = options.find(option);
        return it!= options.end()? it->second : "";
    }

private:
    std::unordered_map<std::string, std::string> options;
};

inline std::string getMacAddress() {
# ifdef __unix__
    DIR* dir;
    struct dirent* ent;
    std::vector<std::string> interfaceNames;

    dir = opendir("/sys/class/net");
    if (dir!= nullptr) {
        while ((ent = readdir(dir))!= nullptr) {
            std::string name(ent->d_name);
            if (name!= "." && name!= ".." && name!= "lo") {
                interfaceNames.push_back(name);
            }
        }
        closedir(dir);
    } else {
        return "";
    }

    for (const auto& interfaceName : interfaceNames) {
        std::string filePath = "/sys/class/net/" + interfaceName + "/address";
        std::ifstream file(filePath);
        if (file.is_open()) {
            std::string mac;
            std::getline(file, mac);
            file.close();
            if(mac.empty()) {
                continue;
            }
            return mac;
        }
    }
#endif

    return "";
}


inline toolkit::Buffer::Ptr compress(const toolkit::Buffer::Ptr & data) {
    z_stream defstream;
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;

    if (deflateInit(&defstream, Z_DEFAULT_COMPRESSION)!= Z_OK) {
        return {};
    }

    auto compressedData = std::make_shared<toolkit::BufferLikeString>();
    defstream.avail_in = data->size();
    defstream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data->data()));

    unsigned char outBuffer[2000];
    do {
        defstream.avail_out = sizeof(outBuffer);
        defstream.next_out = outBuffer;

        if (deflate(&defstream, Z_FINISH) == Z_STREAM_ERROR) {
            deflateEnd(&defstream);
            return {};
        }

        compressedData->append(reinterpret_cast<const char *>(outBuffer), sizeof(outBuffer) - defstream.avail_out);
    } while (defstream.avail_out == 0);

    if (deflateEnd(&defstream)!= Z_OK) {
        return {};
    }
    return compressedData;
}

inline toolkit::Buffer::Ptr decompress(const toolkit::Buffer::Ptr & compressedData) {

    if(compressedData->size()){
        compressedData->data()[0] = 0x78;
        compressedData->data()[1] = 0x9c;
    }
    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = 0;
    infstream.next_in = Z_NULL;

    if (inflateInit(&infstream)!= Z_OK) {
        return {};
    }

    auto decompressedData = std::make_shared<toolkit::BufferLikeString>();
    infstream.avail_in = compressedData->size();
    infstream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressedData->data()));

    unsigned char outBuffer[2000];
    do {
        infstream.avail_out = sizeof(outBuffer);
        infstream.next_out = outBuffer;

        int ret = inflate(&infstream, 0);
        if (ret == Z_STREAM_ERROR || (ret < 0 && ret!= Z_DATA_ERROR)) {
            inflateEnd(&infstream);
            return {};
        }

        decompressedData->append(reinterpret_cast<const char *>(outBuffer),  sizeof(outBuffer) - infstream.avail_out);
    } while (infstream.avail_out == 0);

    if (inflateEnd(&infstream)!= Z_OK) {
        return {};
    }
    return decompressedData;
}

inline bool compareSockAddr(const sockaddr_storage& addr1, const sockaddr_storage& addr2) {
    // 确定地址族
    bool isAddr1V4Mapped = false;
    bool isAddr2V4Mapped = false;

    if (addr1.ss_family == AF_INET6) {
        isAddr1V4Mapped = IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6 *)&addr1)->sin6_addr);
    }
    if (addr2.ss_family == AF_INET6) {
        isAddr2V4Mapped = IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6 *)&addr2)->sin6_addr);
    }

    // 如果两个都是 IPv4
    if (addr1.ss_family == AF_INET && addr2.ss_family == AF_INET) {
        const auto* addr1_in = reinterpret_cast<const sockaddr_in*>(&addr1);
        const auto* addr2_in = reinterpret_cast<const sockaddr_in*>(&addr2);
        return (addr1_in->sin_addr.s_addr == addr2_in->sin_addr.s_addr) &&
               (addr1_in->sin_port == addr2_in->sin_port);
    }

    // 如果一个是 IPv4，一个是 V4 映射 IPv6
    if (isAddr2V4Mapped && addr1.ss_family == AF_INET) {
        const auto* addr1_in = reinterpret_cast<const sockaddr_in*>(&addr1);
        const auto* addr2_in6 = reinterpret_cast<const sockaddr_in6*>(&addr2);
        const auto* v4_addr2 = reinterpret_cast<const in_addr*>(&addr2_in6->sin6_addr.s6_addr[12]);
        return (addr1_in->sin_addr.s_addr == v4_addr2->s_addr) &&
               (addr1_in->sin_port == addr2_in6->sin6_port);
    }

    if (isAddr1V4Mapped && addr2.ss_family == AF_INET) {
        const auto* addr2_in = reinterpret_cast<const sockaddr_in*>(&addr2);
        const auto* addr1_in6 = reinterpret_cast<const sockaddr_in6*>(&addr1);
        const auto* v4_addr1 = reinterpret_cast<const in_addr*>(&addr1_in6->sin6_addr.s6_addr[12]);
        return (v4_addr1->s_addr == addr2_in->sin_addr.s_addr) &&
               (addr1_in6->sin6_port == addr2_in->sin_port);
    }

    // IPv6 地址比较
    if (addr1.ss_family == AF_INET6 && addr2.ss_family == AF_INET6) {
        const auto* addr1_in6 = reinterpret_cast<const sockaddr_in6*>(&addr1);
        const auto* addr2_in6 = reinterpret_cast<const sockaddr_in6*>(&addr2);

        if (isAddr1V4Mapped && !isAddr2V4Mapped) {
            const auto* v4_addr1 = reinterpret_cast<const in_addr*>(&addr1_in6->sin6_addr.s6_addr[12]);
            return memcmp(v4_addr1, &addr2_in6->sin6_addr, sizeof(in_addr)) == 0 &&
                   (addr1_in6->sin6_port == addr2_in6->sin6_port);
        }

        if (!isAddr1V4Mapped && isAddr2V4Mapped) {
            const auto* v4_addr2 = reinterpret_cast<const in_addr*>(&addr2_in6->sin6_addr.s6_addr[12]);
            return memcmp(&addr1_in6->sin6_addr, v4_addr2, sizeof(in_addr)) == 0 &&
                   (addr1_in6->sin6_port == addr2_in6->sin6_port);
        }

        // 如果两个都是普通的 IPv6 地址，进行直接比较
        return (memcmp(&addr1_in6->sin6_addr, &addr2_in6->sin6_addr, sizeof(addr1_in6->sin6_addr)) == 0) &&
               (addr1_in6->sin6_port == addr2_in6->sin6_port);
    }

    // 不支持的地址族
    return false;
}

inline void startDaemon() {
    auto kill_parent_if_failed = true;
#ifndef _WIN32
    static pid_t pid;
    do {
        pid = fork();
        if (pid == -1) {
            WarnL << "fork fail";
            //休眠1秒再试
            sleep(1);
            continue;
        }

        if (pid == 0) {
            //子进程
            return;
        }

        //父进程,监视子进程是否退出
        DebugL << "启动子进程:" << pid;
        signal(SIGINT, [](int) {
            WarnL << "收到主动退出信号,关闭父进程与子进程";
            kill(pid, SIGINT);
            exit(0);
        });

        do {
            int status = 0;
            if (waitpid(pid, &status, 0) >= 0) {
                WarnL << "子进程退出";
                //休眠3秒再启动子进程
                sleep(3);
                //重启子进程，如果子进程重启失败，那么不应该杀掉守护进程，这样守护进程可以一直尝试重启子进程
                kill_parent_if_failed = false;
                break;
            }
            DebugL << "waitpid fail";
        } while (true);
    } while (true);
#endif // _WIN32
}

#endif//TUNNEL_UTILS_H

//
// Created by liangzhuohua on 2024/8/20.
//

#ifndef TUNNEL_UTILS_H
#define TUNNEL_UTILS_H

#include <csignal>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>
#include <zlib.h>
#include <Network/Buffer.h>
#include <Network/sockutil.h>


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
            return mac;
        }
    }

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
    if(!compressedData->empty()){
        compressedData->data()[0] = 0x00;
        compressedData->data()[1] = 0x00;
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
    return toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr1))==toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr2))&&
           toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr1))==toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr2));
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

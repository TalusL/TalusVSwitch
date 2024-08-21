//
// Created by liangzhuohua on 2024/8/20.
//

#ifndef TUNNEL_UTILS_H
#define TUNNEL_UTILS_H

#include <zlib.h>
#include <vector>
#include <string>
#include <dirent.h>
#include <fstream>



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
        compressedData->data()[0] = 0x80;
        compressedData->data()[1] = 0x60;
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

bool compareSockAddr(const sockaddr_storage& addr1, const sockaddr_storage& addr2) {
    return toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr1))==toolkit::SockUtil::inet_ntoa(reinterpret_cast<const sockaddr *>(&addr2))&&
           toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr1))==toolkit::SockUtil::inet_port(reinterpret_cast<const sockaddr *>(&addr2));
}

#endif//TUNNEL_UTILS_H

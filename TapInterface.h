//
// Created by liangzhuohua on 2024/8/19.
//

#ifndef TUNNEL_TAPINTERFACE_H
#define TUNNEL_TAPINTERFACE_H

#include "tuntap++.hh"

class TapInterface : public tuntap::tap{
public:
    static TapInterface & Instance(){
        static TapInterface tapInterface;
        return tapInterface;
    }
};

#endif//TUNNEL_TAPINTERFACE_H

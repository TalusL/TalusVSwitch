

#ifndef APISERVER_H
#define APISERVER_H



class ApiServer {
public:
    static ApiServer& Instance() {
        static ApiServer vsCtrlHelper;
        return vsCtrlHelper;
    }
    bool Start();
};



#endif //APISERVER_H

//
// Created by had on 16-8-23.
//

#ifndef PROCESS_POOL_BASE_EVENT_H
#define PROCESS_POOL_BASE_EVENT_H

#include <iostream>
#include <netinet/in.h>

class base_event {
public:
    base_event(){}
    virtual void init(int& a, int& b, sockaddr_in& sock_addr){ std::cout << "base_event init" << std::endl;}
    virtual void process(){ std::cout << "base_event process" << std::endl;}
};


#endif //PROCESS_POOL_BASE_EVENT_H

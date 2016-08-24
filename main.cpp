#include <iostream>
//#include <sys/socket.h>
#include "base_event.h"
#include "processpool.h"

int main() {
    std::cout << "Hello, World!" << std::endl;

    int fd = 1;

    static processpool<base_event>* pool = processpool<base_event>::create(fd,8);

    pool->run();

    return 0;
}
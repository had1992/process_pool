//
// Created by 304-had on 2016/8/23.
//

#ifndef PROCESS_POOL_PROCESS_H
#define PROCESS_POOL_PROCESS_H

#include <unistd.h>
#include <sys/types.h>

/*描述一个子进程的类，m_pid是目标子进程的PID，m_pipefd是父进程和子进程通信用的管道*/
class process {
public:
    process():m_pid(-1){}

public:
    pid_t m_pid;
    int m_pipefd[2];/*fork子进程，然后在服进程关闭一个描述符eg.s[1], 在子进程中再关闭另一个 eg.s[0],
                     * 则可以实现父子进程之间的双工通信，两端都可读可写；
                     * 当然，仍然遵守和在同一个进程之间工作的原则，一端写，在另一端读取*/
};


#endif //PROCESS_POOL_PROCESS_H

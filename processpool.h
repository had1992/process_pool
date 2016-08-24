//
// Created by 304-had on 2016/8/23.
//

#ifndef PROCESS_POOL_PROCESSPOOL_H
#define PROCESS_POOL_PROCESSPOOL_H

#include <iostream>
#include <assert.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "process.h"

//进程池类：将需要处理的逻辑任务封装为任务类，作为模板参数，以提高代码复用
template< typename T >
class processpool
{
private:
//构造函数，create函数调用
    processpool( int listenfd, int process_number = 8 );
public:
//给定服务器监听的socket，和进程数，创建子进程。（注：单例模式）。 为静态函数，因此可以直接以类名调用
    static processpool< T >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance )//确保只创建唯一的一个进程池实例
        {
            m_instance = new processpool< T >( listenfd, process_number );
        }
        return m_instance;
    }
    ~processpool()
    {
        delete [] m_sub_process;
    }
    void run();    //启动进程池，在其中根据当前进程的标号来区分为父进程或子进程，并分别调用其run_***函数来处理逻辑任务

private:
    void setup_sig_pipe();
    void run_parent();                         //父进程的逻辑任务处理函数：监听listen socket，并通知工作进程
    void run_child();                          //子进程的任务逻辑处理函数：接受连接socket，完成客户任务请求

private:
    static const int MAX_PROCESS_NUMBER = 16;  //进程池允许的最大进程数量
    static const int USER_PER_PROCESS = 65536; //每个子进程处理的最大的客户任务数量
    static const int MAX_EVENT_NUMBER = 10000; //epoll能处理的最大事件数量
    int m_process_number;                      //进程池中的进程数量
    int m_idx;                                 //子进程在池中的编号，从0开始
    int m_epollfd;                             //指向poll内核事件表的文件描述符：每个进程都独立创建一个epoll内核事件表项
    int m_listenfd;                            //监听socket：父进程与所有子进程的监听socket文件描述符指向内核中的同一文件表项
    int m_stop;                                //子进程是否要停止运行
    process* m_sub_process;                    //保存所有子进程的数组
    static processpool< T >* m_instance;       //进程池静态实例：标识全局唯一的进程池
};
template< typename T >
processpool< T >* processpool< T >::m_instance = NULL;//初始化静态成员

static int sig_pipefd[2];//用于处理信号的管道，以实现统一事件源

//将文件描述符设为非阻塞
static int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

//向epoll内核时间表注册事件
static void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );//”只有当事件就绪时，非阻塞才是高校的“
}

//删除fd上的注册事件
static void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

//消息处理函数，实现事件统一处理：只是往号管道写端写入信号消息，具体信号处理逻辑在while循环中统一处理。
// 减短信号处理函数执行时间，从而确保信号不被屏蔽（信号在处理期间，系统不会再次出发该信号）
static void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

static void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

//进程池构造函数
template< typename T >
processpool< T >::processpool( int listenfd, int process_number )
        : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );

    m_sub_process = new process[ process_number ];
    assert( m_sub_process );

    for( int i = 0; i < process_number; ++i )
    {
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd );
        assert( ret == 0 );

        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if( m_sub_process[i].m_pid > 0 )//为父进程
        {
            close( m_sub_process[i].m_pipefd[1] );
            continue;//父进程继续创建好所有的子进程后，才退出该函数
        }
        else
        {
            close( m_sub_process[i].m_pipefd[0] );//子进程
            m_idx = i;//初始化子进程在进程池中的编号（最小为0，而父进程标号为-1）
            break;//子进程一旦创建好，就退出该函数，进入逻辑任务处理（pool->run）
        }
    }
}

//信号管道设置函数
template< typename T >
void processpool< T >::setup_sig_pipe()
{
    m_epollfd = epoll_create( 5 );
    assert( m_epollfd != 1 );

    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );
    assert( ret != -1 );

    setnonblocking( sig_pipefd[1] );  //信号处理函数向写端写入消息
    addfd( m_epollfd, sig_pipefd[0] );//在while中监听读端

    addsig( SIGCHLD, sig_handler );   //子进程退出会暂停
    addsig( SIGTERM, sig_handler );   //终止进程，kill函数默认即发送该信号
    addsig( SIGINT, sig_handler );    //键盘输入以终止该进程ctrl+C
    addsig( SIGPIPE, SIG_IGN );       //忽略往管道读端被关闭的管道写数据的信号
}

//进程池启动函数
template< typename T >
void processpool< T >::run()
{
    if( m_idx != -1 )
    {
        run_child();//子进程
        return;//子进程退出后，记得return
    }
    run_parent();//父进程
}

//子进程逻辑函数
template< typename T >
void processpool< T >::run_child()
{
    setup_sig_pipe();

    int pipefd = m_sub_process[m_idx].m_pipefd[ 1 ];//与父进程的通信管道
    addfd( m_epollfd, pipefd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    T* users = new T [ USER_PER_PROCESS ];          //一次性创建所有的客户端任务对象数组（也用到池的思想，当某个客户任务处理完后，不用释放该对象资源，而是放回池中再利用）
    assert( users );
    int number = 0;
    int ret = -1;

    //监听信号管道、通信管道、连接socket，处理逻辑任务
    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );//
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            std::cout << "epoll failure" << std::endl;
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( ( sockfd == pipefd ) && ( events[i].events & EPOLLIN ) ) //为父进程的通信事件
            {
                int client = 0;
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 )
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
                    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if ( connfd < 0 )
                    {
                        std::cout << "errno is:" << strerror(errno) << std::endl;
                        continue;
                    }
                    addfd( m_epollfd, connfd );
                    users[connfd].init( m_epollfd, connfd, client_address );
                }
            }
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )//为信号管道事件
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int j = 0; j < ret; ++j )
                    {
                        switch( signals[j] )
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else if( events[i].events & EPOLLIN )  //为连接socket事件
            {
                users[sockfd].process();//客户任务对象的逻辑处理函数
            }
            else
            {
                continue;
            }
        }
    }

    delete [] users;
    users = NULL;
    close( pipefd );//关闭与父进程的通信管道的读端
    //close( m_listenfd );//”对象由那个函数创建，就由那个函数销毁“
    close( m_epollfd );
}

//父进程逻辑函数
template< typename T >
void processpool< T >::run_parent()
{
    setup_sig_pipe();

    addfd( m_epollfd, m_listenfd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    //监听listen socket、和信号管道
    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            std::cout << "epoll failure" << std::endl;
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( sockfd == m_listenfd ) //监听到新连接到来，论选出一个子进程，通知该子进程”嘿，有新的连接到了，你接受下！“
            {
                int j =  sub_process_counter;
                do
                {
                    if( m_sub_process[j].m_pid != -1 )
                    {
                        break;
                    }
                    j = (j+1)%m_process_number;
                }
                while( j != sub_process_counter );

                if( m_sub_process[j].m_pid == -1 )//所有子进程都都已经推出
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (j+1)%m_process_number;
                //send( m_sub_process[sub_process_counter++].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                send( m_sub_process[j].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );//通知子进程
                std::cout << "send request to child" << j << std::endl;
                //sub_process_counter %= m_process_number;
            }
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int j = 0; j < ret; ++j )
                    {
                        switch( signals[j] )
                        {
                            case SIGCHLD://子进程退出信号
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    for( int k = 0; k < m_process_number; ++k )
                                    {
                                        if( m_sub_process[k].m_pid == pid )
                                        {
                                            std::cout << "child" << k << "join" << std::endl;
                                            close( m_sub_process[k].m_pipefd[0] );
                                            m_sub_process[k].m_pid = -1;  //设置已经推出的子进程的PID为-1
                                        }
                                    }
                                }
                                m_stop = true;
                                for( int k = 0; k < m_process_number; ++k )
                                {
                                    if( m_sub_process[k].m_pid != -1 )  //只要还有一个子进程没有退出，则父进程继续
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                std::cout << "kill all the child now!" << std::endl;
                                for( int k = 0; k < m_process_number; ++k )
                                {
                                    int pid = m_sub_process[k].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                continue;
            }
        }
    }

    //close( m_listenfd );
    close( m_epollfd );
}

#endif//PROCESS_POOL_PROCESSPOOL_H
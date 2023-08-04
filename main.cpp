#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include <signal.h>
#include "http_conn.h"

#define MAX_FD 65535 //最大的文件描述符数量
#define MAX_EVENT_NUMBER 10000 //监听的最大事件数量

//添加信号捕捉
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);      //定义所有信号掩码（阻塞）
    sigaction(sig, &sa, NULL);      //获取和设置信号， 参1：信号  参2：当前动作  参3：前一个动作
}

//添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);

//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);

//修改文件描述符, 重置socket上EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]){

    if(argc <= 1){
        printf("参数输入不匹配");
        exit(-1);
    }
    // 获取端口号
    int port = atoi(argv[1]);

    //对SIGPIE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化
    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){
        exit(-1);
    }

    //创建一个数组用于保护所有的客户端信息
    http_conn *users = new http_conn[MAX_FD];

    //创建监听的套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    
    //设置端口复用,设置套接字描述符的属性
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    //监听
    listen(listenfd, 5);

    //创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    //将监听的文件描述符添加到epoll对象中,监听不需要使用oneshot
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true){
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        printf("得到了更改的文件描述符个数num:%d\n", num);

        if((num<0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }
        
        //循环遍历
        for(int i=0; i<num; i++){
            int sockfd = events[i].data.fd;
            std::cout<<"events["<<i <<"].event"<<"  "<<events[i].events<<std::endl;
            if(sockfd == listenfd){
                //有客户端连接进来
                printf("有第%d个客户连接进来\n", sockfd);
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                
                int connfd  = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if(http_conn::m_user_count >= MAX_FD){
                    //目前连接数满了
                    //给客户端写一个信息，服务器内部正忙。
                    printf("服务器忙！(申请的文件描述符超过上线了（^……^))");
                    close(connfd);
                    continue;
                }
                // 新的客户的数据初始化， 放到数组中
                users[connfd].init(connfd, client_address);
            }
            else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                //对方异常断开或者错误等事件
                //需要关闭连接
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN){
                //读事件
                if(users[sockfd].read()){
                    //一次性把所有数据都读完
                    printf("一次性把所有数据读完！\n");
                    pool->append(users+sockfd);
                    printf("已经放入线程池\n");
                }
                else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                //写事件
                if(!users[sockfd].write()){
                    //一次性写完所有数据
                    printf("一次性把所有数据写完！\n");
                    users[sockfd].close_conn();
                }
            }
        }//这里是for循环结束
        printf("-----------到此是epoll_wait后的for循环结束!-------------------\n");
    }
    printf("-----------到此是while循环结束!-------------------\n");
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;


    return 0;
}
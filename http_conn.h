#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <iostream>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include "locker.h"
#include <sys/uio.h>

class http_conn{
public:

    static int m_epollfd; //所有的socket上的事件都被注册到同一个epoll对象中
    static int m_user_count; //统计用户的数量
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;  //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 2048; //写缓冲区的大小
    
    //HTTP请求的方法，目前改代码只支持GET
    enum METHOD  //枚举
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    /* 
    解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUERSTLINE:当前正在分析请求行
    CHECK_STATE_HEADER: 当前正在分析头部字段
    CHECK_STATE_CONTENT: 当亲正在解析请求体 
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /* 
    服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST         :    请求不完整，需要继续读取客户数据
        GET_REQUEST        :    表示获得了一个完整的客户请求
        BAD_REQUEST        :    表示客户端请求语法错误    
        NO_RESOURCE        :    表示服务器没有资源
        FORBIDDEN_REQUEST  ：   表示客户堆资源没有足够的访问权限、
        FILE_REQUEST       ：   文件请求，获取文件成功
        INTERNAL_ERROR     :    表示服务器内部错误
        CLOSED_CONNECTION  :    表示客户端已经关闭连接了
    */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    
    /* 
    从状态机的三种可能状态，即行的读取状态，分别表示
    1.读取到一个完整的行 
    2.行出错
    3.行数据尚且不完整（还没有遇到\n\n）
    */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    http_conn() {};
    ~http_conn() {};

    void process();  //处理客户端的请求 
    void init(int sockfd, const sockaddr_in & addr); //初始化新接收的连接
    void close_conn();  //关闭连接
    bool read();   //非阻塞读
    bool write();   //非阻塞写, 写HTTP响应
    void unmap();   // 释放映射资源

private:
    void init();   //初始化连结其余的信息
    HTTP_CODE process_read();   //解析HTTP请求
    bool process_write(HTTP_CODE ret); // 响应HTTP请求
    HTTP_CODE parse_request_line(char *text);  // 解析请求首行
    HTTP_CODE parse_headers(char *text);      // 解析请求头
    HTTP_CODE parse_content(char *text);      // 解析请求体
    LINE_STATUS parse_line();   // 解析行
    char * get_line(){
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();
    bool add_status_line(int status, const char *title);  // 添加响应状态行
    bool add_response(const char *format, ...);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);


private:

    int m_sockfd; //这个HTTP连接的socket
    sockaddr_in m_address; //通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE];  //读缓冲区
    //解释下这个m_read_idx, 我们读完数据是不删除的，比如开始读了10个字节，那么之后读就要从第11个字节开始读
    int m_read_idx;   //标识读缓冲区中以及读入的客户端数据的最后一个字节的下一个未知

    int m_checked_index;  // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;    // 当前正在解析的行的起始位置
    char *m_url;   //获取目标文件的文件名
    char *m_version; // 版本协议，只支持HTTP1.1
    METHOD m_method;  // 请求方法
    char *m_host;     // 主机名
    bool m_linger;   // HTTP请求是否保持连接
    long m_content_length;
    char m_real_file[FILENAME_LEN];
    struct stat m_file_stat;
    char *m_file_address;      // 客户请求的目标文件被mmap到内存中的起始位置
    int m_write_idx;    
    CHECK_STATE m_check_state; // 主状态机当前所处的状态
    char m_write_buf[WRITE_BUFFER_SIZE];  // 写缓冲区
    struct iovec m_iv[2];      // 结构体包含 内存的起始位置和长度， 这里设置成2表示一个 写缓冲区和一个mmap映射的请求体
    int m_iv_count;            // 表示被写内存块的数量
    int bytes_to_send;
    int bytes_have_send;


};





#endif
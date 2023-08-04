#include "http_conn.h"


int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


// 网站的根目录
const char* doc_root = "/home/ztm/webserver/resource";

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//设置文件描述符非阻塞
int setnonblocking(int fd){
    //fcntl系统调用可以用来对已打开的文件描述符进行各种控制操作以改变已打开文件的的各种属性
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;  
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

//添加文件描述符到epoll中
void addfd(int epollfd, int fd, bool one_shot){

    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;           // 水平触发
    // event.events = EPOLLIN | EPOLLET |EPOLLRDHUP;  // 边沿触发， 这里改了之后lisetnfd还要单独处理

    // 即使ET模式，一个socket上某个事件还是可能触发多次， 问题：可能会有两个线程同时修改某个socket上的数据
    if(one_shot){
        // 限制 同一时间只能有一个线程操作一个套接字文件描述符
        event.events | EPOLLONESHOT;     
    }
    // 对 参3：fd  应用的参1：epfd 执行 参2操作
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);

}

// 从epoll中删除文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符,重置socket上EPOLLONESHOT事件，确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT |EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    
    // 设置端口复用,设置套接字描述符的属性
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true);
    m_user_count++; //客户总数+1

    init();
}


void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE; //初始化状态为解析请求首行
    m_checked_index = 0;
    m_start_line = 0;
    m_read_idx = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    // 解析请求行的变量
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_linger = false;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
    
}

// 关闭连接
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 客户总数量-1；
    }
}

// 循环读取客户数据， 直到无数据可读或者对方关闭连接
bool http_conn::read(){
    
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }

    // 读取到的字节
    int bytes_read = 0;
    while(true){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 没有数据
                break;
            }
            return false;
        }
        else if(bytes_read == 0){
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("读取到了数据：%s\n", m_read_buf);
    return true;
}

//主状态机
http_conn::HTTP_CODE http_conn::process_read(){

    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret  =  NO_REQUEST;
    char * text = 0;
    printf("正在解析http请求报文\n");
    printf("----------------------------------\n");
    std::cout<<"m_check_state:"<<m_check_state<<std::endl;
    std::cout<<"line_status:"<<line_status<<std::endl;
    // ----------这里我出现了一个问题这个m_check_state=0；不等于2；
    while(( m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||((line_status = parse_line())== LINE_OK)){
        //解析到了一行完整的数据  或者 解析到了请求体，也是完整的数据
        printf("循环获取http请求报文的信息,并处理!\n");
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_index;
        printf("got 1 http line : %s\n", text);

        switch(m_check_state){

            // 当前正在分析请求行，第一行GET或者POST这行
            case CHECK_STATE_REQUESTLINE:
            {   
                printf("parse_request_line!\n");
                ret = parse_request_line(text);
                std::cout<<"parse_request_line返回值:  " << ret << std::endl;
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                // 我这的干， 这里的==我他么写成了= ，这问题定位是真难找阿
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    return do_request();  // 解析具体信息
                }
    
            }

            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN; // 数据不完整
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }
        // return NO_REQUEST;
    }

    return NO_REQUEST;
}

// 解析HTTP请求行， 获得请求方法，目标URL, HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    // GET /index.html HTTI/1.1
    std::cout<<"请求行："<< text<< std::endl;
    m_url = strpbrk(text, " \t");
    // GET\0/index.html HTTI/1.1
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char * method = text;
    if( strcasecmp(method, "GET")==0){
        m_method = GET;
    }else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        // 没有值
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1")!=0){
        return BAD_REQUEST;
    }

    // http://192.168.1.1:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;  // 192.168.1.1:10000/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }

    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "index.html");

    m_check_state = CHECK_STATE_HEADER;  // 主状态机检查状态变成检查请求头

    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    // 遇到空行，表示头部字段解析完毕
    if(text[0] == '\0'){
        // 如果HTTP请求有消息体，则还需要读取 m_content_length 字节的消息体，
        // 状态机转移到 CHECK_STATE_CONTENT状态
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求头
        return GET_REQUEST;
    }else if( strncasecmp(text, "Connection",11)==0){
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if( strncasecmp(text, "Content-Length:", 15)==0){
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }else if( strncasecmp( text, "Host:", 5)==0){
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }else{
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

// 没有真正的解析HTTP请求的消息体， 只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if( m_read_idx >= (m_content_length + m_checked_index)){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行，判断依据 \r \n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    printf("解析一行！\n");
    for( ; m_checked_index < m_read_idx; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if( temp == '\r'){
            if((m_checked_index + 1) == m_read_idx){
                printf("parse_line返回的是LINE_OPEN!1\n");
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_index + 1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                printf("parse_line返回的是LINE_OK!2\n");
                return LINE_OK;
            }
            return LINE_BAD;
        }else if( temp == '\n'){
            if((m_checked_index > 1) && (m_read_buf[m_checked_index - 1] == '\r') ){
                m_read_buf[m_checked_index - 1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                printf("parse_line返回的是LINE_OK!3\n");
                return LINE_OK;
            }
            printf("parse_line返回的是LINE_BAD!4\n");
            return LINE_BAD;
        }
    }
    printf("parse_line返回的是LINE_OPEN!5\n");
    return LINE_OPEN;
}

// 当得到一个完整、正确的HTTP请求时，我们就要分析目标文件的属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    // "/home/ztm/webserver/resources"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    
    // m_real_file 就是真实资源文件
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len + 1); 
    
    // 获取m_real_file文件的相关的状态信息， -1失败， 0成功
    if( stat(m_real_file, &m_file_stat) < 0){
       return NO_RESOURCE;
    }
    
    // 判断访问权限
    if( !(m_file_stat.st_mode) & S_IROTH){
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if( S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射, 这里就是把网页的数据映射到了地址上
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write(){
    // 一次性写完数据
    int temp = 0;
    // bytes_have_send = 0; // 已发送的字节
    // bytes_to_send = m_write_idx;  // 将要发送的字节 (m_write_idx) 

    if(bytes_to_send == 0){
        // 将要发送的字节为0， 这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1){
        // 分散写 ， 不连续的内存
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if( temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然
            if(errno==EAGAIN){
                modfd( m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
//         if(bytes_to_send <= bytes_have_send){
//             // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否
//             unmap();
//             if(m_linger){
//                 init();
//                 modfd( m_epollfd, m_sockfd, EPOLLIN);
//                 return true;
//             }else {
//                 modfd(m_epollfd, m_sockfd, EPOLLIN);
//                 return false;
//             }
//         }
//     }

//     return true;
// }
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ...){
    if( m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if( len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){
        va_end(arg_list); // 增的
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

// 添加响应状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}

// 响应头 Content-Length
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

// 响应头 Content-Type
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 响应头Connection
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 响应头里 加入空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

//
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

// 根据服务器处理HTTP请求的结果， 决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
    std::cout<< "HTTP_CODE:" << ret<<std::endl;
    switch(ret){
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }

        case BAD_REQUEST:
        {
            add_status_line(404, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
                return false;
            break;
        }

        case NO_RESOURCE:
        {
            add_status_line(400, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }

        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }

        case FILE_REQUEST:   // 请求到了文件
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }

        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 由线程池中的工作线程调用的，这是处理HTTP请求的入口函数
void http_conn::process(){
    // 解析HTTP请求
    // process_read();
    
    HTTP_CODE read_ret = process_read();
    printf("已经执行了process_read\n");
    printf("process_read()返回的 HTTP_CODE:%d\n", read_ret);
    
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    
    
    printf("parse request, create response\n");

    // 生成响应，数据准备好然后响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);

}


- epoll_event

  ```
  event.events = EPOLLIN | EPOLLRDHUP; 
    // EPOLLIN ：表示相应的文件描写叙述符能够读（包含对端SOCKET正常关闭）；
    // EPOLLOUT：表示相应的文件描写叙述符能够写。
    // EPOLLPRI：表示相应的文件描写叙述符有紧急的数据可读（这里应该表示有带外数据到来）；
    // EPOLLERR：表示相应的文件描写叙述符错误发生；
    // EPOLLET： 将EPOLL设为边缘触发(Edge Triggered)模式。这是相对于水平触发(Level Triggered)来说的。
    // EPOLLONESHOT：仅仅监听一次事件。当监听完这次事件之后，就会把这个fd从epoll的队列中删除。
    // EPOLLHUP：表示相应的文件描写叙述符被挂断,
    // EPOLLRDHUP是:从Linux内核2.6.17开始由GNU引入的事件。当socket接收到对方关闭连接时的请求之后触发，有可能是TCP连接被对方关闭，也有可能是对方关闭了写操作。如果不使用EPOLLRDHUP事件，我们也可以单纯的使用EPOLLIN事件然后根据recv函数的返回值来判断socket上收到的是有效数据还是对方关闭连接的请求。
  ```


- process() 函数

  - **解析HTTP请求.**解析请求行、请求头 

  - **生成响应.**

  - ```
    - events[i].events & EPOLLIN     读事件发生
    - users[sockfd].read()         
    - pool->append(user+sockfd)   任务追加到线程池中  
    - threadpool<T>::run()   不断的循环去工作队列里面去取
    - 取到之后执行   process();
    ```


- recv() 函数

  ```
  int recv(SOCKET s, char FAR *buf, int len, int flags);
  参数说明
      第一个参数指定接收端套接字描述符； 
      第二个参数指明一个缓冲区，该缓冲区用来存放recv函数接收到的数据； 
      第三个参数指明buf的长度；
      第四个参数一般置0。
  返回值
      成功执行时，返回接收到的字节数。
      另一端已关闭则返回0。
      失败返回-1，errno被设为以下的某个值 ：
          EAGAIN：套接字已标记为非阻塞，而接收操作被阻塞或者接收超时 
          EBADF：sock不是有效的描述词 
          ECONNREFUSE：远程主机阻绝网络连接 
          EFAULT：内存空间访问出错 
          EINTR：操作被信号中断 
          EINVAL：参数无效 
          ENOMEM：内存不足 
          ENOTCONN：与面向连接关联的套接字尚未被连接上 
          ENOTSOCK：sock索引的不是套接字 当返回值是0时，为正常关闭连接；
  ```


- 一个请求报文

  GET / HTTP/1.1
  Host: 47.100.187.121:38350
  User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/115.0
  Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8
  Accept-Language: zh-CN,zh;q=0.8,zh-TW;q=0.7,zh-HK;q=0.5,en-US;q=0.3,en;q=0.2
  Accept-Encoding: gzip, deflate
  Connection: keep-alive
  Upgrade-Insecure-Requests: 1


- 一个响应报文

  HTTP/1.1 200 OK
  Access-Control-Allow-Credentials: true
  Access-Control-Allow-Methods: GET, POST, OPTIONS
  Access-Control-Allow-Origin: *
  Cache-Control: no-cache
  Connection: keep-alive
  Content-Length: 0
  Content-Type: image/gif
  Date: Mon, 31 Jul 2023 14:53:49 GMT
  Pragma: no-cache
  Server: nginx/1.8.0
  Tracecode: 32297180122540380938073122
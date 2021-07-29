# simple_high_performance_server
这是一个C++高性能服务器的项目，来自一个付费的课程；
课程连接：https://appd872nnyh9503.pc.xiaoe-tech.com/detail/p_5c77e5ed20b1d_lfUshV5R/6
大部分代码进行了重新的注释；下面数项目的总结以及项目的亮点
项目的流程：
首先进行
初始化工作：
1、环境变量的统计，为命名程序做准备
2、配置文件初始化
3、CRC32和内存分配类的初始化
4、信号量的初始化，csocket的第一次初始化（创建套接字）
5、创建守护进程，
创建工作进程：
1、屏蔽所有信号，创建子进程，
2、父进程返回，取消信号屏蔽；睡眠等待信号处理；
3、子进程返回，进行线程池的创建和csoket的二次初始化（信号量、互斥量、线程）；
epoll循环开始
1、创建epoll，创建连接池，将监听套接字绑定连接池的元素，加入epoll的监听序列
2、等待epoll套接字的返回
3、如果监听套接字是返回，从连接池获取一个元素，设置连接套接字的读写回调函数；加入时间队列
4、如果是连接套接字返回，如果是读事件；使用消息头+包头+包体的方法从缓冲区获取内容；加入逻辑处理的队列；生成需要发送的数据包，加入发送队列，唤醒发送任务的线程；
5、如果是连接套接字返回，如果是写事件；调用封装的send函数；如果数据返送成功，取消监听事件；如果只发送部分数据；修改需要发送的数据信息
6、打印连接信息
7、管理时间队列的线程：睡眠一段时间；检查检查时间队列的元素；如果长时间没有收到数据，服务器主动断开连接；
8、管理关闭连接的线程：睡眠一段时间；检查待释放队列的元素；进行时间检查；时间到达；进行延迟释放
9、管理发送任务的线程：通过信号量，一直等待；在被唤醒之后，先检查任务队列；检查队列元素的连接是否过期；不过期，调用封装的send函数；数据一次发送完，发送下一个元素；如果发送部分修改连接元素的与发送有关的数据向；出错打印错误信息；
程序结束：
1、连接池，内存分配队列的回收，线程的终止
2、父进程回收资源终止
父进程回收子进程资源
项目的亮点:
1、通过配置文件可以灵活进行程序配置
2、通过消息头的iCurrsequence（来自连接元素的iCurrsequence）进行校验，防止串话的发生
3、通过时间队列，可以管理一些不断开连接，但是一直不发送数据的连接
4、通过包头的结构体（包含长度），解决了沾包的问题；
5、通过内存分配，可以挤压多个待发送的数据包
6、通过可以iSendCount，在发送缓冲区满了之后，不会尝试发送数据包；

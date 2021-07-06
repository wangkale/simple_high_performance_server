#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    
#include <stdarg.h>   
#include <unistd.h>    
#include <sys/time.h>  
#include <time.h>     
#include <fcntl.h>   
#include <errno.h>     
#include <sys/ioctl.h> 
#include <arpa/inet.h>
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
//当新的连接接入服务器，从连接池取一个连接处理；LT模式，
//快速返回；ET模式，处理接可能多的连接
void CSocekt::ngx_event_accept(lpngx_connection_t oldc)
{

    struct sockaddr    mysockaddr;       
    socklen_t          socklen;
    int                err;
    int                level;
    int                s;
    static int         use_accept4 = 1;  
    lpngx_connection_t newc;      

    socklen = sizeof(mysockaddr);
    do   
    {     
        if(use_accept4)
        {
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK); 
        }
        else
        {
            s = accept(oldc->fd, &mysockaddr, &socklen);
        }
        //建立连接失败，根据错误原因进行处理
        if(s == -1)
        {
            err = errno;
            //系统提示在试一次
            if(err == EAGAIN) 
            {
                return ;
            } 
            level = NGX_LOG_ALERT;
            if (err == ECONNABORTED) 
            {
                level = NGX_LOG_ERR;
            } 
            else if (err == EMFILE || err == ENFILE) 
            {
                level = NGX_LOG_CRIT;
            }
            //使用accept4出现错误
            if(use_accept4 && err == ENOSYS) 
            {
                use_accept4 = 0;  
                continue;        
            }
            //在未建立连接时，客户端发送置位；
            if (err == ECONNABORTED)  
            {
            }
            //描述符达到上限
            if (err == EMFILE || err == ENFILE) 
            {
            }            
            return;
        }  

        //走到这里的，表示accept4()/accept()成功了        
        if(m_onlineUserCount >= m_worker_connections)  
        {
            //ngx_log_stderr(0,"超出系统允许的最大连入用户数(最大允许连入数%d)，关闭连入请求(%d)。",m_worker_connections,s);  
            close(s);
            return ;
        }
        //如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用ngx_get_connection()使用我们短时间内产生大量连接，危及本服务器安全
        if(m_connectionList.size() > (m_worker_connections * 5))
        {
            //比如你允许同时最大2048个连接，但连接池却有了 2048*5这么大的容量，这肯定是表示短时间内 产生大量连接/断开，因为我们的延迟回收机制，这里连接还在垃圾池里没有被回收
            if(m_freeconnectionList.size() < m_worker_connections)
            {
                //整个连接池这么大了，而空闲连接却这么少了，所以我认为是  短时间内 产生大量连接，发一个包后就断开，我们不可能让这种情况持续发生，所以必须断开新入用户的连接
                //一直到m_freeconnectionList变得足够大【连接池中连接被回收的足够多】
                close(s);
                return ;   
            }
        }
     

        //从连接池获取一个连接
        newc = ngx_get_connection(s); 
        if(newc == NULL)
        {
            if(close(s) == -1)
            {
                ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_event_accept()中close(%d)失败!",s);                
            }
            return;
        }
        //设置连接的回调函数，地址，以及套接字；
        memcpy(&newc->s_sockaddr,&mysockaddr,socklen);  
        //使用accept失败，使用非阻塞；
        if(!use_accept4)
        {
            if(setnonblocking(s) == false)
            {
                ngx_close_connection(newc);
                return; 
            }
        }
        newc->listening = oldc->listening;                   
        newc->rhandler = &CSocekt::ngx_read_request_handler; 
        newc->whandler = &CSocekt::ngx_write_request_handler; 
        //将读事件加入epoll
        if(ngx_epoll_oper_event(
                                s,                  
                                EPOLL_CTL_ADD,      
                                EPOLLIN|EPOLLRDHUP, 
                                0,                  
                                newc                
                                ) == -1)         
        {
            ngx_close_connection(newc);
            return;
        }
        if(m_ifkickTimeCount == 1)
        {
            AddToTimerQueue(newc);
        }
        ++m_onlineUserCount;  
        break;  
    } while (1);   
    return;
}


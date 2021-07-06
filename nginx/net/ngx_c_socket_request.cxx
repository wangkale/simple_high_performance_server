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
#include <pthread.h>  
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h" 
//数据的收发

//使用状态机的原理收数据;在服务器收到连接的数据时被调用
void CSocekt::ngx_read_request_handler(lpngx_connection_t pConn)
{  
    bool isflood = false; //是否flood攻击；
    ssize_t reco = recvproc(pConn,pConn->precvbuf,pConn->irecvlen); 
    if(reco <= 0)  
    {
        return;    
    }
    //尚未接收数据
    if(pConn->curStat == _PKG_HD_INIT) 
    {        
        if(reco == m_iLenPkgHeader)
        {   
            ngx_wait_request_handler_proc_p1(pConn,isflood); 
        }
        else
		{
            pConn->curStat        = _PKG_HD_RECVING;                 	
            pConn->precvbuf       = pConn->precvbuf + reco;           
            pConn->irecvlen       = pConn->irecvlen - reco;             
        }
    } 
    //收到部分包头数据
    else if(pConn->curStat == _PKG_HD_RECVING) 
    {
        if(pConn->irecvlen == reco) //
        {
            
            ngx_wait_request_handler_proc_p1(pConn,isflood); 
        }
        else
		{
            pConn->precvbuf       = pConn->precvbuf + reco;              
            pConn->irecvlen       = pConn->irecvlen - reco;              
        }
    }
    //包体尚未开始接收
    else if(pConn->curStat == _PKG_BD_INIT) 
    {
        if(reco == pConn->irecvlen)
        {
            if(m_floodAkEnable == 1) 
            {
                isflood = TestFlood(pConn);
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        }
        else
		{
			pConn->curStat = _PKG_BD_RECVING;					
			pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
		}
    }
    //包体只收了一半
    else if(pConn->curStat == _PKG_BD_RECVING) 
    {
        if(pConn->irecvlen == reco)
        {
            if(m_floodAkEnable == 1) 
            {
                isflood = TestFlood(pConn);
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        }
        else
        {
            pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
        }
    }  
    if(isflood == true)
    {
        zdClosesocketProc(pConn);
    }
    return;
}
//对recv函数进行封装
ssize_t CSocekt::recvproc(lpngx_connection_t pConn,char *buff,ssize_t buflen)  
{
    ssize_t n;
    n = recv(pConn->fd, buff, buflen, 0);     
    //客户端断开连接
    if(n == 0)
    {
        zdClosesocketProc(pConn);        
        return -1;
    }
    if(n < 0) 
    {  
        //非阻塞套接字引发的错误
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");
            return -1; 
        }
        //捕捉到信号引发的错误
        if(errno == EINTR)  
        {
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EINTR成立，出乎我意料！");
            return -1; 
        }
        //下面的错误代表要回收连接,
        //客户端直接关闭，未发送FIN
        if(errno == ECONNRESET)  
        {
        }
        else
        {
            if(errno == EBADF)  
            {
            }
            else
            {
                ngx_log_stderr(errno,"CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！");  
            }
        } 
        zdClosesocketProc(pConn);
        return -1;
    }
    return n; 
}
//解析包头
void CSocekt::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn,bool &isflood)
{    
    CMemory *p_memory = CMemory::GetInstance();		
    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; 
    unsigned short e_pkgLen; 
    e_pkgLen = ntohs(pPkgHeader->pkgLen); 
    //防止恶意包
    //包的长度小于包头
    if(e_pkgLen < m_iLenPkgHeader) 
    {
        pConn->curStat = _PKG_HD_INIT;      
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    //包的长度太长了
    else if(e_pkgLen > (_PKG_MAX_LENGTH-1000))
    {
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    //合法包处理
    else
    {  
        char* pTmpBuffer = (char*)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen, false);
        pConn->precvMemPointer = pTmpBuffer;  
        //消息头填充
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = pConn;
        ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
        //包头的填充
        pTmpBuffer += m_iLenMsgHeader;               
        memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader); 
        if(e_pkgLen == m_iLenPkgHeader)
        {
            //该报文只有包头无包体【我们允许一个包只有包头，没有包体】
            //这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
            if(m_floodAkEnable == 1) 
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        } 
        else
        {
            //开始收包体，注意我的写法
            pConn->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;  //pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体 weizhi
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
        }                       
    }  
    return;
}
//包收完的处理
void CSocekt::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn,bool &isflood)
{
     //正确的包加入消息队列
    if(isflood == false)
    {
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer); 
    }
    else
    {
        CMemory *p_memory = CMemory::GetInstance();
        p_memory->FreeMemory(pConn->precvMemPointer); 
    }
    //为下一次的收包做准备
    pConn->precvMemPointer = NULL;
    pConn->curStat         = _PKG_HD_INIT;                         
    pConn->precvbuf        = pConn->dataHeadInfo;  
    pConn->irecvlen        = m_iLenPkgHeader;  
    return;
}
//对send函数进行封装
ssize_t CSocekt::sendproc(lpngx_connection_t c,char *buff,ssize_t size)  //ssize_t是有符号整型，在32位机器上等同与int，在64位机器上等同与long int，size_t就是无符号型的ssize_t
{
    ssize_t   n;
    for ( ;; )
    {
        n = send(c->fd, buff, size, 0);
        if(n > 0) 
        {        
            return n; 
        }
        //send=0表示超时，对方主动关闭了连接过程
        if(n == 0)
        {
            return 0;
        }
        if(errno == EAGAIN)  
        {
            return -1;  
        }
        if(errno == EINTR) 
        {
            ngx_log_stderr(errno,"CSocekt::sendproc()中send()失败.");  
        }
        else
        {
            return -2;    
        }
    } 
}
//发送数据时的回调函数
void CSocekt::ngx_write_request_handler(lpngx_connection_t pConn)
{      
    CMemory *p_memory = CMemory::GetInstance();
    ssize_t sendsize = sendproc(pConn,pConn->psendbuf,pConn->isendlen);
    if(sendsize > 0 && sendsize != pConn->isendlen)
    {        
        pConn->psendbuf = pConn->psendbuf + sendsize;
		pConn->isendlen = pConn->isendlen - sendsize;	
        return;
    }
    else if(sendsize == -1)
    {
        ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。"); //打印个日志，别的先不干啥
        return;
    }
    //数据发送结束，去掉epoll事件
    if(sendsize > 0 && sendsize == pConn->isendlen) 
    {
        if(ngx_epoll_oper_event(
                pConn->fd,          
                EPOLL_CTL_MOD,      
                EPOLLOUT,           
                1,                
                pConn              
                ) == -1)
        {
            ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");
        }    
    }
    //发送完成的数据释放
    p_memory->FreeMemory(pConn->psendMemPointer); 
    pConn->psendMemPointer = NULL;        
    --pConn->iThrowsendCount;
    //可以继续发送数据
    if(sem_post(&m_semEventSendQueue)==-1)       
        ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");
    return;
}
//使用派生类中的虚函数
void CSocekt::threadRecvProcFunc(char *pMsgBuf)
{   
    return;
}



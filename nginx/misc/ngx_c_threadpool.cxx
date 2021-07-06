#include <stdarg.h>
#include <unistd.h>
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

//静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;  
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER; 
bool CThreadPool::m_shutdown = false;          

//构造函数
CThreadPool::CThreadPool()
{
    m_iRunningThreadNum = 0;  
    m_iLastEmgTime = 0;       
    m_iRecvMsgQueueCount = 0;
}

//析构函数
CThreadPool::~CThreadPool()
{    
    clearMsgRecvQueue();
}

//清理消息队列
void CThreadPool::clearMsgRecvQueue()
{
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
}

//创建线程池，创建成功返回true
bool CThreadPool::Create(int threadNum)
{    
    ThreadItem *pNew;
    int err;
    m_iThreadNum = threadNum;
    for(int i = 0; i < m_iThreadNum; ++i)
    {
        m_threadVector.push_back(pNew = new ThreadItem(this));                    
        err = pthread_create(&pNew->_Handle, NULL, ThreadFunc, pNew);      
        if(err != 0)
        {
            ngx_log_stderr(err,"CThreadPool::Create()创建线程%d失败，返回的错误码为%d!",i,err);
            return false;
        }
        else
        {
        }        
    }
    std::vector<ThreadItem*>::iterator iter;
lblfor:
    for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if( (*iter)->ifrunning == false) 
        {
            usleep(100 * 1000);  
            goto lblfor;
        }
    }
    return true;
}

//线程入库函数
void* CThreadPool::ThreadFunc(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool *pThreadPoolObj = pThread->_pThis;
    CMemory *p_memory = CMemory::GetInstance();	    
    int err;
    pthread_t tid = pthread_self(); 
    while(true)
    {
        err = pthread_mutex_lock(&m_pthreadMutex);  
        if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
        //使用循环，防止虚假唤醒
        while ( (pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
        {
            if(pThread->ifrunning == false)            
                pThread->ifrunning = true; 
            pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex); 
        }
        if(m_shutdown)
        {   
            pthread_mutex_unlock(&m_pthreadMutex); 
            break;                     
        }
        char *jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();     
        pThreadPoolObj->m_MsgRecvQueue.pop_front();              
        --pThreadPoolObj->m_iRecvMsgQueueCount;                    
        err = pthread_mutex_unlock(&m_pthreadMutex); 
        if(err != 0)  ngx_log_stderr(err,"CThreadPool::ThreadFunc()中pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        ++pThreadPoolObj->m_iRunningThreadNum;  
        //调用逻辑处理函数
        g_socket.threadRecvProcFunc(jobbuf); 
        p_memory->FreeMemory(jobbuf);              
        --pThreadPoolObj->m_iRunningThreadNum;     

    } 
    return (void*)0;
}
//结束线程
void CThreadPool::StopAll() 
{  
    //防止重复调用
    if(m_shutdown == true)
    {
        return;
    }
    m_shutdown = true;
    //解锁所有线程
    int err = pthread_cond_broadcast(&m_pthreadCond); 
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!",err);
        return;
    }
    //等待线程返回   
    std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); 
    }
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);    
    //释放线程池的句柄  
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if(*iter)
			delete *iter;
	}
	m_threadVector.clear();
    ngx_log_stderr(0,"CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");
    return;    
}
//消息放入消息队列
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
    int err = pthread_mutex_lock(&m_pthreadMutex);     
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!",err);
    }
    m_MsgRecvQueue.push_back(buf);	         
    ++m_iRecvMsgQueueCount;              
    err = pthread_mutex_unlock(&m_pthreadMutex);   
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
    }
    Call();                                  
    return;
}
//任务到达
void CThreadPool::Call()
{
    int err = pthread_cond_signal(&m_pthreadCond); 
    if(err != 0 )
    {
        ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!",err);
    }
    //如果线程全部在工作
    if(m_iThreadNum == m_iRunningThreadNum)
    {        
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10) 
        {
            m_iLastEmgTime = currtime; 
            ngx_log_stderr(0,"CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    } 
    return;
}

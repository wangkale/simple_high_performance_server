#ifndef __NGX_THREADPOOL_H__
#define __NGX_THREADPOOL_H__
#include <vector>
#include <pthread.h>
#include <atomic>
//线程池类的定义

class CThreadPool
{
public:
    CThreadPool();               
    ~CThreadPool();                           
public:
    bool Create(int threadNum);                    
    void StopAll();                            

    void inMsgRecvQueueAndSignal(char *buf);       
    void Call();                             
    int  getRecvMsgQueueCount(){return m_iRecvMsgQueueCount;} 

private:
    static void* ThreadFunc(void *threadData);        
    void clearMsgRecvQueue();                       

private:
    struct ThreadItem   
    {
        pthread_t   _Handle;                        
        CThreadPool *_pThis;                       	
        bool        ifrunning;                    
        ThreadItem(CThreadPool *pthis):_pThis(pthis),ifrunning(false){}                             
        ~ThreadItem(){}        
    };
private:
    static pthread_mutex_t     m_pthreadMutex;      
    static pthread_cond_t      m_pthreadCond;       
    static bool                m_shutdown;                      //线程运行标志
    int                        m_iThreadNum;                       //要创建的线程数量
    std::atomic<int>           m_iRunningThreadNum; //运行的线程数
    time_t                     m_iLastEmgTime;                        //上次发生线程不足的事件
    std::vector<ThreadItem *>  m_threadVector;      //存放线程的句柄
    std::list<char *>          m_MsgRecvQueue;            //收消息的队列
	int                        m_iRecvMsgQueueCount;            //消息数量
};

#endif

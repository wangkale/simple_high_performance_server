#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   
#include <errno.h>    
#include <unistd.h>
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums,const char *pprocname);
static void ngx_worker_process_cycle(int inum,const char *pprocname);
static void ngx_worker_process_init(int inum);

//变量声明
static u_char  master_process[] = "master process";

//描述：创建worker子进程
void ngx_master_process_cycle()
{    
    sigset_t set;        
    sigemptyset(&set);   //清空信号集
    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
    {        
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_master_process_cycle()中sigprocmask()失败!");
    }
    size_t size;
    int    i;
    size = sizeof(master_process);  
    size += g_argvneedmem;          
    if(size < 1000) 
    {   
        char title[1000] = {0};
        strcpy(title,(const char *)master_process);
        strcat(title," ");  
        for (i = 0; i < g_os_argc; i++)        
        {
            strcat(title,g_os_argv[i]);
        }
        ngx_setproctitle(title); 
        ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【master进程】启动并开始运行......!",title,ngx_pid); 
    }    

    CConfig *p_config = CConfig::GetInstance(); 
    int workprocess = p_config->GetIntDefault("WorkerProcesses",1); 
    ngx_start_worker_processes(workprocess); 
    //父进程依靠信号运行
    sigemptyset(&set); 
    for ( ;; ) 
    {
        sigsuspend(&set); 
        sleep(1);        
    }
    return;
}

//创建指定数量的子进程
static void ngx_start_worker_processes(int threadnums)
{
    int i;
    for (i = 0; i < threadnums; i++) 
    {
        ngx_spawn_process(i,"worker process");
    }
    return;
}

//创建子进程,启动子进程
static int ngx_spawn_process(int inum,const char *pprocname)
{
    pid_t  pid;
    pid = fork();
    switch (pid) 
    {  
    case -1: 
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!",inum,pprocname);
        return -1;
    case 0:
        ngx_parent = ngx_pid;             
        ngx_pid = getpid();               
        ngx_worker_process_cycle(inum,pprocname);  
        break;
    default:     
        break;
    }
    return pid;
}

//子进程运行的过程
static void ngx_worker_process_cycle(int inum,const char *pprocname) 
{
    ngx_process = NGX_PROCESS_WORKER;  
    ngx_worker_process_init(inum);
    ngx_setproctitle(pprocname); 
    ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【worker进程】启动并开始运行......!",pprocname,ngx_pid); 
    for(;;)
    {
        ngx_process_events_and_timers(); 
    } 
    g_threadpool.StopAll();  
    g_socket.Shutdown_subproc();
    return;
}

//子进程初始化，包括信号，线程池，收发线程，epoll初始化
static void ngx_worker_process_init(int inum)
{
    sigset_t  set;     
    sigemptyset(&set); 
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_worker_process_init()中sigprocmask()失败!");
    }
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount",5); 
    if(g_threadpool.Create(tmpthreadnums) == false) 
    {
        exit(-2);
    }
    sleep(1); 
    if(g_socket.Initialize_subproc() == false) 
    {
        exit(-2);
    }
    g_socket.ngx_epoll_init();           
    return;
}

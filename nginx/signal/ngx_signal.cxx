#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>    
#include <errno.h>   
#include <sys/wait.h>  

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h" 

//和信号处理有关的函数

//信号相关的结构体，有信号，信号名，处理函数
typedef struct 
{
    int           signo;       
    const  char   *signame;   
    void  (*handler)(int signo, siginfo_t *siginfo, void *ucontext); 
} ngx_signal_t;
//信号处理函数
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext); 
//子进程回收函数
static void ngx_process_get_status(void);                                   
//指定信号和信号的处理函数
ngx_signal_t  signals[] = {
    // signo      signame             handler
    { SIGHUP,    "SIGHUP",           ngx_signal_handler },          
    { SIGINT,    "SIGINT",           ngx_signal_handler },             
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },       
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },       //子进程终止的信号
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        
    { SIGIO,     "SIGIO",            ngx_signal_handler },              
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL               },        
    { 0,         NULL,               NULL               }         
};
//注册信号处理函数
int ngx_init_signals()
{
    ngx_signal_t      *sig;  
    struct sigaction   sa;   

    for (sig = signals; sig->signo != 0; sig++) 
    {        
        memset(&sa,0,sizeof(struct sigaction));
        if (sig->handler)
        {
            sa.sa_sigaction = sig->handler;
            sa.sa_flags = SA_SIGINFO;      
        }
        else
        {
            sa.sa_handler = SIG_IGN; 
        } 
        sigemptyset(&sa.sa_mask);   
        if (sigaction(sig->signo, &sa, NULL) == -1) 
        {   
            ngx_log_error_core(NGX_LOG_EMERG,errno,"sigaction(%s) failed",sig->signame);
            return -1; 
        }	

    } 
    return 0; 
}
//信号处理函数,判断是那个进程说到的进程，根据信号处理
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext)
{    
    ngx_signal_t    *sig;    
    char            *action; 
    for (sig = signals; sig->signo != 0; sig++)    
    {         
        if (sig->signo == signo) 
        { 
            break;
        }
    }
    action = (char *)"";  

    if(ngx_process == NGX_PROCESS_MASTER)     
    {
        switch (signo)
        {
        case SIGCHLD: 
            ngx_reap = 1;  
            break;
        default:
            break;
        }
    }
    else if(ngx_process == NGX_PROCESS_WORKER) 
    {
    }
    else
    {
    }

    if(siginfo && siginfo->si_pid)  
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action); 
    }
    else
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received %s",signo, sig->signame, action);
    }
    if (signo == SIGCHLD) 
    {
        ngx_process_get_status(); 
    } 
    return;
}
//子进程的回收
static void ngx_process_get_status(void)
{
    pid_t            pid;
    int              status;
    int              err;
    int              one=0; 
    for ( ;; ) 
    {
        pid = waitpid(-1, &status, WNOHANG); 
        if(pid == 0)
        {
            return;
        } 
        if(pid == -1)
        {
            err = errno;
            //被信号打断
            if(err == EINTR)           
            {
                continue;
            }
            if(err == ECHILD  && one)  
            {
                return;
            }
            if (err == ECHILD) 
            {
                ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
                return;
            }
            ngx_log_error_core(NGX_LOG_ALERT,err,"waitpid() failed!");
            return;
        }  
        one = 1; 
        if(WTERMSIG(status)) 
        {
            ngx_log_error_core(NGX_LOG_ALERT,0,"pid = %P exited on signal %d!",pid,WTERMSIG(status)); 
        }
        else
        {
            ngx_log_error_core(NGX_LOG_NOTICE,0,"pid = %P exited with code %d!",pid,WEXITSTATUS(status)); 
        }
    }
    return;
}

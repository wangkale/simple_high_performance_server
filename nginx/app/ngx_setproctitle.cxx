
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  
#include <string.h>
#include "ngx_global.h"
//设置进程的名字
//先迁移环境变量，在复制环境变量；最后清空为移动的环境变量

//环境变量的迁移
void ngx_init_setproctitle()
{   
    gp_envmem = new char[g_envneedmem]; 
    memset(gp_envmem,0,g_envneedmem); 
    char *ptmp = gp_envmem;
    for (int i = 0; environ[i]; i++) 
    {
        size_t size = strlen(environ[i])+1 ; 
        strcpy(ptmp,environ[i]);      
        environ[i] = ptmp;           
        ptmp += size;
    }
    return;
}

//设置可执行程序标题(设置)
void ngx_setproctitle(const char *title)
{
    size_t ititlelen = strlen(title); 
    size_t esy = g_argvneedmem + g_envneedmem; 
    if( esy <= ititlelen)
    {
        return;
    }
    g_os_argv[1] = NULL;  

    char *ptmp = g_os_argv[0]; 
    strcpy(ptmp,title);
    ptmp += ititlelen; 

    size_t cha = esy - ititlelen;  
    memset(ptmp,0,cha);
    return;
}
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
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"  
#include "ngx_logiccomm.h"  
#include "ngx_c_lockmutex.h"  

//定义函数指针
typedef bool (CLogicSocket::*handler)(  lpngx_connection_t pConn,      
                                        LPSTRUC_MSG_HEADER pMsgHeader,  
                                        char *pPkgBody,                 
                                        unsigned short iBodyLength);  

//定义回调函数，根据消息码调用不同的处理函数
static const handler statusHandler[] = 
{
    &CLogicSocket::_HandlePing,                             //【0】：心跳包的实现
    NULL,                                                   
    NULL,                                                   
    NULL,                                                  
    NULL,                                                 
    &CLogicSocket::_HandleRegister,                        //【5】：实现具体的注册功能
    &CLogicSocket::_HandleLogIn,                            //【6】：实现具体的登录功能
};
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler) 
//构造函数
CLogicSocket::CLogicSocket()
{
}
//析构函数
CLogicSocket::~CLogicSocket()
{
}
//初始化函数
bool CLogicSocket::Initialize()
{
    bool bParentInit = CSocekt::Initialize();
    return bParentInit;
}
//根据收包的类型调用不同的处理函数
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{          
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                  
    LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+m_iLenMsgHeader);
    void  *pPkgBody;                                                             
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);                        
    //获取包体
    if(m_iLenPkgHeader == pkglen)
    {
		if(pPkgHeader->crc32 != 0) 
		{
			return; 
		}
		pPkgBody = NULL;
    }
    else 
	{
		pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);		         
		pPkgBody = (void *)(pMsgBuf+m_iLenMsgHeader+m_iLenPkgHeader); 
		int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody,pkglen-m_iLenPkgHeader);
		if(calccrc != pPkgHeader->crc32)
		{
            ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误[服务器:%d/客户端:%d]，丢弃数据!",calccrc,pPkgHeader->crc32); 
			return; 
		}
        else
        {
        }        
	}
    //
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); 
    lpngx_connection_t p_Conn = pMsgHeader->pConn;     
    //包连接断开
    if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
    {
        return; 
    }
    //判断消息码
	if(imsgCode >= AUTH_TOTAL_COMMANDS)
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!",imsgCode); 
        return; 
    }
    //调用处理函数
    if(statusHandler[imsgCode] == NULL) 
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); 
        return;  
    }
    (this->*statusHandler[imsgCode])(p_Conn,pMsgHeader,(char *)pPkgBody,pkglen-m_iLenPkgHeader);
    return;	
}
//
void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time)
{
    CMemory *p_memory = CMemory::GetInstance();
    if(tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence) 
    {
        lpngx_connection_t p_Conn = tmpmsg->pConn;
        if(m_ifTimeOutKick == 1) 
        {
            zdClosesocketProc(p_Conn); 
        }    
        //超时踢的判断标准就是 每次检查的时间间隔*3，超过这个时间没发送心跳包，就踢
        else if( (cur_time - p_Conn->lastPingTime ) > (m_iWaitTime*3+10) ) 
        {
            zdClosesocketProc(p_Conn); 
        }   
        p_memory->FreeMemory(tmpmsg);
    }
    else 
    {
        p_memory->FreeMemory(tmpmsg);
    }
    return;
}

//给客户端发ping包
void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader,unsigned short iMsgCode)
{
    CMemory  *p_memory = CMemory::GetInstance();
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader,false);
    char *p_tmpbuf = p_sendbuf;
    //复制消息头
	memcpy(p_tmpbuf,pMsgHeader,m_iLenMsgHeader);
	p_tmpbuf += m_iLenMsgHeader;
    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf;	
    //填写包头
    pPkgHeader->msgCode = htons(iMsgCode);	
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader); 
	pPkgHeader->crc32 = 0;		

    msgSend(p_sendbuf);
    return;
}
//数据包逻辑处理
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    if(pPkgBody == NULL)  
    {        
        return false;
    }
    int iRecvLen = sizeof(STRUCT_REGISTER); 
    if(iRecvLen != iBodyLength) 
    {     
        return false; 
    }
    CLock lock(&pConn->logicPorcMutex); 
     //解析收到的数据
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody; 
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);        
    p_RecvInfo->username[sizeof(p_RecvInfo->username)-1]=0; 
    p_RecvInfo->password[sizeof(p_RecvInfo->password)-1]=0;
    //给客户端发送数据
	LPCOMM_PKG_HEADER pPkgHeader;	
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(STRUCT_REGISTER);  
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader+iSendLen,false);
    memcpy(p_sendbuf,pMsgHeader,m_iLenMsgHeader);                  
    //填充包头
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf+m_iLenMsgHeader);  
    pPkgHeader->msgCode = _CMD_REGISTER;	                      
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	           
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);        
    //填充包体
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf+m_iLenMsgHeader+m_iLenPkgHeader);	
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);		
    msgSend(p_sendbuf);
    return true;
}
bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{    
    if(pPkgBody == NULL)
    {        
        return false;
    }		    
    int iRecvLen = sizeof(STRUCT_LOGIN); 
    if(iRecvLen != iBodyLength) 
    {     
        return false; 
    }
    CLock lock(&pConn->logicPorcMutex);
     //获取登录的信息
    LPSTRUCT_LOGIN p_RecvInfo = (LPSTRUCT_LOGIN)pPkgBody;     
    p_RecvInfo->username[sizeof(p_RecvInfo->username)-1]=0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password)-1]=0;
    //填充需要发送的数据
	LPCOMM_PKG_HEADER pPkgHeader;	
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(STRUCT_LOGIN);  
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader+iSendLen,false);    
    memcpy(p_sendbuf,pMsgHeader,m_iLenMsgHeader);    
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf+m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);    
    LPSTRUCT_LOGIN p_sendInfo = (LPSTRUCT_LOGIN)(p_sendbuf+m_iLenMsgHeader+m_iLenPkgHeader);
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo,iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);		   
    msgSend(p_sendbuf);
    return true;
}
bool CLogicSocket::_HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    if(iBodyLength != 0)  
		return false; 
    CLock lock(&pConn->logicPorcMutex); 
    pConn->lastPingTime = time(NULL);   
    SendNoBodyPkgToClient(pMsgHeader,_CMD_PING);
    return true;
}
#ifndef __NGX_C_SLOGIC_H__
#define __NGX_C_SLOGIC_H__
#include <sys/socket.h>
#include "ngx_c_socket.h"
//和逻辑处理有关的类

class CLogicSocket : public CSocekt   
{
public:
	CLogicSocket();                                                   
	virtual ~CLogicSocket();                                       
	virtual bool Initialize();                                         
public:
	//通用收发数据相关函数
	void  SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader,unsigned short iMsgCode);
	//各种业务逻辑相关函数都在之类
	bool _HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	bool _HandleLogIn(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	bool _HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time);
public:
	virtual void threadRecvProcFunc(char *pMsgBuf);
};

#endif

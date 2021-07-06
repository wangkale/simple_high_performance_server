#ifndef __NGX_LOCKMUTEX_H__
#define __NGX_LOCKMUTEX_H__
#include <pthread.h> 
//一个简单的智能锁
class CLock
{
public:
	CLock(pthread_mutex_t *pMutex)
	{
		m_pMutex = pMutex;
		pthread_mutex_lock(m_pMutex); 
	}
	~CLock()
	{
		pthread_mutex_unlock(m_pMutex); 
	}
private:
	pthread_mutex_t *m_pMutex;
};
#endif

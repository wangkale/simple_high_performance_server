#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ngx_c_memory.h"
//内存分配的函数
CMemory *CMemory::m_instance = NULL;
//内存的释放函数
void *CMemory::AllocMemory(int memCount,bool ifmemset)
{	    
	void *tmpData = (void *)new char[memCount]; 
    if(ifmemset) 
    {
	    memset(tmpData,0,memCount);
    }
	return tmpData;
}
//内存释放函数
void CMemory::FreeMemory(void *point)
{		
    delete [] ((char *)point); 
}


#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include "Utils/MemoryMgr.h"

static void InitASI()
{

}


extern "C"
{
	static LONG InitCount = 0;
	__declspec(dllexport) void InitializeASI()
	{
		if ( _InterlockedCompareExchange( &InitCount, 1, 0 ) != 0 ) return;
		InitASI();
	}
}

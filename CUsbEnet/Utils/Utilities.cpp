#include "stdafx.h"

#pragma warning(push)
#pragma warning(disable:4826)

namespace Utilities {

	VOID CreateThread(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID param = NULL) {
		HANDLE handle;
		DWORD lpThreadId;
		ExCreateThread(&handle, 0, &lpThreadId, (void*)XapiThreadStartup, lpStartAddress, param, EX_CREATE_FLAG_SYSTEM | CREATE_SUSPENDED);
		XSetThreadProcessor(handle, 4);
		SetThreadPriority(handle, THREAD_PRIORITY_ABOVE_NORMAL);
		ResumeThread(handle);
	}

	FARPROC ResolveFunction(PCHAR ModuleName, DWORD Ordinal) {
		HMODULE mHandle = GetModuleHandle(ModuleName);
		return (mHandle == NULL) ? NULL : GetProcAddress(mHandle, (LPCSTR)Ordinal);
	}
}
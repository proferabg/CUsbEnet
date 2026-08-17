#include "stdafx.h"

namespace Utilities {

	VOID CreateThread(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID param);
	FARPROC ResolveFunction(PCHAR ModuleName, DWORD Ordinal);
}
#pragma once
#include "stdafx.h"

namespace Hooks {

	typedef struct _CXN_BASE_PARTIAL {
		DWORD Flags;
		DWORD Unknown04;
		CNicUser* NicUser;
	} CXN_BASE_PARTIAL, * PCXN_BASE_PARTIAL;

	VOID Install();
	VOID Remove();
}

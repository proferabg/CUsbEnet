#include "stdafx.h"

BYTE DetourAsm[0x500] = { 0 };
DWORD DetourAsmIndex;
CRITICAL_SECTION DetourAsmSection;

int Int24ToInt32(int Value) {
	byte bValue = (Value >> 24) & 0xFF;
	Value &= 0x00FFFFFF;

	// if the value is supposed to be negative and is signed
	if (Value & 0x800000 && bValue != 0x48)
		Value |= 0xFF000000;

	// if the value has an extra one from 'b' calls then remove it
	if (Value & 1)
		Value -= 1;
	return Value;
}

int GetBranchCall(int Address)
{
	int Dest, Temp;
	Dest = *(int*)Address;
	Temp = Dest;
	Dest = Temp & 0x03FFFFFC;
	if (Temp & 0x02000000) Dest |= 0xFC000000;
	Dest = Address + Dest;
	return Dest;
}

void __declspec(naked) SetupCaller()
{
	__asm
	{
		mr r3, r4
		mr r4, r5
		mr r5, r6
		mr r6, r7
		mr r7, r8
		mr r8, r9
		mr r9, r10
		blr
	}
}

VOID PatchInJump(DWORD* Address, void* Dest, BOOL Linked)
{
	DWORD Bytes[4];
	DWORD Destination = reinterpret_cast<DWORD>(Dest);

	// lis r11, Destination@h
	Bytes[0] = 0x3D600000 | ((Destination >> 16) & 0xFFFF);

	// ori r11, r11, Destination@l
	Bytes[1] = 0x616B0000 | (Destination & 0xFFFF);

	// mtctr r11
	Bytes[2] = 0x7D6903A6;

	// bctr / bctrl
	Bytes[3] = Linked ? 0x4E800421 : 0x4E800420;

	*reinterpret_cast<__int64*>(reinterpret_cast<DWORD>(Address) + 0) = *reinterpret_cast<__int64*>(&Bytes[0]);
	*reinterpret_cast<__int64*>(reinterpret_cast<DWORD>(Address) + 8) = *reinterpret_cast<__int64*>(&Bytes[2]);

	__dcbst(0, Address);
	__sync();
	__isync();
}

typedef struct _DETOUR_XEX_IMPORT_DESCRIPTOR {
	DWORD Size;
	DWORD NameTableSize;
	DWORD ModuleCount;
} DETOUR_XEX_IMPORT_DESCRIPTOR, * PDETOUR_XEX_IMPORT_DESCRIPTOR;

DWORD DetourGetModuleImport(HANDLE HModule, HANDLE HImportedModule, DWORD Ordinal) {

	PDETOUR_XEX_IMPORT_DESCRIPTOR importTable;
	DWORD dwModuleCountIndex = 0;
	DWORD dwModuleIndex = 0;
	CHAR szModuleName[0x100];
	CHAR szHImportedModule[0x100];

	// get the resolve address to compair it to
	DWORD dwImportAddress = (DWORD)GetProcAddress((HMODULE)HImportedModule, (LPCSTR)Ordinal);

	// check if it is valid
	if (dwImportAddress == NULL || HModule == NULL)
		return 0;

	// get the importTable from the module
	importTable = (PDETOUR_XEX_IMPORT_DESCRIPTOR)
		RtlImageXexHeaderField((void*)*(DWORD*)((DWORD)HModule + 0x58), 0x000103FF);

	// see if it is valid
	if (importTable == NULL)
		return 0;

	// get the module name from the handle
	WideCharToMultiByte(CP_UTF8, NULL, (wchar_t*)*(DWORD*)((DWORD)HImportedModule + 0x30), -1, szHImportedModule, 0x100, NULL, NULL);

	// go through all imports
	for (DWORD dwModuleCount = 0; dwModuleCount < importTable->ModuleCount; dwModuleCount++)
	{
		// save the index so we can copy the string
		DWORD i = dwModuleCountIndex;
		// get the pointer to the table
		DWORD dwPointer = (DWORD)importTable + 0x0C + importTable->NameTableSize + dwModuleIndex;
		// get the size
		DWORD dwModuleSectionSize = *(DWORD*)dwPointer;
		// go to the data in the table
		dwPointer += 0x24 + 4;

		// update the index
		dwModuleIndex += dwModuleSectionSize;

		// align the size for the update we did
		dwModuleSectionSize -= 0x24 + 4;

		// update the count
		dwModuleCountIndex += strlen((char*)((DWORD)importTable + 0x0C + i));
		// get the import section's module name
		strcpy(szModuleName, (char*)((DWORD)importTable + 0x0C + i));

		// compair if it is the one we are trying to get the address for
		if (!strcmp(szModuleName, szHImportedModule))
		{
			// scan through the table to find the import
			for (DWORD dwSize = 0; dwSize < dwModuleSectionSize; dwSize += 8)
			{
				DWORD dwTempAddr = *(DWORD*)*(DWORD*)(dwPointer + dwSize);
				if (dwTempAddr == dwImportAddress)
					return *(DWORD*)(dwPointer + dwSize + 4);
			}
		}
	}
	return 0;
}
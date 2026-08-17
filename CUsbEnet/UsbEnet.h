#pragma once
#include "stdafx.h"

const BYTE kVendorSpecificClass = 0xFF;
const BYTE kVendorSpecificSubClass = 0xFF;

extern USBD_DRIVER_OBJECT g_EthernetUsbdDriverObject;

VOID		__fastcall UsbEnetNicAddInternalStats(PNIC_STATS Stats);
NTSTATUS	__fastcall UsbEnetNicUpdateMcastMembership(CEnetAddr* Address, DWORD Membership);
DWORD		__fastcall UsbEnetNicGetLinkState();
VOID		__fastcall UsbEnetNicFlushXmitQueue(CNicUser* User);
VOID		__fastcall UsbEnetNicXmit(CNicUser* User, DWORD Queue, PBYTE Buffer, DWORD Length, PVOID Context);
VOID				   UsbEnetDriverEntry();
VOID		__fastcall UsbEnetNicAttachUser(CNicUser* User);
BOOL		__fastcall UsbEnetNicDetachUser(CNicUser* User);
NTSTATUS	__fastcall UsbEnetNicDoSetOpt(CNicUser* User, DWORD Option, const PBYTE OptionValue, DWORD OptionLength, PDWORD Result);
NTSTATUS	__fastcall UsbEnetNicDoGetOpt(CNicUser* User, DWORD Option, PBYTE OptionValue, PDWORD OptionLength, PDWORD Result);
VOID		__fastcall UsbEnetNicSetUnicastAddress(CEnetAddr* Address, DWORD AddressSlot);
VOID		__fastcall UsbEnetBeginPowerDownAndClose();
BOOL		__fastcall UsbEnetIsPowerDownComplete();
DWORD		__fastcall UsbEnetGetReceiveInFlightCount();
VOID		__fastcall UsbEnetAddDevice(PUSBD_DEVICE_NODE DeviceNode);
VOID		__fastcall UsbEnetRemoveDevice(PUSBD_DEVICE_NODE DeviceNode);
BOOLEAN		__fastcall UsbEnetMatchDriver(PUSB_INTERFACE_DESCRIPTOR interfaceDescriptor, PUSB_DEVICE_DESCRIPTOR deviceDescriptor, PUSBD_DRIVER_OBJECT driverObject);
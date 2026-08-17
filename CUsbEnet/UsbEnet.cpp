#include "stdafx.h"
#include "UsbEnetChipset.h"

USBD_DRIVER_OBJECT g_EthernetUsbdDriverObject =
{
    { NULL, NULL },
    0,
    2,
    2,
    0,
    UsbEnetAddDevice,
    UsbEnetRemoveDevice,
    UsbEnetMatchDriver,
    nullptr
};

VOID __fastcall UsbEnetNicAddInternalStats(PNIC_STATS Stats) {
    g_UsbEnet.NicBaseAddInternalStats(&g_UsbEnet, Stats);
}

NTSTATUS __fastcall UsbEnetNicUpdateMcastMembership(CEnetAddr* Address, DWORD Membership) {
    return g_UsbEnet.NicUpdateMcastMembership(Address, Membership);
}

DWORD __fastcall UsbEnetNicGetLinkState() {
    return g_UsbEnet.NicGetLinkState();
}

VOID __fastcall UsbEnetNicFlushXmitQueue(CNicUser* User) {
    g_UsbEnet.NicFlushXmitQueue(User);
}

VOID __fastcall UsbEnetNicXmit(CNicUser* User, DWORD Queue, PBYTE Buffer, DWORD Length, PVOID Context) {
    g_UsbEnet.NicXmit(User, Queue, Buffer, Length, Context);
}

VOID UsbEnetDriverEntry() {
    g_UsbEnet.DriverEntry();
}

VOID __fastcall UsbEnetNicAttachUser(CNicUser* User) {
    g_UsbEnet.NicAttachUser(User);
}

BOOL __fastcall UsbEnetNicDetachUser(CNicUser* User) {
    return g_UsbEnet.NicDetachUser(User);
}

NTSTATUS __fastcall UsbEnetNicDoSetOpt(CNicUser* User, DWORD Option, const PBYTE OptionValue, DWORD OptionLength, PDWORD Result) {
    return g_UsbEnet.NicDoSetOpt(User, Option, OptionValue, OptionLength, Result);
}

NTSTATUS __fastcall UsbEnetNicDoGetOpt(CNicUser* User, DWORD Option, PBYTE OptionValue, PDWORD OptionLength, PDWORD Result) {
    return g_UsbEnet.NicDoGetOpt(User, Option, OptionValue, OptionLength, Result);
}

VOID __fastcall UsbEnetNicSetUnicastAddress(CEnetAddr* Address, DWORD AddressSlot) {
    g_UsbEnet.NicSetUnicastAddress(Address, AddressSlot);
}

VOID __fastcall UsbEnetBeginPowerDownAndClose() {
    g_UsbEnet.BeginPowerDownAndClose();
}

BOOL __fastcall UsbEnetIsPowerDownComplete() {
    return g_UsbEnet.IsPowerDownComplete();
}

DWORD __fastcall UsbEnetGetReceiveInFlightCount() {
    return g_UsbEnet.GetReceiveInFlightCount();
}

VOID __fastcall UsbEnetAddDevice(PUSBD_DEVICE_NODE DeviceNode) {
    const PUSB_ENDPOINT_DESCRIPTOR InterruptEndpoint = UsbdGetEndpointDescriptor(DeviceNode, 0, 3, 1);
    if (InterruptEndpoint == NULL) {
        DbgPrint("[usbenet]: Couldn't retrieve USB interrupt endpoint descriptor!\n");
        UsbdAddDeviceComplete(DeviceNode, STATUS_UNSUCCESSFUL);
        return;
    }

    const PUSB_ENDPOINT_DESCRIPTOR ReceiveEndpoint = UsbdGetEndpointDescriptor(DeviceNode, 0, 2, 1);
    if (ReceiveEndpoint == NULL) {
        DbgPrint("[usbenet]: Couldn't retrieve USB receive bulk endpoint descriptor!\n");
        UsbdAddDeviceComplete(DeviceNode, STATUS_UNSUCCESSFUL);
        return;
    }

    const PUSB_ENDPOINT_DESCRIPTOR TransmitEndpoint = UsbdGetEndpointDescriptor(DeviceNode, 0, 2, 0);
    if (TransmitEndpoint == NULL) {
        DbgPrint("[usbenet]: Couldn't retrieve USB transmit bulk endpoint descriptor!\n");
        UsbdAddDeviceComplete(DeviceNode, STATUS_UNSUCCESSFUL);
        return;
    }

    CUsbEnet* DeviceExtension = static_cast<CUsbEnet*>(g_UsbEnet.AllocDeviceExtension());

    if (DeviceExtension == NULL) {
        DbgPrint("[usbenet]: Couldn't allocate resources for USB Ethernet device extension!\n");
        UsbdAddDeviceComplete(DeviceNode, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    DeviceNode->DeviceExtension = DeviceExtension;

    UsbdAddDeviceComplete(DeviceNode, STATUS_SUCCESS);
    CUsbEnet::InitDeviceExtension(DeviceExtension, DeviceNode, InterruptEndpoint, ReceiveEndpoint, TransmitEndpoint);
}

VOID __fastcall UsbEnetRemoveDevice(PUSBD_DEVICE_NODE DeviceNode) {
    CUsbEnet* DeviceExtension = static_cast<CUsbEnet*>(DeviceNode->DeviceExtension);
    CUsbEnet::BeginRemoveDeviceExtension(DeviceExtension);
}

static const char* UsbEnetIdentifyAsix1790Revision(USHORT DeviceRevision) {
    switch (DeviceRevision) {
        case 0x0100: return "original AX88179";
        case 0x0200: return "AX88179A/B";
        case 0x0300: return "AX88772D (10/100)";
        case 0x0400: return "AX88279";
        default: return "unknown 0B95:1790 revision";
    }
}

BOOLEAN __fastcall UsbEnetMatchDriver(PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor, PUSB_DEVICE_DESCRIPTOR DeviceDescriptor, PUSBD_DRIVER_OBJECT DriverObject) {
	if (InterfaceDescriptor == NULL || DeviceDescriptor == NULL)
		return FALSE;

	BOOL ClassMatch = InterfaceDescriptor->bInterfaceClass == kVendorSpecificClass && InterfaceDescriptor->bInterfaceSubClass == kVendorSpecificSubClass;
	USHORT UsbRevision = _byteswap_ushort(DeviceDescriptor->bcdUSB);
	USHORT DeviceRevision = _byteswap_ushort(DeviceDescriptor->bcdDevice);
	CUsbEnetChipset* Chipset = UsbEnetFindChipset(DeviceDescriptor->idVendor, DeviceDescriptor->idProduct, DeviceRevision);
	BOOL DeviceMatch = Chipset != NULL && Chipset->IsImplemented();

	DbgPrint("[CUsbEnet/Main] interfaceDesc=%p deviceDesc=%p deviceObj=%p\n", InterfaceDescriptor, DeviceDescriptor, DriverObject);
	DbgPrint("[CUsbEnet/Main] USB interface number=%u alt=%u endpoints=%u class=%02X subclass=%02X protocol=%02X\n", InterfaceDescriptor->bInterfaceNumber, InterfaceDescriptor->bAlternateSetting, InterfaceDescriptor->bNumEndpoints, InterfaceDescriptor->bInterfaceClass, InterfaceDescriptor->bInterfaceSubClass, InterfaceDescriptor->bInterfaceProtocol);
	DbgPrint("[CUsbEnet/Main] USB device class=%02X subclass=%02X protocol=%02X ep0=%u configs=%u bcdUSB(raw)=%04X normalized=%04X (%X.%02X) bcdDevice(raw)=%04X normalized=%04X (%X.%02X)\n", DeviceDescriptor->bDeviceClass, DeviceDescriptor->bDeviceSubClass, DeviceDescriptor->bDeviceProtocol, DeviceDescriptor->bMaxPacketSize0, DeviceDescriptor->bNumConfigurations, DeviceDescriptor->bcdUSB, UsbRevision, (UsbRevision >> 8) & 0xFF, UsbRevision & 0xFF, DeviceDescriptor->bcdDevice, DeviceRevision, (DeviceRevision >> 8) & 0xFF, DeviceRevision & 0xFF);
	DbgPrint("[CUsbEnet/Main] USB interface class=%02X subclass=%02X VID(raw)=%04X PID(raw)=%04X classMatch=%d deviceMatch=%d chipset=%s implemented=%d\n", InterfaceDescriptor->bInterfaceClass, InterfaceDescriptor->bInterfaceSubClass, DeviceDescriptor->idVendor, DeviceDescriptor->idProduct, ClassMatch, DeviceMatch, Chipset != NULL ? Chipset->GetName() : "unknown", Chipset != NULL ? Chipset->IsImplemented() : FALSE);

	if (DeviceDescriptor->idVendor == 0x950B && DeviceDescriptor->idProduct == 0x9017) {
		DbgPrint("[CUsbEnet/Main] ASIX 0B95:1790 descriptor revision identifies %s (normalized bcdDevice=0x%04X).\n", UsbEnetIdentifyAsix1790Revision(DeviceRevision), DeviceRevision);

		if (DeviceRevision >= 0x0200 && DeviceRevision <= 0x0400 && !ClassMatch)
			DbgPrint("[CUsbEnet/Main] AX179A-family device is not exposing the FF/FF/00 vendor interface; USB configuration switching is not implemented.\n");
	}

	if (!ClassMatch || Chipset == NULL)
		return FALSE;

	if (!Chipset->IsImplemented()) {
		DbgPrint("[usbenet]: Recognized %s, but its initialization backend is not implemented yet.\n", Chipset->GetName());
		return FALSE;
	}

	g_UsbEnetChipset = Chipset;
	return TRUE;
}
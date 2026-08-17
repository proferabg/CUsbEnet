#pragma once
#include "stdafx.h"

#define USB_DIR_OUT			0		/* to device */
#define USB_DIR_IN			0x80	/* to host */

/*
 * USB types, the second of three bRequestType fields
 */
#define USB_TYPE_MASK			(0x03 << 5)
#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)


 /*
  * USB recipients, the third of three bRequestType fields
  */
#define USB_RECIP_MASK			0x1f
#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03
  /* From Wireless USB 1.0 */
#define USB_RECIP_PORT			0x04
#define USB_RECIP_RPIPE		    0x05

#define USBD_TITLE_RESET_MAX_DRIVER_OBJECTS 16
#define USBD_TITLE_RESET_CONFIGURATION_BUFFER_SIZE 0xED8

enum USB_TRANSFER_TYPE_RE {
    UsbTransferControl = 0,
    UsbTransferIsochronous = 1,
    UsbTransferBulk = 2,
    UsbTransferInterrupt = 3
};

enum USBD_DRIVER_NOTIFICATION_TYPE {
    UsbdDriverNotificationQuiesce = 0,
    UsbdDriverNotificationRestart = 1,
    UsbdDriverNotificationShutdown = 2
};

#pragma pack(push, 1)

typedef struct _USB_DEVICE_REQUEST {
    BYTE  RequestType; // 0x00
    BYTE  Request;      // 0x01
    WORD  Value;        // 0x02, USB little-endian
    WORD  Index;        // 0x04, USB little-endian
    WORD  Length;       // 0x06, USB little-endian
} USB_DEVICE_REQUEST, *PUSB_DEVICE_REQUEST; 
C_ASSERT(sizeof(USB_DEVICE_REQUEST) == 0x08);

typedef struct _USB_DEVICE_DESCRIPTOR {
    BYTE  bLength;              // 0x00
    BYTE  bDescriptorType;      // 0x01
    WORD  bcdUSB;               // 0x02, USB little-endian
    BYTE  bDeviceClass;         // 0x04
    BYTE  bDeviceSubClass;      // 0x05
    BYTE  bDeviceProtocol;      // 0x06
    BYTE  bMaxPacketSize0;      // 0x07
    WORD  idVendor;             // 0x08, USB little-endian
    WORD  idProduct;            // 0x0A, USB little-endian
    WORD  bcdDevice;            // 0x0C, USB little-endian
    BYTE  iManufacturer;        // 0x0E
    BYTE  iProduct;             // 0x0f
    BYTE  iSerialNumber;        // 0x10
    BYTE  bNumConfigurations;   // 0x11
} USB_DEVICE_DESCRIPTOR, *PUSB_DEVICE_DESCRIPTOR;
C_ASSERT(sizeof(USB_DEVICE_DESCRIPTOR) == 0x12);

typedef struct _USB_CONFIGURATION_DESCRIPTOR {
    BYTE  bLength;              // 0x00
    BYTE  bDescriptorType;      // 0x01
    WORD  wTotalLength;         // 0x02, USB little-endian
    BYTE  bNumInterfaces;       // 0x04
    BYTE  bConfigurationValue;  // 0x05
    BYTE  iConfiguration;       // 0x06
    BYTE  bmAttributes;         // 0x07
    BYTE  MaxPower;             // 0x08
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;
C_ASSERT(sizeof(USB_CONFIGURATION_DESCRIPTOR) == 0x09);

typedef struct _USB_INTERFACE_DESCRIPTOR {
    BYTE bLength;               // 0x00
    BYTE bDescriptorType;       // 0x01
    BYTE bInterfaceNumber;      // 0x02
    BYTE bAlternateSetting;     // 0x03
    BYTE bNumEndpoints;         // 0x04
    BYTE bInterfaceClass;       // 0x05
    BYTE bInterfaceSubClass;    // 0x06
    BYTE bInterfaceProtocol;    // 0x07
    BYTE iInterface;            // 0x08
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;
C_ASSERT(sizeof(USB_INTERFACE_DESCRIPTOR) == 0x09);

typedef struct _USB_ENDPOINT_DESCRIPTOR {
    BYTE bLength;           // 0x00
    BYTE bDescriptorType;   // 0x01
    BYTE bEndpointAddress;  // 0x02
    BYTE bmAttributes;      // 0x03
    WORD wMaxPacketSize;    // 0x04, USB little-endian
    BYTE bInterval;         // 0x06
} USB_ENDPOINT_DESCRIPTOR, *PUSB_ENDPOINT_DESCRIPTOR;
C_ASSERT(sizeof(USB_ENDPOINT_DESCRIPTOR) == 0x07);

typedef struct _USBD_DEVICE_NODE {
    PVOID DeviceExtension;              /* 0x00 */
    PVOID DriverObject;                 /* 0x04 */
    BYTE LocationFlags;                 /* 0x08 */
    BYTE EndpointZeroMaxPacketSize;     /* 0x09 */
    BYTE DeviceAddressEndpointIndex;    /* 0x0A */
    BYTE TypeEnumerationFlags;          /* 0x0B */
    BYTE SecurityNodeIndex;             /* 0x0C, 0xFF means none */
    BYTE ParentNodeIndex;               /* 0x0D, 0xFF means none/root */
    BYTE FirstChildNodeIndex;           /* 0x0E, 0xFF means none */
    BYTE NextSiblingNodeIndex;          /* 0x0F, 0xFF means none */
} USBD_DEVICE_NODE, * PUSBD_DEVICE_NODE;
C_ASSERT(sizeof(USBD_DEVICE_NODE) == 0x10);


typedef struct _USBD_ASYNC_REQUEST USBD_ASYNC_REQUEST;
typedef USBD_ASYNC_REQUEST* PUSBD_ASYNC_REQUEST;
typedef VOID(__fastcall* PUSBD_ASYNC_COMPLETION_ROUTINE)(PVOID Request, NTSTATUS Status);

typedef struct _USBD_ASYNC_REQUEST {
    PVOID Context;                                      /* 0x00, caller-owned completion context */
    PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine;   /* 0x04 */
    PVOID EndpointHandle;                               /* 0x08 */
    PUSBD_ASYNC_REQUEST InternalNext;                   /* 0x0C, cancel/internal queue link */
    BYTE TransferFlags;                                 /* 0x10 */
    BYTE CancelPending;                                 /* 0x11 */
    BYTE HostControllerIndex;                           /* 0x12 */
    BYTE DeviceNodeIndex;                               /* 0x13 */
} USBD_ASYNC_REQUEST;
C_ASSERT(sizeof(USBD_ASYNC_REQUEST) == 0x14);

typedef struct _USBD_TRANSFER_REQUEST {
    USBD_ASYNC_REQUEST Request;                         /* +0x00 */
    PVOID Buffer;                                       /* +0x14 */
    DWORD BufferLength;                                 /* +0x18 */
    DWORD BytesTransferred;                             /* +0x1C */
} USBD_TRANSFER_REQUEST, * PUSBD_TRANSFER_REQUEST;
C_ASSERT(sizeof(USBD_TRANSFER_REQUEST) == 0x20);


typedef struct _USBD_CONTROL_REQUEST {
    USBD_TRANSFER_REQUEST Transfer;  // +0x00
    USB_DEVICE_REQUEST Setup;       // +0x20
} USBD_CONTROL_REQUEST, *PUSBD_CONTROL_REQUEST;
C_ASSERT(sizeof(USBD_CONTROL_REQUEST) == 0x28);

typedef struct _USBD_TIMER {
    PVOID Context; // +0x00
    PVOID Routine; // +0x04
    LIST_ENTRY ListEntry; // +0x08
    QWORD DueTime; // +0x10
} USBD_TIMER, * PUSBD_TIMER;

typedef struct _USBD_DRIVER_OBJECT USBD_DRIVER_OBJECT;
typedef USBD_DRIVER_OBJECT* PUSBD_DRIVER_OBJECT;

// Static interface-driver object returned by UsbdGetInterfaceLevelDriverObject.
// Offsets +0x0C and +0x10 are confirmed AddDevice/RemoveDevice callbacks.
typedef VOID (__fastcall *USBD_ADD_DEVICE_ROUTINE)(PUSBD_DEVICE_NODE Device);
typedef VOID (__fastcall *USBD_REMOVE_DEVICE_ROUTINE)(PUSBD_DEVICE_NODE Device);
typedef BOOLEAN(__fastcall* USBD_MATCH_DRIVER_ROUTINE)(PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor, PUSB_DEVICE_DESCRIPTOR DeviceDescriptor, PUSBD_DRIVER_OBJECT DriverObject);
typedef VOID(__fastcall* USBD_DEVICE_NOTIFICATION_ROUTINE)(PUSBD_DEVICE_NODE DeviceNode, BOOLEAN Added, PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor);

typedef struct _USBD_DRIVER_OBJECT {
    LIST_ENTRY RegisteredDriverListEntry;                // +0x00
    BYTE ObjectType;                                     // +0x08
    BYTE DriverLevel;                                    // +0x09
    BYTE MatchLevel;                                     // +0x0A
    BYTE Flags;                                          // +0x0B
    USBD_ADD_DEVICE_ROUTINE AddDevice;                   // +0x0C
    USBD_REMOVE_DEVICE_ROUTINE RemoveDevice;             // +0x10
    USBD_MATCH_DRIVER_ROUTINE MatchDriver;               // +0x14
    USBD_DEVICE_NOTIFICATION_ROUTINE DeviceNotification; // +0x18
} USBD_DRIVER_OBJECT;

static_assert(offsetof(USBD_DRIVER_OBJECT, AddDevice) == 0x0C, "Bad AddDevice offset");
static_assert(offsetof(USBD_DRIVER_OBJECT, RemoveDevice) == 0x10, "Bad RemoveDevice offset");
static_assert(offsetof(USBD_DRIVER_OBJECT, MatchDriver) == 0x14, "Bad MatchDriver offset");
static_assert(offsetof(USBD_DRIVER_OBJECT, DeviceNotification) == 0x18, "Bad notification offset");
static_assert(sizeof(USBD_DRIVER_OBJECT) == 0x1C, "Bad driver object size");

#define USBD_UNRECOGNIZED_PORT_ENTRY_COUNT 20

typedef struct _USBD_UNRECOGNIZED_PORT_ENTRY {
    PUSBD_DEVICE_NODE HubDeviceNode;
    BYTE PortMask;
    BYTE Reserved[3];
} USBD_UNRECOGNIZED_PORT_ENTRY, * PUSBD_UNRECOGNIZED_PORT_ENTRY;

C_ASSERT(sizeof(USBD_UNRECOGNIZED_PORT_ENTRY) == 8);

extern "C" {
    VOID UsbdAddDeviceComplete(PUSBD_DEVICE_NODE Device, NTSTATUS Status); //740
    VOID UsbdCancelAsyncTransfer(PUSBD_TRANSFER_REQUEST Request); //741
    PUSB_ENDPOINT_DESCRIPTOR UsbdGetEndpointDescriptor(PUSBD_DEVICE_NODE Device, DWORD MatchingOrdinal, DWORD TransferType, BOOL DirectionIn); //744
    NTSTATUS UsbdOpenDefaultEndpoint(PUSBD_DEVICE_NODE device, PVOID* endpointHandle); //746
    NTSTATUS UsbdOpenEndpoint(PUSBD_DEVICE_NODE Device, DWORD TransferType, BYTE EndpointAddress, USHORT MaximumPacketSize, DWORD Interval, PVOID* EndpointHandle); //747
    VOID UsbdQueueAsyncTransfer(PVOID endpointHandle, PUSBD_TRANSFER_REQUEST request); //748
    NTSTATUS UsbdQueueCloseDefaultEndpoint(PUSBD_DEVICE_NODE DeviceNode, PUSBD_ASYNC_REQUEST request); //749
    NTSTATUS UsbdQueueCloseEndpoint(PUSBD_DEVICE_NODE DeviceNode, PUSBD_ASYNC_REQUEST Request); //750
    VOID UsbdRemoveDeviceComplete(PUSBD_DEVICE_NODE Device); //751
    NTSTATUS UsbdRegisterDriverObject(PUSBD_DRIVER_OBJECT driverObject); //755
    VOID UsbdUnregisterDriverObject(PUSBD_DRIVER_OBJECT driverObject); //756
}

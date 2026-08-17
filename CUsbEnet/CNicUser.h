#pragma once
#include "stdafx.h"

#define XNET_STANDARD_RECEIVE_FILTER 0x1Au
#define XNET_XBDM_RECEIVE_FILTER     0x14u


enum NIC_RECV_DEST {
    NIC_RECV_DEST_PROMISCUOUS = 0,
    NIC_RECV_DEST_UNICAST = 1,
    NIC_RECV_DEST_ALTERNATE_UNICAST = 2,
    NIC_RECV_DEST_MULTICAST = 3,
    NIC_RECV_DEST_BROADCAST = 4
};

typedef enum _NIC_INTERFACE_ID {
    NicInterfaceEmac = 0x01,
    NicInterfaceWireless = 0x02,
    NicInterfaceUsbEnet = 0x08,
    NicInterfaceKd = 0x10
} NIC_INTERFACE_ID;

enum NIC_RECV_DEST_FLAGS {
    NIC_RECV_DEST_FLAG_PROMISCUOUS = 1 << NIC_RECV_DEST_PROMISCUOUS,
    NIC_RECV_DEST_FLAG_UNICAST = 1 << NIC_RECV_DEST_UNICAST,
    NIC_RECV_DEST_FLAG_ALTERNATE_UNICAST = 1 << NIC_RECV_DEST_ALTERNATE_UNICAST,
    NIC_RECV_DEST_FLAG_MULTICAST = 1 << NIC_RECV_DEST_MULTICAST,
    NIC_RECV_DEST_FLAG_BROADCAST = 1 << NIC_RECV_DEST_BROADCAST
};

typedef VOID(__cdecl* PNIC_RECEIVE_CALLBACK)(PVOID Context, PVOID Frame, DWORD FrameLength, DWORD InterfaceId, DWORD DestinationClass);
typedef VOID(__cdecl* PNIC_XMIT_COMPLETE_CALLBACK)(PVOID Context, PVOID CompletionCookie);
typedef VOID(__cdecl* PNIC_LINK_STATE_CALLBACK)(PVOID Context);

#pragma pack(push, 4)
typedef struct _NIC_ATTACH_INFO {
    DWORD DeviceFlags;                                          /* +0x00 */
    DWORD ReceiveFilterMask;                                    /* +0x04 */
    PVOID CallbackContext;                                      /* +0x08 */
    PNIC_RECEIVE_CALLBACK ReceiveCallback;                      /* +0x0C */
    PNIC_XMIT_COMPLETE_CALLBACK XmitCompleteCallback;           /* +0x10 */
    PNIC_LINK_STATE_CALLBACK LinkStateCallback;                 /* +0x14 */
    DWORD Unknown18;                                            /* +0x18 */
} NIC_ATTACH_INFO, * PNIC_ATTACH_INFO; /* size 0x1C */

typedef struct _OMNI_NIC_USER {
    LIST_ENTRY ProviderLink;            /* +0x00 */
    PVOID AttachInfo;                   /* +0x08 */
    DWORD ProviderState;                /* +0x0C */
} OMNI_NIC_USER, * POMNI_NIC_USER;      /* size 0x10 */

class CNicUser {
public:
    LIST_ENTRY NicBaseListEntry;            // 0x00
    LIST_ENTRY WirelessListEntry;           // 0x08
    OMNI_NIC_USER WirelessProviderContext;  // 0x10
    NIC_ATTACH_INFO AttachInfo;             // 0x20
    NIC_ATTACH_INFO OriginalAttachInfo;     // 0x3C

    __forceinline VOID NotifyReceive(PVOID frame, DWORD frameLength, NIC_RECV_DEST destination) {
        if (!AttachInfo.ReceiveCallback)
            return;

        if (destination > NIC_RECV_DEST_BROADCAST)
            return;

        const DWORD requiredFilter = 1u << destination;

        if ((AttachInfo.ReceiveFilterMask & requiredFilter) == 0)
            return;

        AttachInfo.ReceiveCallback(AttachInfo.CallbackContext, frame, frameLength, NicInterfaceUsbEnet, destination);
    }

    __forceinline VOID NotifyXmitComplete(PVOID completionCookie) {
        if (AttachInfo.XmitCompleteCallback) {
            AttachInfo.XmitCompleteCallback(AttachInfo.CallbackContext, completionCookie);
        }
    }

    __forceinline VOID NotifyLinkStateChanged()  {
        if (AttachInfo.LinkStateCallback) {
            AttachInfo.LinkStateCallback(AttachInfo.CallbackContext);
        }
    }


    __forceinline BOOL IsStandardXNetUser() {
        return OriginalAttachInfo.ReceiveFilterMask == XNET_STANDARD_RECEIVE_FILTER;
    }

    __forceinline BOOL IsXBDMXNetUser() {
        return OriginalAttachInfo.ReceiveFilterMask == XNET_XBDM_RECEIVE_FILTER;
    }

};

#pragma pack(pop)

C_ASSERT(sizeof(NIC_ATTACH_INFO) == 0x1C);
C_ASSERT(sizeof(OMNI_NIC_USER) == 0x10);
C_ASSERT(sizeof(CNicUser) == 0x58);

C_ASSERT(FIELD_OFFSET(CNicUser, NicBaseListEntry) == 0x00);
C_ASSERT(FIELD_OFFSET(CNicUser, WirelessListEntry) == 0x08);
C_ASSERT(FIELD_OFFSET(CNicUser, WirelessProviderContext) == 0x10);
C_ASSERT(FIELD_OFFSET(CNicUser, AttachInfo) == 0x20);
C_ASSERT(FIELD_OFFSET(CNicUser, OriginalAttachInfo) == 0x3C);

C_ASSERT(FIELD_OFFSET(CNicUser, AttachInfo.CallbackContext) == 0x28);
C_ASSERT(FIELD_OFFSET(CNicUser, AttachInfo.ReceiveCallback) == 0x2C);
C_ASSERT(FIELD_OFFSET(CNicUser, AttachInfo.XmitCompleteCallback) == 0x30);
C_ASSERT(FIELD_OFFSET(CNicUser, AttachInfo.LinkStateCallback) == 0x34);
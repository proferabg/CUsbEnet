#pragma once
#include "stdafx.h"
#include "UsbEnetTransport.h"

enum {
    RECV_PACKET_COUNT = 48,
    XMIT_TRACKER_COUNT = 64,
    XMIT_BUFFER_COUNT = XMIT_TRACKER_COUNT,
    XMIT_PACKET_COUNT = 80,
    ReceiveBufferSize = 0x4000,
    XmitBufferSize = 0x3000,
    XmitBufferOffset = RECV_PACKET_COUNT * ReceiveBufferSize,
    DmaBufferSize = XmitBufferOffset + XMIT_BUFFER_COUNT * XmitBufferSize,
    ControlScratchBufferSize = 8,
    InterruptStatusBufferSize = 8,
    InterruptStatusBufferOffset = DmaBufferSize + ControlScratchBufferSize,
    PhysicalBufferSize = InterruptStatusBufferOffset + InterruptStatusBufferSize,
    MaximumAggregateSize = 0x2FFC,
    XmitSubmitThreshold = 0x2C00,
    MaximumFramesPerXmit = 8,
    MaximumDebugFramesPerXmit = 8,
    DebugImmediateFrameSize = 256,
    MaximumPendingXmitCount = 24,
    MaximumTitlePendingXmitCount = MaximumPendingXmitCount - 1,
    EnableDispatchLevelDirectXmit = 1, // Submit directly only when already on USB processor 2 at DISPATCH_LEVEL.
    TitleReceiveFilterMask = 0x1A,
    DebugReceiveFilterMask = 0x14,
    ThroughputStatsPeriod = 5000,
    MinimumReceiveFrameSize = 42
};

enum NIC_LINK_STATE_FLAGS {
    NIC_LINK_STATE_ACTIVE = 0x00000001,
    NIC_LINK_STATE_100_MBPS = 0x00000002,
    NIC_LINK_STATE_10_MBPS = 0x00000004,
    NIC_LINK_STATE_FULL_DUPLEX = 0x00000008,
    NIC_LINK_STATE_HALF_DUPLEX = 0x00000010,
    NIC_LINK_STATE_WIRELESS = 0x00000020,
    NIC_LINK_STATE_1000_MBPS = 0x00000040,
    NIC_LINK_STATE_NEGOTIATION_COMPLETE = 0x00010000,
    NIC_LINK_STATE_TX_FLOW_CONTROL = 0x00020000
};

enum MII_REGISTER {
    MII_BMCR = 0,
    MII_BMSR = 1,
    MII_ADVERTISE = 4,
    MII_LPA = 5,
    MII_CTRL1000 = 9,
    MII_STAT1000 = 10
};

enum MII_CONTROL_FLAGS {
    MII_BMCR_SPEED1000 = 0x0040,
    MII_BMCR_FULL_DUPLEX = 0x0100,
    MII_BMCR_RESTART_AUTONEG = 0x0200,
    MII_BMCR_AUTONEG_ENABLE = 0x1000,
    MII_BMCR_SPEED100 = 0x2000,
    MII_BMCR_RESET = 0x8000
};

enum MII_STATUS_FLAGS {
    MII_BMSR_LINK_STATUS = 0x0004,
    MII_BMSR_AUTONEG_COMPLETE = 0x0020
};

enum MII_ADVERTISEMENT_FLAGS {
    MII_ADVERTISE_10_HALF = 0x0020,
    MII_ADVERTISE_10_FULL = 0x0040,
    MII_ADVERTISE_100_HALF = 0x0080,
    MII_ADVERTISE_100_FULL = 0x0100
};

enum MII_GIGABIT_CONTROL_FLAGS {
    MII_CTRL1000_HALF = 0x0100,
    MII_CTRL1000_FULL = 0x0200
};

enum MII_GIGABIT_STATUS_FLAGS {
    MII_STAT1000_LP_HALF = 0x0400,
    MII_STAT1000_LP_FULL = 0x0800
};

enum USBENET_FLAGS {
    ReceiveRunning                          = 0x00000001,
    ControlOutstanding                      = 0x00000002,
    USBENET_STATE_REFRESH_PHY_REGISTERS      = 0x00000004,
    USBENET_STATE_NOTIFY_LINK_STATE          = 0x00000008,
    USBENET_STATE_00080000                  = 0x00080000,
    USBENET_STATE_00100000                  = 0x00100000,
    USBENET_STATE_CALLBACK_IN_PROGRESS      = 0x00200000,
    USBENET_STATE_READ_ALL_PHY_REGISTERS    = 0x00400000,
    USBENET_STATE_LINK_STATE_UPDATE_PENDING = 0x00800000,
    USBENET_STATE_PHY_WRITE_IN_PROGRESS     = 0x01000000,
    USBENET_STATE_PHY_READ_IN_PROGRESS      = 0x02000000,
    USBENET_STATE_SERIAL_MGMT_CONTROL       = 0x04000000,
    USBENET_STATE_ACTIVE                    = 0x08000000,
    USBENET_STATE_TRANSFER_IN_PROGRESS      = 0x10000000,
    USBENET_STATE_CAN_USER_TRANSFER         = 0x20000000,
    USBENET_STATE_STOPPING                  = 0x40000000,
    USBENET_STATE_RESETTING                 = 0x80000000
};

enum XMIT_FLAG {
    XMIT_FLAG_WAITING     = 0x10000000,
    XMIT_FLAG_CRC_PADDING = 0x20000000,
    XMIT_FLAG_BUSY        = 0x40000000,
    XMIT_FLAG_QUEUED      = 0x80000000
};

#pragma pack(push, 1)

typedef struct _USB_ENET_USER_ENTRY {
    LIST_ENTRY Link;   
    CNicUser* User;   
} USB_ENET_USER_ENTRY, * PUSB_ENET_USER_ENTRY;

typedef struct _XMIT_PACKET XMIT_PACKET;
typedef XMIT_PACKET* PXMIT_PACKET;

typedef struct _XMIT_TRACKER {
    USBD_TRANSFER_REQUEST Transfer; // +0x00
    KDPC CompletionDpc;             // +0x20
    PXMIT_PACKET FirstPacket;       // +0x3C
    DWORD Flags;                    // +0x40
} XMIT_TRACKER, * PXMIT_TRACKER;
C_ASSERT(sizeof(XMIT_TRACKER) == 0x44);

typedef struct _XMIT_PACKET {
    CNicUser* User;
    PVOID CompletionContext;
    PXMIT_TRACKER Tracker;
    DWORD Length;
} XMIT_PACKET;
C_ASSERT(sizeof(XMIT_PACKET) == 0x10);

typedef struct _RECV_TRANSFER {
    USBD_TRANSFER_REQUEST Transfer;	// 0x00
    ULONG InFlight;		            // 0x20
} RECV_TRANSFER, *PRECV_TRANSFER;
C_ASSERT(sizeof(RECV_TRANSFER) == 0x24);
#pragma pack(pop)

class CUsbEnet : public CNicBase
{
public:
    // Stock CUsbEnet device extension. The field names expose the portions that
    // are used by the reconstructed driver while preserving the recovered ABI.
    volatile LONG DeviceAttached;                          // +0x160
    DWORD InitStage;                                       // +0x164
    PVOID PhysicalMemory;                                  // +0x168
    PUSBD_DEVICE_NODE DeviceNode;                          // +0x16C
    PVOID DefaultEndpoint;                                 // +0x170
    PVOID ReceiveEndpoint;                                 // +0x174
    PVOID TransmitEndpoint;                                // +0x178
    USHORT TransmitMaxPacketSize;                          // +0x17C
    BYTE ReceiveEndpointAddress;                           // +0x17E
    BYTE TransmitEndpointAddress;                          // +0x17F
    USBD_ASYNC_REQUEST CloseRequest;                       // +0x180
    KDPC ControlDpc;                                       // +0x194
    USBD_CONTROL_REQUEST ControlRequest;                   // +0x1B0
    KEVENT ControlEvent;                                   // +0x1D8
    USHORT CurrentPhyRegister;                             // +0x1E8
    USHORT CurrentPhyValue;                                // +0x1EA
    USHORT PhyAddress;                                     // +0x1EC
    USHORT PhyRegisters[12];                               // +0x1EE
    BYTE Reserved206[2];                                   // +0x206
    DWORD LinkPollTick;                                    // +0x208
    DWORD LinkState;                                       // +0x20C
    XMIT_PACKET XmitPackets[XMIT_PACKET_COUNT];            // +0x210
    XMIT_TRACKER XmitTrackers[XMIT_TRACKER_COUNT];         // +0x710
    PXMIT_PACKET NextXmitPacket;                           // +0x1810
    PXMIT_TRACKER NextXmitTracker;                         // +0x1814
    DWORD ActiveXmitCount;                                 // +0x1818
    DWORD PendingXmitCount;                                // +0x181C
    RECV_TRANSFER RecvPackets[RECV_PACKET_COUNT];          // +0x1820
    DWORD TimerTick;                                       // +0x1EE0
    CEnetAddr NodeId;                                      // +0x1EE8
    BYTE Reserved1EEA[2];                                  // +0x1EEA
    DWORD Flags;                                           // +0x1EEC

    // needed to track CNicUser because it does not contain a unique list for UsbEnet anymore
    LIST_ENTRY m_UserList;
    KSPIN_LOCK m_UserListLock;

    VOID        __fastcall AbortUserControlTransfer();
    NTSTATUS    __fastcall NicUpdateMcastMembership(CEnetAddr* Address, DWORD Add);
    DWORD       __fastcall NicGetLinkState();
    VOID        __fastcall NicDoTimerWaitForDeviceAdd();
    VOID        __fastcall NicDoTimerAdvanceInitStage();
    NTSTATUS    __fastcall PrepareForUserControlTransfer();
    VOID        __fastcall WaitForUserControlTransferResult();
    NTSTATUS    __fastcall QueueControlTransfer(PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE RequestType, BYTE Request, WORD Value, WORD Index, WORD Length, const PVOID Buffer);
    VOID        __fastcall CompleteControlTransfer();
    VOID        __fastcall ControlSwitchProcs();
    VOID        __fastcall CompleteReceive(PRECV_TRANSFER Packet, NTSTATUS Status);
    VOID        __fastcall CompleteStartReceiving(NTSTATUS Status);
    VOID        __fastcall CompleteTransmit(PXMIT_TRACKER Tracker, NTSTATUS Status);
    VOID        __fastcall BulkXmitSwitchProcs(PXMIT_TRACKER Tracker);
    CUsbEnet*   __fastcall AllocDeviceExtension();
    VOID        __fastcall FreeDeviceExtension(PVOID DeviceExtension);
    VOID        __fastcall NicFlushXmitQueue(CNicUser* User);
    VOID        __fastcall NicXmit(CNicUser* User, DWORD Unknown, PVOID Buffer, DWORD Length, PVOID CompletionContext);
    static VOID __fastcall NicTimerWaitForDeviceAddDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    static VOID __fastcall NicTimerAdvanceInitStageDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    VOID        __fastcall ContinueAsyncCloseAndDropLock();
    static VOID __fastcall DpcControlSwitchProcsRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    static VOID __fastcall AsyncCompletionRoutineBulkRecv(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineInterruptStatus(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    VOID        __fastcall StartInterruptLinkStatus();
    VOID        __fastcall PauseInterruptLinkStatus();
    BOOL        __fastcall IsInterruptLinkStatusInFlight();
    VOID        __fastcall ResetTransportEndpoint(USBENET_TRANSPORT_PIPE Pipe);
    VOID        __fastcall ResetUsbDevice();
    VOID        __fastcall CompleteInterruptLinkStatus(NTSTATUS Status);
    VOID        __fastcall SubmitReceivePacket(PRECV_TRANSFER Packet);
    VOID        __fastcall SubmitReceive();
    static VOID __fastcall AsyncCompletionRoutineStartReceiving(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineBulkXmit(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall DpcBulkXmitSwitchProcsRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    VOID        __fastcall BeginPowerDownAndClose();
    BOOL        __fastcall IsPowerDownComplete();
    DWORD       __fastcall GetReceiveInFlightCount();
    VOID        __fastcall BeginRemove();
    VOID        __fastcall DriverEntry();
    static VOID __fastcall AsyncCompletionRoutineClose(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    VOID        __fastcall StartReceiving();
    VOID        __fastcall CompleteStopReceiving(NTSTATUS Status);
    VOID        __fastcall WriteNodeId();
    static VOID __fastcall BeginRemoveDeviceExtension(PVOID DeviceExtension);
    VOID        __fastcall UpdateRecvFilter();
    static VOID __fastcall AsyncCompletionRoutineStopReceiving(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    VOID        __fastcall NicAttachUser(CNicUser* User);
    BOOL        __fastcall NicDetachUser(CNicUser* User);
    VOID        __fastcall NotifyLinkStateChangedToUsers();
    VOID        __fastcall StopAndRestartReceiving();
    NTSTATUS    __fastcall NicDoSetOpt(CNicUser* User, DWORD Option, const PBYTE OptionValue, DWORD OptionLength, PDWORD Result);
    NTSTATUS    __fastcall NicDoGetOpt(CNicUser* User, DWORD Option, PBYTE OptionValue, DWORD* OptionLength, PDWORD Result);
    VOID        __fastcall PrintThroughputStats(DWORD CurrentTick);
    VOID        __fastcall NoteReceiveRestart();
    VOID        __fastcall NicDoTimerRunning();
    static VOID __fastcall NicTimerRunningDpc(PKDPC Dpc, PVOID Context, PVOID SystemArgument1, PVOID SystemArgument2);
    VOID        __fastcall AdvanceInitStage();
    VOID        __fastcall Init(PUSBD_DEVICE_NODE DeviceNode, const PUSB_ENDPOINT_DESCRIPTOR InterruptEndpointDescriptor, const PUSB_ENDPOINT_DESCRIPTOR ReceiveEndpointDescriptor, const PUSB_ENDPOINT_DESCRIPTOR TransmitEndpointDescriptor);
    VOID        __fastcall NicSetUnicastAddress(CEnetAddr* Address, DWORD AddressSlot);
    static VOID __fastcall InitDeviceExtension(CUsbEnet* DeviceExtension, PUSBD_DEVICE_NODE DeviceNode, const PUSB_ENDPOINT_DESCRIPTOR DefaultEndpoint, const PUSB_ENDPOINT_DESCRIPTOR ReceiveEndpoint, const PUSB_ENDPOINT_DESCRIPTOR TransmitEndpoint);
    BOOL        __fastcall IsUserAttached(CNicUser* User);

    static __forceinline CNicUser* GetUsbEnetUserFromEntry(PLIST_ENTRY Entry) {
        PUSB_ENET_USER_ENTRY TrackedUser = CONTAINING_RECORD(Entry, USB_ENET_USER_ENTRY, Link);
        return TrackedUser->User;
    }

private:

};
#if DEVKIT_ONLY
C_ASSERT(sizeof(CUsbEnet) == 0x1EFC);
#else
C_ASSERT(sizeof(CUsbEnet) == 0x1EC4);
#endif

extern CUsbEnet g_UsbEnet;

#pragma once
#include "UsbEnetTransport.h"

class CUsbEnet;

enum USBENET_RX_PARSE_RESULT {
    UsbEnetRxParseComplete = 0,
    UsbEnetRxParseFrame,
    UsbEnetRxParseSkip,
    UsbEnetRxParseError
};

typedef struct _USBENET_RX_PARSE_CONTEXT {
    PBYTE Buffer;
    DWORD Length;
    DWORD DataOffset;
    DWORD MetadataOffset;
    DWORD MetadataIndex;
    DWORD MetadataCount;
} USBENET_RX_PARSE_CONTEXT, *PUSBENET_RX_PARSE_CONTEXT;

typedef struct _USBENET_RX_FRAME {
    PBYTE Data;
    DWORD Length;
    DWORD Flags;
} USBENET_RX_FRAME, *PUSBENET_RX_FRAME;

class CUsbEnetChipset {
public:
    virtual const char* GetName() const = 0;
    virtual USHORT GetVendorIdRaw() const = 0;
    virtual USHORT GetProductIdRaw() const = 0;
    virtual BOOL IsImplemented() const = 0;
    virtual BOOL SupportsTransmitAggregation() const = 0;
    virtual DWORD GetMaximumFrameSize() const = 0;
    virtual DWORD GetMaximumAggregateTransferSize() const = 0;
    virtual DWORD GetTransmitHeaderSize() const = 0;
    virtual DWORD GetTransmitTerminatorSize() const = 0;

    virtual BOOL IsReady(CUsbEnet* Device) = 0;
    virtual BOOL IsNodeIdAvailable(CUsbEnet* Device) = 0;
    virtual VOID OnUnicastAddressChanged(CUsbEnet* Device) = 0;
    virtual VOID ResetState(CUsbEnet* Device) = 0;
    virtual VOID AdvanceInitStage(CUsbEnet* Device) = 0;
    virtual VOID StartReceiving(CUsbEnet* Device) = 0;
    virtual VOID WriteNodeId(CUsbEnet* Device) = 0;
    virtual VOID UpdateReceiveFilter(CUsbEnet* Device) = 0;
    virtual VOID RestartReceiving(CUsbEnet* Device) = 0;
    virtual NTSTATUS RequestFullReinitialize(CUsbEnet* Device, DWORD Reason);
    virtual VOID HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe);
    virtual VOID HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status);
    virtual BOOL UsesInterruptLinkStatus() const;
    virtual BOOL ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length);
    virtual VOID RunTimer(CUsbEnet* Device) = 0;
    virtual NTSTATUS BeginReadPhy(CUsbEnet* Device, USHORT Register) = 0;
    virtual NTSTATUS BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value) = 0;
    virtual BOOL InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context) = 0;
    virtual USBENET_RX_PARSE_RESULT GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame) = 0;
    virtual BOOL AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator) = 0;
};

extern CUsbEnetChipset* g_UsbEnetChipset;

CUsbEnetChipset* UsbEnetFindChipset(USHORT VendorIdRaw, USHORT ProductIdRaw, USHORT DeviceRevision);

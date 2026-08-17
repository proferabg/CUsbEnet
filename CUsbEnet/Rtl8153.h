// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "UsbEnetChipset.h"

enum RTL8153_REGISTER_SPACE {
    Rtl8153RegisterUsb = 0x0000,
    Rtl8153RegisterPla = 0x0100
};

enum RTL8153_INIT_STAGE {
    Rtl8153InitBeginning = 0,
    Rtl8153InitReadVersion,
    Rtl8153InitReadBootControl,
    Rtl8153InitReadHardwareMacLow,
    Rtl8153InitReadHardwareMacHigh,
    Rtl8153InitReadMiscGate,
    Rtl8153InitWriteMiscGate,
    Rtl8153InitReadReceiveControl,
    Rtl8153InitWriteReceiveControlStopped,
    Rtl8153InitResetNic,
    Rtl8153InitPollNicReset,
    Rtl8153InitReadBmuReset,
    Rtl8153InitWriteBmuResetClear,
    Rtl8153InitWriteBmuResetSet,
    Rtl8153InitReadOobControl,
    Rtl8153InitWriteOobControl,
    Rtl8153InitReadSffStatus,
    Rtl8153InitWriteSffStatus,
    Rtl8153InitPollLinkListReady,
    Rtl8153InitReadSffReinit,
    Rtl8153InitWriteSffReinit,
    Rtl8153InitPollLinkListReinit,
    Rtl8153InitReadCpcr,
    Rtl8153InitWriteCpcr,
    Rtl8153InitWriteOcpBase,
    Rtl8153InitReadBmcr,
    Rtl8153InitWriteBmcr,
    Rtl8153InitWriteRms,
    Rtl8153InitWriteMtps,
    Rtl8153InitReadTcrAutoFifo,
    Rtl8153InitWriteTcrAutoFifo,
    Rtl8153InitResetNicSecond,
    Rtl8153InitPollNicResetSecond,
    Rtl8153InitWriteRxFifo0,
    Rtl8153InitWriteRxFifo1,
    Rtl8153InitWriteRxFifo2,
    Rtl8153InitWriteTxFifo,
    Rtl8153InitWriteRxBufferThreshold,
    Rtl8153InitReadMacPowerControl3,
    Rtl8153InitWriteMacPowerControl3,
    Rtl8153InitWriteRxEarlyTimeout,
    Rtl8153InitWriteRxExtraAggregateTimer,
    Rtl8153InitWriteRxEarlySize,
    Rtl8153InitReadUsbControl,
    Rtl8153InitWriteUsbControl,
    Rtl8153InitWaitingForEthernetAddress,
    Rtl8153InitWriteMacConfig,
    Rtl8153InitWriteMacLow,
    Rtl8153InitWriteMacHigh,
    Rtl8153InitWriteMacNormal,
    Rtl8153InitWriteMulticastLow,
    Rtl8153InitWriteMulticastHigh,
    Rtl8153InitWriteReceiveControl,
    Rtl8153InitReadCommand,
    Rtl8153InitWriteCommand,
    Rtl8153InitWriteRxAggregationChange,
    Rtl8153InitReadMiscUngate,
    Rtl8153InitWriteMiscUngate,
    Rtl8153InitStartReceiving,
    Rtl8153InitReadLinkStatus,
    Rtl8153InitReady,
    Rtl8153InitComplete,
    Rtl8153InitFailed
};

enum RTL8153_RUNTIME_OPERATION {
    Rtl8153RuntimeNone = 0,
    Rtl8153RuntimeLinkPoll,
    Rtl8153RuntimeFilterWrite,
    Rtl8153RuntimeMacConfig,
    Rtl8153RuntimeMacLow,
    Rtl8153RuntimeMacHigh,
    Rtl8153RuntimeMacNormal,
    Rtl8153RuntimePhyRead,
    Rtl8153RuntimePhyWrite
};

#pragma pack(push, 1)
typedef struct _RTL8153_RX_DESCRIPTOR {
    DWORD Options1;
    DWORD Options2;
    DWORD Options3;
    DWORD Options4;
    DWORD Options5;
    DWORD Options6;
} RTL8153_RX_DESCRIPTOR, *PRTL8153_RX_DESCRIPTOR;

typedef struct _RTL8153_TX_DESCRIPTOR {
    DWORD Options1;
    DWORD Options2;
} RTL8153_TX_DESCRIPTOR, *PRTL8153_TX_DESCRIPTOR;
#pragma pack(pop)

C_ASSERT(sizeof(RTL8153_RX_DESCRIPTOR) == 0x18);
C_ASSERT(sizeof(RTL8153_TX_DESCRIPTOR) == 0x08);

class CRtl8153 : public CUsbEnetChipset {
public:
    virtual const char* GetName() const;
    virtual USHORT GetVendorIdRaw() const;
    virtual USHORT GetProductIdRaw() const;
    virtual BOOL IsImplemented() const;
    virtual BOOL SupportsTransmitAggregation() const;
    virtual DWORD GetMaximumFrameSize() const;
    virtual DWORD GetMaximumAggregateTransferSize() const;
    virtual DWORD GetTransmitHeaderSize() const;
    virtual DWORD GetTransmitTerminatorSize() const;

    virtual BOOL IsReady(CUsbEnet* Device);
    virtual BOOL IsNodeIdAvailable(CUsbEnet* Device);
    virtual VOID OnUnicastAddressChanged(CUsbEnet* Device);
    virtual VOID ResetState(CUsbEnet* Device);
    virtual VOID AdvanceInitStage(CUsbEnet* Device);
    virtual VOID StartReceiving(CUsbEnet* Device);
    virtual VOID WriteNodeId(CUsbEnet* Device);
    virtual VOID UpdateReceiveFilter(CUsbEnet* Device);
    virtual VOID RestartReceiving(CUsbEnet* Device);
    virtual NTSTATUS RequestFullReinitialize(CUsbEnet* Device, DWORD Reason);
    virtual BOOL UsesInterruptLinkStatus() const;
    virtual BOOL ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length);
    virtual VOID RunTimer(CUsbEnet* Device);
    virtual NTSTATUS BeginReadPhy(CUsbEnet* Device, USHORT Register);
    virtual NTSTATUS BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value);
    virtual BOOL InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context);
    virtual USBENET_RX_PARSE_RESULT GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame);
    virtual BOOL AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator);

private:
    static const BYTE VendorRequest = 0x05;
    static const USHORT ByteEnableDword = 0x00FF;
    static const USHORT ByteEnableWord = 0x0033;
    static const USHORT ByteEnableByte = 0x0011;

    static const USHORT PlaIdr = 0xC000;
    static const USHORT PlaRcr = 0xC010;
    static const USHORT PlaRms = 0xC016;
    static const USHORT PlaRxFifoCtrl0 = 0xC0A0;
    static const USHORT PlaRxFifoCtrl1 = 0xC0A4;
    static const USHORT PlaRxFifoCtrl2 = 0xC0A8;
    static const USHORT PlaMar = 0xCD00;
    static const USHORT PlaBackup = 0xD000;
    static const USHORT PlaBootCtrl = 0xE004;
    static const USHORT PlaMacPowerCtrl3 = 0xE0CC;
    static const USHORT PlaTcr0 = 0xE610;
    static const USHORT PlaMtps = 0xE615;
    static const USHORT PlaTxFifoCtrl = 0xE618;
    static const USHORT PlaCr = 0xE813;
    static const USHORT PlaCrwecr = 0xE81C;
    static const USHORT PlaOobCtrl = 0xE84F;
    static const USHORT PlaCpcr = 0xE854;
    static const USHORT PlaMisc1 = 0xE85A;
    static const USHORT PlaOcpGphyBase = 0xE86C;
    static const USHORT PlaSffStatus7 = 0xE8DE;
    static const USHORT PlaPhyStatus = 0xE908;

    static const USHORT UsbUsbCtrl = 0xD406;
    static const USHORT UsbRxBufferThreshold = 0xD40C;
    static const USHORT UsbRxEarlyTimeout = 0xD42C;
    static const USHORT UsbRxEarlySize = 0xD42E;
    static const USHORT UsbRxExtraAggregateTimer = 0xD432;
    static const USHORT UsbUptRxdmaOwn = 0xD437;
    static const USHORT UsbBmuReset = 0xD4B0;

    static const USHORT OcpBaseMii = 0xA400;
    static const USHORT OcpWindowBase = 0xB000;
    static const USHORT OcpMiiBmcr = 0xB400;
    static const USHORT OcpBaseValue = 0xA000;

    static const DWORD ReceiveAcceptPhysical = 0x00000002;
    static const DWORD ReceiveAcceptMulticast = 0x00000004;
    static const DWORD ReceiveAcceptBroadcast = 0x00000008;
    static const DWORD ReceiveAcceptPromiscuous = 0x00000001;
    static const DWORD ReceiveAcceptAll = 0x0000000F;

    static const BYTE CommandReset = 0x10;
    static const BYTE CommandReceiveEnable = 0x08;
    static const BYTE CommandTransmitEnable = 0x04;
    static const BYTE ConfigMode = 0xC0;
    static const BYTE ConfigNormal = 0x00;
    static const BYTE OobNow = 0x80;
    static const BYTE OobLinkListReady = 0x02;
    static const USHORT SffReinitLinkList = 0x8000;
    static const USHORT SffMcuBorwEnable = 0x4000;
    static const USHORT MiscRxReadyGated = 0x0008;
    static const USHORT BootAutoloadDone = 0x0002;
    static const BYTE BmuResetIn = 0x01;
    static const BYTE BmuResetOut = 0x02;
    static const USHORT UsbRxAggregationDisable = 0x0010;
    static const USHORT UsbRxZeroEnable = 0x0080;
    static const USHORT CpcrRxVlan = 0x0040;
    static const USHORT TcrAutoFifo = 0x0080;
    static const USHORT PlaMcuSpeedDownEnable = 0x4000;
    static const BYTE MtpsJumbo = 192;
    static const DWORD RxFifoThreshold1Normal = 0x00080002;
    static const USHORT RxFifoThreshold2Normal = 0x00A0;
    static const USHORT RxFifoThreshold3Normal = 0x0110;
    static const DWORD TxFifoThresholdNormal = 0x01000008;
    static const DWORD UsbRxThresholdHigh = 0x7A120180;
    static const DWORD UsbRxThresholdB = 0x00010001;
    // The register uses an 8 ns unit. A 500 us window lets high-speed RTL8153
    // parts form useful RX aggregates without materially affecting XBDM latency.
    static const USHORT LegacyHighSpeedEarlyTimeout = 62500;
    static const USHORT Rtl8153BEarlyTimeout = 16;
    static const USHORT Rtl8153BExtraAggregateTimer = 1875;
    static const BYTE RxDmaOwnUpdate = 0x01;
    static const BYTE RxDmaOwnClear = 0x02;
    static const USHORT BmcrPowerDown = 0x0800;
    static const USHORT BmcrAutoNegotiationEnable = 0x1000;
    static const USHORT BmcrRestartAutoNegotiation = 0x0200;

    static const USHORT LinkInterrupt = 0x0004;
    static const USHORT LinkStatus = 0x0002;
    static const USHORT LinkFullDuplex = 0x0001;
    static const USHORT Link10Mbps = 0x0004;
    static const USHORT Link100Mbps = 0x0008;
    static const USHORT Link1000Mbps = 0x0010;
    static const USHORT LinkRxFlow = 0x0020;
    static const USHORT LinkTxFlow = 0x0040;

    static const DWORD TxFirstSegment = 0x80000000;
    static const DWORD TxLastSegment = 0x40000000;
    static const DWORD TxLengthMask = 0x0003FFFF;
    static const DWORD RxLengthMask = 0x00007FFF;
    static const DWORD EthernetFcsLength = 4;
    static const DWORD RxAlignment = 8;
    static const DWORD TxAlignment = 4;
    static const DWORD StandardFrameLimit = 1522;
    static const DWORD VersionMask = 0x00007CF0;

    NTSTATUS QueueRead(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register);
    NTSTATUS QueueWriteByte(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register, BYTE Value);
    NTSTATUS QueueWriteWord(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register, USHORT Value);
    NTSTATUS QueueWriteDword(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register, DWORD Value);
    VOID CompleteRegisterOperation(CUsbEnet* Device, NTSTATUS Status);
    DWORD ReadAlignedDword(CUsbEnet* Device) const;
    BYTE ExtractByte(DWORD Value, USHORT Register) const;
    USHORT ExtractWord(DWORD Value, USHORT Register) const;
    DWORD BuildReceiveControl(CUsbEnet* Device) const;
    DWORD DecodeLinkState(USHORT Status) const;
    VOID ApplyLinkStatus(CUsbEnet* Device, USHORT Status, PBOOL Notify);
    VOID ServiceRuntimeRequests(CUsbEnet* Device);
    VOID ContinueRuntimeMacWrite(CUsbEnet* Device);
    VOID FailInitialization(CUsbEnet* Device, NTSTATUS Status, const char* Operation);
    const char* VersionName(DWORD Version) const;
    static BOOL IsSupportedHardwareVersion(DWORD Version);
    static BOOL IsValidEthernetAddress(const CEnetAddr* Address);

    static VOID __fastcall AsyncCompletionRoutine(PVOID Request, NTSTATUS Status);

    RTL8153_RUNTIME_OPERATION m_RuntimeOperation;
    RTL8153_REGISTER_SPACE m_PendingSpace;
    USHORT m_PendingRegister;
    DWORD m_HardwareVersion;
    DWORD m_ReceiveControlBase;
    DWORD m_CommandValue;
    USHORT m_MiscValue;
    USHORT m_CpcrValue;
    USHORT m_BmcrValue;
    USHORT m_MacPowerControl3Value;
    USHORT m_SffValue;
    USHORT m_UsbControlValue;
    USHORT m_TcrValue;
    USHORT m_LastPhyStatus;
    DWORD m_ResetPollCount;
    DWORD m_LinkListPollCount;
    DWORD m_InterruptEventCount;
    DWORD m_LastLinkPollTick;
    BYTE m_BmuValue;
    BYTE m_OobValue;
    BOOL m_NodeIdValid;
    BOOL m_InterruptStatusSeen;
    BOOL m_LinkPollPending;
    BOOL m_FilterUpdatePending;
    BOOL m_MacUpdatePending;
    BYTE m_RuntimeMacStep;
};

extern CRtl8153 g_Rtl8153Chipset;

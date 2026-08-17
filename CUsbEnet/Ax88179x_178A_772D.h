#pragma once

#include "UsbEnetChipset.h"

enum AX88179_COMMAND {
    AX88179_ACCESS_MAC = 0x01,
    AX88179_ACCESS_PHY = 0x02,
    AX88179_ACCESS_EEPROM = 0x04,
    AX88179_ACCESS_EFUSE = 0x05,
    AX88179_RELOAD_EEPROM_EFUSE = 0x06
};

enum AX88179_MAC_REGISTER {
    AX88179_PHYSICAL_LINK_STATUS = 0x02,
    AX88179_GENERAL_STATUS = 0x03,
    AX88179_CHIP_STATUS = 0x05,
    AX88179_RX_CTL = 0x0B,
    AX88179_NODE_ID = 0x10,
    AX88179_MULTICAST_FILTER = 0x16,
    AX88179_MEDIUM_STATUS_MODE = 0x22,
    AX88179_MONITOR_MODE = 0x24,
    AX88179_GPIO_CONTROL = 0x25,
    AX88179_PHY_POWER_RESET = 0x26,
    AX88179_RX_BULKIN_QCTRL = 0x2E,
    AX88179_CLOCK_SELECT = 0x33,
    AX88179_RX_CHECKSUM_CONTROL = 0x34,
    AX88179_TX_CHECKSUM_CONTROL = 0x35,
    AX88179_PAUSE_WATERLVL_HIGH = 0x54,
    AX88179_PAUSE_WATERLVL_LOW = 0x55
};

enum AX88179_CHIP_VERSION {
    AX88179_CHIP_VERSION_INVALID = 0x0,
    AX88179_CHIP_VERSION_AX88179 = 0x4,
    AX88179_CHIP_VERSION_AX88179A = 0x6,
    AX88179_CHIP_VERSION_AX88279 = 0x7
};

enum AX88179_RX_CONTROL_FLAGS {
    AX88179_RX_CTL_STOP = 0x0000,
    AX88179_RX_CTL_PRO = 0x0001,
    AX88179_RX_CTL_AMALL = 0x0002,
    AX88179_RX_CTL_AB = 0x0008,
    AX88179_RX_CTL_AM = 0x0010,
    AX88179_RX_CTL_AP = 0x0020,
    AX88179_RX_CTL_START = 0x0080,
    AX88179_RX_CTL_DROPCRCERR = 0x0100,
    AX88179_RX_CTL_IPE = 0x0200
};

enum AX88179_MEDIUM_FLAGS {
    AX88179_MEDIUM_GIGAMODE = 0x0001,
    AX88179_MEDIUM_FULL_DUPLEX = 0x0002,
    AX88179_MEDIUM_EN_125MHZ = 0x0008,
    AX88179_MEDIUM_RXFLOW_CTRLEN = 0x0010,
    AX88179_MEDIUM_TXFLOW_CTRLEN = 0x0020,
    AX88179_MEDIUM_RECEIVE_EN = 0x0100,
    AX88179_MEDIUM_PS = 0x0200,
    AX88179_MEDIUM_JUMBO_EN = 0x8040
};

enum AX88179_PHY_POWER_FLAGS {
    AX88179_PHY_POWER_BZ = 0x0010,
    AX88179_PHY_POWER_IPRL = 0x0020,
    AX88179_PHY_POWER_AUTODETACH = 0x1000
};

enum AX88179_CLOCK_FLAGS {
    AX88179_CLOCK_BCS = 0x01,
    AX88179_CLOCK_ACS = 0x02,
    AX88179_CLOCK_ULR = 0x08
};

enum AX88179_MONITOR_FLAGS {
    AX88179_MONITOR_RWLC = 0x02,
    AX88179_MONITOR_RWMP = 0x04,
    AX88179_MONITOR_PMEPOL = 0x20,
    AX88179_MONITOR_PMETYPE = 0x40
};

enum AX88179_CHECKSUM_FLAGS {
    AX88179_CHECKSUM_IP = 0x01,
    AX88179_CHECKSUM_TCP = 0x02,
    AX88179_CHECKSUM_UDP = 0x04,
    AX88179_CHECKSUM_TCPV6 = 0x20,
    AX88179_CHECKSUM_UDPV6 = 0x40
};

enum AX88179_PHY_REGISTER {
    AX88179_PHY_PHYSICAL_STATUS = 0x11
};

enum AX88179_PHY_STATUS_FLAGS {
    AX88179_PHY_STATUS_SPEED_MASK = 0xC000,
    AX88179_PHY_STATUS_GIGABIT = 0x8000,
    AX88179_PHY_STATUS_100 = 0x4000,
    AX88179_PHY_STATUS_FULL_DUPLEX = 0x2000,
    AX88179_PHY_STATUS_LINK = 0x0400
};

enum AX88179_RX_HEADER_FLAGS {
    AX88179_RXHDR_L3CSUM_ERR = 0x00000002,
    AX88179_RXHDR_L4CSUM_ERR = 0x00000001,
    AX88179_RXHDR_RX_OK = 0x00000800,
    AX88179_RXHDR_BMC = 0x00008000,
    AX88179_RXHDR_CRC_ERR = 0x20000000,
    AX88179_RXHDR_MII_ERR = 0x40000000,
    AX88179_RXHDR_DROP_ERR = 0x80000000
};

enum AX88179_TX_HEADER_FLAGS {
    AX88179_TXHDR_PADDING = 0x80008000
};

enum AX88179_LINK_RESET_STAGE {
    Ax88179LinkResetIdle = 0,
    Ax88179LinkResetStopReceive,
    Ax88179LinkResetStartReceive,
    Ax88179LinkResetReadTxFifo,
    Ax88179LinkResetWriteBulkIn,
    Ax88179LinkResetWriteMedium,
    Ax88179LinkResetReadRxControl,
    Ax88179LinkResetReadMedium,
    Ax88179LinkResetReadBulkIn,
    Ax88179LinkResetReadNodeId,
    Ax88179LinkResetReadClock,
    Ax88179LinkResetReadPhyPower,
    Ax88179LinkResetReadMonitor,
    Ax88179LinkResetReadRxChecksum,
    Ax88179LinkResetReadTxChecksum,
    Ax88179LinkResetReadChipStatus
};

enum AX88179_INIT_STAGE {
    Ax88179InitBeginning = 0,
    Ax88179InitPowerDownPhy,
    Ax88179InitPowerUpPhy,
    Ax88179InitPausePhy,
    Ax88179InitEnableClocks,
    Ax88179InitPauseClock,
    Ax88179InitReadNodeId,
    Ax88179InitWaitingForEthernetAddress,
    Ax88179InitWriteNodeId,
    Ax88179InitConfigureBulkIn,
    Ax88179InitWritePauseWaterLow,
    Ax88179InitWritePauseWaterHigh,
    Ax88179InitEnableRxChecksum,
    Ax88179InitEnableTxChecksum,
    Ax88179InitStartReceiving,
    Ax88179InitWriteMonitorMode,
    Ax88179InitWriteMediumMode,
    Ax88179InitWaitPhyReady,
    Ax88179InitPhyWriteMmdAccess1,
    Ax88179InitPhyWriteMmdData1,
    Ax88179InitPhyWriteMmdAccess2,
    Ax88179InitPhyWriteMmdData2,
    Ax88179InitPhyReadBmcr,
    Ax88179InitPhyClearBmcr,
    Ax88179InitPhyPauseAfterBmcrClear,
    Ax88179InitPhyWriteAdvertisement,
    Ax88179InitPhyWriteGigabitAdvertisement,
    Ax88179InitRestartAutonegotiation,
    Ax88179InitReady,
    Ax88179InitComplete,
    Ax88179InitStageCount
};

class CAx88179_178A : public CUsbEnetChipset {
public:
    virtual USHORT GetVendorIdRaw() const;
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
    virtual VOID HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe);
    virtual VOID HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status);
    virtual VOID RunTimer(CUsbEnet* Device);
    virtual NTSTATUS BeginReadPhy(CUsbEnet* Device, USHORT Register);
    virtual NTSTATUS BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value);
    virtual BOOL UsesInterruptLinkStatus() const;
    virtual BOOL ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length);
    virtual BOOL InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context);
    virtual USBENET_RX_PARSE_RESULT GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame);
    virtual BOOL AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator);

private:
    NTSTATUS QueueMacRead(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length);
    NTSTATUS QueueMacWrite8(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, BYTE Value);
    NTSTATUS QueueMacWrite16(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, USHORT Value);
    NTSTATUS QueueMacWrite(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length, const PVOID Buffer);
    USHORT BuildReceiveControl(CUsbEnet* Device) const;
    USHORT BuildMediumMode(DWORD LinkState) const;
    VOID CompleteInitTransfer(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteReadNodeId(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteWriteNodeId(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteReadPhy(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteWritePhy(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteWriteMediumMode(CUsbEnet* Device, NTSTATUS Status);
    BOOL UpdateLinkState(CUsbEnet* Device, USHORT PhyStatus);
    BOOL StartLinkReset(CUsbEnet* Device, DWORD LinkState);
    BOOL QueueNextLinkResetTransfer(CUsbEnet* Device);
    VOID CompleteLinkResetTransfer(CUsbEnet* Device, NTSTATUS Status);
    VOID LogLinkResetReadback(CUsbEnet* Device, BYTE Stage, NTSTATUS Status);
    VOID NotifyLinkStateChanged(CUsbEnet* Device);
    VOID RequestHardReceiveRecovery(CUsbEnet* Device);
    VOID ResetRecoveryCounters(USBENET_TRANSPORT_PIPE Pipe);
    VOID ProcessRecovery(CUsbEnet* Device, DWORD CurrentTick);

    static VOID __fastcall AsyncCompletionRoutineInitTransfer(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineReadNodeId(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteNodeId(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineReadPhy(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWritePhy(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteMediumMode(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineLinkReset(PVOID Request, NTSTATUS Status);

    BOOL m_NodeIdValid;
    BOOL m_NotifyLinkAfterMediumWrite;
    BOOL m_LinkResetInProgress;
    BOOL m_RxRecoveryInProgress;
    BOOL m_RxHardRecoveryPending;
    BYTE m_LinkResetStage;
    BYTE m_LinkResetRetries;
    USHORT m_ReceiveControl;
    USHORT m_LastPhyStatus;
    DWORD m_LinkResetLinkState;
    BOOL m_InterruptStatusSeen;
    BOOL m_InterruptLinkUp;
    BOOL m_InterruptPhyRefreshPending;
    DWORD m_InterruptEventCount;
    DWORD m_RxAggregateSequence;
    DWORD m_RxInvalidAggregateSequence;
    DWORD m_RxInvalidAggregateStreak;
    DWORD m_RxInvalidAggregateSignature;
    DWORD m_RxRecoveryStartTick;
    DWORD m_RxHardRecoveryLinkGraceTick;
    DWORD m_RxHardRecoveryCount;
    DWORD m_RxDescriptorSequence;
    DWORD m_RxDiscardDescriptorSequence;
    DWORD m_RxFrameSequence;
    DWORD m_TxFrameSequence;
    DWORD m_PhyReadSequence;
    DWORD m_PhyWriteSequence;
    BYTE m_PhyReadyRetryCount;
    BOOL m_PhyReadyRetryPending;
    USHORT m_PhyInitBmcr;
    BYTE m_RecoveryLevel;
    BYTE m_RecoveryPipe;
    BOOL m_FullReinitializePending;
    BOOL m_FullReinitializeActive;
    BOOL m_RecoveryEndpointsReset;
    BOOL m_DeviceResetPending;
    DWORD m_RecoveryReason;
    DWORD m_RecoveryCount;
    DWORD m_RecoveryRequestTick;
    DWORD m_RecoveryErrorCount[3];
    DWORD m_RecoveryLastErrorTick[3];
    DWORD m_TxOutstandingSinceTick;
    DWORD m_TxTimeoutCount;
};

class CAx88179 : public CAx88179_178A {
public:
    virtual const char* GetName() const;
    virtual USHORT GetProductIdRaw() const;
    virtual BOOL SupportsTransmitAggregation() const;
};

class CAx88178A : public CAx88179_178A {
public:
    virtual const char* GetName() const;
    virtual USHORT GetProductIdRaw() const;
};

extern CAx88179 g_Ax88179Chipset;
extern CAx88178A g_Ax88178AChipset;

enum AX179A_VARIANT {
    Ax179AVariant88179AB = 0,
    Ax179AVariant88772D,
    Ax179AVariant88279
};

enum AX179A_COMMAND {
    AX179A_ACCESS_MAC = 0x01,
    AX179A_ACCESS_PHY = 0x02,
    AX179A_RELOAD_EEPROM_EFUSE = 0x06,
    AX179A_FW_MODE = 0x08,
    AX179A_ACCESS_BL = 0x2A,
    AX179A_PHY_POWER = 0x31
};

enum AX179A_MAC_REGISTER {
    AX179A_PHYSICAL_LINK_STATUS = 0x02,
    AX179A_CHIP_STATUS = 0x05,
    AX179A_ETH_TX_GAP = 0x0D,
    AX179A_BFM_DATA = 0x0E,
    AX179A_NODE_ID = 0x10,
    AX179A_MULTICAST_FILTER = 0x16,
    AX179A_MEDIUM_STATUS_MODE = 0x22,
    AX179A_MONITOR_MODE = 0x24,
    AX179A_VLAN_ID_CONTROL = 0x2B,
    AX179A_RX_BULKIN_QCTRL = 0x2E,
    AX179A_RX_CHECKSUM_CONTROL = 0x34,
    AX179A_TX_CHECKSUM_CONTROL = 0x35,
    AX179A_MAC_BM_INT_MASK = 0x41,
    AX179A_MAC_BM_RX_DMA_CTL = 0x43,
    AX179A_MAC_BM_TX_DMA_CTL = 0x46,
    // AX88179A/AX88772D generation: the two-byte pause value begins at 0x54.
    AX179A_PAUSE_WATERLVL_LOW = 0x54,
    AX179A_PAUSE_WATERLVL_HIGH = 0x55,
    AX179A_MAC_RX_STATUS_CDC = 0x6D,
    AX179A_MAC_ARC_CTRL = 0x9E,
    AX179A_MAC_SWP_CTRL = 0xB1,
    AX179A_MAC_TX_PAUSE = 0xB2,
    AX179A_MAC_CDC_DELAY_TX = 0xB5,
    AX179A_MAC_PATH = 0xB7,
    AX179A_NEW_PAUSE_CTRL = 0xB8,
    AX179A_MAC_BULK_OUT_CTRL = 0xB9,
    AX179A_MAC_RX_DATA_CDC_CNT = 0xC0,
    AX179A_MAC_LSO_ENHANCE_CTRL = 0xC3,
    AX179A_MAC_TX_HDR_CKSUM = 0xCC,
    AX179A_EP5_EHR = 0xF9
};

enum AX179A_RX_CONTROL_FLAGS {
    AX179A_RX_CTL_STOP = 0x0000,
    AX179A_RX_CTL_PRO = 0x0001,
    AX179A_RX_CTL_AMALL = 0x0002,
    AX179A_RX_CTL_AB = 0x0008,
    AX179A_RX_CTL_AM = 0x0010,
    AX179A_RX_CTL_AP = 0x0020,
    AX179A_RX_CTL_START = 0x0080,
    AX179A_RX_CTL_DROPCRCERR = 0x0100,
    AX179A_RX_CTL_IPE = 0x0200
};

enum AX179A_MEDIUM_FLAGS {
    AX179A_MEDIUM_GIGAMODE = 0x0001,
    AX179A_MEDIUM_FULL_DUPLEX = 0x0002,
    AX179A_MEDIUM_EN_125MHZ = 0x0008,
    AX179A_MEDIUM_RXFLOW_CTRLEN = 0x0010,
    AX179A_MEDIUM_TXFLOW_CTRLEN = 0x0020,
    AX179A_MEDIUM_RECEIVE_EN = 0x0100,
    AX179A_MEDIUM_PS = 0x0200,
    AX179A_MEDIUM_JUMBO_EN = 0x8040
};

enum AX179A_PHY_STATUS_FLAGS {
    AX179A_PHY_STATUS_SPEED_MASK = 0xC000,
    AX179A_PHY_STATUS_2500 = 0xC000,
    AX179A_PHY_STATUS_GIGABIT = 0x8000,
    AX179A_PHY_STATUS_100 = 0x4000,
    AX179A_PHY_STATUS_FULL_DUPLEX = 0x2000,
    AX179A_PHY_STATUS_LINK = 0x0400
};

enum AX179A_LINK_SPEED {
    Ax179ALinkNone = 0,
    Ax179ALink10,
    Ax179ALink100,
    Ax179ALink1000,
    Ax179ALink2500
};

enum AX179A_INIT_STAGE {
    Ax179AInitBeginning = 0,
    Ax179AInitReadChipStatus,
    Ax179AInitReadFirmware0,
    Ax179AInitReadFirmware1,
    Ax179AInitReadFirmware2,
    Ax179AInitEnableFirmwareMode,
    Ax179AInitReloadNonvolatileState,
    Ax179AInitPowerDownPhy,
    Ax179AInitPausePowerDown,
    Ax179AInitPowerUpPhy,
    Ax179AInitPausePowerUp,
    Ax179AInitEnableBulkOut,
    Ax179AInitStopReceive,
    Ax179AInitWritePauseLow,
    Ax179AInitWritePauseHigh,
    Ax179AInitDisableVlan,
    Ax179AInitMaskInterrupts,
    Ax179AInitDisableRxDma,
    Ax179AInitDisableTxDma,
    Ax179AInitDisableArc,
    Ax179AInitDisableSwp,
    Ax179AInitDisableTxHeaderChecksum,
    Ax179AInitReadNodeId,
    Ax179AInitWaitingForEthernetAddress,
    Ax179AInitWriteNodeId,
    Ax179AInitReadNodeIdBack,
    Ax179AInitEnableRxChecksum,
    Ax179AInitEnableTxChecksum,
    Ax179AInitStartReceiving,
    Ax179AInitWriteMonitorMode,
    Ax179AInitWriteDefaultMedium,
    Ax179AInitWaitPhyReady,
    Ax179AInitPhyWriteMmdAccess1,
    Ax179AInitPhyWriteMmdData1,
    Ax179AInitPhyWriteMmdAccess2,
    Ax179AInitPhyWriteMmdData2,
    Ax179AInitPhyReadBmcr,
    Ax179AInitPhyClearBmcr,
    Ax179AInitPhyPauseAfterBmcrClear,
    Ax179AInitPhyWriteAdvertisement,
    Ax179AInitPhyWriteGigabitAdvertisement,
    Ax179AInitRestartAutonegotiation,
    Ax179AInitReady,
    Ax179AInitComplete,
    Ax179AInitStageCount
};

enum AX179A_LINK_RESET_STAGE {
    Ax179ALinkResetIdle = 0,
    Ax179ALinkResetStopReceive,
    Ax179ALinkResetStopMacPath,
    Ax179ALinkResetWriteCdcDelay,
    Ax179ALinkResetWritePauseWatermark,
    Ax179ALinkResetWriteTxGap,
    Ax179ALinkResetWriteEp5,
    Ax179ALinkResetWriteNewPause,
    Ax179ALinkResetWriteTxPause,
    Ax179ALinkResetWriteRxStatus,
    Ax179ALinkResetWriteRxDataCount,
    Ax179ALinkResetWriteBfmData,
    Ax179ALinkResetWriteLsoEnhance,
    Ax179ALinkResetWriteBulkIn,
    Ax179ALinkResetWriteMedium,
    Ax179ALinkResetStartReceive,
    Ax179ALinkResetStartMacPath,
    Ax179ALinkResetComplete
};

class CAx179AFamily : public CUsbEnetChipset {
public:
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
    virtual VOID HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe);
    virtual VOID HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status);
    virtual VOID RunTimer(CUsbEnet* Device);
    virtual NTSTATUS BeginReadPhy(CUsbEnet* Device, USHORT Register);
    virtual NTSTATUS BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value);
    virtual BOOL UsesInterruptLinkStatus() const;
    virtual BOOL ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length);
    virtual BOOL InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context);
    virtual USBENET_RX_PARSE_RESULT GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame);
    virtual BOOL AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator);

protected:
    virtual AX179A_VARIANT GetVariant() const = 0;
    virtual BYTE GetExpectedChipVersion() const = 0;
    virtual BOOL UsesIpAlignment() const = 0;
    virtual BOOL SupportsGigabit() const = 0;
    virtual DWORD GetMaximumWireSpeed() const = 0;

private:
    NTSTATUS QueueCommand(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Request, WORD Value, WORD Index, WORD Length, const PVOID Buffer);
    NTSTATUS QueueMacRead(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length);
    NTSTATUS QueueMacWrite8(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, BYTE Value);
    NTSTATUS QueueMacWrite16(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, USHORT Value);
    NTSTATUS QueueMacWrite(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length, const PVOID Buffer);
    USHORT BuildReceiveControl(CUsbEnet* Device) const;
    USHORT BuildMediumMode() const;
    const BYTE* SelectBulkInProfile() const;
    VOID RecoverXboxAddresses(CUsbEnet* Device);
    VOID CompleteInitTransfer(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteNodeWrite(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteReadPhy(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteWritePhy(CUsbEnet* Device, NTSTATUS Status);
    BOOL UpdateLinkState(CUsbEnet* Device, USHORT PhyStatus);
    BOOL StartLinkReset(CUsbEnet* Device);
    BOOL QueueNextLinkResetTransfer(CUsbEnet* Device);
    VOID CompleteLinkResetTransfer(CUsbEnet* Device, NTSTATUS Status);
    VOID NotifyLinkStateChanged(CUsbEnet* Device);
    VOID ResetRecoveryCounters(USBENET_TRANSPORT_PIPE Pipe);
    VOID ProcessRecovery(CUsbEnet* Device, DWORD CurrentTick);

    static VOID __fastcall AsyncCompletionRoutineInitTransfer(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineNodeWrite(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineReadPhy(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWritePhy(PVOID Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineLinkReset(PVOID Request, NTSTATUS Status);

    BOOL m_NodeIdValid;
    BOOL m_LinkResetInProgress;
    BOOL m_RxRecoveryInProgress;
    BYTE m_ChipStatus;
    BYTE m_FirmwareVersion[4];
    BYTE m_LinkResetStage;
    BYTE m_LinkSpeed;
    BOOL m_FullDuplex;
    USHORT m_ReceiveControl;
    USHORT m_LastPhyStatus;
    BOOL m_InterruptStatusSeen;
    BOOL m_InterruptLinkUp;
    BOOL m_InterruptPhyRefreshPending;
    DWORD m_InterruptEventCount;
    DWORD m_RxAggregateSequence;
    DWORD m_RxDescriptorSequence;
    DWORD m_RxFrameSequence;
    DWORD m_TxFrameSequence;
    DWORD m_RxInvalidAggregateStreak;
    ULONGLONG m_RxInvalidAggregateSignature;
    DWORD m_RxRecoveryStartTick;
    DWORD m_RxHardRecoveryCount;
    BYTE m_PhyReadyRetryCount;
    BOOL m_PhyReadyRetryPending;
    USHORT m_PhyInitBmcr;
    DWORD m_ReinitializeLinkGraceTick;
    BYTE m_RecoveryLevel;
    BYTE m_RecoveryPipe;
    BOOL m_FullReinitializePending;
    BOOL m_FullReinitializeActive;
    BOOL m_RecoveryEndpointsReset;
    BOOL m_DeviceResetPending;
    DWORD m_RecoveryReason;
    DWORD m_RecoveryCount;
    DWORD m_RecoveryRequestTick;
    DWORD m_RecoveryErrorCount[3];
    DWORD m_RecoveryLastErrorTick[3];
    DWORD m_TxOutstandingSinceTick;
    DWORD m_TxTimeoutCount;
};

class CAx88179AB : public CAx179AFamily {
public:
    virtual const char* GetName() const;

protected:
    virtual AX179A_VARIANT GetVariant() const;
    virtual BYTE GetExpectedChipVersion() const;
    virtual BOOL UsesIpAlignment() const;
    virtual BOOL SupportsGigabit() const;
    virtual DWORD GetMaximumWireSpeed() const;
};

class CAx88772D : public CAx179AFamily {
public:
    virtual const char* GetName() const;

protected:
    virtual AX179A_VARIANT GetVariant() const;
    virtual BYTE GetExpectedChipVersion() const;
    virtual BOOL UsesIpAlignment() const;
    virtual BOOL SupportsGigabit() const;
    virtual DWORD GetMaximumWireSpeed() const;
};

class CAx88279 : public CAx179AFamily {
public:
    virtual const char* GetName() const;

protected:
    virtual AX179A_VARIANT GetVariant() const;
    virtual BYTE GetExpectedChipVersion() const;
    virtual BOOL UsesIpAlignment() const;
    virtual BOOL SupportsGigabit() const;
    virtual DWORD GetMaximumWireSpeed() const;
};

extern CAx88179AB g_Ax88179ABChipset;
extern CAx88772D g_Ax88772DChipset;
extern CAx88279 g_Ax88279Chipset;

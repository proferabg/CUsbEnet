#pragma once

#include "UsbEnetChipset.h"

enum AX_CMD {
    AX_CMD_SET_SW_MII         = 0x06,
    AX_CMD_READ_MII_REG       = 0x07,
    AX_CMD_WRITE_MII_REG      = 0x08,
    AX_CMD_STATMNGSTS_REG     = 0x09,
    AX_CMD_SET_HW_MII         = 0x0A,
    AX_CMD_READ_EEPROM        = 0x0B,
    AX_CMD_WRITE_EEPROM       = 0x0C,
    AX_CMD_WRITE_ENABLE       = 0x0D,
    AX_CMD_WRITE_DISABLE      = 0x0E,
    AX_CMD_READ_RX_CTL        = 0x0F,
    AX_CMD_WRITE_RX_CTL       = 0x10,
    AX_CMD_READ_IPG012        = 0x11,
    AX_CMD_WRITE_IPG0         = 0x12,
    AX_CMD_WRITE_IPG1         = 0x13,
    AX_CMD_READ_NODE_ID       = 0x13,
    AX_CMD_WRITE_NODE_ID      = 0x14,
    AX_CMD_WRITE_IPG2         = 0x15,
    AX_CMD_WRITE_MULTI_FILTER = 0x16,
    AX88172_CMD_READ_NODE_ID  = 0x17,
    AX_CMD_READ_PHY_ID        = 0x19,
    AX_CMD_READ_MEDIUM_STATUS = 0x1A,
    AX_CMD_WRITE_MEDIUM_MODE  = 0x1B,
    AX_CMD_READ_MONITOR_MODE  = 0x1C,
    AX_CMD_WRITE_MONITOR_MODE = 0x1D,
    AX_CMD_READ_GPIOS         = 0x1E,
    AX_CMD_WRITE_GPIOS        = 0x1F,
    AX_CMD_SW_RESET           = 0x20,
    AX_CMD_SW_PHY_STATUS      = 0x21,
    AX_CMD_SW_PHY_SELECT      = 0x22,
    AX_QCTCTRL                = 0x2A
};

enum AX_RX_CTL {
    AX_RX_CTL_STOP      = 0x0000,
    AX_RX_CTL_PRO       = 0x0001,
    AX_RX_CTL_AMALL     = 0x0002,
    AX_RX_CTL_SEP       = 0x0004,
    AX_RX_CTL_AB        = 0x0008,
    AX_RX_CTL_AM        = 0x0010,
    AX_RX_CTL_AP        = 0x0020,
    AX_RX_CTL_START     = 0x0080,
    AX_RX_CTL_MFB_2048  = 0x0000,
    AX_RX_CTL_MFB_4096  = 0x0100,
    AX_RX_CTL_MFB_8192  = 0x0200,
    AX_RX_CTL_MFB_16384 = 0x0300,
    AX_RX_CTL_MFB_MASK  = 0x0300
};

enum AX_MEDIUM_FLAGS {
    AX_MEDIUM_GM   = 0x0001,
    AX_MEDIUM_FD   = 0x0002,
    AX_MEDIUM_AC   = 0x0004,
    AX_MEDIUM_ENCK = 0x0008,
    AX_MEDIUM_RFC  = 0x0010,
    AX_MEDIUM_TFC  = 0x0020,
    AX_MEDIUM_JFE  = 0x0040,
    AX_MEDIUM_PF   = 0x0080,
    AX_MEDIUM_RE   = 0x0100,
    AX_MEDIUM_PS   = 0x0200,
    AX_MEDIUM_SBP  = 0x0800,
    AX_MEDIUM_SM   = 0x1000
};

#define AX_MEDIUM_DEFAULT (AX_MEDIUM_AC | AX_MEDIUM_ENCK | AX_MEDIUM_RE)

enum USBENET_INIT_STAGE {
    UsbEnetInitBeginning = 0,
    UsbEnetInitReadEepromConfig,
    UsbEnetInitWriteGpio1,
    UsbEnetInitPause1,
    UsbEnetInitWriteGpio2,
    UsbEnetInitPause2,
    UsbEnetInitWriteGpio3,
    UsbEnetInitPause3,
    UsbEnetInitWriteGpio4,
    UsbEnetInitPause4,
    UsbEnetInitSoftwareResetClear,
    UsbEnetInitPauseResetClear,
    UsbEnetInitSoftwareReset,
    UsbEnetInitPause5,
    UsbEnetInitReadNodeId,
    UsbEnetInitWaitingForEthernetAddress,
    UsbEnetInitWriteNodeId,
    UsbEnetInitReadPhyAddressRegister,
    UsbEnetInitWaitPhyReady,
    UsbEnetInitReadPhyRegisters,
    UsbEnetInitWriteMediumStatusRegister,
    UsbEnetInitWriteMiiControlRegister,
    UsbEnetInitWritePhyAnar,
    UsbEnetInitWritePhyExpansion,
    UsbEnetInitWritePhy1000TControl,
    UsbEnetInitWriteMarvellExtPhySpecificCtrl,
    UsbEnetInitWritePhyControl,
    UsbEnetInitWriteIpg,
    UsbEnetInitStartReceiving,
    UsbEnetInitReady,
    UsbEnetInitComplete,
    UsbEnetInitStageCount
};

enum PHY_REGISTER {
    PhyRegisterControl = 0,
    PhyRegisterStatus = 1,
    PhyRegisterIdentifier1 = 2,
    PhyRegisterIdentifier2 = 3,
    PhyRegisterAnar = 4,
    PhyRegisterLinkPartner = 5,
    PhyRegisterExpansion = 6,
    PhyRegisterNextPage = 7,
    PhyRegisterLinkPartnerNext = 8,
    PhyRegister1000TControl = 9,
    PhyRegister1000TStatus = 10,
    PhyRegisterExtendedStatus = 11
};

enum REALTEK_PHY_INIT_STEP {
    RealtekPhyInitIdle = 0,
    RealtekPhyInitWritePage5,
    RealtekPhyInitClearRegister0C,
    RealtekPhyInitReadRegister01,
    RealtekPhyInitSetRegister01Bit7,
    RealtekPhyInitRestorePage0,
    RealtekPhyInitWriteLedPage2,
    RealtekPhyInitWriteLedRegister,
    RealtekPhyInitRestorePage0AfterLed,
    RealtekPhyInitRestorePage0AfterFailure,
    RealtekPhyInitComplete
};

enum AX88178_PHY_CONFIG_STEP {
    Ax88178PhyConfigIdle = 0,
    Ax88178PhyConfigEnableEeprom,
    Ax88178PhyConfigReadEepromWord17,
    Ax88178PhyConfigDisableEeprom
};

enum AX88178_FILTER_STEP {
    Ax88178FilterIdle = 0,
    Ax88178FilterWriteHash,
    Ax88178FilterWriteRxControl
};


enum AX88178_MARVELL_LED_STEP {
    Ax88178MarvellLedIdle = 0,
    Ax88178MarvellLedRead,
    Ax88178MarvellLedWrite
};

enum AX88178_RECOVERY_LEVEL {
    Ax88178RecoveryIdle = 0,
    Ax88178RecoveryResetPipe,
    Ax88178RecoveryReinitialize,
    Ax88178RecoveryResetDevice
};

#pragma pack(push, 1)
typedef struct _AX88178_RECV_HEADER {
    WORD Length;
    WORD LengthComplement;
} AX88178_RECV_HEADER, *PAX88178_RECV_HEADER;

typedef struct _AX88178_PHY_PATCH_ENTRY {
    USHORT Register;
    USHORT Value;
} AX88178_PHY_PATCH_ENTRY, *PAX88178_PHY_PATCH_ENTRY;
#pragma pack(pop)
C_ASSERT(sizeof(AX88178_RECV_HEADER) == 0x4);
C_ASSERT(sizeof(AX88178_PHY_PATCH_ENTRY) == 0x4);

class CAx88178 : public CUsbEnetChipset {
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
    virtual VOID HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe);
    virtual VOID HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status);
    virtual BOOL UsesInterruptLinkStatus() const;
    virtual BOOL ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length);
    virtual VOID RunTimer(CUsbEnet* Device);
    virtual NTSTATUS BeginReadPhy(CUsbEnet* Device, USHORT Register);
    virtual NTSTATUS BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value);
    virtual BOOL InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context);
    virtual USBENET_RX_PARSE_RESULT GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame);
    virtual BOOL AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator);

private:
    static VOID IncrementEthernetAddress(CEnetAddr* Address);
    static VOID PrintLinkMode(DWORD State, USHORT Control, USHORT BasicStatus, USHORT Advertisement, USHORT LinkPartnerAbility, USHORT GigabitControl, USHORT GigabitStatus);
    static DWORD EthernetCrc32(const BYTE* Data, DWORD Length);
    static BOOL IsRecognizedPhyIdentifier(USHORT Identifier1);

    VOID ReadNodeId(CUsbEnet* Device);
    VOID CompleteReadNodeId(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteWriteNodeId(CUsbEnet* Device, NTSTATUS Status);
    VOID ReadPhyAddressRegister(CUsbEnet* Device);
    VOID CompleteReadPhyAddressRegister(CUsbEnet* Device, NTSTATUS Status);
    VOID WriteMediumStatus(CUsbEnet* Device);
    VOID CompleteWriteMediumStatus(CUsbEnet* Device, NTSTATUS Status);
    VOID WriteGpio(CUsbEnet* Device, BYTE Value);
    VOID CompleteWriteGpio(CUsbEnet* Device, NTSTATUS Status);
    VOID SoftwareReset(CUsbEnet* Device, BYTE Value);
    VOID CompleteSoftwareReset(CUsbEnet* Device, NTSTATUS Status);
    VOID WriteMiiControl(CUsbEnet* Device);
    VOID CompleteWriteMiiControl(CUsbEnet* Device, NTSTATUS Status);
    VOID WriteIpg(CUsbEnet* Device);
    VOID CompleteWriteIpg(CUsbEnet* Device, NTSTATUS Status);
    VOID UpdateLinkState(CUsbEnet* Device);
    VOID UpdateMarvellLed(CUsbEnet* Device);

    NTSTATUS BeginReadAllPhy(CUsbEnet* Device);
    VOID CompleteReadPhy(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteWritePhy(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteAcquireSerialMgmtControl(CUsbEnet* Device, NTSTATUS Status);
    VOID CompleteReleaseSerialMgmtControl(CUsbEnet* Device, NTSTATUS Status);
    VOID FinishPhyTransaction(CUsbEnet* Device, NTSTATUS ReleaseStatus);

    VOID BeginEepromConfig(CUsbEnet* Device, BOOL InitialRead);
    NTSTATUS QueuePhyConfigStep(CUsbEnet* Device);
    VOID CompletePhyConfigStep(CUsbEnet* Device, NTSTATUS Status);
    VOID ContinueVendorPhyInitialization(CUsbEnet* Device);

    NTSTATUS QueueRealtekPhyInitStep(CUsbEnet* Device);
    BOOL AdvanceRealtekPhyInit(CUsbEnet* Device);
    VOID BeginPhyPatch(CUsbEnet* Device, const AX88178_PHY_PATCH_ENTRY* Patch, DWORD Count, const char* Name);
    BOOL QueueNextPhyPatch(CUsbEnet* Device);

    USHORT BuildReceiveControl(CUsbEnet* Device, BYTE Filter[8], PBOOL WriteFilter) const;
    VOID BeginFilterUpdate(CUsbEnet* Device, BOOL SubmitReceiveRing);
    VOID CompleteFilterUpdate(CUsbEnet* Device, NTSTATUS Status);

    VOID ProcessRecovery(CUsbEnet* Device, DWORD CurrentTick);
    VOID ResetRecoveryCounters(USBENET_TRANSPORT_PIPE Pipe);

    static VOID __fastcall AsyncCompletionRoutineReadNodeId(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteNodeId(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineReadPhyAddressRegister(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteMediumStatus(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteGpio(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineSoftwareReset(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteMiiControl(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWriteIpg(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineReadPhy(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineWritePhy(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineAcquireSerialMgmtControl(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineReleaseSerialMgmtControl(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutinePhyConfig(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);
    static VOID __fastcall AsyncCompletionRoutineFilterUpdate(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status);

    REALTEK_PHY_INIT_STEP m_RealtekPhyInitStep;
    BOOL m_RealtekPhyInitOperationFailed;
    AX88178_PHY_CONFIG_STEP m_PhyConfigStep;
    BOOL m_PhyConfigInitialRead;
    USHORT m_EepromWord17;
    BOOL m_EepromWord17Valid;
    USHORT m_PhyMode;
    USHORT m_LedMode;
    DWORD m_PhyReadyRetryCount;
    BOOL m_PhyReadyRetryPending;
    const AX88178_PHY_PATCH_ENTRY* m_PhyPatch;
    DWORD m_PhyPatchCount;
    DWORD m_PhyPatchIndex;
    const char* m_PhyPatchName;
    BOOL m_PhyPatchOperationFailed;
    AX88178_FILTER_STEP m_FilterStep;
    BYTE m_MulticastFilter[8];
    BYTE m_PendingMulticastFilter[8];
    USHORT m_PendingReceiveControl;
    BOOL m_FilterSubmitReceiveRing;
    BOOL m_FilterUpdatePending;
    BOOL m_InterruptStatusSeen;
    BOOL m_InterruptLinkUp;
    BOOL m_InterruptPhyRefreshPending;
    BOOL m_InterruptLinkAuthoritative;
    DWORD m_InterruptEventCount;
    DWORD m_LastFallbackPhyPollTick;
    DWORD m_LastMarvellLedState;
    AX88178_MARVELL_LED_STEP m_MarvellLedStep;
    AX88178_RECOVERY_LEVEL m_RecoveryLevel;
    USBENET_TRANSPORT_PIPE m_RecoveryPipe;
    DWORD m_RecoveryRequestTick;
    DWORD m_RecoveryLastErrorTick[3];
    DWORD m_RecoveryErrorCount[3];
    DWORD m_RecoveryReason;
    DWORD m_TxOutstandingSinceTick;
    DWORD m_TxTimeoutCount;
    BOOL m_FullReinitializePending;
    BOOL m_FullReinitializeActive;
    BOOL m_ReinitializeDrainStarted;
    BOOL m_DeviceResetPending;
    DWORD m_RecoveryCount;
};

extern CAx88178 g_Ax88178Chipset;

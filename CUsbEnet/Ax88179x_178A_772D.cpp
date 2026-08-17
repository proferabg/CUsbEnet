#include "stdafx.h"
#include "UsbEnetChipset.h"
#include "Ax88179x_178A_772D.h"

#if USBENET_DEBUG
#define USBENET_LOG(_fmt, ...) DbgPrint("[Ax88179x] " _fmt "\n", __VA_ARGS__)
#define USBENET_LOG0(_msg) DbgPrint("[Ax88179x] " _msg "\n")
#else
#define USBENET_LOG(_fmt, ...) ((void)0)
#define USBENET_LOG0(_msg) ((void)0)
#endif

#if DEVKIT_ONLY
#define NULL_OWNER_THREAD(_this) { _this->LockOwnerThread = NULL; }
#else
#define NULL_OWNER_THREAD(val) { __noop; }
#endif

#if _TRAP && DEVKIT_ONLY
#define TRAP_ASSERT(Expression) if (!(Expression)) { __asm { twi 31, r0, 0x19 } }
#define TRAP_IRQL(val) { TRAP_ASSERT(KeGetCurrentIrql() == val); }
#define TRAP_THREAD(val) { TRAP_ASSERT(KeGetCurrentThread() == val); }
#else
#define TRAP_ASSERT(Expression) { __noop; }
#define TRAP_IRQL(val) { __noop; }
#define TRAP_THREAD(val) { __noop; }
#endif

CAx88179 g_Ax88179Chipset;
CAx88178A g_Ax88178AChipset;

/* Limit device-side RX aggregation to the existing 0x4000-byte receive buffers. */
static BYTE Ax88179BulkInQueue[5] = { 7, 0x20, 3, 0x0E, 0xFF };
static const BYTE Ax88179ChecksumControl = AX88179_CHECKSUM_IP | AX88179_CHECKSUM_TCP | AX88179_CHECKSUM_UDP | AX88179_CHECKSUM_TCPV6 | AX88179_CHECKSUM_UDPV6;
static const BYTE Ax88179MonitorMode = AX88179_MONITOR_PMETYPE | AX88179_MONITOR_PMEPOL | AX88179_MONITOR_RWMP;
static const BYTE Ax88179ReadTxFifoRequest = 0x81;
static const WORD Ax88179ReadTxFifoValue = 0x008C;
static const DWORD Ax88179TxFifoBusy = 0x40000000;
static const BYTE Ax88179MaximumLinkResetRetries = 16;
static const USHORT Ax88179PhyAddress = 0x03;
static const USHORT Ax88179DefaultMediumMode = AX88179_MEDIUM_RECEIVE_EN | AX88179_MEDIUM_TXFLOW_CTRLEN | AX88179_MEDIUM_RXFLOW_CTRLEN | AX88179_MEDIUM_FULL_DUPLEX | AX88179_MEDIUM_GIGAMODE;
static const BYTE Ax88179PhyReadyMaximumRetries = 31;
static const DWORD Ax88179PhyReadyRetryPeriod = 50;
static const DWORD Ax88179PhyBmcrSettlePeriod = 60;
static const USHORT Ax88179PhyBmcrClearMask = 0xB3FF;
static const USHORT Ax88179PhyAdvertisement = 0x0DE1;
static const USHORT Ax88179PhyGigabitAdvertisement = MII_CTRL1000_FULL;
static const DWORD Ax88179FamilyTransportErrorWindow = 10000;
static const DWORD Ax88179FamilyTransmitTimeout = 5000;
static const DWORD Ax88179FamilyFullReinitDrainTimeout = 1000;
static const DWORD Ax88179FamilyFullReinitFailureTimeout = 5000;
static const DWORD Ax88179FamilyRxDesyncEscalationDelay = 250;

enum AX88179_FAMILY_RECOVERY_LEVEL {
    Ax88179FamilyRecoveryIdle = 0,
    Ax88179FamilyRecoveryResetPipe,
    Ax88179FamilyRecoveryReinitialize,
    Ax88179FamilyRecoveryResetDevice
};

static BOOL Ax88179ShouldLogDiagnosticEvent(DWORD Count) {
    return Count <= 32 || (Count & (Count - 1)) == 0;
}

static const char* Ax88179ChipVersionName(BYTE Version) {
    switch (Version) {
        case AX88179_CHIP_VERSION_AX88179: return "AX88179";
        case AX88179_CHIP_VERSION_AX88179A: return "AX88179A/B or AX88772D/E generation";
        case AX88179_CHIP_VERSION_AX88279: return "AX88279";
        default: return "unknown";
    }
}

static VOID Ax88179IncrementEthernetAddress(CEnetAddr* Address) {
    PBYTE Bytes = reinterpret_cast<PBYTE>(Address);

    for (INT Index = static_cast<INT>(sizeof(CEnetAddr)) - 1; Index >= 0; Index--) {
        Bytes[Index]++;

        if (Bytes[Index] != 0)
            break;
    }
}

static const char* Ax88179InitStageName(DWORD Stage) {
    switch (Stage) {
        case Ax88179InitBeginning: return "Beginning";
        case Ax88179InitPowerDownPhy: return "PowerDownPhy";
        case Ax88179InitPowerUpPhy: return "PowerUpPhy";
        case Ax88179InitPausePhy: return "PausePhy";
        case Ax88179InitEnableClocks: return "EnableClocks";
        case Ax88179InitPauseClock: return "PauseClock";
        case Ax88179InitReadNodeId: return "ReadNodeId";
        case Ax88179InitWaitingForEthernetAddress: return "WaitingForEthernetAddress";
        case Ax88179InitWriteNodeId: return "WriteNodeId";
        case Ax88179InitConfigureBulkIn: return "ConfigureBulkIn";
        case Ax88179InitWritePauseWaterLow: return "WritePauseWaterLow";
        case Ax88179InitWritePauseWaterHigh: return "WritePauseWaterHigh";
        case Ax88179InitEnableRxChecksum: return "EnableRxChecksum";
        case Ax88179InitEnableTxChecksum: return "EnableTxChecksum";
        case Ax88179InitStartReceiving: return "StartReceiving";
        case Ax88179InitWriteMonitorMode: return "WriteMonitorMode";
        case Ax88179InitWriteMediumMode: return "WriteMediumMode";
        case Ax88179InitWaitPhyReady: return "WaitPhyReady";
        case Ax88179InitPhyWriteMmdAccess1: return "PhyWriteMmdAccess1";
        case Ax88179InitPhyWriteMmdData1: return "PhyWriteMmdData1";
        case Ax88179InitPhyWriteMmdAccess2: return "PhyWriteMmdAccess2";
        case Ax88179InitPhyWriteMmdData2: return "PhyWriteMmdData2";
        case Ax88179InitPhyReadBmcr: return "PhyReadBmcr";
        case Ax88179InitPhyClearBmcr: return "PhyClearBmcr";
        case Ax88179InitPhyPauseAfterBmcrClear: return "PhyPauseAfterBmcrClear";
        case Ax88179InitPhyWriteAdvertisement: return "PhyWriteAdvertisement";
        case Ax88179InitPhyWriteGigabitAdvertisement: return "PhyWriteGigabitAdvertisement";
        case Ax88179InitRestartAutonegotiation: return "RestartAutonegotiation";
        case Ax88179InitReady: return "Ready";
        case Ax88179InitComplete: return "Complete";
        default: return "Unknown";
    }
}

static const char* Ax88179LinkResetStageName(BYTE Stage) {
    switch (Stage) {
        case Ax88179LinkResetIdle: return "Idle";
        case Ax88179LinkResetStopReceive: return "StopReceive";
        case Ax88179LinkResetStartReceive: return "StartReceive";
        case Ax88179LinkResetReadTxFifo: return "ReadTxFifo";
        case Ax88179LinkResetWriteBulkIn: return "WriteBulkIn";
        case Ax88179LinkResetWriteMedium: return "WriteMedium";
        case Ax88179LinkResetReadRxControl: return "ReadRxControl";
        case Ax88179LinkResetReadMedium: return "ReadMedium";
        case Ax88179LinkResetReadBulkIn: return "ReadBulkIn";
        case Ax88179LinkResetReadNodeId: return "ReadNodeId";
        case Ax88179LinkResetReadClock: return "ReadClock";
        case Ax88179LinkResetReadPhyPower: return "ReadPhyPower";
        case Ax88179LinkResetReadMonitor: return "ReadMonitor";
        case Ax88179LinkResetReadRxChecksum: return "ReadRxChecksum";
        case Ax88179LinkResetReadTxChecksum: return "ReadTxChecksum";
        case Ax88179LinkResetReadChipStatus: return "ReadChipStatus";
        default: return "Unknown";
    }
}

const char* CAx88179::GetName() const {
    return "AX88179";
}

USHORT CAx88179::GetProductIdRaw() const {
    return 0x9017;
}

BOOL CAx88179::SupportsTransmitAggregation() const {
    /* The original AX88179 TX format is one 8-byte descriptor followed by one
     * Ethernet frame per USB bulk transfer.  Multi-record TX aggregation was
     * experimental and becomes timing-sensitive when normal diagnostics are
     * disabled, corrupting Netshare/XNet connections while USB still reports
     * successful completions.  Keep multiple USB requests in flight, but do
     * not place multiple Ethernet frames in a single AX88179 transfer. */
    return FALSE;
}

const char* CAx88178A::GetName() const {
    return "AX88178A";
}

USHORT CAx88178A::GetProductIdRaw() const {
    return 0x8A17;
}

USHORT CAx88179_178A::GetVendorIdRaw() const {
    return 0x950B;
}

BOOL CAx88179_178A::IsImplemented() const {
    return TRUE;
}

BOOL CAx88179_178A::SupportsTransmitAggregation() const {
    return FALSE;
}

DWORD CAx88179_178A::GetMaximumFrameSize() const {
    return 0x0FFC;
}

DWORD CAx88179_178A::GetMaximumAggregateTransferSize() const {
    return XmitBufferSize - 3;
}

DWORD CAx88179_178A::GetTransmitHeaderSize() const {
    return sizeof(DWORD) * 2;
}

DWORD CAx88179_178A::GetTransmitTerminatorSize() const {
    return 0;
}

BOOL CAx88179_178A::IsReady(CUsbEnet* Device) {
    return Device->InitStage == Ax88179InitComplete;
}

BOOL CAx88179_178A::IsNodeIdAvailable(CUsbEnet* Device) {
    UNREFERENCED_PARAMETER(Device);
    return m_NodeIdValid;
}

VOID CAx88179_178A::OnUnicastAddressChanged(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);
    const BYTE* Unicast = reinterpret_cast<const BYTE*>(&Device->UnicastAddress);
    const BYTE* Alternate = reinterpret_cast<const BYTE*>(&Device->AlternateUnicastAddress);
    DbgPrint("[usbenet]: %s unicast update stage=%u (%s) title=%02X:%02X:%02X:%02X:%02X:%02X debug=%02X:%02X:%02X:%02X:%02X:%02X filter=0x%08X.\n", g_UsbEnetChipset->GetName(), Device->InitStage, Ax88179InitStageName(Device->InitStage), Unicast[0], Unicast[1], Unicast[2], Unicast[3], Unicast[4], Unicast[5], Alternate[0], Alternate[1], Alternate[2], Alternate[3], Alternate[4], Alternate[5], Device->AggregateReceiveFilter);

    if (Device->InitStage == Ax88179InitWaitingForEthernetAddress)
        AdvanceInitStage(Device);
    else if (Device->InitStage > Ax88179InitWaitingForEthernetAddress && Device->InitStage <= Ax88179InitComplete)
        WriteNodeId(Device);
}

VOID CAx88179_178A::ResetState(CUsbEnet* Device) {
    Device->InitStage = Ax88179InitBeginning;
    Device->PhyAddress = Ax88179PhyAddress;
    m_NodeIdValid = FALSE;
    m_NotifyLinkAfterMediumWrite = FALSE;
    m_LinkResetInProgress = FALSE;
    m_RxRecoveryInProgress = FALSE;
    m_RxHardRecoveryPending = FALSE;
    m_LinkResetStage = Ax88179LinkResetIdle;
    m_LinkResetRetries = 0;
    m_ReceiveControl = 0;
    m_LastPhyStatus = 0;
    m_LinkResetLinkState = 0;
    m_InterruptStatusSeen = FALSE;
    m_InterruptLinkUp = FALSE;
    m_InterruptPhyRefreshPending = FALSE;
    m_InterruptEventCount = 0;
    m_RxAggregateSequence = 0;
    m_RxInvalidAggregateSequence = 0;
    m_RxInvalidAggregateStreak = 0;
    m_RxInvalidAggregateSignature = 0;
    m_RxRecoveryStartTick = 0;
    m_RxHardRecoveryLinkGraceTick = 0;
    m_RxHardRecoveryCount = 0;
    m_RxDescriptorSequence = 0;
    m_RxDiscardDescriptorSequence = 0;
    m_RxFrameSequence = 0;
    m_TxFrameSequence = 0;
    m_PhyReadSequence = 0;
    m_PhyWriteSequence = 0;
    m_PhyReadyRetryCount = 0;
    m_PhyReadyRetryPending = FALSE;
    m_PhyInitBmcr = 0;
    m_RecoveryLevel = Ax88179FamilyRecoveryIdle;
    m_RecoveryPipe = UsbEnetTransportReceive;
    m_FullReinitializePending = FALSE;
    m_FullReinitializeActive = FALSE;
    m_RecoveryEndpointsReset = FALSE;
    m_DeviceResetPending = FALSE;
    m_RecoveryReason = 0;
    m_RecoveryCount = 0;
    m_RecoveryRequestTick = 0;
    memset(m_RecoveryErrorCount, 0, sizeof(m_RecoveryErrorCount));
    memset(m_RecoveryLastErrorTick, 0, sizeof(m_RecoveryLastErrorTick));
    m_TxOutstandingSinceTick = 0;
    m_TxTimeoutCount = 0;
}

NTSTATUS CAx88179_178A::QueueMacRead(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length) {
    return Device->QueueControlTransfer(CompletionRoutine, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_ACCESS_MAC, Register, Length, Length, NULL);
}

NTSTATUS CAx88179_178A::QueueMacWrite8(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, BYTE Value) {
    return QueueMacWrite(Device, CompletionRoutine, Register, sizeof(Value), &Value);
}

NTSTATUS CAx88179_178A::QueueMacWrite16(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, USHORT Value) {
    USHORT WriteValue = _byteswap_ushort(Value);
    return QueueMacWrite(Device, CompletionRoutine, Register, sizeof(WriteValue), &WriteValue);
}

NTSTATUS CAx88179_178A::QueueMacWrite(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length, const PVOID Buffer) {
    return Device->QueueControlTransfer(CompletionRoutine, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_ACCESS_MAC, Register, Length, Length, Buffer);
}

USHORT CAx88179_178A::BuildReceiveControl(CUsbEnet* Device) const {
    USHORT ReceiveControl = AX88179_RX_CTL_DROPCRCERR | AX88179_RX_CTL_IPE | AX88179_RX_CTL_START | AX88179_RX_CTL_AP;

    if ((Device->AggregateReceiveFilter & 0xFF) != 0)
        ReceiveControl |= AX88179_RX_CTL_PRO;

    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_BROADCAST) != 0)
        ReceiveControl |= AX88179_RX_CTL_AB;

    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_MULTICAST) != 0)
        ReceiveControl |= AX88179_RX_CTL_AMALL;

    return ReceiveControl;
}

USHORT CAx88179_178A::BuildMediumMode(DWORD LinkState) const {
    USHORT MediumMode = AX88179_MEDIUM_RECEIVE_EN | AX88179_MEDIUM_TXFLOW_CTRLEN | AX88179_MEDIUM_RXFLOW_CTRLEN;

    if ((LinkState & NIC_LINK_STATE_1000_MBPS) != 0)
        MediumMode |= AX88179_MEDIUM_GIGAMODE | AX88179_MEDIUM_EN_125MHZ;
    else if ((LinkState & NIC_LINK_STATE_100_MBPS) != 0)
        MediumMode |= AX88179_MEDIUM_PS;

    if ((LinkState & NIC_LINK_STATE_FULL_DUPLEX) != 0)
        MediumMode |= AX88179_MEDIUM_FULL_DUPLEX;

    return MediumMode;
}

VOID CAx88179_178A::AdvanceInitStage(CUsbEnet* Device) {
    USBENET_LOG("Ax88179AdvanceInitStage Device=%p currentStage=%u", Device, Device->InitStage);
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        Device->InitStage = Ax88179InitComplete;
        return;
    }

    AX88179_INIT_STAGE NextStage = static_cast<AX88179_INIT_STAGE>(Device->InitStage + 1);
    TRAP_ASSERT(NextStage > Ax88179InitBeginning && NextStage < Ax88179InitStageCount);
    Device->InitStage = NextStage;
    const BYTE* Unicast = reinterpret_cast<const BYTE*>(&Device->UnicastAddress);
    const BYTE* Alternate = reinterpret_cast<const BYTE*>(&Device->AlternateUnicastAddress);
    DbgPrint("[usbenet]: %s init stage %u (%s) flags=0x%08X filter=0x%08X unicast=%02X:%02X:%02X:%02X:%02X:%02X alternate=%02X:%02X:%02X:%02X:%02X:%02X\n", g_UsbEnetChipset->GetName(), Device->InitStage, Ax88179InitStageName(Device->InitStage), Device->Flags, Device->AggregateReceiveFilter, Unicast[0], Unicast[1], Unicast[2], Unicast[3], Unicast[4], Unicast[5], Alternate[0], Alternate[1], Alternate[2], Alternate[3], Alternate[4], Alternate[5]);

    NTSTATUS Status = STATUS_SUCCESS;

    switch (NextStage) {
        case Ax88179InitPowerDownPhy:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineInitTransfer, AX88179_PHY_POWER_RESET, 0);
            break;

        case Ax88179InitPowerUpPhy:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineInitTransfer, AX88179_PHY_POWER_RESET, AX88179_PHY_POWER_IPRL);
            break;

        case Ax88179InitPausePhy:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 500);
            return;

        case Ax88179InitEnableClocks:
            CNicBase::NicBaseShutdown(Device);
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX88179_CLOCK_SELECT, AX88179_CLOCK_ACS | AX88179_CLOCK_BCS);
            break;

        case Ax88179InitPauseClock:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 200);
            return;

        case Ax88179InitReadNodeId:
            CNicBase::NicBaseShutdown(Device);
            Status = QueueMacRead(Device, AsyncCompletionRoutineReadNodeId, AX88179_NODE_ID, sizeof(CEnetAddr));
            break;

        case Ax88179InitWaitingForEthernetAddress: {
            CEnetAddr* TitleAddress = Main::GetKernelTitleEthernetAddress();
            CEnetAddr* DebugAddress = Main::GetKernelDebugEthernetAddress();
            BOOL TitleAddressValid = TitleAddress != NULL && !TitleAddress->IsZero() && !TitleAddress->IsMulticast();
            BOOL DebugAddressValid = DebugAddress != NULL && !DebugAddress->IsZero() && !DebugAddress->IsMulticast();

            if (TitleAddress != NULL) {
                const BYTE* TitleBytes = reinterpret_cast<const BYTE*>(TitleAddress);
                DbgPrint("[usbenet]: %s existing title MAC %02X:%02X:%02X:%02X:%02X:%02X valid=%u.\n", g_UsbEnetChipset->GetName(), TitleBytes[0], TitleBytes[1], TitleBytes[2], TitleBytes[3], TitleBytes[4], TitleBytes[5], TitleAddressValid);
            }

            if (Main::Devkit && DebugAddress != NULL) {
                const BYTE* DebugBytes = reinterpret_cast<const BYTE*>(DebugAddress);
                DbgPrint("[usbenet]: %s existing debug MAC %02X:%02X:%02X:%02X:%02X:%02X valid=%u.\n", g_UsbEnetChipset->GetName(), DebugBytes[0], DebugBytes[1], DebugBytes[2], DebugBytes[3], DebugBytes[4], DebugBytes[5], DebugAddressValid);
            }

            if (TitleAddressValid)
                memcpy(&Device->UnicastAddress, TitleAddress, sizeof(CEnetAddr));

            if (Main::Devkit) {
                if (DebugAddressValid) {
                    memcpy(&Device->AlternateUnicastAddress, DebugAddress, sizeof(CEnetAddr));
                } else if (TitleAddressValid) {
                    memcpy(&Device->AlternateUnicastAddress, TitleAddress, sizeof(CEnetAddr));
                    Ax88179IncrementEthernetAddress(&Device->AlternateUnicastAddress);
                }
            } else {
                Device->AlternateUnicastAddress.SetZero();
            }

            if (Device->UnicastAddress.IsZero()) {
                DbgPrint("[usbenet]: %s waiting for the %s title unicast address before programming AX_NODE_ID.\n", g_UsbEnetChipset->GetName(), Main::Devkit ? "checked/dev" : "17559 retail");
                return;
            }

            const BYTE* Primary = reinterpret_cast<const BYTE*>(&Device->UnicastAddress);
            if (Main::Devkit) {
                const BYTE* Alternate = reinterpret_cast<const BYTE*>(&Device->AlternateUnicastAddress);
                DbgPrint("[usbenet]: %s recovered primary MAC %02X:%02X:%02X:%02X:%02X:%02X and alternate MAC %02X:%02X:%02X:%02X:%02X:%02X.\n", g_UsbEnetChipset->GetName(), Primary[0], Primary[1], Primary[2], Primary[3], Primary[4], Primary[5], Alternate[0], Alternate[1], Alternate[2], Alternate[3], Alternate[4], Alternate[5]);
            } else {
                DbgPrint("[usbenet]: %s recovered retail title MAC %02X:%02X:%02X:%02X:%02X:%02X; no alternate MAC.\n", g_UsbEnetChipset->GetName(), Primary[0], Primary[1], Primary[2], Primary[3], Primary[4], Primary[5]);
            }
            AdvanceInitStage(Device);
            return;
        }

        case Ax88179InitWriteNodeId:
            WriteNodeId(Device);
            return;

        case Ax88179InitConfigureBulkIn:
            Status = QueueMacWrite(Device, AsyncCompletionRoutineInitTransfer, AX88179_RX_BULKIN_QCTRL, sizeof(Ax88179BulkInQueue), Ax88179BulkInQueue);
            break;

        case Ax88179InitWritePauseWaterLow:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX88179_PAUSE_WATERLVL_LOW, 0x34);
            break;

        case Ax88179InitWritePauseWaterHigh:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX88179_PAUSE_WATERLVL_HIGH, 0x52);
            break;

        case Ax88179InitEnableRxChecksum:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX88179_RX_CHECKSUM_CONTROL, Ax88179ChecksumControl);
            break;

        case Ax88179InitEnableTxChecksum:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX88179_TX_CHECKSUM_CONTROL, Ax88179ChecksumControl);
            break;

        case Ax88179InitStartReceiving:
            StartReceiving(Device);
            return;

        case Ax88179InitWriteMonitorMode:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX88179_MONITOR_MODE, Ax88179MonitorMode);
            break;

        case Ax88179InitWriteMediumMode:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineInitTransfer, AX88179_MEDIUM_STATUS_MODE, Ax88179DefaultMediumMode);
            break;

        case Ax88179InitWaitPhyReady:
            m_PhyReadyRetryCount = 0;
            m_PhyReadyRetryPending = FALSE;
            Status = BeginReadPhy(Device, 2);
            break;

        case Ax88179InitPhyWriteMmdAccess1:
            Status = BeginWritePhy(Device, 0x0D, 0x0007);
            break;

        case Ax88179InitPhyWriteMmdData1:
            Status = BeginWritePhy(Device, 0x0E, 0x003C);
            break;

        case Ax88179InitPhyWriteMmdAccess2:
            Status = BeginWritePhy(Device, 0x0D, 0x4007);
            break;

        case Ax88179InitPhyWriteMmdData2:
            Status = BeginWritePhy(Device, 0x0E, 0x0000);
            break;

        case Ax88179InitPhyReadBmcr:
            Status = BeginReadPhy(Device, MII_BMCR);
            break;

        case Ax88179InitPhyClearBmcr:
            Status = BeginWritePhy(Device, MII_BMCR, m_PhyInitBmcr);
            break;

        case Ax88179InitPhyPauseAfterBmcrClear:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, Ax88179PhyBmcrSettlePeriod);
            return;

        case Ax88179InitPhyWriteAdvertisement:
            Status = BeginWritePhy(Device, MII_ADVERTISE, Ax88179PhyAdvertisement);
            break;

        case Ax88179InitPhyWriteGigabitAdvertisement:
            Status = BeginWritePhy(Device, MII_CTRL1000, Ax88179PhyGigabitAdvertisement);
            break;

        case Ax88179InitRestartAutonegotiation:
            Status = BeginWritePhy(Device, MII_BMCR, static_cast<USHORT>(m_PhyInitBmcr | MII_BMCR_AUTONEG_ENABLE | MII_BMCR_RESTART_AUTONEG));
            break;

        case Ax88179InitReady:
            Device->LinkPollTick = KeTimeStampBundle->TickCount;
            Device->InitStage = Ax88179InitComplete;
            Device->StartInterruptLinkStatus();
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 200);
            if (m_FullReinitializeActive) {
                m_FullReinitializeActive = FALSE;
                for (DWORD Pipe = 0; Pipe < ARRAYSIZE(m_RecoveryErrorCount); Pipe++)
                    ResetRecoveryCounters(static_cast<USBENET_TRANSPORT_PIPE>(Pipe));
                m_TxTimeoutCount = 0;
                m_TxOutstandingSinceTick = 0;
                DbgPrint("[usbenet-recovery]: %s full hardware reinitialization completed reason=%u count=%u.\n", GetName(), m_RecoveryReason, m_RecoveryCount);
            }
            DbgPrint("[usbenet]: %s initialization complete; PDB-derived PHY initialization and interrupt-driven link monitoring enabled with a 5000 ms PHY fallback.\n", g_UsbEnetChipset->GetName());
            return;

        default:
            TRAP_ASSERT(FALSE);
            return;
    }

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s initialization stage %u could not queue control transfer (0x%08X); continuing.\n", g_UsbEnetChipset->GetName(), Device->InitStage, Status);
        AdvanceInitStage(Device);
    }
}

VOID CAx88179_178A::CompleteInitTransfer(CUsbEnet* Device, NTSTATUS Status) {
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    DbgPrint("[usbenet]: %s init completion stage=%u (%s) status=0x%08X bytesTransferred=%u.\n", g_UsbEnetChipset->GetName(), Device->InitStage, Ax88179InitStageName(Device->InitStage), Status, Device->ControlRequest.Transfer.BytesTransferred);

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s initialization transfer for stage %u failed with status 0x%08X; continuing.\n", g_UsbEnetChipset->GetName(), Device->InitStage, Status);

    if (Device->InitStage != Ax88179InitComplete)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88179_178A::CompleteReadNodeId(CUsbEnet* Device, NTSTATUS Status) {
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    m_NodeIdValid = FALSE;

    if (NT_SUCCESS(Status) && Device->ControlRequest.Transfer.BytesTransferred >= sizeof(CEnetAddr)) {
        memcpy(&Device->NodeId, reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize, sizeof(CEnetAddr));
        m_NodeIdValid = !Device->NodeId.IsZero() && !Device->NodeId.IsMulticast();
        PBYTE Address = reinterpret_cast<PBYTE>(&Device->NodeId);
        DbgPrint("[usbenet]: %s hardware address %02X:%02X:%02X:%02X:%02X:%02X.\n", g_UsbEnetChipset->GetName(), Address[0], Address[1], Address[2], Address[3], Address[4], Address[5]);
    } else {
        DbgPrint("[usbenet]: %s node ID read failed with status 0x%08X and %u bytes.\n", g_UsbEnetChipset->GetName(), Status, Device->ControlRequest.Transfer.BytesTransferred);
    }

    AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88179_178A::WriteNodeId(CUsbEnet* Device) {
    const CEnetAddr* Address = NULL;

    if (!Device->UnicastAddress.IsZero())
        Address = &Device->UnicastAddress;
    else if (!Device->AlternateUnicastAddress.IsZero())
        Address = &Device->AlternateUnicastAddress;

    if (Address == NULL) {
        if (Device->InitStage != Ax88179InitComplete)
            AdvanceInitStage(Device);
        return;
    }

    const BYTE* Bytes = reinterpret_cast<const BYTE*>(Address);
    DbgPrint("[usbenet]: %s programming receive MAC %02X:%02X:%02X:%02X:%02X:%02X at stage %u.\n", g_UsbEnetChipset->GetName(), Bytes[0], Bytes[1], Bytes[2], Bytes[3], Bytes[4], Bytes[5], Device->InitStage);
    NTSTATUS Status = QueueMacWrite(Device, AsyncCompletionRoutineWriteNodeId, AX88179_NODE_ID, sizeof(CEnetAddr), const_cast<CEnetAddr*>(Address));

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s could not queue node ID write (0x%08X).\n", g_UsbEnetChipset->GetName(), Status);
        if (Device->InitStage != Ax88179InitComplete)
            AdvanceInitStage(Device);
    }
}

VOID CAx88179_178A::CompleteWriteNodeId(CUsbEnet* Device, NTSTATUS Status) {
    BOOL ReadbackQueued = FALSE;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s node ID write failed with status 0x%08X.\n", g_UsbEnetChipset->GetName(), Status);
    } else if (Device->InitStage == Ax88179InitWriteNodeId) {
        NTSTATUS ReadStatus = QueueMacRead(Device, AsyncCompletionRoutineReadNodeId, AX88179_NODE_ID, sizeof(CEnetAddr));
        if (NT_SUCCESS(ReadStatus))
            ReadbackQueued = TRUE;
        else
            DbgPrint("[usbenet]: %s could not queue node ID readback (0x%08X).\n", g_UsbEnetChipset->GetName(), ReadStatus);
    }

    if (!ReadbackQueued && Device->InitStage != Ax88179InitComplete)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88179_178A::StartReceiving(CUsbEnet* Device) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    m_RxRecoveryInProgress = FALSE;
    m_RxInvalidAggregateStreak = 0;
    m_RxInvalidAggregateSignature = 0;
    Device->Flags |= ReceiveRunning;
    Device->SubmitReceive();
    m_ReceiveControl = BuildReceiveControl(Device);
    DbgPrint("[usbenet]: %s starting receive RX_CTL=0x%04X aggregateFilter=0x%08X.\n", g_UsbEnetChipset->GetName(), m_ReceiveControl, Device->AggregateReceiveFilter);

    NTSTATUS Status = QueueMacWrite16(Device, (PUSBD_ASYNC_COMPLETION_ROUTINE)CUsbEnet::AsyncCompletionRoutineStartReceiving, AX88179_RX_CTL, m_ReceiveControl);
    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s could not queue RX control write (0x%08X).\n", g_UsbEnetChipset->GetName(), Status);
}

VOID CAx88179_178A::UpdateReceiveFilter(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Device->InitStage < Ax88179InitStartReceiving || (Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    StartReceiving(Device);
}

VOID CAx88179_178A::RestartReceiving(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || (Device->Flags & ReceiveRunning) == 0)
        return;

    if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0) {
        m_RxRecoveryInProgress = FALSE;
        DbgPrint("[usbenet]: [RECOVERY] %s RX restart deferred because the control endpoint is busy.\n", g_UsbEnetChipset->GetName());
        return;
    }

    Device->Flags &= ~ReceiveRunning;
    Device->NoteReceiveRestart();

    for (ULONG Index = 0; Index < RECV_PACKET_COUNT; Index++) {
        PRECV_TRANSFER Packet = &Device->RecvPackets[Index];
        if (Packet->InFlight != 0)
            UsbdCancelAsyncTransfer(&Packet->Transfer);
    }

    NTSTATUS Status = QueueMacWrite16(Device, (PUSBD_ASYNC_COMPLETION_ROUTINE)CUsbEnet::AsyncCompletionRoutineStopReceiving, AX88179_RX_CTL, AX88179_RX_CTL_STOP);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s could not queue RX stop (0x%08X); receive completions will restore submissions.\n", g_UsbEnetChipset->GetName(), Status);
        Device->Flags |= ReceiveRunning;
    }
}

VOID CAx88179_178A::RequestHardReceiveRecovery(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (m_FullReinitializePending || m_FullReinitializeActive || (Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    ++m_RxHardRecoveryCount;
    DWORD Reason = 0x500 + m_RxHardRecoveryCount;
    NTSTATUS Status = RequestFullReinitialize(Device, Reason);
    if (NT_SUCCESS(Status)) {
        m_RxHardRecoveryPending = FALSE;
        DbgPrint("[usbenet-recovery]: %s stale RX drain escalated to the unified full-reinitialize path attempt=%u.\n", GetName(), m_RxHardRecoveryCount);
    } else {
        m_RxHardRecoveryPending = TRUE;
        Device->Flags &= ~ReceiveRunning;
        DbgPrint("[usbenet-recovery]: %s could not start full reinitialization yet (0x%08X); retrying from the timer.\n", GetName(), Status);
    }
}

VOID CAx88179_178A::ResetRecoveryCounters(USBENET_TRANSPORT_PIPE Pipe) {
    DWORD Index = static_cast<DWORD>(Pipe);
    if (Index < ARRAYSIZE(m_RecoveryErrorCount)) {
        m_RecoveryErrorCount[Index] = 0;
        m_RecoveryLastErrorTick[Index] = 0;
    }
}

VOID CAx88179_178A::HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe) {
    if (Pipe == UsbEnetTransportTransmit && Device->PendingXmitCount == 1)
        m_TxOutstandingSinceTick = KeTimeStampBundle->TickCount;
}

VOID CAx88179_178A::HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || Status == STATUS_CANCELLED || Status == STATUS_DEVICE_REMOVED || Status == STATUS_DEVICE_NOT_CONNECTED)
        return;

    DWORD Index = static_cast<DWORD>(Pipe);
    if (Index >= ARRAYSIZE(m_RecoveryErrorCount))
        return;

    DWORD CurrentTick = KeTimeStampBundle->TickCount;
    if (Pipe == UsbEnetTransportTransmit && Device->PendingXmitCount <= 1) {
        m_TxOutstandingSinceTick = 0;
        if (NT_SUCCESS(Status))
            m_TxTimeoutCount = 0;
    }

    if (NT_SUCCESS(Status)) {
        if (m_RecoveryLastErrorTick[Index] != 0 && CurrentTick - m_RecoveryLastErrorTick[Index] > Ax88179FamilyTransportErrorWindow)
            ResetRecoveryCounters(Pipe);
        return;
    }

    if (m_RecoveryLastErrorTick[Index] == 0 || CurrentTick - m_RecoveryLastErrorTick[Index] > Ax88179FamilyTransportErrorWindow)
        m_RecoveryErrorCount[Index] = 0;
    m_RecoveryLastErrorTick[Index] = CurrentTick;
    DWORD Count = ++m_RecoveryErrorCount[Index];
    BYTE Level = Count >= 5 ? Ax88179FamilyRecoveryResetDevice : Count >= 3 ? Ax88179FamilyRecoveryReinitialize : Ax88179FamilyRecoveryResetPipe;

    if (Level > m_RecoveryLevel) {
        m_RecoveryLevel = Level;
        m_RecoveryPipe = static_cast<BYTE>(Pipe);
        m_RecoveryRequestTick = CurrentTick;
        m_RecoveryReason = 0x100 + (Index << 8) + Count;
        DbgPrint("[usbenet-recovery]: %s transport error pipe=%u status=0x%08X count=%u level=%u.\n", GetName(), Index, Status, Count, static_cast<DWORD>(Level));
    }
}

NTSTATUS CAx88179_178A::RequestFullReinitialize(CUsbEnet* Device, DWORD Reason) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || Device->InitStage != Ax88179InitComplete)
        return STATUS_DEVICE_NOT_READY;
    if (m_FullReinitializePending || m_FullReinitializeActive)
        return STATUS_DEVICE_BUSY;

    m_RxHardRecoveryPending = FALSE;
    m_FullReinitializePending = TRUE;
    m_RecoveryEndpointsReset = FALSE;
    m_RecoveryReason = Reason;
    m_RecoveryRequestTick = KeTimeStampBundle->TickCount;
    ++m_RecoveryCount;
    Device->PauseInterruptLinkStatus();
    Device->Flags &= ~ReceiveRunning;

    for (DWORD Index = 0; Index < RECV_PACKET_COUNT; Index++) {
        if (Device->RecvPackets[Index].InFlight != 0)
            UsbdCancelAsyncTransfer(&Device->RecvPackets[Index].Transfer);
    }
    for (DWORD Index = 0; Index < XMIT_TRACKER_COUNT; Index++) {
        if ((Device->XmitTrackers[Index].Flags & XMIT_FLAG_BUSY) != 0)
            UsbdCancelAsyncTransfer(&Device->XmitTrackers[Index].Transfer);
    }

    DbgPrint("[usbenet-recovery]: %s full hardware reinitialization requested reason=%u count=%u RX=%u TX=%u.\n", GetName(), Reason, m_RecoveryCount, Device->GetReceiveInFlightCount(), Device->PendingXmitCount);
    return STATUS_SUCCESS;
}

VOID CAx88179_178A::ProcessRecovery(CUsbEnet* Device, DWORD CurrentTick) {
    if (m_RecoveryLevel != Ax88179FamilyRecoveryIdle && !m_FullReinitializePending && !m_FullReinitializeActive && !m_RxHardRecoveryPending) {
        BYTE Level = m_RecoveryLevel;
        USBENET_TRANSPORT_PIPE Pipe = static_cast<USBENET_TRANSPORT_PIPE>(m_RecoveryPipe);
        DWORD Reason = m_RecoveryReason;
        m_RecoveryLevel = Ax88179FamilyRecoveryIdle;

        if (Level == Ax88179FamilyRecoveryResetPipe) {
            Device->ResetTransportEndpoint(Pipe);
            if (Pipe == UsbEnetTransportReceive && (Device->Flags & ReceiveRunning) != 0)
                RestartReceiving(Device);
            DbgPrint("[usbenet-recovery]: %s reset transport endpoint %u%s.\n", GetName(), static_cast<DWORD>(Pipe), Pipe == UsbEnetTransportReceive ? " and restarted the RX engine" : "");
        } else if (Level == Ax88179FamilyRecoveryReinitialize) {
            RequestFullReinitialize(Device, Reason);
        } else {
            Device->PauseInterruptLinkStatus();
            Device->Flags &= ~ReceiveRunning;
            Device->LinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
            m_DeviceResetPending = TRUE;
            DbgPrint("[usbenet-recovery]: %s scheduling USB device reset after repeated transport failures.\n", GetName());
        }
    }

    if (m_RxHardRecoveryPending && !m_FullReinitializePending && !m_FullReinitializeActive) {
        m_RxHardRecoveryPending = FALSE;
        RequestHardReceiveRecovery(Device);
    }

    if (!m_FullReinitializePending)
        return;

    DWORD Age = CurrentTick - m_RecoveryRequestTick;
    if (Age >= Ax88179FamilyFullReinitDrainTimeout) {
        if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0)
            UsbdCancelAsyncTransfer(&Device->ControlRequest.Transfer);
        Device->ResetTransportEndpoint(UsbEnetTransportReceive);
        Device->ResetTransportEndpoint(UsbEnetTransportTransmit);
        Device->ResetTransportEndpoint(UsbEnetTransportInterrupt);
        if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0)
            Device->Flags &= ~(USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS);
    }

    BOOL DrainBlocked = Device->GetReceiveInFlightCount() != 0 || Device->PendingXmitCount != 0 || Device->IsInterruptLinkStatusInFlight() || (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0;
    if (DrainBlocked) {
        if (Age >= Ax88179FamilyFullReinitFailureTimeout) {
            DbgPrint("[usbenet-recovery]: %s reinitialize drain timed out after %u ms RX=%u TX=%u interrupt=%u flags=0x%08X; escalating to USB device reset.\n", GetName(), Age, Device->GetReceiveInFlightCount(), Device->PendingXmitCount, Device->IsInterruptLinkStatusInFlight(), Device->Flags);
            m_FullReinitializePending = FALSE;
            m_FullReinitializeActive = FALSE;
            m_DeviceResetPending = TRUE;
        }
        return;
    }

    if (!m_RecoveryEndpointsReset) {
        Device->ResetTransportEndpoint(UsbEnetTransportReceive);
        Device->ResetTransportEndpoint(UsbEnetTransportTransmit);
        Device->ResetTransportEndpoint(UsbEnetTransportInterrupt);
        if (Device->PhysicalMemory != NULL)
            memset(Device->PhysicalMemory, 0, RECV_PACKET_COUNT * ReceiveBufferSize);
        m_RecoveryEndpointsReset = TRUE;
        DbgPrint("[usbenet-recovery]: %s all transports are quiescent; reset endpoint data toggles and cleared RX DMA before hardware replay.\n", GetName());
    }

    DWORD PreservedLinkState = Device->LinkState;
    DWORD Reason = m_RecoveryReason;
    DWORD Count = m_RecoveryCount;
    DWORD RequestTick = m_RecoveryRequestTick;
    ResetState(Device);
    m_FullReinitializePending = FALSE;
    m_FullReinitializeActive = TRUE;
    m_RecoveryReason = Reason;
    m_RecoveryCount = Count;
    m_RecoveryRequestTick = RequestTick;
    m_RxHardRecoveryLinkGraceTick = CurrentTick;
    Device->Flags &= ~(ReceiveRunning | USBENET_STATE_00080000 | USBENET_STATE_NOTIFY_LINK_STATE | USBENET_STATE_REFRESH_PHY_REGISTERS | USBENET_STATE_READ_ALL_PHY_REGISTERS | USBENET_STATE_LINK_STATE_UPDATE_PENDING);
    Device->LinkState = PreservedLinkState;
    Device->TimerTick = CurrentTick;
    Device->InitStage = Ax88179InitBeginning;
    DbgPrint("[usbenet-recovery]: %s transports fully drained and flushed; replaying the PDB-derived PHY sequence, clocks, MAC, bulk-in, medium, interrupt, and RX initialization.\n", GetName());
    AdvanceInitStage(Device);
}

NTSTATUS CAx88179_178A::BeginReadPhy(CUsbEnet* Device, USHORT Register) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return STATUS_DEVICE_REMOVED;

    if ((Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return STATUS_DEVICE_BUSY;

    Device->CurrentPhyRegister = Register;
    Device->CurrentPhyValue = 0;
    Device->Flags |= USBENET_STATE_PHY_READ_IN_PROGRESS;

    ++m_PhyReadSequence;
    NTSTATUS Status = Device->QueueControlTransfer(AsyncCompletionRoutineReadPhy, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_ACCESS_PHY, Ax88179PhyAddress, Register, sizeof(USHORT), NULL);
    if (!NT_SUCCESS(Status))
        Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;

    if (!NT_SUCCESS(Status) || Ax88179ShouldLogDiagnosticEvent(m_PhyReadSequence))
        DbgPrint("[usbenet]: %s PHY read queue #%u register=0x%04X status=0x%08X flags=0x%08X.\n", g_UsbEnetChipset->GetName(), m_PhyReadSequence, Register, Status, Device->Flags);

    return Status;
}

NTSTATUS CAx88179_178A::BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return STATUS_DEVICE_REMOVED;

    if ((Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return STATUS_DEVICE_BUSY;

    Device->CurrentPhyRegister = Register;
    Device->CurrentPhyValue = Value;
    Device->Flags |= USBENET_STATE_PHY_WRITE_IN_PROGRESS;
    USHORT WriteValue = _byteswap_ushort(Value);

    ++m_PhyWriteSequence;
    NTSTATUS Status = Device->QueueControlTransfer(AsyncCompletionRoutineWritePhy, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_ACCESS_PHY, Ax88179PhyAddress, Register, sizeof(WriteValue), &WriteValue);
    if (!NT_SUCCESS(Status))
        Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;

    DbgPrint("[usbenet]: %s PHY write queue #%u register=0x%04X value=0x%04X wire=%02X %02X status=0x%08X flags=0x%08X.\n", g_UsbEnetChipset->GetName(), m_PhyWriteSequence, Register, Value, reinterpret_cast<PBYTE>(&WriteValue)[0], reinterpret_cast<PBYTE>(&WriteValue)[1], Status, Device->Flags);
    return Status;
}

BOOL CAx88179_178A::UpdateLinkState(CUsbEnet* Device, USHORT PhyStatus) {
    DWORD CurrentTick = KeTimeStampBundle->TickCount;
    BOOL LinkActive = (PhyStatus & AX88179_PHY_STATUS_LINK) != 0;
    DWORD NewLinkState = 0;

    /* A full RX recovery power-cycles the PHY. Do not publish that expected,
     * temporary link-down to XAM or it will reset every socket while the USB
     * adapter is still recovering. Publish a real down state after five
     * seconds if the link does not return. */
    if (m_RxHardRecoveryLinkGraceTick != 0) {
        if (!LinkActive && CurrentTick - m_RxHardRecoveryLinkGraceTick < 5000) {
            m_LastPhyStatus = PhyStatus;
            return FALSE;
        }

        m_RxHardRecoveryLinkGraceTick = 0;
    }

    if (LinkActive) {
        NewLinkState |= NIC_LINK_STATE_ACTIVE | NIC_LINK_STATE_NEGOTIATION_COMPLETE | NIC_LINK_STATE_TX_FLOW_CONTROL;

        if ((PhyStatus & AX88179_PHY_STATUS_SPEED_MASK) == AX88179_PHY_STATUS_GIGABIT)
            NewLinkState |= NIC_LINK_STATE_1000_MBPS;
        else if ((PhyStatus & AX88179_PHY_STATUS_SPEED_MASK) == AX88179_PHY_STATUS_100)
            NewLinkState |= NIC_LINK_STATE_100_MBPS;
        else
            NewLinkState |= NIC_LINK_STATE_10_MBPS;

        if ((PhyStatus & AX88179_PHY_STATUS_FULL_DUPLEX) != 0)
            NewLinkState |= NIC_LINK_STATE_FULL_DUPLEX;
        else
            NewLinkState |= NIC_LINK_STATE_HALF_DUPLEX;
    } else if (CurrentTick - Device->LinkPollTick >= 3500) {
        NewLinkState |= NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    }

    if (NewLinkState == Device->LinkState)
        return FALSE;

    Device->LinkState = NewLinkState;
    Device->LinkPollTick = KeTimeStampBundle->TickCount;
    m_LastPhyStatus = PhyStatus;

    DbgPrint("[usbenet]: %s link state=0x%08X PHY status=0x%04X.\n", g_UsbEnetChipset->GetName(), NewLinkState, PhyStatus);

    if ((NewLinkState & NIC_LINK_STATE_ACTIVE) == 0)
        return TRUE;

    return !StartLinkReset(Device, NewLinkState);
}

BOOL CAx88179_178A::StartLinkReset(CUsbEnet* Device, DWORD LinkState) {
    if (m_LinkResetInProgress)
        return TRUE;

    m_LinkResetInProgress = TRUE;
    m_LinkResetStage = Ax88179LinkResetStopReceive;
    m_LinkResetRetries = 0;
    m_LinkResetLinkState = LinkState;
    DbgPrint("[usbenet]: %s beginning link-up RX/TX FIFO reset.\n", g_UsbEnetChipset->GetName());

    if (QueueNextLinkResetTransfer(Device))
        return TRUE;

    m_LinkResetInProgress = FALSE;
    m_LinkResetStage = Ax88179LinkResetIdle;
    DbgPrint("[usbenet]: %s could not start link-up reset sequence.\n", g_UsbEnetChipset->GetName());
    return FALSE;
}

BOOL CAx88179_178A::QueueNextLinkResetTransfer(CUsbEnet* Device) {
    NTSTATUS Status = STATUS_SUCCESS;

    switch (m_LinkResetStage) {
        case Ax88179LinkResetStopReceive:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, AX88179_RX_CTL, AX88179_RX_CTL_STOP);
            break;

        case Ax88179LinkResetStartReceive:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, AX88179_RX_CTL, m_ReceiveControl);
            break;

        case Ax88179LinkResetReadTxFifo:
            Status = Device->QueueControlTransfer(AsyncCompletionRoutineLinkReset, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, Ax88179ReadTxFifoRequest, Ax88179ReadTxFifoValue, 0, sizeof(DWORD), NULL);
            break;

        case Ax88179LinkResetWriteBulkIn:
            Status = QueueMacWrite(Device, AsyncCompletionRoutineLinkReset, AX88179_RX_BULKIN_QCTRL, sizeof(Ax88179BulkInQueue), Ax88179BulkInQueue);
            break;

        case Ax88179LinkResetWriteMedium:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, AX88179_MEDIUM_STATUS_MODE, BuildMediumMode(m_LinkResetLinkState));
            break;

        case Ax88179LinkResetReadRxControl:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_RX_CTL, sizeof(USHORT));
            break;

        case Ax88179LinkResetReadMedium:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_MEDIUM_STATUS_MODE, sizeof(USHORT));
            break;

        case Ax88179LinkResetReadBulkIn:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_RX_BULKIN_QCTRL, sizeof(Ax88179BulkInQueue));
            break;

        case Ax88179LinkResetReadNodeId:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_NODE_ID, sizeof(CEnetAddr));
            break;

        case Ax88179LinkResetReadClock:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_CLOCK_SELECT, sizeof(BYTE));
            break;

        case Ax88179LinkResetReadPhyPower:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_PHY_POWER_RESET, sizeof(USHORT));
            break;

        case Ax88179LinkResetReadMonitor:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_MONITOR_MODE, sizeof(BYTE));
            break;

        case Ax88179LinkResetReadRxChecksum:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_RX_CHECKSUM_CONTROL, sizeof(BYTE));
            break;

        case Ax88179LinkResetReadTxChecksum:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_TX_CHECKSUM_CONTROL, sizeof(BYTE));
            break;

        case Ax88179LinkResetReadChipStatus:
            Status = QueueMacRead(Device, AsyncCompletionRoutineLinkReset, AX88179_CHIP_STATUS, sizeof(BYTE));
            break;

        default:
            return FALSE;
    }

    DbgPrint("[usbenet]: %s link reset queue stage=%u (%s) status=0x%08X flags=0x%08X RX_CTL=0x%04X link=0x%08X\n", g_UsbEnetChipset->GetName(), m_LinkResetStage, Ax88179LinkResetStageName(m_LinkResetStage), Status, Device->Flags, m_ReceiveControl, m_LinkResetLinkState);

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s link-up reset stage %u could not queue transfer (0x%08X).\n", g_UsbEnetChipset->GetName(), m_LinkResetStage, Status);
        return FALSE;
    }

    return TRUE;
}

VOID CAx88179_178A::LogLinkResetReadback(CUsbEnet* Device, BYTE Stage, NTSTATUS Status) {
    BYTE Data[6] = { 0, 0, 0, 0, 0, 0 };
    DWORD BytesTransferred = Device->ControlRequest.Transfer.BytesTransferred;

    if (NT_SUCCESS(Status) && BytesTransferred != 0)
        memcpy(Data, reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize, BytesTransferred < sizeof(Data) ? BytesTransferred : sizeof(Data));

    DbgPrint("[usbenet]: %s register readback stage=%u (%s) status=0x%08X bytes=%u data=%02X %02X %02X %02X %02X %02X\n", g_UsbEnetChipset->GetName(), Stage, Ax88179LinkResetStageName(Stage), Status, BytesTransferred, Data[0], Data[1], Data[2], Data[3], Data[4], Data[5]);

    if (!NT_SUCCESS(Status))
        return;

    if (Stage == Ax88179LinkResetReadRxControl && BytesTransferred >= sizeof(USHORT))
        DbgPrint("[usbenet]: %s RX_CTL readback=0x%04X expected=0x%04X.\n", g_UsbEnetChipset->GetName(), _byteswap_ushort(*reinterpret_cast<UNALIGNED USHORT*>(Data)), m_ReceiveControl);
    else if (Stage == Ax88179LinkResetReadMedium && BytesTransferred >= sizeof(USHORT))
        DbgPrint("[usbenet]: %s MEDIUM_MODE readback=0x%04X expected=0x%04X.\n", g_UsbEnetChipset->GetName(), _byteswap_ushort(*reinterpret_cast<UNALIGNED USHORT*>(Data)), BuildMediumMode(m_LinkResetLinkState));
    else if (Stage == Ax88179LinkResetReadBulkIn && BytesTransferred >= sizeof(Ax88179BulkInQueue))
        DbgPrint("[usbenet]: %s BULKIN_QCTRL readback=%02X %02X %02X %02X %02X expected=%02X %02X %02X %02X %02X.\n", g_UsbEnetChipset->GetName(), Data[0], Data[1], Data[2], Data[3], Data[4], Ax88179BulkInQueue[0], Ax88179BulkInQueue[1], Ax88179BulkInQueue[2], Ax88179BulkInQueue[3], Ax88179BulkInQueue[4]);
    else if (Stage == Ax88179LinkResetReadNodeId && BytesTransferred >= sizeof(CEnetAddr))
        DbgPrint("[usbenet]: %s NODE_ID readback=%02X:%02X:%02X:%02X:%02X:%02X title=%02X:%02X:%02X:%02X:%02X:%02X debug=%02X:%02X:%02X:%02X:%02X:%02X.\n", g_UsbEnetChipset->GetName(), Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], reinterpret_cast<PBYTE>(&Device->UnicastAddress)[0], reinterpret_cast<PBYTE>(&Device->UnicastAddress)[1], reinterpret_cast<PBYTE>(&Device->UnicastAddress)[2], reinterpret_cast<PBYTE>(&Device->UnicastAddress)[3], reinterpret_cast<PBYTE>(&Device->UnicastAddress)[4], reinterpret_cast<PBYTE>(&Device->UnicastAddress)[5], reinterpret_cast<PBYTE>(&Device->AlternateUnicastAddress)[0], reinterpret_cast<PBYTE>(&Device->AlternateUnicastAddress)[1], reinterpret_cast<PBYTE>(&Device->AlternateUnicastAddress)[2], reinterpret_cast<PBYTE>(&Device->AlternateUnicastAddress)[3], reinterpret_cast<PBYTE>(&Device->AlternateUnicastAddress)[4], reinterpret_cast<PBYTE>(&Device->AlternateUnicastAddress)[5]);
    else if (Stage == Ax88179LinkResetReadClock && BytesTransferred >= sizeof(BYTE))
        DbgPrint("[usbenet]: %s CLOCK_SELECT readback=0x%02X expected=0x%02X.\n", g_UsbEnetChipset->GetName(), Data[0], AX88179_CLOCK_ACS | AX88179_CLOCK_BCS);
    else if (Stage == Ax88179LinkResetReadPhyPower && BytesTransferred >= sizeof(USHORT))
        DbgPrint("[usbenet]: %s PHY_POWER_RESET readback=0x%04X expectedBits=0x%04X.\n", g_UsbEnetChipset->GetName(), _byteswap_ushort(*reinterpret_cast<UNALIGNED USHORT*>(Data)), AX88179_PHY_POWER_IPRL);
    else if (Stage == Ax88179LinkResetReadMonitor && BytesTransferred >= sizeof(BYTE))
        DbgPrint("[usbenet]: %s MONITOR_MODE readback=0x%02X expected=0x%02X.\n", g_UsbEnetChipset->GetName(), Data[0], Ax88179MonitorMode);
    else if (Stage == Ax88179LinkResetReadRxChecksum && BytesTransferred >= sizeof(BYTE))
        DbgPrint("[usbenet]: %s RX_CHECKSUM readback=0x%02X expected=0x%02X.\n", g_UsbEnetChipset->GetName(), Data[0], Ax88179ChecksumControl);
    else if (Stage == Ax88179LinkResetReadTxChecksum && BytesTransferred >= sizeof(BYTE))
        DbgPrint("[usbenet]: %s TX_CHECKSUM readback=0x%02X expected=0x%02X.\n", g_UsbEnetChipset->GetName(), Data[0], Ax88179ChecksumControl);
    else if (Stage == Ax88179LinkResetReadChipStatus && BytesTransferred >= sizeof(BYTE)) {
        BYTE Version = static_cast<BYTE>((Data[0] >> 4) & 0x0F);
        DbgPrint("[usbenet]: %s CHIP_STATUS readback=0x%02X chipVersion=0x%X identification=%s.\n", g_UsbEnetChipset->GetName(), Data[0], Version, Ax88179ChipVersionName(Version));
        if (Version == AX88179_CHIP_VERSION_AX88179A)
            DbgPrint("[usbenet]: CHIP_STATUS identifies the AX179A generation; use bcdDevice 2.00 for AX88179A/B and 3.00 for AX88772D/E.\n");
    }
}

VOID CAx88179_178A::CompleteLinkResetTransfer(CUsbEnet* Device, NTSTATUS Status) {
    BOOL NotifyUsers = FALSE;
    BYTE CompletedStage = m_LinkResetStage;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    DbgPrint("[usbenet]: %s link reset completion stage=%u (%s) status=0x%08X bytesTransferred=%u.\n", g_UsbEnetChipset->GetName(), CompletedStage, Ax88179LinkResetStageName(CompletedStage), Status, Device->ControlRequest.Transfer.BytesTransferred);

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s link-up reset stage %u failed with status 0x%08X.\n", g_UsbEnetChipset->GetName(), CompletedStage, Status);

    if (CompletedStage >= Ax88179LinkResetReadRxControl)
        LogLinkResetReadback(Device, CompletedStage, Status);

    if (CompletedStage == Ax88179LinkResetReadTxFifo) {
        DWORD FifoStatus = 0;

        if (NT_SUCCESS(Status) && Device->ControlRequest.Transfer.BytesTransferred >= sizeof(DWORD))
            FifoStatus = _byteswap_ulong(*reinterpret_cast<UNALIGNED DWORD*>(reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize));

        if ((FifoStatus & Ax88179TxFifoBusy) != 0 && m_LinkResetRetries < Ax88179MaximumLinkResetRetries) {
            ++m_LinkResetRetries;
            DbgPrint("[usbenet]: %s TX FIFO busy status=0x%08X retry=%u.\n", g_UsbEnetChipset->GetName(), FifoStatus, m_LinkResetRetries);
            m_LinkResetStage = Ax88179LinkResetStopReceive;
        } else {
            if ((FifoStatus & Ax88179TxFifoBusy) != 0)
                DbgPrint("[usbenet]: %s TX FIFO remained busy after %u retries; continuing.\n", g_UsbEnetChipset->GetName(), m_LinkResetRetries);
            else
                DbgPrint("[usbenet]: %s TX FIFO ready status=0x%08X retries=%u.\n", g_UsbEnetChipset->GetName(), FifoStatus, m_LinkResetRetries);
            m_LinkResetStage = Ax88179LinkResetWriteBulkIn;
        }
    } else {
        switch (CompletedStage) {
            case Ax88179LinkResetStopReceive: m_LinkResetStage = Ax88179LinkResetStartReceive; break;
            case Ax88179LinkResetStartReceive: m_LinkResetStage = Ax88179LinkResetReadTxFifo; break;
            case Ax88179LinkResetWriteBulkIn: m_LinkResetStage = Ax88179LinkResetWriteMedium; break;
            case Ax88179LinkResetWriteMedium: m_LinkResetStage = Ax88179LinkResetReadRxControl; break;
            case Ax88179LinkResetReadRxControl: m_LinkResetStage = Ax88179LinkResetReadMedium; break;
            case Ax88179LinkResetReadMedium: m_LinkResetStage = Ax88179LinkResetReadBulkIn; break;
            case Ax88179LinkResetReadBulkIn: m_LinkResetStage = Ax88179LinkResetReadNodeId; break;
            case Ax88179LinkResetReadNodeId: m_LinkResetStage = Ax88179LinkResetReadClock; break;
            case Ax88179LinkResetReadClock: m_LinkResetStage = Ax88179LinkResetReadPhyPower; break;
            case Ax88179LinkResetReadPhyPower: m_LinkResetStage = Ax88179LinkResetReadMonitor; break;
            case Ax88179LinkResetReadMonitor: m_LinkResetStage = Ax88179LinkResetReadRxChecksum; break;
            case Ax88179LinkResetReadRxChecksum: m_LinkResetStage = Ax88179LinkResetReadTxChecksum; break;
            case Ax88179LinkResetReadTxChecksum: m_LinkResetStage = Ax88179LinkResetReadChipStatus; break;
            case Ax88179LinkResetReadChipStatus:
                m_LinkResetInProgress = FALSE;
                m_LinkResetStage = Ax88179LinkResetIdle;
                NotifyUsers = TRUE;
                DbgPrint("[usbenet]: %s link-up reset and register snapshot complete.\n", g_UsbEnetChipset->GetName());
                break;
            default:
                break;
        }
    }

    if (m_LinkResetInProgress && !QueueNextLinkResetTransfer(Device)) {
        m_LinkResetInProgress = FALSE;
        m_LinkResetStage = Ax88179LinkResetIdle;
        NotifyUsers = TRUE;
    }

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (NotifyUsers)
        NotifyLinkStateChanged(Device);
}

VOID CAx88179_178A::CompleteReadPhy(CUsbEnet* Device, NTSTATUS Status) {
    BOOL NotifyUsers = FALSE;
    BOOL AdvanceInitialization = FALSE;
    BOOL RetryPhyReady = FALSE;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;

    if (NT_SUCCESS(Status) && Device->ControlRequest.Transfer.BytesTransferred >= sizeof(USHORT)) {
        USHORT Value = _byteswap_ushort(*reinterpret_cast<UNALIGNED USHORT*>(reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize));
        Device->CurrentPhyValue = Value;

        if (Ax88179ShouldLogDiagnosticEvent(m_PhyReadSequence))
            DbgPrint("[usbenet]: %s PHY read complete #%u register=0x%04X value=0x%04X status=0x%08X bytes=%u.\n", g_UsbEnetChipset->GetName(), m_PhyReadSequence, Device->CurrentPhyRegister, Value, Status, Device->ControlRequest.Transfer.BytesTransferred);

        if (Device->CurrentPhyRegister < ARRAYSIZE(Device->PhyRegisters))
            Device->PhyRegisters[Device->CurrentPhyRegister] = Value;

        if (Device->InitStage == Ax88179InitWaitPhyReady && Device->CurrentPhyRegister == 2) {
            if (Value != 0 && Value != 0xFFFF) {
                DbgPrint("[usbenet]: %s PHY became ready after %u retries; PHYSID1=0x%04X.\n", GetName(), m_PhyReadyRetryCount, Value);
                AdvanceInitialization = TRUE;
            } else if (m_PhyReadyRetryCount < Ax88179PhyReadyMaximumRetries) {
                ++m_PhyReadyRetryCount;
                m_PhyReadyRetryPending = TRUE;
                RetryPhyReady = TRUE;
            } else {
                DbgPrint("[usbenet]: %s PHY readiness timed out after %u retries; continuing with PHYSID1=0x%04X as the Windows driver does after its bounded wait.\n", GetName(), m_PhyReadyRetryCount, Value);
                AdvanceInitialization = TRUE;
            }
        } else if (Device->InitStage == Ax88179InitPhyReadBmcr && Device->CurrentPhyRegister == MII_BMCR) {
            m_PhyInitBmcr = static_cast<USHORT>(Value & Ax88179PhyBmcrClearMask);
            DbgPrint("[usbenet]: %s PDB-derived PHY setup BMCR original=0x%04X sanitized=0x%04X.\n", GetName(), Value, m_PhyInitBmcr);
            AdvanceInitialization = TRUE;
        } else if (Device->CurrentPhyRegister == AX88179_PHY_PHYSICAL_STATUS) {
            m_InterruptPhyRefreshPending = FALSE;
            NotifyUsers = UpdateLinkState(Device, Value);
        }
    } else {
        DbgPrint("[usbenet]: %s PHY register 0x%04X read failed with status 0x%08X.\n", g_UsbEnetChipset->GetName(), Device->CurrentPhyRegister, Status);

        if (Device->InitStage == Ax88179InitWaitPhyReady) {
            if (m_PhyReadyRetryCount < Ax88179PhyReadyMaximumRetries) {
                ++m_PhyReadyRetryCount;
                m_PhyReadyRetryPending = TRUE;
                RetryPhyReady = TRUE;
            } else {
                AdvanceInitialization = TRUE;
            }
        } else if (Device->InitStage == Ax88179InitPhyReadBmcr) {
            m_PhyInitBmcr = 0;
            AdvanceInitialization = TRUE;
        }
    }

    if (RetryPhyReady)
        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88179PhyReadyRetryPeriod);
    else if (AdvanceInitialization && Device->InitStage != Ax88179InitComplete)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (NotifyUsers)
        NotifyLinkStateChanged(Device);
}

VOID CAx88179_178A::CompleteWritePhy(CUsbEnet* Device, NTSTATUS Status) {
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;

    DbgPrint("[usbenet]: %s PHY write complete #%u register=0x%04X value=0x%04X status=0x%08X bytes=%u.\n", g_UsbEnetChipset->GetName(), m_PhyWriteSequence, Device->CurrentPhyRegister, Device->CurrentPhyValue, Status, Device->ControlRequest.Transfer.BytesTransferred);

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s PHY register 0x%04X write failed with status 0x%08X.\n", g_UsbEnetChipset->GetName(), Device->CurrentPhyRegister, Status);

    if (Device->InitStage >= Ax88179InitPhyWriteMmdAccess1 && Device->InitStage <= Ax88179InitRestartAutonegotiation)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88179_178A::CompleteWriteMediumMode(CUsbEnet* Device, NTSTATUS Status) {
    BOOL NotifyUsers = m_NotifyLinkAfterMediumWrite;
    m_NotifyLinkAfterMediumWrite = FALSE;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s medium-mode write failed with status 0x%08X.\n", g_UsbEnetChipset->GetName(), Status);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (NotifyUsers)
        NotifyLinkStateChanged(Device);
}

VOID CAx88179_178A::NotifyLinkStateChanged(CUsbEnet* Device) {
    Device->NotifyLinkStateChangedToUsers();
}

BOOL CAx88179_178A::UsesInterruptLinkStatus() const {
    return TRUE;
}

BOOL CAx88179_178A::ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Data == NULL || Length < 8)
        return FALSE;

    BOOL LinkUp = (Data[2] & 0x01) != 0;
    BOOL FirstStatus = !m_InterruptStatusSeen;
    BOOL LinkChanged = FirstStatus || LinkUp != m_InterruptLinkUp;
    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    m_InterruptStatusSeen = TRUE;
    m_InterruptLinkUp = LinkUp;
    ++m_InterruptEventCount;
    Device->LinkPollTick = CurrentTick;

    if (LinkChanged || m_InterruptEventCount <= 4 || (m_InterruptEventCount & (m_InterruptEventCount - 1)) == 0)
        DbgPrint("[usbenet]: %s interrupt status #%u bytes=%02X %02X %02X %02X %02X %02X %02X %02X link=%u.\n", GetName(), m_InterruptEventCount, Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7], LinkUp);

    if (LinkUp) {
        m_InterruptPhyRefreshPending = TRUE;
        if (!m_LinkResetInProgress && (Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS | USBENET_STATE_CAN_USER_TRANSFER)) == 0) {
            NTSTATUS Status = BeginReadPhy(Device, AX88179_PHY_PHYSICAL_STATUS);
            if (NT_SUCCESS(Status))
                m_InterruptPhyRefreshPending = FALSE;
        }
        return FALSE;
    }

    m_InterruptPhyRefreshPending = FALSE;
    if (m_RxHardRecoveryLinkGraceTick != 0 && CurrentTick - m_RxHardRecoveryLinkGraceTick < 5000)
        return FALSE;
    m_RxHardRecoveryLinkGraceTick = 0;
    DWORD NewLinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    BOOL Notify = Device->LinkState != NewLinkState;
    Device->LinkState = NewLinkState;
    return Notify;
}

VOID CAx88179_178A::RunTimer(CUsbEnet* Device) {
    NicBaseTakeLockAtRaisedIrql(Device);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    if (Device->InitStage == Ax88179InitWaitPhyReady) {
        if (m_PhyReadyRetryPending && (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) == 0) {
            m_PhyReadyRetryPending = FALSE;
            NTSTATUS Status = BeginReadPhy(Device, 2);
            if (!NT_SUCCESS(Status)) {
                m_PhyReadyRetryPending = TRUE;
                CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88179PhyReadyRetryPeriod);
            }
        }

        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    if (Device->InitStage == Ax88179InitComplete)
        Device->PrintThroughputStats(CurrentTick);

    {
        if (Device->InitStage == Ax88179InitComplete && Device->PendingXmitCount != 0 && m_TxOutstandingSinceTick != 0 && CurrentTick - m_TxOutstandingSinceTick >= Ax88179FamilyTransmitTimeout && m_RecoveryLevel == Ax88179FamilyRecoveryIdle && !m_FullReinitializePending && !m_FullReinitializeActive) {
            DWORD TimeoutCount = ++m_TxTimeoutCount;
            m_RecoveryLevel = TimeoutCount >= 5 ? Ax88179FamilyRecoveryResetDevice : TimeoutCount >= 3 ? Ax88179FamilyRecoveryReinitialize : Ax88179FamilyRecoveryResetPipe;
            m_RecoveryPipe = UsbEnetTransportTransmit;
            m_RecoveryRequestTick = CurrentTick;
            m_RecoveryReason = 0x200 + TimeoutCount;
            m_TxOutstandingSinceTick = CurrentTick;
            DbgPrint("[usbenet-recovery]: %s TX timeout with %u pending transfers; count=%u level=%u.\n", GetName(), Device->PendingXmitCount, TimeoutCount, static_cast<DWORD>(m_RecoveryLevel));
        }

        ProcessRecovery(Device, CurrentTick);
        BOOL ResetDevice = m_DeviceResetPending;
        m_DeviceResetPending = FALSE;
        if (ResetDevice) {
            TRAP_ASSERT(Device->PreviousIrql == 0xEE);
            TRAP_THREAD(Device->LockOwnerThread);
            NULL_OWNER_THREAD(Device);
            KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
            Device->ResetUsbDevice();
            return;
        }

        if (m_FullReinitializePending) {
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 25);
            TRAP_ASSERT(Device->PreviousIrql == 0xEE);
            TRAP_THREAD(Device->LockOwnerThread);
            NULL_OWNER_THREAD(Device);
            KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
            return;
        }

        if (Device->InitStage != Ax88179InitComplete) {
            TRAP_ASSERT(Device->PreviousIrql == 0xEE);
            TRAP_THREAD(Device->LockOwnerThread);
            NULL_OWNER_THREAD(Device);
            KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
            return;
        }

        if ((Device->LinkState & NIC_LINK_STATE_ACTIVE) != 0 && (Device->Flags & ReceiveRunning) != 0 && (Device->Flags & USBENET_STATE_00080000) != 0 && CurrentTick - Device->TimerTick > 5000) {
            RestartReceiving(Device);
            Device->TimerTick = CurrentTick;
        } else if (!m_LinkResetInProgress && (Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS | USBENET_STATE_CAN_USER_TRANSFER)) == 0 && (m_InterruptPhyRefreshPending || !m_InterruptStatusSeen || CurrentTick - Device->LinkPollTick >= 5000)) {
            NTSTATUS Status = BeginReadPhy(Device, AX88179_PHY_PHYSICAL_STATUS);
            if (NT_SUCCESS(Status))
                m_InterruptPhyRefreshPending = FALSE;
        }

        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 1000);
    }

    TRAP_ASSERT(Device->PreviousIrql == 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
}

BOOL CAx88179_178A::InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context) {
    UNREFERENCED_PARAMETER(Device);
    ++m_RxAggregateSequence;

    if (Length < sizeof(DWORD)) {
        DbgPrint("[usbenet]: [DISCARD] AX88179/178A transfer is too short for its aggregate trailer (%u bytes).\n", Length);
        return FALSE;
    }

    DWORD RawTrailer = *reinterpret_cast<UNALIGNED ULONG*>(Buffer + Length - sizeof(DWORD));
    DWORD Trailer = _byteswap_ulong(RawTrailer);
    DWORD MetadataCount = Trailer & 0xFFFF;
    DWORD MetadataOffset = Trailer >> 16;
    DWORD TrailerOffset = Length - sizeof(DWORD);

    if (Ax88179ShouldLogDiagnosticEvent(m_RxAggregateSequence))
        DbgPrint("[usbenet]: AX88179 RX aggregate #%u length=%u rawTrailer=0x%08X trailer=0x%08X count=%u metadataOffset=%u first=%02X %02X %02X %02X %02X %02X %02X %02X last=%02X %02X %02X %02X %02X %02X %02X %02X\n", m_RxAggregateSequence, Length, RawTrailer, Trailer, MetadataCount, MetadataOffset, Buffer[0], Buffer[1], Buffer[2], Buffer[3], Buffer[4], Buffer[5], Buffer[6], Buffer[7], Length > 7 ? Buffer[Length - 8] : 0, Length > 6 ? Buffer[Length - 7] : 0, Length > 5 ? Buffer[Length - 6] : 0, Length > 4 ? Buffer[Length - 5] : 0, Length > 3 ? Buffer[Length - 4] : 0, Length > 2 ? Buffer[Length - 3] : 0, Length > 1 ? Buffer[Length - 2] : 0, Buffer[Length - 1]);

    if (MetadataCount == 0 || MetadataOffset > TrailerOffset || MetadataCount > (TrailerOffset - MetadataOffset) / sizeof(DWORD)) {
        DWORD Signature = Trailer ^ (Length * 0x9E3779B9);
        ++m_RxInvalidAggregateSequence;

        if (Signature == m_RxInvalidAggregateSignature)
            ++m_RxInvalidAggregateStreak;
        else {
            m_RxInvalidAggregateSignature = Signature;
            m_RxInvalidAggregateStreak = 1;
        }

        if (m_RxInvalidAggregateStreak <= 4 || (m_RxInvalidAggregateStreak & (m_RxInvalidAggregateStreak - 1)) == 0)
            DbgPrint("[usbenet]: [DISCARD] AX88179/178A invalid aggregate trailer count=%u metadataOffset=%u transferLength=%u streak=%u drain=%u.\n", MetadataCount, MetadataOffset, Length, m_RxInvalidAggregateStreak, m_RxRecoveryInProgress);

        if (!m_RxRecoveryInProgress && m_RxFrameSequence != 0 && Length == ReceiveBufferSize && m_RxInvalidAggregateStreak == 4) {
            m_RxRecoveryInProgress = TRUE;
            m_RxRecoveryStartTick = KeTimeStampBundle->TickCount;
            DbgPrint("[usbenet]: [RECOVERY] AX88179 detected stale full-buffer RX completions after a stall; draining them without cancelling the RX ring.\n");
        }

        if (m_RxRecoveryInProgress && !m_RxHardRecoveryPending && Length == ReceiveBufferSize && (m_RxInvalidAggregateStreak >= 256 || KeTimeStampBundle->TickCount - m_RxRecoveryStartTick >= 250))
            RequestHardReceiveRecovery(Device);

        return FALSE;
    }

    if (m_RxRecoveryInProgress)
        DbgPrint("[usbenet]: [RECOVERY] AX88179 received a valid aggregate; stale RX drain completed without resetting the ring.\n");

    m_RxRecoveryInProgress = FALSE;
    m_RxHardRecoveryCount = 0;
    m_RxInvalidAggregateStreak = 0;
    m_RxInvalidAggregateSignature = 0;
    m_RxRecoveryStartTick = 0;
    Context->Buffer = Buffer;
    Context->Length = Length;
    Context->DataOffset = 0;
    Context->MetadataOffset = MetadataOffset;
    Context->MetadataIndex = 0;
    Context->MetadataCount = MetadataCount;
    return TRUE;
}

USBENET_RX_PARSE_RESULT CAx88179_178A::GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame) {
    UNREFERENCED_PARAMETER(Device);

    while (Context->MetadataIndex < Context->MetadataCount) {
        DWORD DescriptorIndex = Context->MetadataIndex;
        DWORD HeaderOffset = Context->MetadataOffset + DescriptorIndex * sizeof(DWORD);
        DWORD RawPacketHeader = *reinterpret_cast<UNALIGNED ULONG*>(Context->Buffer + HeaderOffset);
        DWORD PacketHeader = _byteswap_ulong(RawPacketHeader);
        DWORD PacketLength = (PacketHeader >> 16) & 0x1FFF;
        DWORD AlignedPacketLength = (PacketLength + 7) & ~7;
        ++Context->MetadataIndex;
        ++m_RxDescriptorSequence;

        if (Ax88179ShouldLogDiagnosticEvent(m_RxDescriptorSequence))
            DbgPrint("[usbenet]: AX88179 RX descriptor #%u aggregate=%u index=%u/%u headerOffset=%u raw=0x%08X header=0x%08X length=%u aligned=%u dataOffset=%u flags={drop=%u crc=%u l3=%u l4=%u}\n", m_RxDescriptorSequence, m_RxAggregateSequence, DescriptorIndex, Context->MetadataCount, HeaderOffset, RawPacketHeader, PacketHeader, PacketLength, AlignedPacketLength, Context->DataOffset, (PacketHeader & AX88179_RXHDR_DROP_ERR) != 0, (PacketHeader & AX88179_RXHDR_CRC_ERR) != 0, (PacketHeader & AX88179_RXHDR_L3CSUM_ERR) != 0, (PacketHeader & AX88179_RXHDR_L4CSUM_ERR) != 0);

        if (PacketLength == 0)
            continue;

        if (Context->DataOffset + AlignedPacketLength > Context->MetadataOffset) {
            DbgPrint("[usbenet]: [DISCARD] AX88179/178A packet length %u overlaps aggregate metadata at offset %u.\n", PacketLength, Context->MetadataOffset);
            return UsbEnetRxParseError;
        }

        PBYTE Packet = Context->Buffer + Context->DataOffset;
        Context->DataOffset += AlignedPacketLength;

        if ((PacketHeader & (AX88179_RXHDR_CRC_ERR | AX88179_RXHDR_MII_ERR | AX88179_RXHDR_DROP_ERR)) != 0 || PacketLength < 16) {
            ++m_RxDiscardDescriptorSequence;

            if (m_RxDiscardDescriptorSequence <= 4 || (m_RxDiscardDescriptorSequence & (m_RxDiscardDescriptorSequence - 1)) == 0)
                DbgPrint("[usbenet]: [DISCARD] AX88179/178A hardware RX discard #%u header=0x%08X length=%u flags={drop=%u mii=%u crc=%u rxOk=%u bmc=%u runt=%u}.\n", m_RxDiscardDescriptorSequence, PacketHeader, PacketLength, (PacketHeader & AX88179_RXHDR_DROP_ERR) != 0, (PacketHeader & AX88179_RXHDR_MII_ERR) != 0, (PacketHeader & AX88179_RXHDR_CRC_ERR) != 0, (PacketHeader & AX88179_RXHDR_RX_OK) != 0, (PacketHeader & AX88179_RXHDR_BMC) != 0, PacketLength < 16);

            return UsbEnetRxParseSkip;
        }

        Frame->Data = Packet + 2;
        Frame->Length = PacketLength - 2;
        Frame->Flags = PacketHeader;
        ++m_RxFrameSequence;

        if (Ax88179ShouldLogDiagnosticEvent(m_RxFrameSequence) && Frame->Length >= 14) {
            PBYTE Ethernet = Frame->Data;
            USHORT EtherType = static_cast<USHORT>((static_cast<USHORT>(Ethernet[12]) << 8) | Ethernet[13]);
            DbgPrint("[usbenet]: AX88179 RX frame #%u length=%u type=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X packetPrefix=%02X %02X\n", m_RxFrameSequence, Frame->Length, EtherType, Ethernet[0], Ethernet[1], Ethernet[2], Ethernet[3], Ethernet[4], Ethernet[5], Ethernet[6], Ethernet[7], Ethernet[8], Ethernet[9], Ethernet[10], Ethernet[11], Packet[0], Packet[1]);
        }

        return UsbEnetRxParseFrame;
    }

    return UsbEnetRxParseComplete;
}

BOOL CAx88179_178A::AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator) {
    DWORD AlignmentPadding = (0 - AggregateOffset) & 3;
    DWORD Required = AlignmentPadding + sizeof(DWORD) * 2 + Length;

    if (Required > Capacity)
        return FALSE;

    if (AggregateOffset != 0) {
        PBYTE AggregateStart = Buffer - AggregateOffset;
        DWORD RecordOffset = 0;
        PBYTE PreviousHeader2 = NULL;

        while (RecordOffset < AggregateOffset) {
            RecordOffset = (RecordOffset + 3) & ~3;

            if (RecordOffset + sizeof(DWORD) * 2 > AggregateOffset)
                return FALSE;

            DWORD PreviousLength = _byteswap_ulong(*reinterpret_cast<UNALIGNED ULONG*>(AggregateStart + RecordOffset));

            if (PreviousLength == 0 || PreviousLength > GetMaximumFrameSize() || RecordOffset + sizeof(DWORD) * 2 + PreviousLength > AggregateOffset)
                return FALSE;

            PreviousHeader2 = AggregateStart + RecordOffset + sizeof(DWORD);
            RecordOffset += sizeof(DWORD) * 2 + PreviousLength;
        }

        if (RecordOffset != AggregateOffset || PreviousHeader2 == NULL)
            return FALSE;

        DWORD PreviousFlags = _byteswap_ulong(*reinterpret_cast<UNALIGNED ULONG*>(PreviousHeader2));
        PreviousFlags &= ~AX88179_TXHDR_PADDING;
        *reinterpret_cast<UNALIGNED ULONG*>(PreviousHeader2) = _byteswap_ulong(PreviousFlags);
    }

    if (AlignmentPadding != 0)
        memset(Buffer, 0, AlignmentPadding);

    PBYTE Record = Buffer + AlignmentPadding;
    DWORD Header1 = Length;
    DWORD Header2 = 0;
    DWORD TransferLength = AggregateOffset + Required;
    ++m_TxFrameSequence;

    if (Device->TransmitMaxPacketSize != 0 && (TransferLength % Device->TransmitMaxPacketSize) == 0)
        Header2 |= AX88179_TXHDR_PADDING;

    *reinterpret_cast<UNALIGNED ULONG*>(Record) = _byteswap_ulong(Header1);
    *reinterpret_cast<UNALIGNED ULONG*>(Record + sizeof(DWORD)) = _byteswap_ulong(Header2);
    memcpy(Record + sizeof(DWORD) * 2, Frame, Length);

    if (Ax88179ShouldLogDiagnosticEvent(m_TxFrameSequence) && Length >= 14) {
        const BYTE* Ethernet = static_cast<const BYTE*>(Frame);
        USHORT EtherType = static_cast<USHORT>((static_cast<USHORT>(Ethernet[12]) << 8) | Ethernet[13]);
        DbgPrint("[usbenet]: AX88179 TX frame #%u frameLength=%u required=%u capacity=%u aggregateOffset=%u alignment=%u transferLength=%u header1=0x%08X header2=0x%08X type=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X\n", m_TxFrameSequence, Length, Required, Capacity, AggregateOffset, AlignmentPadding, TransferLength, Header1, Header2, EtherType, Ethernet[0], Ethernet[1], Ethernet[2], Ethernet[3], Ethernet[4], Ethernet[5], Ethernet[6], Ethernet[7], Ethernet[8], Ethernet[9], Ethernet[10], Ethernet[11]);
    }

    *FramedLength = Required;
    *BytesWritten = Required;
    *HasTerminator = FALSE;
    return TRUE;
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineInitTransfer(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteInitTransfer(Device, Status);
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineReadNodeId(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteReadNodeId(Device, Status);
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineWriteNodeId(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteWriteNodeId(Device, Status);
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineReadPhy(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteReadPhy(Device, Status);
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineWritePhy(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteWritePhy(Device, Status);
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineWriteMediumMode(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteWriteMediumMode(Device, Status);
}

VOID __fastcall CAx88179_178A::AsyncCompletionRoutineLinkReset(PVOID Request, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST AsyncRequest = static_cast<PUSBD_ASYNC_REQUEST>(Request);
    CUsbEnet* Device = static_cast<CUsbEnet*>(AsyncRequest->Context);
    TRAP_ASSERT(Device != NULL);
    static_cast<CAx88179_178A*>(g_UsbEnetChipset)->CompleteLinkResetTransfer(Device, Status);
}

// AX88179A/B, AX88772D and AX88279 firmware-family implementation.
CAx88179AB g_Ax88179ABChipset;
CAx88772D g_Ax88772DChipset;
CAx88279 g_Ax88279Chipset;

static const BYTE Ax179AChecksumControl = 0x67;
static const BYTE Ax179AMonitorMode = 0x04;
static const BYTE Ax179AMacPathReady = 0x03;
static const BYTE Ax179AMacBulkOutEnable = 0x02;
static const BYTE Ax179APhyAddress = 0x03;
static const BYTE Ax179APhyPhysicalStatus = 0x11;
static const BYTE Ax179AFirmwareMode = 0x01;
static const BYTE Ax179APhyPowerEnabled = 0x02;
static const DWORD Ax179ATxDescriptorLengthMask = 0x001FFFFF;
static const DWORD Ax179ATxDescriptorDropPadding = 0x10000000;
static const DWORD Ax179ARxDescriptorRxOk = 0x00000800;
static const DWORD Ax179ARxDescriptorDrop = 0x80000000;
static const ULONGLONG Ax179ARxDescriptorLengthMask = 0x000000007FFF0000ui64;
static const DWORD Ax179ARxDescriptorLengthShift = 16;
static const DWORD Ax179ARxDescriptorCountMask = 0x00001FFF;
static const DWORD Ax179ARxDescriptorOffsetMask = 0xFFFFE000;
static const DWORD Ax179ARxDescriptorOffsetShift = 13;

static const BYTE Ax88179ABBulkIn1000Hs[5] = { 0x05, 0xC0, 0x02, 0x06, 0x0F };
static const BYTE Ax88179ABBulkIn100FullHs[5] = { 0x05, 0xC0, 0x04, 0x06, 0x0F };
static const BYTE Ax88179ABBulkIn100HalfHs[5] = { 0x07, 0xC0, 0x04, 0x06, 0x0F };
static const BYTE Ax88772DBulkIn100FullHs[5] = { 0x05, 0xC0, 0x04, 0x06, 0x0F };
static const BYTE Ax88772DBulkIn100HalfHs[5] = { 0x07, 0xC0, 0x04, 0x06, 0x0F };
static const BYTE Ax179ABulkIn10Fs[5] = { 0x07, 0x00, 0x00, 0x03, 0x3F };
static const BYTE Ax88279BulkIn1000Hs[5] = { 0x07, 0xC0, 0x02, 0x06, 0x0F };
static const BYTE Ax88279BulkIn100Hs[5] = { 0x07, 0x80, 0x01, 0x03, 0x0F };

static BOOL Ax179AShouldLog(DWORD Count) {
    return Count <= 32 || (Count & (Count - 1)) == 0;
}

static ULONGLONG Ax179AReadLe64(const BYTE* Buffer) {
    ULONGLONG Value = 0;

    for (DWORD Index = 0; Index < 8; Index++)
        Value |= static_cast<ULONGLONG>(Buffer[Index]) << (Index * 8);

    return Value;
}

static VOID Ax179AWriteLe64(BYTE* Buffer, ULONGLONG Value) {
    for (DWORD Index = 0; Index < 8; Index++)
        Buffer[Index] = static_cast<BYTE>(Value >> (Index * 8));
}

static DWORD Ax179AAlign8(DWORD Value) {
    return (Value + 7) & ~7;
}

static VOID Ax179AIncrementEthernetAddress(CEnetAddr* Address) {
    PBYTE Bytes = reinterpret_cast<PBYTE>(Address);

    for (INT Index = static_cast<INT>(sizeof(CEnetAddr)) - 1; Index >= 0; Index--) {
        Bytes[Index]++;

        if (Bytes[Index] != 0)
            break;
    }
}

static const char* Ax179AInitStageName(DWORD Stage) {
    switch (Stage) {
        case Ax179AInitBeginning: return "Beginning";
        case Ax179AInitReadChipStatus: return "ReadChipStatus";
        case Ax179AInitReadFirmware0: return "ReadFirmware0";
        case Ax179AInitReadFirmware1: return "ReadFirmware1";
        case Ax179AInitReadFirmware2: return "ReadFirmware2";
        case Ax179AInitEnableFirmwareMode: return "EnableFirmwareMode";
        case Ax179AInitReloadNonvolatileState: return "ReloadNonvolatileState";
        case Ax179AInitPowerDownPhy: return "PowerDownPhy";
        case Ax179AInitPausePowerDown: return "PausePowerDown";
        case Ax179AInitPowerUpPhy: return "PowerUpPhy";
        case Ax179AInitPausePowerUp: return "PausePowerUp";
        case Ax179AInitEnableBulkOut: return "EnableBulkOut";
        case Ax179AInitStopReceive: return "StopReceive";
        case Ax179AInitWritePauseLow: return "WritePauseLow";
        case Ax179AInitWritePauseHigh: return "WritePauseHigh";
        case Ax179AInitDisableVlan: return "DisableVlan";
        case Ax179AInitMaskInterrupts: return "MaskInterrupts";
        case Ax179AInitDisableRxDma: return "DisableRxDma";
        case Ax179AInitDisableTxDma: return "DisableTxDma";
        case Ax179AInitDisableArc: return "DisableArc";
        case Ax179AInitDisableSwp: return "DisableSwp";
        case Ax179AInitDisableTxHeaderChecksum: return "DisableTxHeaderChecksum";
        case Ax179AInitReadNodeId: return "ReadNodeId";
        case Ax179AInitWaitingForEthernetAddress: return "WaitingForEthernetAddress";
        case Ax179AInitWriteNodeId: return "WriteNodeId";
        case Ax179AInitReadNodeIdBack: return "ReadNodeIdBack";
        case Ax179AInitEnableRxChecksum: return "EnableRxChecksum";
        case Ax179AInitEnableTxChecksum: return "EnableTxChecksum";
        case Ax179AInitStartReceiving: return "StartReceiving";
        case Ax179AInitWriteMonitorMode: return "WriteMonitorMode";
        case Ax179AInitWriteDefaultMedium: return "WriteDefaultMedium";
        case Ax179AInitWaitPhyReady: return "WaitPhyReady";
        case Ax179AInitPhyWriteMmdAccess1: return "PhyWriteMmdAccess1";
        case Ax179AInitPhyWriteMmdData1: return "PhyWriteMmdData1";
        case Ax179AInitPhyWriteMmdAccess2: return "PhyWriteMmdAccess2";
        case Ax179AInitPhyWriteMmdData2: return "PhyWriteMmdData2";
        case Ax179AInitPhyReadBmcr: return "PhyReadBmcr";
        case Ax179AInitPhyClearBmcr: return "PhyClearBmcr";
        case Ax179AInitPhyPauseAfterBmcrClear: return "PhyPauseAfterBmcrClear";
        case Ax179AInitPhyWriteAdvertisement: return "PhyWriteAdvertisement";
        case Ax179AInitPhyWriteGigabitAdvertisement: return "PhyWriteGigabitAdvertisement";
        case Ax179AInitRestartAutonegotiation: return "RestartAutonegotiation";
        case Ax179AInitReady: return "Ready";
        case Ax179AInitComplete: return "Complete";
        default: return "Unknown";
    }
}

static const char* Ax179ALinkResetStageName(BYTE Stage) {
    switch (Stage) {
        case Ax179ALinkResetIdle: return "Idle";
        case Ax179ALinkResetStopReceive: return "StopReceive";
        case Ax179ALinkResetStopMacPath: return "StopMacPath";
        case Ax179ALinkResetWriteCdcDelay: return "WriteCdcDelay";
        case Ax179ALinkResetWritePauseWatermark: return "WritePauseWatermark";
        case Ax179ALinkResetWriteTxGap: return "WriteTxGap";
        case Ax179ALinkResetWriteEp5: return "WriteEp5";
        case Ax179ALinkResetWriteNewPause: return "WriteNewPause";
        case Ax179ALinkResetWriteTxPause: return "WriteTxPause";
        case Ax179ALinkResetWriteRxStatus: return "WriteRxStatus";
        case Ax179ALinkResetWriteRxDataCount: return "WriteRxDataCount";
        case Ax179ALinkResetWriteBfmData: return "WriteBfmData";
        case Ax179ALinkResetWriteLsoEnhance: return "WriteLsoEnhance";
        case Ax179ALinkResetWriteBulkIn: return "WriteBulkIn";
        case Ax179ALinkResetWriteMedium: return "WriteMedium";
        case Ax179ALinkResetStartReceive: return "StartReceive";
        case Ax179ALinkResetStartMacPath: return "StartMacPath";
        case Ax179ALinkResetComplete: return "Complete";
        default: return "Unknown";
    }
}

const char* CAx88179AB::GetName() const {
    return "AX88179A/B";
}

AX179A_VARIANT CAx88179AB::GetVariant() const {
    return Ax179AVariant88179AB;
}

BYTE CAx88179AB::GetExpectedChipVersion() const {
    return 0x06;
}

BOOL CAx88179AB::UsesIpAlignment() const {
    return FALSE;
}

BOOL CAx88179AB::SupportsGigabit() const {
    return TRUE;
}

DWORD CAx88179AB::GetMaximumWireSpeed() const {
    return 1000;
}

const char* CAx88772D::GetName() const {
    return "AX88772D";
}

AX179A_VARIANT CAx88772D::GetVariant() const {
    return Ax179AVariant88772D;
}

BYTE CAx88772D::GetExpectedChipVersion() const {
    return 0x06;
}

BOOL CAx88772D::UsesIpAlignment() const {
    return FALSE;
}

BOOL CAx88772D::SupportsGigabit() const {
    return FALSE;
}

DWORD CAx88772D::GetMaximumWireSpeed() const {
    return 100;
}

const char* CAx88279::GetName() const {
    return "AX88279";
}

AX179A_VARIANT CAx88279::GetVariant() const {
    return Ax179AVariant88279;
}

BYTE CAx88279::GetExpectedChipVersion() const {
    return 0x07;
}

BOOL CAx88279::UsesIpAlignment() const {
    return TRUE;
}

BOOL CAx88279::SupportsGigabit() const {
    return TRUE;
}

DWORD CAx88279::GetMaximumWireSpeed() const {
    return 2500;
}

USHORT CAx179AFamily::GetVendorIdRaw() const {
    return 0x950B;
}

USHORT CAx179AFamily::GetProductIdRaw() const {
    return 0x9017;
}

BOOL CAx179AFamily::IsImplemented() const {
    return TRUE;
}

BOOL CAx179AFamily::SupportsTransmitAggregation() const {
    return TRUE;
}

DWORD CAx179AFamily::GetMaximumFrameSize() const {
    return 0x0FFC;
}

DWORD CAx179AFamily::GetMaximumAggregateTransferSize() const {
    return XmitBufferSize - sizeof(ULONGLONG);
}

DWORD CAx179AFamily::GetTransmitHeaderSize() const {
    return sizeof(ULONGLONG);
}

DWORD CAx179AFamily::GetTransmitTerminatorSize() const {
    return sizeof(ULONGLONG);
}

BOOL CAx179AFamily::IsReady(CUsbEnet* Device) {
    return Device->InitStage == Ax179AInitComplete;
}

BOOL CAx179AFamily::IsNodeIdAvailable(CUsbEnet* Device) {
    UNREFERENCED_PARAMETER(Device);
    return m_NodeIdValid;
}

VOID CAx179AFamily::OnUnicastAddressChanged(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Device->InitStage == Ax179AInitWaitingForEthernetAddress)
        AdvanceInitStage(Device);
    else if (Device->InitStage > Ax179AInitWaitingForEthernetAddress && Device->InitStage <= Ax179AInitComplete)
        WriteNodeId(Device);
}

VOID CAx179AFamily::ResetState(CUsbEnet* Device) {
    Device->InitStage = Ax179AInitBeginning;
    Device->PhyAddress = Ax179APhyAddress;
    m_NodeIdValid = FALSE;
    m_LinkResetInProgress = FALSE;
    m_RxRecoveryInProgress = FALSE;
    m_ChipStatus = 0;
    memset(m_FirmwareVersion, 0, sizeof(m_FirmwareVersion));
    m_LinkResetStage = Ax179ALinkResetIdle;
    m_LinkSpeed = Ax179ALinkNone;
    m_FullDuplex = FALSE;
    m_ReceiveControl = 0;
    m_LastPhyStatus = 0;
    m_InterruptStatusSeen = FALSE;
    m_InterruptLinkUp = FALSE;
    m_InterruptPhyRefreshPending = FALSE;
    m_InterruptEventCount = 0;
    m_RxAggregateSequence = 0;
    m_RxDescriptorSequence = 0;
    m_RxFrameSequence = 0;
    m_TxFrameSequence = 0;
    m_RxInvalidAggregateStreak = 0;
    m_RxInvalidAggregateSignature = 0;
    m_RxRecoveryStartTick = 0;
    m_RxHardRecoveryCount = 0;
    m_PhyReadyRetryCount = 0;
    m_PhyReadyRetryPending = FALSE;
    m_PhyInitBmcr = 0;
    m_ReinitializeLinkGraceTick = 0;
    m_RecoveryLevel = Ax88179FamilyRecoveryIdle;
    m_RecoveryPipe = UsbEnetTransportReceive;
    m_FullReinitializePending = FALSE;
    m_FullReinitializeActive = FALSE;
    m_RecoveryEndpointsReset = FALSE;
    m_DeviceResetPending = FALSE;
    m_RecoveryReason = 0;
    m_RecoveryCount = 0;
    m_RecoveryRequestTick = 0;
    memset(m_RecoveryErrorCount, 0, sizeof(m_RecoveryErrorCount));
    memset(m_RecoveryLastErrorTick, 0, sizeof(m_RecoveryLastErrorTick));
    m_TxOutstandingSinceTick = 0;
    m_TxTimeoutCount = 0;
}

NTSTATUS CAx179AFamily::QueueCommand(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Request, WORD Value, WORD Index, WORD Length, const PVOID Buffer) {
    BYTE RequestType = Buffer == NULL && Length != 0 ? USB_DIR_IN : USB_DIR_OUT;
    return Device->QueueControlTransfer(CompletionRoutine, RequestType | USB_TYPE_VENDOR | USB_RECIP_DEVICE, Request, Value, Index, Length, Buffer);
}

NTSTATUS CAx179AFamily::QueueMacRead(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length) {
    return Device->QueueControlTransfer(CompletionRoutine, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_ACCESS_MAC, Register, Length, Length, NULL);
}

NTSTATUS CAx179AFamily::QueueMacWrite8(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, BYTE Value) {
    return QueueMacWrite(Device, CompletionRoutine, Register, sizeof(Value), &Value);
}

NTSTATUS CAx179AFamily::QueueMacWrite16(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, USHORT Value) {
    USHORT WireValue = _byteswap_ushort(Value);
    return QueueMacWrite(Device, CompletionRoutine, Register, sizeof(WireValue), &WireValue);
}

NTSTATUS CAx179AFamily::QueueMacWrite(CUsbEnet* Device, PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE Register, WORD Length, const PVOID Buffer) {
    return Device->QueueControlTransfer(CompletionRoutine, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_ACCESS_MAC, Register, Length, Length, Buffer);
}

USHORT CAx179AFamily::BuildReceiveControl(CUsbEnet* Device) const {
    USHORT ReceiveControl = AX179A_RX_CTL_DROPCRCERR | AX179A_RX_CTL_START | AX179A_RX_CTL_AP;

    if (UsesIpAlignment())
        ReceiveControl |= AX179A_RX_CTL_IPE;

    if ((Device->AggregateReceiveFilter & 0xFF) != 0)
        ReceiveControl |= AX179A_RX_CTL_PRO;

    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_BROADCAST) != 0)
        ReceiveControl |= AX179A_RX_CTL_AB;

    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_MULTICAST) != 0)
        ReceiveControl |= AX179A_RX_CTL_AM;

    return ReceiveControl;
}

USHORT CAx179AFamily::BuildMediumMode() const {
    USHORT MediumMode = AX179A_MEDIUM_RECEIVE_EN | AX179A_MEDIUM_TXFLOW_CTRLEN | AX179A_MEDIUM_RXFLOW_CTRLEN;

    if (m_LinkSpeed == Ax179ALink1000 || m_LinkSpeed == Ax179ALink2500)
        MediumMode |= AX179A_MEDIUM_GIGAMODE;
    else if (m_LinkSpeed == Ax179ALink100)
        MediumMode |= AX179A_MEDIUM_PS;

    if (m_FullDuplex)
        MediumMode |= AX179A_MEDIUM_FULL_DUPLEX;

    return MediumMode;
}

const BYTE* CAx179AFamily::SelectBulkInProfile() const {
    if (GetVariant() == Ax179AVariant88772D) {
        if (m_LinkSpeed == Ax179ALink100)
            return m_FullDuplex ? Ax88772DBulkIn100FullHs : Ax88772DBulkIn100HalfHs;
        return Ax179ABulkIn10Fs;
    }

    if (GetVariant() == Ax179AVariant88279) {
        if (m_LinkSpeed == Ax179ALink1000 || m_LinkSpeed == Ax179ALink2500)
            return Ax88279BulkIn1000Hs;
        if (m_LinkSpeed == Ax179ALink100)
            return Ax88279BulkIn100Hs;
        return Ax179ABulkIn10Fs;
    }

    if (m_LinkSpeed == Ax179ALink1000)
        return Ax88179ABBulkIn1000Hs;
    if (m_LinkSpeed == Ax179ALink100)
        return m_FullDuplex ? Ax88179ABBulkIn100FullHs : Ax88179ABBulkIn100HalfHs;
    return Ax179ABulkIn10Fs;
}

VOID CAx179AFamily::RecoverXboxAddresses(CUsbEnet* Device) {
    CEnetAddr* TitleAddress = Main::GetKernelTitleEthernetAddress();
    CEnetAddr* DebugAddress = Main::GetKernelDebugEthernetAddress();
    BOOL TitleValid = TitleAddress != NULL && !TitleAddress->IsZero() && !TitleAddress->IsMulticast();
    BOOL DebugValid = DebugAddress != NULL && !DebugAddress->IsZero() && !DebugAddress->IsMulticast();

    if (TitleValid)
        memcpy(&Device->UnicastAddress, TitleAddress, sizeof(CEnetAddr));

    if (Main::Devkit) {
        if (DebugValid)
            memcpy(&Device->AlternateUnicastAddress, DebugAddress, sizeof(CEnetAddr));
        else if (TitleValid) {
            memcpy(&Device->AlternateUnicastAddress, TitleAddress, sizeof(CEnetAddr));
            Ax179AIncrementEthernetAddress(&Device->AlternateUnicastAddress);
        }
    } else {
        Device->AlternateUnicastAddress.SetZero();
    }

    const BYTE* Primary = reinterpret_cast<const BYTE*>(&Device->UnicastAddress);
    if (Main::Devkit) {
        const BYTE* Alternate = reinterpret_cast<const BYTE*>(&Device->AlternateUnicastAddress);
        DbgPrint("[usbenet]: %s recovered title MAC %02X:%02X:%02X:%02X:%02X:%02X and debug MAC %02X:%02X:%02X:%02X:%02X:%02X.\n", GetName(), Primary[0], Primary[1], Primary[2], Primary[3], Primary[4], Primary[5], Alternate[0], Alternate[1], Alternate[2], Alternate[3], Alternate[4], Alternate[5]);
    } else {
        DbgPrint("[usbenet]: %s recovered retail title MAC %02X:%02X:%02X:%02X:%02X:%02X; no debug XNet address is configured.\n", GetName(), Primary[0], Primary[1], Primary[2], Primary[3], Primary[4], Primary[5]);
    }
}

VOID CAx179AFamily::AdvanceInitStage(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        Device->InitStage = Ax179AInitComplete;
        return;
    }

    AX179A_INIT_STAGE NextStage = static_cast<AX179A_INIT_STAGE>(Device->InitStage + 1);
    TRAP_ASSERT(NextStage > Ax179AInitBeginning && NextStage < Ax179AInitStageCount);
    Device->InitStage = NextStage;
    DbgPrint("[usbenet]: %s init stage %u (%s).\n", GetName(), Device->InitStage, Ax179AInitStageName(Device->InitStage));

    NTSTATUS Status = STATUS_SUCCESS;
    BYTE ByteValue = 0;

    switch (NextStage) {
        case Ax179AInitReadChipStatus:
            Status = QueueMacRead(Device, AsyncCompletionRoutineInitTransfer, AX179A_CHIP_STATUS, 1);
            break;

        case Ax179AInitReadFirmware0:
        case Ax179AInitReadFirmware1:
        case Ax179AInitReadFirmware2:
            Status = Device->QueueControlTransfer(AsyncCompletionRoutineInitTransfer, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_ACCESS_BL, static_cast<WORD>(0xFD + NextStage - Ax179AInitReadFirmware0), 1, 1, NULL);
            break;

        case Ax179AInitEnableFirmwareMode:
            Status = Device->QueueControlTransfer(AsyncCompletionRoutineInitTransfer, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_FW_MODE, Ax179AFirmwareMode, 0, 0, NULL);
            break;

        case Ax179AInitReloadNonvolatileState:
            Status = Device->QueueControlTransfer(AsyncCompletionRoutineInitTransfer, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_RELOAD_EEPROM_EFUSE, 0, 0, 0, NULL);
            break;

        case Ax179AInitPowerDownPhy:
            ByteValue = 0;
            Status = Device->QueueControlTransfer(AsyncCompletionRoutineInitTransfer, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_PHY_POWER, 0, 0, 1, &ByteValue);
            break;

        case Ax179AInitPausePowerDown:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 250);
            return;

        case Ax179AInitPowerUpPhy:
            CNicBase::NicBaseShutdown(Device);
            ByteValue = Ax179APhyPowerEnabled;
            Status = Device->QueueControlTransfer(AsyncCompletionRoutineInitTransfer, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_PHY_POWER, 0, 0, 1, &ByteValue);
            break;

        case Ax179AInitPausePowerUp:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 250);
            return;

        case Ax179AInitEnableBulkOut:
            CNicBase::NicBaseShutdown(Device);
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_BULK_OUT_CTRL, Ax179AMacBulkOutEnable);
            break;

        case Ax179AInitStopReceive:
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineInitTransfer, 0x0B, AX179A_RX_CTL_STOP);
            break;

        case Ax179AInitWritePauseLow:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_PAUSE_WATERLVL_LOW, 0x04);
            break;

        case Ax179AInitWritePauseHigh:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_PAUSE_WATERLVL_HIGH, 0x10);
            break;

        case Ax179AInitDisableVlan:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_VLAN_ID_CONTROL, 0);
            break;

        case Ax179AInitMaskInterrupts:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_BM_INT_MASK, 0xFF);
            break;

        case Ax179AInitDisableRxDma:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_BM_RX_DMA_CTL, 0);
            break;

        case Ax179AInitDisableTxDma:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_BM_TX_DMA_CTL, 0);
            break;

        case Ax179AInitDisableArc:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_ARC_CTRL, 0);
            break;

        case Ax179AInitDisableSwp:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_SWP_CTRL, 0);
            break;

        case Ax179AInitDisableTxHeaderChecksum:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MAC_TX_HDR_CKSUM, 0);
            break;

        case Ax179AInitReadNodeId:
            Status = QueueMacRead(Device, AsyncCompletionRoutineInitTransfer, AX179A_NODE_ID, sizeof(CEnetAddr));
            break;

        case Ax179AInitWaitingForEthernetAddress:
            RecoverXboxAddresses(Device);
            if (Device->UnicastAddress.IsZero() && Device->AlternateUnicastAddress.IsZero()) {
                DbgPrint("[usbenet]: %s waiting for a valid Xbox MAC address.\n", GetName());
                return;
            }
            AdvanceInitStage(Device);
            return;

        case Ax179AInitWriteNodeId:
            WriteNodeId(Device);
            return;

        case Ax179AInitReadNodeIdBack:
            Status = QueueMacRead(Device, AsyncCompletionRoutineInitTransfer, AX179A_NODE_ID, sizeof(CEnetAddr));
            break;

        case Ax179AInitEnableRxChecksum:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_RX_CHECKSUM_CONTROL, Ax179AChecksumControl);
            break;

        case Ax179AInitEnableTxChecksum:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_TX_CHECKSUM_CONTROL, Ax179AChecksumControl);
            break;

        case Ax179AInitStartReceiving:
            StartReceiving(Device);
            return;

        case Ax179AInitWriteMonitorMode:
            Status = QueueMacWrite8(Device, AsyncCompletionRoutineInitTransfer, AX179A_MONITOR_MODE, Ax179AMonitorMode);
            break;

        case Ax179AInitWriteDefaultMedium:
            m_LinkSpeed = SupportsGigabit() ? Ax179ALink1000 : Ax179ALink100;
            m_FullDuplex = TRUE;
            Status = QueueMacWrite16(Device, AsyncCompletionRoutineInitTransfer, AX179A_MEDIUM_STATUS_MODE, BuildMediumMode());
            break;

        case Ax179AInitWaitPhyReady:
            m_PhyReadyRetryCount = 0;
            m_PhyReadyRetryPending = FALSE;
            Status = BeginReadPhy(Device, 2);
            break;

        case Ax179AInitPhyWriteMmdAccess1:
            /* The legacy PDB and the matched unified AX88179_PhyInitial body confirm
             * this Realtek MMD sequence for AX88179/178A and AX88179A/B.  Revisions
             * 0x0300+ branch to a newer private helper, so do not guess its writes. */
            if (GetVariant() != Ax179AVariant88179AB) {
                Device->InitStage = Ax179AInitPhyWriteMmdData2;
                AdvanceInitStage(Device);
                return;
            }
            Status = BeginWritePhy(Device, 0x0D, 0x0007);
            break;

        case Ax179AInitPhyWriteMmdData1:
            Status = BeginWritePhy(Device, 0x0E, 0x003C);
            break;

        case Ax179AInitPhyWriteMmdAccess2:
            Status = BeginWritePhy(Device, 0x0D, 0x4007);
            break;

        case Ax179AInitPhyWriteMmdData2:
            Status = BeginWritePhy(Device, 0x0E, 0x0000);
            break;

        case Ax179AInitPhyReadBmcr:
            /* AX88279 has additional 2.5-Gbit advertisement state outside the old
             * PDB. Preserve the existing firmware-owned setup instead of replacing
             * it with the legacy 10/100/1000 advertisement values. */
            if (GetVariant() == Ax179AVariant88279) {
                Device->InitStage = Ax179AInitPhyWriteGigabitAdvertisement;
                AdvanceInitStage(Device);
                return;
            }
            Status = BeginReadPhy(Device, MII_BMCR);
            break;

        case Ax179AInitPhyClearBmcr:
            Status = BeginWritePhy(Device, MII_BMCR, m_PhyInitBmcr);
            break;

        case Ax179AInitPhyPauseAfterBmcrClear:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, Ax88179PhyBmcrSettlePeriod);
            return;

        case Ax179AInitPhyWriteAdvertisement:
            Status = BeginWritePhy(Device, MII_ADVERTISE, Ax88179PhyAdvertisement);
            break;

        case Ax179AInitPhyWriteGigabitAdvertisement:
            Status = BeginWritePhy(Device, MII_CTRL1000, SupportsGigabit() ? Ax88179PhyGigabitAdvertisement : 0);
            break;

        case Ax179AInitRestartAutonegotiation:
            Status = BeginWritePhy(Device, MII_BMCR, GetVariant() == Ax179AVariant88279 ? 0x1200 : static_cast<USHORT>(m_PhyInitBmcr | MII_BMCR_AUTONEG_ENABLE | MII_BMCR_RESTART_AUTONEG));
            break;

        case Ax179AInitReady:
            Device->LinkPollTick = KeTimeStampBundle->TickCount;
            Device->InitStage = Ax179AInitComplete;
            Device->StartInterruptLinkStatus();
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 200);
            if (m_FullReinitializeActive) {
                m_FullReinitializeActive = FALSE;
                for (DWORD Pipe = 0; Pipe < ARRAYSIZE(m_RecoveryErrorCount); Pipe++)
                    ResetRecoveryCounters(static_cast<USBENET_TRANSPORT_PIPE>(Pipe));
                m_TxTimeoutCount = 0;
                m_TxOutstandingSinceTick = 0;
                DbgPrint("[usbenet-recovery]: %s full hardware reinitialization completed reason=%u count=%u.\n", GetName(), m_RecoveryReason, m_RecoveryCount);
            }
            DbgPrint("[usbenet]: %s initialization complete; chipStatus=0x%02X FW=%u.%u.%u.%u, PDB-grounded PHY setup and interrupt-driven link monitoring enabled with a 5000 ms PHY fallback.\n", GetName(), m_ChipStatus, m_FirmwareVersion[0], m_FirmwareVersion[1], m_FirmwareVersion[2], m_FirmwareVersion[3]);
            return;

        default:
            TRAP_ASSERT(FALSE);
            return;
    }

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s initialization stage %u could not queue transfer (0x%08X); continuing.\n", GetName(), Device->InitStage, Status);
        AdvanceInitStage(Device);
    }
}

VOID CAx179AFamily::CompleteInitTransfer(CUsbEnet* Device, NTSTATUS Status) {
    DWORD CompletedStage = Device->InitStage;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    PBYTE Data = reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize;
    DWORD Bytes = Device->ControlRequest.Transfer.BytesTransferred;

    if (NT_SUCCESS(Status)) {
        if (CompletedStage == Ax179AInitReadChipStatus && Bytes >= 1) {
            m_ChipStatus = Data[0];
            BYTE Version = static_cast<BYTE>((m_ChipStatus >> 4) & 0x0F);
            DbgPrint("[usbenet]: %s CHIP_STATUS=0x%02X version=0x%X expected=0x%X.\n", GetName(), m_ChipStatus, Version, GetExpectedChipVersion());
            if (Version != GetExpectedChipVersion())
                DbgPrint("[usbenet]: [WARNING] %s descriptor revision and CHIP_STATUS do not agree.\n", GetName());
        } else if (CompletedStage >= Ax179AInitReadFirmware0 && CompletedStage <= Ax179AInitReadFirmware2 && Bytes >= 1) {
            m_FirmwareVersion[CompletedStage - Ax179AInitReadFirmware0] = Data[0];
        } else if ((CompletedStage == Ax179AInitReadNodeId || CompletedStage == Ax179AInitReadNodeIdBack) && Bytes >= sizeof(CEnetAddr)) {
            memcpy(&Device->NodeId, Data, sizeof(CEnetAddr));
            m_NodeIdValid = !Device->NodeId.IsZero() && !Device->NodeId.IsMulticast();
            DbgPrint("[usbenet]: %s NODE_ID %s=%02X:%02X:%02X:%02X:%02X:%02X valid=%u.\n", GetName(), CompletedStage == Ax179AInitReadNodeId ? "hardware" : "readback", Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], m_NodeIdValid);
        }
    } else {
        DbgPrint("[usbenet]: %s initialization stage %u (%s) failed with 0x%08X.\n", GetName(), CompletedStage, Ax179AInitStageName(CompletedStage), Status);
    }

    if (Device->InitStage != Ax179AInitComplete)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx179AFamily::WriteNodeId(CUsbEnet* Device) {
    const CEnetAddr* Address = NULL;

    if (!Device->UnicastAddress.IsZero())
        Address = &Device->UnicastAddress;
    else if (!Device->AlternateUnicastAddress.IsZero())
        Address = &Device->AlternateUnicastAddress;

    if (Address == NULL) {
        if (Device->InitStage != Ax179AInitComplete)
            AdvanceInitStage(Device);
        return;
    }

    const BYTE* Bytes = reinterpret_cast<const BYTE*>(Address);
    DbgPrint("[usbenet]: %s programming NODE_ID %02X:%02X:%02X:%02X:%02X:%02X.\n", GetName(), Bytes[0], Bytes[1], Bytes[2], Bytes[3], Bytes[4], Bytes[5]);
    NTSTATUS Status = QueueMacWrite(Device, AsyncCompletionRoutineNodeWrite, AX179A_NODE_ID, sizeof(CEnetAddr), const_cast<CEnetAddr*>(Address));

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s could not queue NODE_ID write (0x%08X).\n", GetName(), Status);
        if (Device->InitStage == Ax179AInitWriteNodeId)
            AdvanceInitStage(Device);
    }
}

VOID CAx179AFamily::CompleteNodeWrite(CUsbEnet* Device, NTSTATUS Status) {
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (NT_SUCCESS(Status)) {
        const CEnetAddr* Address = !Device->UnicastAddress.IsZero() ? &Device->UnicastAddress : &Device->AlternateUnicastAddress;
        memcpy(&Device->NodeId, Address, sizeof(CEnetAddr));
        m_NodeIdValid = TRUE;
    } else {
        DbgPrint("[usbenet]: %s NODE_ID write failed with 0x%08X.\n", GetName(), Status);
    }

    if (Device->InitStage == Ax179AInitWriteNodeId)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx179AFamily::StartReceiving(CUsbEnet* Device) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    m_RxRecoveryInProgress = FALSE;
    m_RxInvalidAggregateStreak = 0;
    m_RxInvalidAggregateSignature = 0;
    Device->Flags |= ReceiveRunning;
    Device->SubmitReceive();
    m_ReceiveControl = BuildReceiveControl(Device);
    DbgPrint("[usbenet]: %s starting RX_CTL=0x%04X IP-align=%u.\n", GetName(), m_ReceiveControl, UsesIpAlignment());

    NTSTATUS Status = QueueMacWrite16(Device, (PUSBD_ASYNC_COMPLETION_ROUTINE)CUsbEnet::AsyncCompletionRoutineStartReceiving, 0x0B, m_ReceiveControl);
    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s could not queue RX start (0x%08X).\n", GetName(), Status);
}

VOID CAx179AFamily::UpdateReceiveFilter(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Device->InitStage < Ax179AInitStartReceiving || (Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    StartReceiving(Device);
}

VOID CAx179AFamily::RestartReceiving(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || (Device->Flags & ReceiveRunning) == 0)
        return;

    if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0) {
        m_RxRecoveryInProgress = FALSE;
        DbgPrint("[usbenet]: [RECOVERY] %s RX restart deferred because the control endpoint is busy.\n", GetName());
        return;
    }

    Device->Flags &= ~ReceiveRunning;
    Device->NoteReceiveRestart();

    for (ULONG Index = 0; Index < RECV_PACKET_COUNT; Index++) {
        PRECV_TRANSFER Packet = &Device->RecvPackets[Index];
        if (Packet->InFlight != 0)
            UsbdCancelAsyncTransfer(&Packet->Transfer);
    }

    NTSTATUS Status = QueueMacWrite16(Device, (PUSBD_ASYNC_COMPLETION_ROUTINE)CUsbEnet::AsyncCompletionRoutineStopReceiving, 0x0B, AX179A_RX_CTL_STOP);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s could not queue RX stop (0x%08X); receive completions will restore submissions.\n", GetName(), Status);
        Device->Flags |= ReceiveRunning;
        m_RxRecoveryInProgress = FALSE;
    }
}

VOID CAx179AFamily::ResetRecoveryCounters(USBENET_TRANSPORT_PIPE Pipe) {
    DWORD Index = static_cast<DWORD>(Pipe);
    if (Index < ARRAYSIZE(m_RecoveryErrorCount)) {
        m_RecoveryErrorCount[Index] = 0;
        m_RecoveryLastErrorTick[Index] = 0;
    }
}

VOID CAx179AFamily::HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe) {
    if (Pipe == UsbEnetTransportTransmit && Device->PendingXmitCount == 1)
        m_TxOutstandingSinceTick = KeTimeStampBundle->TickCount;
}

VOID CAx179AFamily::HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || Status == STATUS_CANCELLED || Status == STATUS_DEVICE_REMOVED || Status == STATUS_DEVICE_NOT_CONNECTED)
        return;

    DWORD Index = static_cast<DWORD>(Pipe);
    if (Index >= ARRAYSIZE(m_RecoveryErrorCount))
        return;

    DWORD CurrentTick = KeTimeStampBundle->TickCount;
    if (Pipe == UsbEnetTransportTransmit && Device->PendingXmitCount <= 1) {
        m_TxOutstandingSinceTick = 0;
        if (NT_SUCCESS(Status))
            m_TxTimeoutCount = 0;
    }

    if (NT_SUCCESS(Status)) {
        if (m_RecoveryLastErrorTick[Index] != 0 && CurrentTick - m_RecoveryLastErrorTick[Index] > Ax88179FamilyTransportErrorWindow)
            ResetRecoveryCounters(Pipe);
        return;
    }

    if (m_RecoveryLastErrorTick[Index] == 0 || CurrentTick - m_RecoveryLastErrorTick[Index] > Ax88179FamilyTransportErrorWindow)
        m_RecoveryErrorCount[Index] = 0;
    m_RecoveryLastErrorTick[Index] = CurrentTick;
    DWORD Count = ++m_RecoveryErrorCount[Index];
    BYTE Level = Count >= 5 ? Ax88179FamilyRecoveryResetDevice : Count >= 3 ? Ax88179FamilyRecoveryReinitialize : Ax88179FamilyRecoveryResetPipe;

    if (Level > m_RecoveryLevel) {
        m_RecoveryLevel = Level;
        m_RecoveryPipe = static_cast<BYTE>(Pipe);
        m_RecoveryRequestTick = CurrentTick;
        m_RecoveryReason = 0x300 + (Index << 8) + Count;
        DbgPrint("[usbenet-recovery]: %s transport error pipe=%u status=0x%08X count=%u level=%u.\n", GetName(), Index, Status, Count, static_cast<DWORD>(Level));
    }
}

NTSTATUS CAx179AFamily::RequestFullReinitialize(CUsbEnet* Device, DWORD Reason) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || Device->InitStage != Ax179AInitComplete)
        return STATUS_DEVICE_NOT_READY;
    if (m_FullReinitializePending || m_FullReinitializeActive)
        return STATUS_DEVICE_BUSY;

    m_FullReinitializePending = TRUE;
    m_RecoveryEndpointsReset = FALSE;
    m_RecoveryReason = Reason;
    m_RecoveryRequestTick = KeTimeStampBundle->TickCount;
    ++m_RecoveryCount;
    Device->PauseInterruptLinkStatus();
    Device->Flags &= ~ReceiveRunning;

    for (DWORD Index = 0; Index < RECV_PACKET_COUNT; Index++) {
        if (Device->RecvPackets[Index].InFlight != 0)
            UsbdCancelAsyncTransfer(&Device->RecvPackets[Index].Transfer);
    }
    for (DWORD Index = 0; Index < XMIT_TRACKER_COUNT; Index++) {
        if ((Device->XmitTrackers[Index].Flags & XMIT_FLAG_BUSY) != 0)
            UsbdCancelAsyncTransfer(&Device->XmitTrackers[Index].Transfer);
    }

    DbgPrint("[usbenet-recovery]: %s full hardware reinitialization requested reason=%u count=%u RX=%u TX=%u.\n", GetName(), Reason, m_RecoveryCount, Device->GetReceiveInFlightCount(), Device->PendingXmitCount);
    return STATUS_SUCCESS;
}

VOID CAx179AFamily::ProcessRecovery(CUsbEnet* Device, DWORD CurrentTick) {
    if (m_RecoveryLevel != Ax88179FamilyRecoveryIdle && !m_FullReinitializePending && !m_FullReinitializeActive) {
        BYTE Level = m_RecoveryLevel;
        USBENET_TRANSPORT_PIPE Pipe = static_cast<USBENET_TRANSPORT_PIPE>(m_RecoveryPipe);
        DWORD Reason = m_RecoveryReason;
        m_RecoveryLevel = Ax88179FamilyRecoveryIdle;

        if (Level == Ax88179FamilyRecoveryResetPipe) {
            Device->ResetTransportEndpoint(Pipe);
            if (Pipe == UsbEnetTransportReceive && (Device->Flags & ReceiveRunning) != 0)
                RestartReceiving(Device);
            DbgPrint("[usbenet-recovery]: %s reset transport endpoint %u%s.\n", GetName(), static_cast<DWORD>(Pipe), Pipe == UsbEnetTransportReceive ? " and restarted the RX engine" : "");
        } else if (Level == Ax88179FamilyRecoveryReinitialize) {
            RequestFullReinitialize(Device, Reason);
        } else {
            Device->PauseInterruptLinkStatus();
            Device->Flags &= ~ReceiveRunning;
            Device->LinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
            m_DeviceResetPending = TRUE;
            DbgPrint("[usbenet-recovery]: %s scheduling USB device reset after repeated transport failures.\n", GetName());
        }
    }

    if (!m_FullReinitializePending)
        return;

    DWORD Age = CurrentTick - m_RecoveryRequestTick;
    if (Age >= Ax88179FamilyFullReinitDrainTimeout) {
        if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0)
            UsbdCancelAsyncTransfer(&Device->ControlRequest.Transfer);
        Device->ResetTransportEndpoint(UsbEnetTransportReceive);
        Device->ResetTransportEndpoint(UsbEnetTransportTransmit);
        Device->ResetTransportEndpoint(UsbEnetTransportInterrupt);
        if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0)
            Device->Flags &= ~(USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS);
    }

    BOOL DrainBlocked = Device->GetReceiveInFlightCount() != 0 || Device->PendingXmitCount != 0 || Device->IsInterruptLinkStatusInFlight() || (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0;
    if (DrainBlocked) {
        if (Age >= Ax88179FamilyFullReinitFailureTimeout) {
            DbgPrint("[usbenet-recovery]: %s reinitialize drain timed out after %u ms RX=%u TX=%u interrupt=%u flags=0x%08X; escalating to USB device reset.\n", GetName(), Age, Device->GetReceiveInFlightCount(), Device->PendingXmitCount, Device->IsInterruptLinkStatusInFlight(), Device->Flags);
            m_FullReinitializePending = FALSE;
            m_FullReinitializeActive = FALSE;
            m_DeviceResetPending = TRUE;
        }
        return;
    }

    if (!m_RecoveryEndpointsReset) {
        Device->ResetTransportEndpoint(UsbEnetTransportReceive);
        Device->ResetTransportEndpoint(UsbEnetTransportTransmit);
        Device->ResetTransportEndpoint(UsbEnetTransportInterrupt);
        if (Device->PhysicalMemory != NULL)
            memset(Device->PhysicalMemory, 0, RECV_PACKET_COUNT * ReceiveBufferSize);
        m_RecoveryEndpointsReset = TRUE;
        DbgPrint("[usbenet-recovery]: %s all transports are quiescent; reset endpoint data toggles and cleared RX DMA before hardware replay.\n", GetName());
    }

    DWORD PreservedLinkState = Device->LinkState;
    DWORD Reason = m_RecoveryReason;
    DWORD Count = m_RecoveryCount;
    DWORD RequestTick = m_RecoveryRequestTick;
    ResetState(Device);
    m_FullReinitializePending = FALSE;
    m_FullReinitializeActive = TRUE;
    m_RecoveryReason = Reason;
    m_RecoveryCount = Count;
    m_RecoveryRequestTick = RequestTick;
    m_ReinitializeLinkGraceTick = CurrentTick;
    Device->Flags &= ~(ReceiveRunning | USBENET_STATE_00080000 | USBENET_STATE_NOTIFY_LINK_STATE | USBENET_STATE_REFRESH_PHY_REGISTERS | USBENET_STATE_READ_ALL_PHY_REGISTERS | USBENET_STATE_LINK_STATE_UPDATE_PENDING);
    Device->LinkState = PreservedLinkState;
    Device->TimerTick = CurrentTick;
    Device->InitStage = Ax179AInitBeginning;
    DbgPrint("[usbenet-recovery]: %s transports fully drained and flushed; replaying firmware, PDB-grounded PHY setup, MAC, bulk-in, medium, interrupt, and RX initialization.\n", GetName());
    AdvanceInitStage(Device);
}

NTSTATUS CAx179AFamily::BeginReadPhy(CUsbEnet* Device, USHORT Register) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return STATUS_DEVICE_REMOVED;
    if ((Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return STATUS_DEVICE_BUSY;

    Device->CurrentPhyRegister = Register;
    Device->CurrentPhyValue = 0;
    Device->Flags |= USBENET_STATE_PHY_READ_IN_PROGRESS;
    NTSTATUS Status = Device->QueueControlTransfer(AsyncCompletionRoutineReadPhy, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_ACCESS_PHY, Ax179APhyAddress, Register, sizeof(USHORT), NULL);
    if (!NT_SUCCESS(Status))
        Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
    return Status;
}

NTSTATUS CAx179AFamily::BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return STATUS_DEVICE_REMOVED;
    if ((Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return STATUS_DEVICE_BUSY;

    Device->CurrentPhyRegister = Register;
    Device->CurrentPhyValue = Value;
    Device->Flags |= USBENET_STATE_PHY_WRITE_IN_PROGRESS;
    USHORT WireValue = _byteswap_ushort(Value);
    NTSTATUS Status = Device->QueueControlTransfer(AsyncCompletionRoutineWritePhy, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX179A_ACCESS_PHY, Ax179APhyAddress, Register, sizeof(WireValue), &WireValue);
    if (!NT_SUCCESS(Status))
        Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;
    return Status;
}

BOOL CAx179AFamily::UpdateLinkState(CUsbEnet* Device, USHORT PhyStatus) {
    DWORD CurrentTick = KeTimeStampBundle->TickCount;
    // The interrupt endpoint is the authoritative physical-link source for the
    // firmware-backed AX88179A/772D family. Register 0x11 can report the speed
    // and duplex fields without repeating GMII_PHY_PHYSR_LINK (for example,
    // this adapter returns 0x9000 while the interrupt reports link=1).
    BOOL LinkActive = m_InterruptStatusSeen ? m_InterruptLinkUp : (PhyStatus & AX179A_PHY_STATUS_LINK) != 0;
    DWORD NewLinkState = 0;
    AX179A_LINK_SPEED NewSpeed = Ax179ALinkNone;
    BOOL FullDuplex = FALSE;

    if (m_ReinitializeLinkGraceTick != 0) {
        if (!LinkActive && CurrentTick - m_ReinitializeLinkGraceTick < 5000) {
            m_LastPhyStatus = PhyStatus;
            return FALSE;
        }
        m_ReinitializeLinkGraceTick = 0;
    }

    if (LinkActive) {
        USHORT SpeedBits = PhyStatus & AX179A_PHY_STATUS_SPEED_MASK;

        if (SpeedBits == AX179A_PHY_STATUS_2500 && GetMaximumWireSpeed() >= 2500)
            NewSpeed = Ax179ALink2500;
        else if (SpeedBits == AX179A_PHY_STATUS_GIGABIT && SupportsGigabit())
            NewSpeed = Ax179ALink1000;
        else if (SpeedBits == AX179A_PHY_STATUS_100)
            NewSpeed = Ax179ALink100;
        else
            NewSpeed = Ax179ALink10;

        FullDuplex = (PhyStatus & AX179A_PHY_STATUS_FULL_DUPLEX) != 0;
        // Gigabit and 2.5-Gbit operation on these adapters is full duplex. The
        // AX88179A PHY status value observed here is 0x9000, which carries the
        // gigabit speed code but not the legacy 0x2000 duplex bit.
        if (NewSpeed == Ax179ALink1000 || NewSpeed == Ax179ALink2500)
            FullDuplex = TRUE;
        NewLinkState = NIC_LINK_STATE_ACTIVE | NIC_LINK_STATE_NEGOTIATION_COMPLETE | NIC_LINK_STATE_TX_FLOW_CONTROL;

        if (NewSpeed == Ax179ALink1000 || NewSpeed == Ax179ALink2500)
            NewLinkState |= NIC_LINK_STATE_1000_MBPS;
        else if (NewSpeed == Ax179ALink100)
            NewLinkState |= NIC_LINK_STATE_100_MBPS;
        else
            NewLinkState |= NIC_LINK_STATE_10_MBPS;

        NewLinkState |= FullDuplex ? NIC_LINK_STATE_FULL_DUPLEX : NIC_LINK_STATE_HALF_DUPLEX;
    } else if (CurrentTick - Device->LinkPollTick >= 3500) {
        NewLinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    }

    if (NewLinkState == Device->LinkState && NewSpeed == m_LinkSpeed && FullDuplex == m_FullDuplex)
        return FALSE;

    Device->LinkState = NewLinkState;
    Device->LinkPollTick = CurrentTick;
    m_LastPhyStatus = PhyStatus;
    m_LinkSpeed = static_cast<BYTE>(NewSpeed);
    m_FullDuplex = FullDuplex;
    DWORD DisplaySpeed = NewSpeed == Ax179ALink2500 ? 2500 : NewSpeed == Ax179ALink1000 ? 1000 : NewSpeed == Ax179ALink100 ? 100 : NewSpeed == Ax179ALink10 ? 10 : 0;
    DbgPrint("[usbenet]: %s link state=0x%08X source=%s PHY=0x%04X speed=%u duplex=%s.\n", GetName(), NewLinkState, m_InterruptStatusSeen ? "interrupt+mii" : "mii", PhyStatus, DisplaySpeed, FullDuplex ? "full" : "half");

    if ((NewLinkState & NIC_LINK_STATE_ACTIVE) == 0)
        return TRUE;

    return !StartLinkReset(Device);
}

BOOL CAx179AFamily::StartLinkReset(CUsbEnet* Device) {
    if (m_LinkResetInProgress)
        return TRUE;

    m_LinkResetInProgress = TRUE;
    m_LinkResetStage = Ax179ALinkResetStopReceive;
    DbgPrint("[usbenet]: %s beginning experimental AX179A link reset.\n", GetName());

    if (QueueNextLinkResetTransfer(Device))
        return TRUE;

    m_LinkResetInProgress = FALSE;
    m_LinkResetStage = Ax179ALinkResetIdle;
    return FALSE;
}

BOOL CAx179AFamily::QueueNextLinkResetTransfer(CUsbEnet* Device) {
    NTSTATUS Status = STATUS_SUCCESS;
    BYTE Data[5] = { 0, 0, 0, 0, 0 };

    for (;;) {
        switch (m_LinkResetStage) {
            case Ax179ALinkResetWriteTxPause:
                if (m_LinkSpeed != Ax179ALink2500) {
                    m_LinkResetStage = Ax179ALinkResetWriteRxStatus;
                    continue;
                }
                Data[0] = 0x00; Data[1] = 0xF8; Data[2] = 0x07;
                Status = QueueMacWrite(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_TX_PAUSE, 3, Data);
                break;

            case Ax179ALinkResetWriteLsoEnhance:
                if (m_LinkSpeed != Ax179ALink2500) {
                    m_LinkResetStage = Ax179ALinkResetWriteBulkIn;
                    continue;
                }
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_LSO_ENHANCE_CTRL, 0x1D);
                break;

            case Ax179ALinkResetStopReceive:
                Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, 0x0B, AX179A_RX_CTL_STOP);
                break;

            case Ax179ALinkResetStopMacPath:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_PATH, 0);
                break;

            case Ax179ALinkResetWriteCdcDelay:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_CDC_DELAY_TX, 0xA5);
                break;

            case Ax179ALinkResetWritePauseWatermark:
                Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, AX179A_PAUSE_WATERLVL_LOW, 0x0410);
                break;

            case Ax179ALinkResetWriteTxGap:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_ETH_TX_GAP, 0);
                break;

            case Ax179ALinkResetWriteEp5:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_EP5_EHR, 0x07);
                break;

            case Ax179ALinkResetWriteNewPause:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_NEW_PAUSE_CTRL, 0x29);
                break;

            case Ax179ALinkResetWriteRxStatus:
                if (m_LinkSpeed == Ax179ALink2500) {
                    Data[0] = 0x78; Data[1] = 0x60; Data[2] = 0x00;
                } else if (m_LinkSpeed == Ax179ALink10) {
                    Data[0] = 0xFA; Data[1] = 0x70; Data[2] = 0xFF;
                } else {
                    Data[0] = 0x78; Data[1] = 0x70; Data[2] = 0x00;
                }
                Status = QueueMacWrite(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_RX_STATUS_CDC, 3, Data);
                break;

            case Ax179ALinkResetWriteRxDataCount:
                if (m_LinkSpeed == Ax179ALink2500) {
                    Data[0] = 0x40; Data[1] = 0x34;
                    Status = QueueMacWrite(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_RX_DATA_CDC_CNT, 2, Data);
                } else {
                    Data[0] = m_LinkSpeed == Ax179ALink10 ? 0xFA : 0x40;
                    Status = QueueMacWrite(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_RX_DATA_CDC_CNT, 1, Data);
                }
                break;

            case Ax179ALinkResetWriteBfmData:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_BFM_DATA, m_LinkSpeed == Ax179ALink2500 ? 0x80 : 0);
                break;

            case Ax179ALinkResetWriteBulkIn:
                Status = QueueMacWrite(Device, AsyncCompletionRoutineLinkReset, AX179A_RX_BULKIN_QCTRL, 5, const_cast<PBYTE>(SelectBulkInProfile()));
                break;

            case Ax179ALinkResetWriteMedium:
                Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, AX179A_MEDIUM_STATUS_MODE, BuildMediumMode());
                break;

            case Ax179ALinkResetStartReceive:
                Status = QueueMacWrite16(Device, AsyncCompletionRoutineLinkReset, 0x0B, m_ReceiveControl);
                break;

            case Ax179ALinkResetStartMacPath:
                Status = QueueMacWrite8(Device, AsyncCompletionRoutineLinkReset, AX179A_MAC_PATH, Ax179AMacPathReady);
                break;

            default:
                return FALSE;
        }
        break;
    }

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: %s link reset stage %u (%s) could not queue (0x%08X).\n", GetName(), m_LinkResetStage, Ax179ALinkResetStageName(m_LinkResetStage), Status);
        return FALSE;
    }

    return TRUE;
}

VOID CAx179AFamily::CompleteLinkResetTransfer(CUsbEnet* Device, NTSTATUS Status) {
    BOOL Notify = FALSE;
    BYTE CompletedStage = m_LinkResetStage;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s link reset stage %u (%s) failed with 0x%08X.\n", GetName(), CompletedStage, Ax179ALinkResetStageName(CompletedStage), Status);

    switch (CompletedStage) {
        case Ax179ALinkResetStopReceive: m_LinkResetStage = Ax179ALinkResetStopMacPath; break;
        case Ax179ALinkResetStopMacPath: m_LinkResetStage = Ax179ALinkResetWriteCdcDelay; break;
        case Ax179ALinkResetWriteCdcDelay: m_LinkResetStage = Ax179ALinkResetWritePauseWatermark; break;
        case Ax179ALinkResetWritePauseWatermark: m_LinkResetStage = Ax179ALinkResetWriteTxGap; break;
        case Ax179ALinkResetWriteTxGap: m_LinkResetStage = Ax179ALinkResetWriteEp5; break;
        case Ax179ALinkResetWriteEp5: m_LinkResetStage = Ax179ALinkResetWriteNewPause; break;
        case Ax179ALinkResetWriteNewPause: m_LinkResetStage = Ax179ALinkResetWriteTxPause; break;
        case Ax179ALinkResetWriteTxPause: m_LinkResetStage = Ax179ALinkResetWriteRxStatus; break;
        case Ax179ALinkResetWriteRxStatus: m_LinkResetStage = Ax179ALinkResetWriteRxDataCount; break;
        case Ax179ALinkResetWriteRxDataCount: m_LinkResetStage = Ax179ALinkResetWriteBfmData; break;
        case Ax179ALinkResetWriteBfmData: m_LinkResetStage = Ax179ALinkResetWriteLsoEnhance; break;
        case Ax179ALinkResetWriteLsoEnhance: m_LinkResetStage = Ax179ALinkResetWriteBulkIn; break;
        case Ax179ALinkResetWriteBulkIn: m_LinkResetStage = Ax179ALinkResetWriteMedium; break;
        case Ax179ALinkResetWriteMedium: m_LinkResetStage = Ax179ALinkResetStartReceive; break;
        case Ax179ALinkResetStartReceive: m_LinkResetStage = Ax179ALinkResetStartMacPath; break;
        case Ax179ALinkResetStartMacPath:
            m_LinkResetStage = Ax179ALinkResetComplete;
            m_LinkResetInProgress = FALSE;
            Notify = TRUE;
            DbgPrint("[usbenet]: %s link reset complete.\n", GetName());
            break;
        default:
            break;
    }

    if (m_LinkResetInProgress && !QueueNextLinkResetTransfer(Device)) {
        m_LinkResetInProgress = FALSE;
        m_LinkResetStage = Ax179ALinkResetIdle;
        Notify = TRUE;
    }

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (Notify)
        NotifyLinkStateChanged(Device);
}

VOID CAx179AFamily::CompleteReadPhy(CUsbEnet* Device, NTSTATUS Status) {
    BOOL Notify = FALSE;
    BOOL Advance = FALSE;
    BOOL RetryPhyReady = FALSE;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;

    if (NT_SUCCESS(Status) && Device->ControlRequest.Transfer.BytesTransferred >= sizeof(USHORT)) {
        USHORT Value = _byteswap_ushort(*reinterpret_cast<UNALIGNED USHORT*>(reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize));
        Device->CurrentPhyValue = Value;
        if (Device->CurrentPhyRegister < ARRAYSIZE(Device->PhyRegisters))
            Device->PhyRegisters[Device->CurrentPhyRegister] = Value;

        if (Device->InitStage == Ax179AInitWaitPhyReady && Device->CurrentPhyRegister == 2) {
            if (Value != 0 && Value != 0xFFFF) {
                DbgPrint("[usbenet]: %s external PHY became ready after %u retries; PHYSID1=0x%04X.\n", GetName(), m_PhyReadyRetryCount, Value);
                Advance = TRUE;
            } else if (m_PhyReadyRetryCount < Ax88179PhyReadyMaximumRetries) {
                ++m_PhyReadyRetryCount;
                m_PhyReadyRetryPending = TRUE;
                RetryPhyReady = TRUE;
            } else {
                DbgPrint("[usbenet]: [WARNING] %s external PHY did not become ready after %u retries; continuing with PHYSID1=0x%04X.\n", GetName(), m_PhyReadyRetryCount, Value);
                Advance = TRUE;
            }
        } else if (Device->InitStage == Ax179AInitPhyReadBmcr && Device->CurrentPhyRegister == MII_BMCR) {
            m_PhyInitBmcr = static_cast<USHORT>(Value & Ax88179PhyBmcrClearMask);
            if (m_PhyInitBmcr != Value)
                Advance = TRUE;
            else {
                Device->InitStage = Ax179AInitPhyPauseAfterBmcrClear;
                AdvanceInitStage(Device);
            }
        } else if (Device->CurrentPhyRegister == Ax179APhyPhysicalStatus) {
            m_InterruptPhyRefreshPending = FALSE;
            Notify = UpdateLinkState(Device, Value);
        }
    } else {
        DbgPrint("[usbenet]: %s PHY read 0x%04X failed with 0x%08X.\n", GetName(), Device->CurrentPhyRegister, Status);
        if (Device->InitStage == Ax179AInitWaitPhyReady) {
            if (m_PhyReadyRetryCount < Ax88179PhyReadyMaximumRetries) {
                ++m_PhyReadyRetryCount;
                m_PhyReadyRetryPending = TRUE;
                RetryPhyReady = TRUE;
            } else {
                Advance = TRUE;
            }
        } else if (Device->InitStage == Ax179AInitPhyReadBmcr) {
            m_PhyInitBmcr = 0;
            Device->InitStage = Ax179AInitPhyPauseAfterBmcrClear;
            AdvanceInitStage(Device);
        }
    }

    if (Advance)
        AdvanceInitStage(Device);
    else if (RetryPhyReady)
        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88179PhyReadyRetryPeriod);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (Notify)
        NotifyLinkStateChanged(Device);
}

VOID CAx179AFamily::CompleteWritePhy(CUsbEnet* Device, NTSTATUS Status) {
    DWORD CompletedStage = Device->InitStage;
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: %s PHY write 0x%04X failed with 0x%08X.\n", GetName(), Device->CurrentPhyRegister, Status);

    if (CompletedStage >= Ax179AInitPhyWriteMmdAccess1 && CompletedStage <= Ax179AInitRestartAutonegotiation)
        AdvanceInitStage(Device);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx179AFamily::NotifyLinkStateChanged(CUsbEnet* Device) {
    Device->NotifyLinkStateChangedToUsers();
}

BOOL CAx179AFamily::UsesInterruptLinkStatus() const {
    return TRUE;
}

BOOL CAx179AFamily::ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Data == NULL || Length < 8)
        return FALSE;

    BOOL LinkUp = (Data[2] & 0x01) != 0;
    BOOL FirstStatus = !m_InterruptStatusSeen;
    BOOL LinkChanged = FirstStatus || LinkUp != m_InterruptLinkUp;
    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    m_InterruptStatusSeen = TRUE;
    m_InterruptLinkUp = LinkUp;
    ++m_InterruptEventCount;
    Device->LinkPollTick = CurrentTick;

    if (LinkChanged || m_InterruptEventCount <= 4 || (m_InterruptEventCount & (m_InterruptEventCount - 1)) == 0)
        DbgPrint("[usbenet]: %s interrupt status #%u bytes=%02X %02X %02X %02X %02X %02X %02X %02X link=%u.\n", GetName(), m_InterruptEventCount, Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7], LinkUp);

    if (LinkUp) {
        m_InterruptPhyRefreshPending = TRUE;
        if (!m_LinkResetInProgress && (Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS | USBENET_STATE_CAN_USER_TRANSFER)) == 0) {
            NTSTATUS Status = BeginReadPhy(Device, Ax179APhyPhysicalStatus);
            if (NT_SUCCESS(Status))
                m_InterruptPhyRefreshPending = FALSE;
        }
        return FALSE;
    }

    m_InterruptPhyRefreshPending = FALSE;
    if (m_ReinitializeLinkGraceTick != 0 && CurrentTick - m_ReinitializeLinkGraceTick < 5000)
        return FALSE;
    m_ReinitializeLinkGraceTick = 0;
    m_LinkSpeed = Ax179ALinkNone;
    m_FullDuplex = FALSE;
    DWORD NewLinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    BOOL Notify = Device->LinkState != NewLinkState;
    Device->LinkState = NewLinkState;
    return Notify;
}

VOID CAx179AFamily::RunTimer(CUsbEnet* Device) {
    NicBaseTakeLockAtRaisedIrql(Device);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    if (Device->InitStage == Ax179AInitWaitPhyReady) {
        if (m_PhyReadyRetryPending && (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) == 0) {
            m_PhyReadyRetryPending = FALSE;
            NTSTATUS Status = BeginReadPhy(Device, 2);
            if (!NT_SUCCESS(Status)) {
                m_PhyReadyRetryPending = TRUE;
                CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88179PhyReadyRetryPeriod);
            }
        }

        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    if (Device->InitStage == Ax179AInitComplete)
        Device->PrintThroughputStats(CurrentTick);

    if (Device->InitStage == Ax179AInitComplete && Device->PendingXmitCount != 0 && m_TxOutstandingSinceTick != 0 && CurrentTick - m_TxOutstandingSinceTick >= Ax88179FamilyTransmitTimeout && m_RecoveryLevel == Ax88179FamilyRecoveryIdle && !m_FullReinitializePending && !m_FullReinitializeActive) {
        DWORD TimeoutCount = ++m_TxTimeoutCount;
        m_RecoveryLevel = TimeoutCount >= 5 ? Ax88179FamilyRecoveryResetDevice : TimeoutCount >= 3 ? Ax88179FamilyRecoveryReinitialize : Ax88179FamilyRecoveryResetPipe;
        m_RecoveryPipe = UsbEnetTransportTransmit;
        m_RecoveryRequestTick = CurrentTick;
        m_RecoveryReason = 0x400 + TimeoutCount;
        m_TxOutstandingSinceTick = CurrentTick;
        DbgPrint("[usbenet-recovery]: %s TX timeout with %u pending transfers; count=%u level=%u.\n", GetName(), Device->PendingXmitCount, TimeoutCount, static_cast<DWORD>(m_RecoveryLevel));
    }

    ProcessRecovery(Device, CurrentTick);
    BOOL ResetDevice = m_DeviceResetPending;
    m_DeviceResetPending = FALSE;
    if (ResetDevice) {
        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        Device->ResetUsbDevice();
        return;
    }

    if (m_FullReinitializePending) {
        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 25);
        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    if (Device->InitStage != Ax179AInitComplete) {
        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    if (!m_LinkResetInProgress && (Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS | USBENET_STATE_CAN_USER_TRANSFER)) == 0 && (m_InterruptPhyRefreshPending || !m_InterruptStatusSeen || CurrentTick - Device->LinkPollTick >= 5000)) {
        NTSTATUS Status = BeginReadPhy(Device, Ax179APhyPhysicalStatus);
        if (NT_SUCCESS(Status))
            m_InterruptPhyRefreshPending = FALSE;
    }

    CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 1000);

    TRAP_ASSERT(Device->PreviousIrql == 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
}

BOOL CAx179AFamily::InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context) {
    ++m_RxAggregateSequence;

    if (Length < sizeof(ULONGLONG))
        return FALSE;

    ULONGLONG Header = Ax179AReadLe64(Buffer + Length - sizeof(ULONGLONG));
    DWORD PacketCount = static_cast<DWORD>(Header) & Ax179ARxDescriptorCountMask;
    DWORD MetadataOffset = (static_cast<DWORD>(Header) & Ax179ARxDescriptorOffsetMask) >> Ax179ARxDescriptorOffsetShift;
    DWORD TrailerOffset = Length - sizeof(ULONGLONG);
    BOOL Valid = PacketCount != 0 && PacketCount <= TrailerOffset / sizeof(ULONGLONG) && MetadataOffset == TrailerOffset - PacketCount * sizeof(ULONGLONG);

    if (!Valid) {
        ULONGLONG Signature = Header ^ (static_cast<ULONGLONG>(Length) << 32);
        if (Signature == m_RxInvalidAggregateSignature)
            ++m_RxInvalidAggregateStreak;
        else {
            m_RxInvalidAggregateSignature = Signature;
            m_RxInvalidAggregateStreak = 1;
        }

        if (m_RxInvalidAggregateStreak <= 4 || (m_RxInvalidAggregateStreak & (m_RxInvalidAggregateStreak - 1)) == 0)
            DbgPrint("[usbenet]: [DISCARD] %s invalid AX179A aggregate count=%u metadataOffset=%u length=%u header=0x%08X%08X streak=%u.\n", GetName(), PacketCount, MetadataOffset, Length, static_cast<DWORD>(Header >> 32), static_cast<DWORD>(Header), m_RxInvalidAggregateStreak);

        if (!m_RxRecoveryInProgress && m_RxFrameSequence != 0 && Length == ReceiveBufferSize && m_RxInvalidAggregateStreak == 4) {
            m_RxRecoveryInProgress = TRUE;
            m_RxRecoveryStartTick = KeTimeStampBundle->TickCount;
            DbgPrint("[usbenet]: [RECOVERY] %s detected stale full-buffer RX completions after a stall; draining them without cancelling the RX ring.\n", GetName());
        }

        if (m_RxRecoveryInProgress && Length == ReceiveBufferSize && !m_FullReinitializePending && !m_FullReinitializeActive && (m_RxInvalidAggregateStreak >= 256 || KeTimeStampBundle->TickCount - m_RxRecoveryStartTick >= Ax88179FamilyRxDesyncEscalationDelay)) {
            ++m_RxHardRecoveryCount;
            DWORD Reason = 0x600 + m_RxHardRecoveryCount;
            NTSTATUS RecoveryStatus = RequestFullReinitialize(Device, Reason);
            DbgPrint("[usbenet-recovery]: %s persistent stale RX drain escalation attempt=%u status=0x%08X.\n", GetName(), m_RxHardRecoveryCount, RecoveryStatus);
        }
        return FALSE;
    }

    if (m_RxRecoveryInProgress)
        DbgPrint("[usbenet]: [RECOVERY] %s received a valid aggregate; stale RX drain completed without resetting the ring.\n", GetName());

    m_RxRecoveryInProgress = FALSE;
    m_RxInvalidAggregateStreak = 0;
    m_RxInvalidAggregateSignature = 0;
    m_RxRecoveryStartTick = 0;
    Context->Buffer = Buffer;
    Context->Length = Length;
    Context->DataOffset = 0;
    Context->MetadataOffset = MetadataOffset;
    Context->MetadataIndex = 0;
    Context->MetadataCount = PacketCount;
    return TRUE;
}

USBENET_RX_PARSE_RESULT CAx179AFamily::GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame) {
    UNREFERENCED_PARAMETER(Device);

    if (Context->MetadataIndex >= Context->MetadataCount)
        return Context->DataOffset == Context->MetadataOffset ? UsbEnetRxParseComplete : UsbEnetRxParseError;

    DWORD DescriptorOffset = Context->MetadataOffset + Context->MetadataIndex * sizeof(ULONGLONG);
    if (DescriptorOffset + sizeof(ULONGLONG) > Context->Length - sizeof(ULONGLONG))
        return UsbEnetRxParseError;

    ULONGLONG Descriptor = Ax179AReadLe64(Context->Buffer + DescriptorOffset);
    DWORD StoredLength = static_cast<DWORD>((Descriptor & Ax179ARxDescriptorLengthMask) >> Ax179ARxDescriptorLengthShift);
    DWORD AlignedLength = Ax179AAlign8(StoredLength);
    DWORD PrefixLength = UsesIpAlignment() ? 2 : 0;
    DWORD DataOffset = Context->DataOffset;
    ++Context->MetadataIndex;
    Context->DataOffset += AlignedLength;
    ++m_RxDescriptorSequence;

    if (StoredLength < PrefixLength || Context->DataOffset > Context->MetadataOffset)
        return UsbEnetRxParseError;

    if ((static_cast<DWORD>(Descriptor) & Ax179ARxDescriptorDrop) != 0 || (static_cast<DWORD>(Descriptor) & Ax179ARxDescriptorRxOk) == 0)
        return UsbEnetRxParseSkip;

    Frame->Data = Context->Buffer + DataOffset + PrefixLength;
    Frame->Length = StoredLength - PrefixLength;
    Frame->Flags = static_cast<DWORD>(Descriptor);
    ++m_RxFrameSequence;

    if (Ax179AShouldLog(m_RxFrameSequence) && Frame->Length >= 14)
        DbgPrint("[usbenet]: %s RX frame #%u length=%u type=0x%02X%02X dst=%02X:%02X:%02X:%02X:%02X:%02X.\n", GetName(), m_RxFrameSequence, Frame->Length, Frame->Data[12], Frame->Data[13], Frame->Data[0], Frame->Data[1], Frame->Data[2], Frame->Data[3], Frame->Data[4], Frame->Data[5]);

    return UsbEnetRxParseFrame;
}

BOOL CAx179AFamily::AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator) {
    if (Length > Ax179ATxDescriptorLengthMask)
        return FALSE;

    if (AggregateOffset != 0) {
        PBYTE AggregateStart = Buffer - AggregateOffset;
        DWORD Offset = 0;

        while (Offset < AggregateOffset) {
            ULONGLONG ExistingDescriptor = Ax179AReadLe64(AggregateStart + Offset);
            DWORD ExistingLength = static_cast<DWORD>(ExistingDescriptor) & Ax179ATxDescriptorLengthMask;
            DWORD ExistingRecord = Ax179AAlign8(sizeof(ULONGLONG) + ExistingLength);
            if (ExistingRecord == 0 || Offset + ExistingRecord > AggregateOffset)
                return FALSE;
            if (Offset + ExistingRecord == AggregateOffset) {
                ExistingDescriptor &= ~static_cast<ULONGLONG>(Ax179ATxDescriptorDropPadding);
                Ax179AWriteLe64(AggregateStart + Offset, ExistingDescriptor);
            }
            Offset += ExistingRecord;
        }
    }

    DWORD RecordLength = Ax179AAlign8(sizeof(ULONGLONG) + Length);
    BOOL AddTerminator = Device->TransmitMaxPacketSize != 0 && ((AggregateOffset + RecordLength) % Device->TransmitMaxPacketSize) == 0;
    DWORD TotalLength = RecordLength + (AddTerminator ? sizeof(ULONGLONG) : 0);
    if (TotalLength > Capacity)
        return FALSE;

    ULONGLONG Descriptor = Length & Ax179ATxDescriptorLengthMask;
    if (AddTerminator)
        Descriptor |= Ax179ATxDescriptorDropPadding;

    Ax179AWriteLe64(Buffer, Descriptor);
    memcpy(Buffer + sizeof(ULONGLONG), Frame, Length);
    memset(Buffer + sizeof(ULONGLONG) + Length, 0, TotalLength - sizeof(ULONGLONG) - Length);
    *FramedLength = RecordLength;
    *BytesWritten = TotalLength;
    *HasTerminator = AddTerminator;
    ++m_TxFrameSequence;

    if (Ax179AShouldLog(m_TxFrameSequence) && Length >= 14) {
        const BYTE* Ethernet = static_cast<const BYTE*>(Frame);
        DbgPrint("[usbenet]: %s TX frame #%u length=%u record=%u aggregateOffset=%u terminator=%u type=0x%02X%02X.\n", GetName(), m_TxFrameSequence, Length, RecordLength, AggregateOffset, AddTerminator, Ethernet[12], Ethernet[13]);
    }

    return TRUE;
}

VOID __fastcall CAx179AFamily::AsyncCompletionRoutineInitTransfer(PVOID RequestPointer, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST Request = static_cast<PUSBD_ASYNC_REQUEST>(RequestPointer);
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    static_cast<CAx179AFamily*>(g_UsbEnetChipset)->CompleteInitTransfer(Device, Status);
}

VOID __fastcall CAx179AFamily::AsyncCompletionRoutineNodeWrite(PVOID RequestPointer, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST Request = static_cast<PUSBD_ASYNC_REQUEST>(RequestPointer);
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    static_cast<CAx179AFamily*>(g_UsbEnetChipset)->CompleteNodeWrite(Device, Status);
}

VOID __fastcall CAx179AFamily::AsyncCompletionRoutineReadPhy(PVOID RequestPointer, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST Request = static_cast<PUSBD_ASYNC_REQUEST>(RequestPointer);
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    static_cast<CAx179AFamily*>(g_UsbEnetChipset)->CompleteReadPhy(Device, Status);
}

VOID __fastcall CAx179AFamily::AsyncCompletionRoutineWritePhy(PVOID RequestPointer, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST Request = static_cast<PUSBD_ASYNC_REQUEST>(RequestPointer);
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    static_cast<CAx179AFamily*>(g_UsbEnetChipset)->CompleteWritePhy(Device, Status);
}

VOID __fastcall CAx179AFamily::AsyncCompletionRoutineLinkReset(PVOID RequestPointer, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST Request = static_cast<PUSBD_ASYNC_REQUEST>(RequestPointer);
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    static_cast<CAx179AFamily*>(g_UsbEnetChipset)->CompleteLinkResetTransfer(Device, Status);
}

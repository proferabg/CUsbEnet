// SPDX-License-Identifier: GPL-2.0-only
#include "stdafx.h"
#include "Rtl8153.h"
#include "CUsbEnet.h"
#include "Main.h"

#if USBENET_DEBUG
#define USBENET_LOG(_fmt, ...) DbgPrint("[RTL8153] " _fmt "\n", __VA_ARGS__)
#define USBENET_LOG0(_msg) DbgPrint("[RTL8153] " _msg "\n")
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
#define TRAP_THREAD(val) { TRAP_ASSERT(KeGetCurrentThread() == val); }
#else
#define TRAP_ASSERT(Expression) { __noop; }
#define TRAP_THREAD(val) { __noop; }
#endif

CRtl8153 g_Rtl8153Chipset;

static DWORD Rtl8153AlignUp(DWORD Value, DWORD Alignment) {
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static VOID Rtl8153IncrementEthernetAddress(CEnetAddr* Address) {
    for (INT Index = sizeof(Address->_ab) - 1; Index >= 0; --Index) {
        if (++Address->_ab[Index] != 0)
            break;
    }
}

const char* CRtl8153::GetName() const {
    return "RTL8153";
}

USHORT CRtl8153::GetVendorIdRaw() const {
    // USB descriptor 0x0BDA appears byte-swapped when read as a USHORT on PPC.
    return 0xDA0B;
}

USHORT CRtl8153::GetProductIdRaw() const {
    // USB descriptor 0x8153 appears byte-swapped when read as a USHORT on PPC.
    return 0x5381;
}

BOOL CRtl8153::IsImplemented() const {
    return TRUE;
}

BOOL CRtl8153::SupportsTransmitAggregation() const {
    return TRUE;
}

DWORD CRtl8153::GetMaximumFrameSize() const {
    return StandardFrameLimit;
}

DWORD CRtl8153::GetMaximumAggregateTransferSize() const {
    return MaximumAggregateSize;
}

DWORD CRtl8153::GetTransmitHeaderSize() const {
    return sizeof(RTL8153_TX_DESCRIPTOR);
}

DWORD CRtl8153::GetTransmitTerminatorSize() const {
    return 0;
}

BOOL CRtl8153::IsReady(CUsbEnet* Device) {
    return Device->InitStage == Rtl8153InitComplete;
}

BOOL CRtl8153::IsNodeIdAvailable(CUsbEnet* Device) {
    UNREFERENCED_PARAMETER(Device);
    return m_NodeIdValid;
}

BOOL CRtl8153::IsValidEthernetAddress(const CEnetAddr* Address) {
    return Address != NULL && !Address->IsZero() && !Address->IsMulticast();
}

BOOL CRtl8153::IsSupportedHardwareVersion(DWORD Version) {
    return Version == 0x5C00 || Version == 0x5C10 || Version == 0x5C20 || Version == 0x5C30 || Version == 0x6000 || Version == 0x6010;
}

const char* CRtl8153::VersionName(DWORD Version) const {
    switch (Version) {
        case 0x5C00: return "RTL8153 v3";
        case 0x5C10: return "RTL8153 v4";
        case 0x5C20: return "RTL8153 v5";
        case 0x5C30: return "RTL8153 v6";
        case 0x6000: return "RTL8153B v8";
        case 0x6010: return "RTL8153B v9";
        default: return "unknown RTL8153 revision";
    }
}

VOID CRtl8153::ResetState(CUsbEnet* Device) {
    Device->InitStage = Rtl8153InitBeginning;
    Device->PhyAddress = 0;
    m_RuntimeOperation = Rtl8153RuntimeNone;
    m_PendingSpace = Rtl8153RegisterPla;
    m_PendingRegister = 0;
    m_HardwareVersion = 0;
    m_ReceiveControlBase = 0;
    m_CommandValue = 0;
    m_MiscValue = 0;
    m_CpcrValue = 0;
    m_BmcrValue = 0;
    m_MacPowerControl3Value = 0;
    m_SffValue = 0;
    m_UsbControlValue = 0;
    m_TcrValue = 0;
    m_LastPhyStatus = 0;
    m_ResetPollCount = 0;
    m_LinkListPollCount = 0;
    m_InterruptEventCount = 0;
    m_LastLinkPollTick = 0;
    m_BmuValue = 0;
    m_OobValue = 0;
    m_NodeIdValid = FALSE;
    m_InterruptStatusSeen = FALSE;
    m_LinkPollPending = FALSE;
    m_FilterUpdatePending = FALSE;
    m_MacUpdatePending = FALSE;
    m_RuntimeMacStep = 0;
}

NTSTATUS CRtl8153::QueueRead(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register) {
    m_PendingSpace = Space;
    m_PendingRegister = Register;
    USHORT AlignedRegister = static_cast<USHORT>(Register & ~3);
    return Device->QueueControlTransfer(AsyncCompletionRoutine, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, VendorRequest, AlignedRegister, static_cast<USHORT>(Space), sizeof(DWORD), NULL);
}

NTSTATUS CRtl8153::QueueWriteByte(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register, BYTE Value) {
    DWORD Offset = Register & 3;
    DWORD Data = static_cast<DWORD>(Value) << (Offset * 8);
    DWORD WireData = _byteswap_ulong(Data);
    USHORT ByteEnable = static_cast<USHORT>(ByteEnableByte << Offset);
    USHORT AlignedRegister = static_cast<USHORT>(Register & ~3);
    m_PendingSpace = Space;
    m_PendingRegister = Register;
    return Device->QueueControlTransfer(AsyncCompletionRoutine, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, VendorRequest, AlignedRegister, static_cast<USHORT>(Space | ByteEnable), sizeof(WireData), &WireData);
}

NTSTATUS CRtl8153::QueueWriteWord(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register, USHORT Value) {
    DWORD Offset = Register & 3;
    if (Offset > 2)
        return STATUS_INVALID_PARAMETER;

    DWORD Data = static_cast<DWORD>(Value) << (Offset * 8);
    DWORD WireData = _byteswap_ulong(Data);
    USHORT ByteEnable = static_cast<USHORT>(ByteEnableWord << Offset);
    USHORT AlignedRegister = static_cast<USHORT>(Register & ~3);
    m_PendingSpace = Space;
    m_PendingRegister = Register;
    return Device->QueueControlTransfer(AsyncCompletionRoutine, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, VendorRequest, AlignedRegister, static_cast<USHORT>(Space | ByteEnable), sizeof(WireData), &WireData);
}

NTSTATUS CRtl8153::QueueWriteDword(CUsbEnet* Device, RTL8153_REGISTER_SPACE Space, USHORT Register, DWORD Value) {
    if ((Register & 3) != 0)
        return STATUS_INVALID_PARAMETER;

    DWORD WireData = _byteswap_ulong(Value);
    m_PendingSpace = Space;
    m_PendingRegister = Register;
    return Device->QueueControlTransfer(AsyncCompletionRoutine, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, VendorRequest, Register, static_cast<USHORT>(Space | ByteEnableDword), sizeof(WireData), &WireData);
}

DWORD CRtl8153::ReadAlignedDword(CUsbEnet* Device) const {
    PBYTE Scratch = static_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize;
    return _byteswap_ulong(*reinterpret_cast<UNALIGNED DWORD*>(Scratch));
}

BYTE CRtl8153::ExtractByte(DWORD Value, USHORT Register) const {
    return static_cast<BYTE>((Value >> ((Register & 3) * 8)) & 0xFF);
}

USHORT CRtl8153::ExtractWord(DWORD Value, USHORT Register) const {
    return static_cast<USHORT>((Value >> ((Register & 3) * 8)) & 0xFFFF);
}

DWORD CRtl8153::BuildReceiveControl(CUsbEnet* Device) const {
    DWORD ReceiveControl = m_ReceiveControlBase | ReceiveAcceptPhysical;

    if ((Device->AggregateReceiveFilter & (NIC_RECV_DEST_FLAG_PROMISCUOUS | NIC_RECV_DEST_FLAG_ALTERNATE_UNICAST)) != 0)
        ReceiveControl |= ReceiveAcceptPromiscuous;
    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_MULTICAST) != 0)
        ReceiveControl |= ReceiveAcceptMulticast;
    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_BROADCAST) != 0)
        ReceiveControl |= ReceiveAcceptBroadcast;

    return ReceiveControl;
}

DWORD CRtl8153::DecodeLinkState(USHORT Status) const {
    DWORD LinkStateValue = NIC_LINK_STATE_NEGOTIATION_COMPLETE;

    if ((Status & LinkStatus) == 0)
        return LinkStateValue;

    LinkStateValue |= NIC_LINK_STATE_ACTIVE;

    if ((Status & Link1000Mbps) != 0)
        LinkStateValue |= NIC_LINK_STATE_1000_MBPS;
    else if ((Status & Link100Mbps) != 0)
        LinkStateValue |= NIC_LINK_STATE_100_MBPS;
    else
        LinkStateValue |= NIC_LINK_STATE_10_MBPS;

    LinkStateValue |= (Status & LinkFullDuplex) != 0 ? NIC_LINK_STATE_FULL_DUPLEX : NIC_LINK_STATE_HALF_DUPLEX;

    if ((Status & LinkTxFlow) != 0)
        LinkStateValue |= NIC_LINK_STATE_TX_FLOW_CONTROL;

    return LinkStateValue;
}

VOID CRtl8153::ApplyLinkStatus(CUsbEnet* Device, USHORT Status, PBOOL Notify) {
    DWORD NewLinkState = DecodeLinkState(Status);
    m_LastPhyStatus = Status;

    if (Device->LinkState != NewLinkState) {
        Device->LinkState = NewLinkState;
        if (Notify != NULL)
            *Notify = TRUE;

        DbgPrint("[usbenet]: RTL8153 link state=0x%08X phystatus=0x%04X speed=%s duplex=%s.\n",
            NewLinkState,
            Status,
            (NewLinkState & NIC_LINK_STATE_1000_MBPS) != 0 ? "1000" : (NewLinkState & NIC_LINK_STATE_100_MBPS) != 0 ? "100" : (NewLinkState & NIC_LINK_STATE_ACTIVE) != 0 ? "10" : "down",
            (NewLinkState & NIC_LINK_STATE_FULL_DUPLEX) != 0 ? "full" : "half");
    }
}

VOID CRtl8153::FailInitialization(CUsbEnet* Device, NTSTATUS Status, const char* Operation) {
    DbgPrint("[usbenet]: RTL8153 initialization failed at stage %u (%s), status=0x%08X.\n", Device->InitStage, Operation, Status);
    Device->InitStage = Rtl8153InitFailed;
    Device->LinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
}

VOID CRtl8153::AdvanceInitStage(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    RTL8153_INIT_STAGE NextStage = static_cast<RTL8153_INIT_STAGE>(Device->InitStage + 1);
    Device->InitStage = NextStage;
    NTSTATUS Status = STATUS_SUCCESS;

    USBENET_LOG("RTL8153 init stage=%u", Device->InitStage);

    switch (NextStage) {
        case Rtl8153InitReadVersion:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaTcr0);
            break;

        case Rtl8153InitReadBootControl:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaBootCtrl);
            break;

        case Rtl8153InitReadHardwareMacLow:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaBackup);
            break;

        case Rtl8153InitReadHardwareMacHigh:
            Status = QueueRead(Device, Rtl8153RegisterPla, static_cast<USHORT>(PlaBackup + 4));
            break;

        case Rtl8153InitReadMiscGate:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaMisc1);
            break;

        case Rtl8153InitWriteMiscGate:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaMisc1, static_cast<USHORT>(m_MiscValue | MiscRxReadyGated));
            break;

        case Rtl8153InitReadReceiveControl:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaRcr);
            break;

        case Rtl8153InitWriteReceiveControlStopped:
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaRcr, m_ReceiveControlBase & ~ReceiveAcceptAll);
            break;

        case Rtl8153InitResetNic:
            m_ResetPollCount = 0;
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCr, CommandReset);
            break;

        case Rtl8153InitPollNicReset:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaCr);
            break;

        case Rtl8153InitReadBmuReset:
            Status = QueueRead(Device, Rtl8153RegisterUsb, UsbBmuReset);
            break;

        case Rtl8153InitWriteBmuResetClear:
            Status = QueueWriteByte(Device, Rtl8153RegisterUsb, UsbBmuReset, static_cast<BYTE>(m_BmuValue & ~(BmuResetIn | BmuResetOut)));
            break;

        case Rtl8153InitWriteBmuResetSet:
            Status = QueueWriteByte(Device, Rtl8153RegisterUsb, UsbBmuReset, static_cast<BYTE>(m_BmuValue | BmuResetIn | BmuResetOut));
            break;

        case Rtl8153InitReadOobControl:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaOobCtrl);
            break;

        case Rtl8153InitWriteOobControl:
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaOobCtrl, static_cast<BYTE>(m_OobValue & ~OobNow));
            break;

        case Rtl8153InitReadSffStatus:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaSffStatus7);
            break;

        case Rtl8153InitWriteSffStatus:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaSffStatus7, static_cast<USHORT>(m_SffValue & ~SffMcuBorwEnable));
            break;

        case Rtl8153InitPollLinkListReady:
            m_LinkListPollCount = 0;
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaOobCtrl);
            break;

        case Rtl8153InitReadSffReinit:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaSffStatus7);
            break;

        case Rtl8153InitWriteSffReinit:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaSffStatus7, static_cast<USHORT>(m_SffValue | SffReinitLinkList));
            break;

        case Rtl8153InitPollLinkListReinit:
            m_LinkListPollCount = 0;
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaOobCtrl);
            break;

        case Rtl8153InitReadCpcr:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaCpcr);
            break;

        case Rtl8153InitWriteCpcr:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaCpcr, static_cast<USHORT>(m_CpcrValue & ~CpcrRxVlan));
            break;

        case Rtl8153InitWriteOcpBase:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaOcpGphyBase, OcpBaseValue);
            break;

        case Rtl8153InitReadBmcr:
            Status = QueueRead(Device, Rtl8153RegisterPla, OcpMiiBmcr);
            break;

        case Rtl8153InitWriteBmcr:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, OcpMiiBmcr, static_cast<USHORT>((m_BmcrValue & ~BmcrPowerDown) | BmcrAutoNegotiationEnable | BmcrRestartAutoNegotiation));
            break;

        case Rtl8153InitWriteRms:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaRms, static_cast<USHORT>(StandardFrameLimit));
            break;

        case Rtl8153InitWriteMtps:
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaMtps, MtpsJumbo);
            break;

        case Rtl8153InitReadTcrAutoFifo:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaTcr0);
            break;

        case Rtl8153InitWriteTcrAutoFifo:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaTcr0, static_cast<USHORT>(m_TcrValue | TcrAutoFifo));
            break;

        case Rtl8153InitResetNicSecond:
            m_ResetPollCount = 0;
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCr, CommandReset);
            break;

        case Rtl8153InitPollNicResetSecond:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaCr);
            break;

        case Rtl8153InitWriteRxFifo0:
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaRxFifoCtrl0, RxFifoThreshold1Normal);
            break;

        case Rtl8153InitWriteRxFifo1:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaRxFifoCtrl1, RxFifoThreshold2Normal);
            break;

        case Rtl8153InitWriteRxFifo2:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaRxFifoCtrl2, RxFifoThreshold3Normal);
            break;

        case Rtl8153InitWriteTxFifo:
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaTxFifoCtrl, TxFifoThresholdNormal);
            break;

        case Rtl8153InitWriteRxBufferThreshold: {
            BOOL Rtl8153B = m_HardwareVersion == 0x6000 || m_HardwareVersion == 0x6010;
            Status = QueueWriteDword(Device, Rtl8153RegisterUsb, UsbRxBufferThreshold, Rtl8153B ? UsbRxThresholdB : UsbRxThresholdHigh);
            break;
        }

        case Rtl8153InitReadMacPowerControl3:
            if (m_HardwareVersion == 0x6000 || m_HardwareVersion == 0x6010)
                Status = QueueRead(Device, Rtl8153RegisterPla, PlaMacPowerCtrl3);
            else {
                AdvanceInitStage(Device);
                return;
            }
            break;

        case Rtl8153InitWriteMacPowerControl3:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaMacPowerCtrl3, static_cast<USHORT>(m_MacPowerControl3Value & ~PlaMcuSpeedDownEnable));
            break;

        case Rtl8153InitWriteRxEarlyTimeout: {
            BOOL Rtl8153B = m_HardwareVersion == 0x6000 || m_HardwareVersion == 0x6010;
            Status = QueueWriteWord(Device, Rtl8153RegisterUsb, UsbRxEarlyTimeout, Rtl8153B ? Rtl8153BEarlyTimeout : LegacyHighSpeedEarlyTimeout);
            break;
        }

        case Rtl8153InitWriteRxExtraAggregateTimer:
            if (m_HardwareVersion == 0x6000 || m_HardwareVersion == 0x6010)
                Status = QueueWriteWord(Device, Rtl8153RegisterUsb, UsbRxExtraAggregateTimer, Rtl8153BExtraAggregateTimer);
            else {
                AdvanceInitStage(Device);
                return;
            }
            break;

        case Rtl8153InitWriteRxEarlySize: {
            DWORD Reserved = StandardFrameLimit + EthernetFcsLength + sizeof(RTL8153_RX_DESCRIPTOR) + RxAlignment;
            DWORD Available = ReceiveBufferSize > Reserved ? ReceiveBufferSize - Reserved : 0;
            USHORT Divisor = (m_HardwareVersion == 0x6000 || m_HardwareVersion == 0x6010) ? 8 : 4;
            Status = QueueWriteWord(Device, Rtl8153RegisterUsb, UsbRxEarlySize, static_cast<USHORT>(Available / Divisor));
            break;
        }

        case Rtl8153InitReadUsbControl:
            Status = QueueRead(Device, Rtl8153RegisterUsb, UsbUsbCtrl);
            break;

        case Rtl8153InitWriteUsbControl:
            Status = QueueWriteWord(Device, Rtl8153RegisterUsb, UsbUsbCtrl, static_cast<USHORT>(m_UsbControlValue & ~(UsbRxAggregationDisable | UsbRxZeroEnable)));
            break;

        case Rtl8153InitWaitingForEthernetAddress: {
            CEnetAddr* TitleAddress = Main::GetKernelTitleEthernetAddress();
            CEnetAddr* DebugAddress = Main::GetKernelDebugEthernetAddress();

            if (IsValidEthernetAddress(TitleAddress))
                memcpy(&Device->UnicastAddress, TitleAddress, sizeof(CEnetAddr));

            if (Main::Devkit) {
                if (IsValidEthernetAddress(DebugAddress)) {
                    memcpy(&Device->AlternateUnicastAddress, DebugAddress, sizeof(CEnetAddr));
                } else if (IsValidEthernetAddress(TitleAddress)) {
                    memcpy(&Device->AlternateUnicastAddress, TitleAddress, sizeof(CEnetAddr));
                    Rtl8153IncrementEthernetAddress(&Device->AlternateUnicastAddress);
                }
            } else {
                Device->AlternateUnicastAddress.SetZero();
            }

            if (!IsValidEthernetAddress(&Device->UnicastAddress)) {
                DbgPrint("[usbenet]: RTL8153 waiting for title Ethernet address.\n");
                return;
            }

            const BYTE* Mac = Device->UnicastAddress._ab;
            DbgPrint("[usbenet]: RTL8153 using title MAC %02X:%02X:%02X:%02X:%02X:%02X.\n", Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
            AdvanceInitStage(Device);
            return;
        }

        case Rtl8153InitWriteMacConfig:
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCrwecr, ConfigMode);
            break;

        case Rtl8153InitWriteMacLow: {
            const BYTE* Mac = Device->UnicastAddress._ab;
            DWORD Value = static_cast<DWORD>(Mac[0]) | (static_cast<DWORD>(Mac[1]) << 8) | (static_cast<DWORD>(Mac[2]) << 16) | (static_cast<DWORD>(Mac[3]) << 24);
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaIdr, Value);
            break;
        }

        case Rtl8153InitWriteMacHigh: {
            const BYTE* Mac = Device->UnicastAddress._ab;
            USHORT Value = static_cast<USHORT>(Mac[4] | (static_cast<USHORT>(Mac[5]) << 8));
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, static_cast<USHORT>(PlaIdr + 4), Value);
            break;
        }

        case Rtl8153InitWriteMacNormal:
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCrwecr, ConfigNormal);
            break;

        case Rtl8153InitWriteMulticastLow:
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaMar, 0xFFFFFFFF);
            break;

        case Rtl8153InitWriteMulticastHigh:
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, static_cast<USHORT>(PlaMar + 4), 0xFFFFFFFF);
            break;

        case Rtl8153InitWriteReceiveControl:
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaRcr, BuildReceiveControl(Device));
            break;

        case Rtl8153InitReadCommand:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaCr);
            break;

        case Rtl8153InitWriteCommand:
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCr, static_cast<BYTE>(m_CommandValue | CommandReceiveEnable | CommandTransmitEnable));
            break;

        case Rtl8153InitWriteRxAggregationChange:
            if (m_HardwareVersion == 0x6000 || m_HardwareVersion == 0x6010)
                Status = QueueWriteByte(Device, Rtl8153RegisterUsb, UsbUptRxdmaOwn, RxDmaOwnUpdate | RxDmaOwnClear);
            else {
                AdvanceInitStage(Device);
                return;
            }
            break;

        case Rtl8153InitReadMiscUngate:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaMisc1);
            break;

        case Rtl8153InitWriteMiscUngate:
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, PlaMisc1, static_cast<USHORT>(m_MiscValue & ~MiscRxReadyGated));
            break;

        case Rtl8153InitStartReceiving:
            StartReceiving(Device);
            AdvanceInitStage(Device);
            return;

        case Rtl8153InitReadLinkStatus:
            Status = QueueRead(Device, Rtl8153RegisterPla, PlaPhyStatus);
            break;

        case Rtl8153InitReady:
            Device->StartInterruptLinkStatus();
            Device->LinkPollTick = KeTimeStampBundle->TickCount;
            m_LastLinkPollTick = Device->LinkPollTick;
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 1000);
            AdvanceInitStage(Device);
            return;

        case Rtl8153InitComplete:
            DbgPrint("[usbenet]: RTL8153 initialization complete; revision=0x%04X (%s), hardware MAC=%02X:%02X:%02X:%02X:%02X:%02X.\n",
                m_HardwareVersion, VersionName(m_HardwareVersion), Device->NodeId._ab[0], Device->NodeId._ab[1], Device->NodeId._ab[2], Device->NodeId._ab[3], Device->NodeId._ab[4], Device->NodeId._ab[5]);
            return;

        default:
            FailInitialization(Device, STATUS_INVALID_DEVICE_STATE, "invalid stage");
            return;
    }

    if (!NT_SUCCESS(Status))
        FailInitialization(Device, Status, "queue control transfer");
}

VOID CRtl8153::CompleteRegisterOperation(CUsbEnet* Device, NTSTATUS Status) {
    BOOL Notify = FALSE;
    RTL8153_RUNTIME_OPERATION RuntimeOperation = m_RuntimeOperation;
    DWORD CompletedStage = Device->InitStage;

    NicBaseTakeLock(Device);

    if (!NT_SUCCESS(Status)) {
        Device->CompleteControlTransfer();

        if (RuntimeOperation == Rtl8153RuntimePhyRead)
            Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
        else if (RuntimeOperation == Rtl8153RuntimePhyWrite)
            Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;

        m_RuntimeOperation = Rtl8153RuntimeNone;

        if (CompletedStage < Rtl8153InitComplete)
            FailInitialization(Device, Status, "control completion");
        else
            DbgPrint("[usbenet]: RTL8153 runtime control operation %u failed with 0x%08X.\n", RuntimeOperation, Status);

        TRAP_ASSERT(Device->PreviousIrql != 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
        return;
    }

    DWORD Value = ReadAlignedDword(Device);

    if (RuntimeOperation != Rtl8153RuntimeNone) {
        switch (RuntimeOperation) {
            case Rtl8153RuntimeLinkPoll:
                ApplyLinkStatus(Device, ExtractWord(Value, PlaPhyStatus), &Notify);
                Device->LinkPollTick = KeTimeStampBundle->TickCount;
                m_LinkPollPending = FALSE;
                break;

            case Rtl8153RuntimeFilterWrite:
                m_FilterUpdatePending = FALSE;
                break;

            case Rtl8153RuntimeMacConfig:
            case Rtl8153RuntimeMacLow:
            case Rtl8153RuntimeMacHigh:
                ++m_RuntimeMacStep;
                break;

            case Rtl8153RuntimeMacNormal:
                m_RuntimeMacStep = 0;
                m_MacUpdatePending = FALSE;
                break;

            case Rtl8153RuntimePhyRead:
                Device->CurrentPhyValue = ExtractWord(Value, m_PendingRegister);
                if (Device->CurrentPhyRegister < ARRAYSIZE(Device->PhyRegisters))
                    Device->PhyRegisters[Device->CurrentPhyRegister] = Device->CurrentPhyValue;
                Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
                break;

            case Rtl8153RuntimePhyWrite:
                Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;
                break;
        }

        Device->CompleteControlTransfer();
        m_RuntimeOperation = Rtl8153RuntimeNone;

        if (RuntimeOperation >= Rtl8153RuntimeMacConfig && RuntimeOperation <= Rtl8153RuntimeMacNormal)
            ContinueRuntimeMacWrite(Device);
        else
            ServiceRuntimeRequests(Device);
    } else {
        PBYTE Scratch = static_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize;

        switch (CompletedStage) {
            case Rtl8153InitReadVersion:
                m_HardwareVersion = (Value >> 16) & VersionMask;
                DbgPrint("[usbenet]: RTL8153 revision=0x%04X (%s).\n", m_HardwareVersion, VersionName(m_HardwareVersion));
                if (!IsSupportedHardwareVersion(m_HardwareVersion))
                    FailInitialization(Device, STATUS_NOT_SUPPORTED, "unsupported RTL8153 revision");
                break;

            case Rtl8153InitReadBootControl: {
                USHORT Boot = ExtractWord(Value, PlaBootCtrl);
                if ((Boot & BootAutoloadDone) == 0)
                    DbgPrint("[usbenet]: [WARNING] RTL8153 autoload-done bit is clear (BOOT_CTRL=0x%04X); preserving adapter firmware and continuing.\n", Boot);
                break;
            }

            case Rtl8153InitReadHardwareMacLow:
                memcpy(&Device->NodeId._ab[0], Scratch, 4);
                break;

            case Rtl8153InitReadHardwareMacHigh:
                memcpy(&Device->NodeId._ab[4], Scratch, 2);
                m_NodeIdValid = IsValidEthernetAddress(&Device->NodeId);
                DbgPrint("[usbenet]: RTL8153 hardware MAC %02X:%02X:%02X:%02X:%02X:%02X valid=%u.\n", Device->NodeId._ab[0], Device->NodeId._ab[1], Device->NodeId._ab[2], Device->NodeId._ab[3], Device->NodeId._ab[4], Device->NodeId._ab[5], m_NodeIdValid);
                break;

            case Rtl8153InitReadMiscGate:
            case Rtl8153InitReadMiscUngate:
                m_MiscValue = ExtractWord(Value, PlaMisc1);
                break;

            case Rtl8153InitReadReceiveControl:
                m_ReceiveControlBase = Value & ~ReceiveAcceptAll;
                break;

            case Rtl8153InitReadCpcr:
                m_CpcrValue = ExtractWord(Value, PlaCpcr);
                break;

            case Rtl8153InitReadBmcr:
                m_BmcrValue = ExtractWord(Value, OcpMiiBmcr);
                break;

            case Rtl8153InitReadMacPowerControl3:
                m_MacPowerControl3Value = ExtractWord(Value, PlaMacPowerCtrl3);
                break;

            case Rtl8153InitReadTcrAutoFifo:
                m_TcrValue = ExtractWord(Value, PlaTcr0);
                break;

            case Rtl8153InitPollNicReset:
            case Rtl8153InitPollNicResetSecond: {
                BYTE Command = ExtractByte(Value, PlaCr);
                if ((Command & CommandReset) != 0) {
                    ++m_ResetPollCount;
                    Device->CompleteControlTransfer();
                    if (m_ResetPollCount > 1000) {
                        FailInitialization(Device, STATUS_IO_TIMEOUT, "NIC reset timeout");
                    } else {
                        NTSTATUS PollStatus = QueueRead(Device, Rtl8153RegisterPla, PlaCr);
                        if (!NT_SUCCESS(PollStatus))
                            FailInitialization(Device, PollStatus, "NIC reset poll");
                    }
                    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
                    TRAP_THREAD(Device->LockOwnerThread);
                    NULL_OWNER_THREAD(Device);
                    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
                    return;
                }
                break;
            }

            case Rtl8153InitReadBmuReset:
                m_BmuValue = ExtractByte(Value, UsbBmuReset);
                break;

            case Rtl8153InitReadOobControl:
                m_OobValue = ExtractByte(Value, PlaOobCtrl);
                break;

            case Rtl8153InitReadSffStatus:
            case Rtl8153InitReadSffReinit:
                m_SffValue = ExtractWord(Value, PlaSffStatus7);
                break;

            case Rtl8153InitPollLinkListReady:
            case Rtl8153InitPollLinkListReinit: {
                BYTE Oob = ExtractByte(Value, PlaOobCtrl);
                if ((Oob & OobLinkListReady) == 0) {
                    ++m_LinkListPollCount;
                    Device->CompleteControlTransfer();
                    if (m_LinkListPollCount > 1000) {
                        FailInitialization(Device, STATUS_IO_TIMEOUT, "link-list ready timeout");
                    } else {
                        NTSTATUS PollStatus = QueueRead(Device, Rtl8153RegisterPla, PlaOobCtrl);
                        if (!NT_SUCCESS(PollStatus))
                            FailInitialization(Device, PollStatus, "link-list ready poll");
                    }
                    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
                    TRAP_THREAD(Device->LockOwnerThread);
                    NULL_OWNER_THREAD(Device);
                    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
                    return;
                }
                break;
            }

            case Rtl8153InitReadUsbControl:
                m_UsbControlValue = ExtractWord(Value, UsbUsbCtrl);
                break;

            case Rtl8153InitReadCommand:
                m_CommandValue = ExtractByte(Value, PlaCr);
                break;

            case Rtl8153InitReadLinkStatus:
                ApplyLinkStatus(Device, ExtractWord(Value, PlaPhyStatus), &Notify);
                break;
        }

        Device->CompleteControlTransfer();

        if (Device->InitStage != Rtl8153InitFailed)
            AdvanceInitStage(Device);
    }

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (Notify)
        Device->NotifyLinkStateChangedToUsers();
}

VOID __fastcall CRtl8153::AsyncCompletionRoutine(PVOID RequestPointer, NTSTATUS Status) {
    PUSBD_ASYNC_REQUEST Request = static_cast<PUSBD_ASYNC_REQUEST>(RequestPointer);
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Rtl8153Chipset.CompleteRegisterOperation(Device, Status);
}

VOID CRtl8153::StartReceiving(CUsbEnet* Device) {
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    Device->Flags |= ReceiveRunning;
    Device->SubmitReceive();
}

VOID CRtl8153::RestartReceiving(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    Device->Flags |= ReceiveRunning;
    Device->SubmitReceive();
}

VOID CRtl8153::OnUnicastAddressChanged(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Device->InitStage == Rtl8153InitWaitingForEthernetAddress) {
        if (IsValidEthernetAddress(&Device->UnicastAddress))
            AdvanceInitStage(Device);
        return;
    }

    if (Device->InitStage != Rtl8153InitComplete)
        return;

    m_MacUpdatePending = TRUE;
    ServiceRuntimeRequests(Device);
}

VOID CRtl8153::WriteNodeId(CUsbEnet* Device) {
    OnUnicastAddressChanged(Device);
}

VOID CRtl8153::UpdateReceiveFilter(CUsbEnet* Device) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Device->InitStage < Rtl8153InitWriteReceiveControl || Device->InitStage == Rtl8153InitFailed)
        return;

    m_FilterUpdatePending = TRUE;
    ServiceRuntimeRequests(Device);
}

VOID CRtl8153::ContinueRuntimeMacWrite(CUsbEnet* Device) {
    if (!m_MacUpdatePending || (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0 || Device->InitStage != Rtl8153InitComplete)
        return;

    const BYTE* Mac = Device->UnicastAddress._ab;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (m_RuntimeMacStep) {
        case 0:
            m_RuntimeOperation = Rtl8153RuntimeMacConfig;
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCrwecr, ConfigMode);
            break;

        case 1: {
            DWORD Value = static_cast<DWORD>(Mac[0]) | (static_cast<DWORD>(Mac[1]) << 8) | (static_cast<DWORD>(Mac[2]) << 16) | (static_cast<DWORD>(Mac[3]) << 24);
            m_RuntimeOperation = Rtl8153RuntimeMacLow;
            Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaIdr, Value);
            break;
        }

        case 2: {
            USHORT Value = static_cast<USHORT>(Mac[4] | (static_cast<USHORT>(Mac[5]) << 8));
            m_RuntimeOperation = Rtl8153RuntimeMacHigh;
            Status = QueueWriteWord(Device, Rtl8153RegisterPla, static_cast<USHORT>(PlaIdr + 4), Value);
            break;
        }

        default:
            m_RuntimeOperation = Rtl8153RuntimeMacNormal;
            Status = QueueWriteByte(Device, Rtl8153RegisterPla, PlaCrwecr, ConfigNormal);
            break;
    }

    if (!NT_SUCCESS(Status)) {
        m_RuntimeOperation = Rtl8153RuntimeNone;
        return;
    }
}

VOID CRtl8153::ServiceRuntimeRequests(CUsbEnet* Device) {
    if (Device->InitStage != Rtl8153InitComplete || (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_RESETTING | USBENET_STATE_STOPPING | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return;

    if (m_MacUpdatePending) {
        ContinueRuntimeMacWrite(Device);
        return;
    }

    if (m_FilterUpdatePending) {
        m_RuntimeOperation = Rtl8153RuntimeFilterWrite;
        NTSTATUS Status = QueueWriteDword(Device, Rtl8153RegisterPla, PlaRcr, BuildReceiveControl(Device));
        if (!NT_SUCCESS(Status))
            m_RuntimeOperation = Rtl8153RuntimeNone;
        return;
    }

    if (m_LinkPollPending) {
        m_RuntimeOperation = Rtl8153RuntimeLinkPoll;
        NTSTATUS Status = QueueRead(Device, Rtl8153RegisterPla, PlaPhyStatus);
        if (!NT_SUCCESS(Status))
            m_RuntimeOperation = Rtl8153RuntimeNone;
    }
}

NTSTATUS CRtl8153::RequestFullReinitialize(CUsbEnet* Device, DWORD Reason) {
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Reason);
    return STATUS_NOT_SUPPORTED;
}

BOOL CRtl8153::UsesInterruptLinkStatus() const {
    return TRUE;
}

BOOL CRtl8153::ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Data == NULL || Length < 2)
        return FALSE;

    USHORT Interrupt = static_cast<USHORT>(Data[0] | (static_cast<USHORT>(Data[1]) << 8));
    ++m_InterruptEventCount;
    m_InterruptStatusSeen = TRUE;
    m_LinkPollPending = TRUE;

    BOOL LinkChange = (Interrupt & LinkInterrupt) != 0;

    if (LinkChange && (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0)
        ServiceRuntimeRequests(Device);

    if (m_InterruptEventCount <= 8 || (m_InterruptEventCount & (m_InterruptEventCount - 1)) == 0)
        DbgPrint("[usbenet]: RTL8153 interrupt #%u raw=0x%04X linkChange=%u.\n", m_InterruptEventCount, Interrupt, LinkChange);

    return FALSE;
}

VOID CRtl8153::RunTimer(CUsbEnet* Device) {
    NicBaseTakeLockAtRaisedIrql(Device);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        TRAP_ASSERT(Device->PreviousIrql == 0xEE);
        TRAP_THREAD(Device->LockOwnerThread);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    if (Device->InitStage == Rtl8153InitWaitingForEthernetAddress) {
        CEnetAddr* TitleAddress = Main::GetKernelTitleEthernetAddress();
        if (IsValidEthernetAddress(TitleAddress)) {
            memcpy(&Device->UnicastAddress, TitleAddress, sizeof(CEnetAddr));
            AdvanceInitStage(Device);
        }
    } else if (Device->InitStage == Rtl8153InitComplete) {
        Device->PrintThroughputStats(CurrentTick);

        if (CurrentTick - m_LastLinkPollTick >= 1000) {
            m_LinkPollPending = TRUE;
            m_LastLinkPollTick = CurrentTick;
        }

        ServiceRuntimeRequests(Device);
        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, 1000);
    }

    TRAP_ASSERT(Device->PreviousIrql == 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
}

NTSTATUS CRtl8153::BeginReadPhy(CUsbEnet* Device, USHORT Register) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Register > 0x1F || (Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return STATUS_DEVICE_BUSY;

    Device->CurrentPhyRegister = Register;
    Device->Flags |= USBENET_STATE_PHY_READ_IN_PROGRESS;
    m_RuntimeOperation = Rtl8153RuntimePhyRead;
    USHORT Address = static_cast<USHORT>(OcpWindowBase | ((OcpBaseMii + Register * 2) & 0x0FFF));
    NTSTATUS Status = QueueRead(Device, Rtl8153RegisterPla, Address);

    if (!NT_SUCCESS(Status)) {
        m_RuntimeOperation = Rtl8153RuntimeNone;
        Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
    }

    return Status;
}

NTSTATUS CRtl8153::BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Register > 0x1F || (Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return STATUS_DEVICE_BUSY;

    Device->CurrentPhyRegister = Register;
    Device->CurrentPhyValue = Value;
    Device->Flags |= USBENET_STATE_PHY_WRITE_IN_PROGRESS;
    m_RuntimeOperation = Rtl8153RuntimePhyWrite;
    USHORT Address = static_cast<USHORT>(OcpWindowBase | ((OcpBaseMii + Register * 2) & 0x0FFF));
    NTSTATUS Status = QueueWriteWord(Device, Rtl8153RegisterPla, Address, Value);

    if (!NT_SUCCESS(Status)) {
        m_RuntimeOperation = Rtl8153RuntimeNone;
        Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;
    }

    return Status;
}

BOOL CRtl8153::InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context) {
    UNREFERENCED_PARAMETER(Device);

    if (Buffer == NULL || Context == NULL || Length < sizeof(RTL8153_RX_DESCRIPTOR))
        return FALSE;

    Context->Buffer = Buffer;
    Context->Length = Length;
    Context->DataOffset = 0;
    Context->MetadataOffset = 0;
    Context->MetadataIndex = 0;
    Context->MetadataCount = 0;
    return TRUE;
}

USBENET_RX_PARSE_RESULT CRtl8153::GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame) {
    UNREFERENCED_PARAMETER(Device);

    if (Context->DataOffset >= Context->Length)
        return UsbEnetRxParseComplete;

    DWORD Remaining = Context->Length - Context->DataOffset;
    if (Remaining < sizeof(RTL8153_RX_DESCRIPTOR))
        return UsbEnetRxParseComplete;

    PBYTE Record = Context->Buffer + Context->DataOffset;
    PRTL8153_RX_DESCRIPTOR Descriptor = reinterpret_cast<PRTL8153_RX_DESCRIPTOR>(Record);
    DWORD Options1 = _byteswap_ulong(Descriptor->Options1);
    DWORD PacketLength = Options1 & RxLengthMask;

    if (PacketLength == 0)
        return UsbEnetRxParseComplete;

    DWORD RecordLength = sizeof(RTL8153_RX_DESCRIPTOR) + PacketLength;
    DWORD AlignedLength = Rtl8153AlignUp(RecordLength, RxAlignment);

    if (PacketLength <= EthernetFcsLength || PacketLength > StandardFrameLimit + EthernetFcsLength || RecordLength > Remaining) {
        DbgPrint("[usbenet]: [DISCARD] RTL8153 invalid RX descriptor offset=%u opts1=0x%08X packetLength=%u remaining=%u.\n", Context->DataOffset, Options1, PacketLength, Remaining);
        return UsbEnetRxParseError;
    }

    Frame->Data = Record + sizeof(RTL8153_RX_DESCRIPTOR);
    Frame->Length = PacketLength - EthernetFcsLength;
    Frame->Flags = Options1;
    Context->DataOffset += AlignedLength <= Remaining ? AlignedLength : RecordLength;
    return UsbEnetRxParseFrame;
}

BOOL CRtl8153::AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator) {
    UNREFERENCED_PARAMETER(Device);

    if (Buffer == NULL || Frame == NULL || FramedLength == NULL || BytesWritten == NULL || HasTerminator == NULL || Length == 0 || Length > StandardFrameLimit)
        return FALSE;

    DWORD AlignmentPadding = (TxAlignment - (AggregateOffset & (TxAlignment - 1))) & (TxAlignment - 1);
    DWORD Required = AlignmentPadding + sizeof(RTL8153_TX_DESCRIPTOR) + Length;

    if (Required > Capacity)
        return FALSE;

    if (AlignmentPadding != 0)
        memset(Buffer, 0, AlignmentPadding);

    PRTL8153_TX_DESCRIPTOR Descriptor = reinterpret_cast<PRTL8153_TX_DESCRIPTOR>(Buffer + AlignmentPadding);
    DWORD Options1 = TxFirstSegment | TxLastSegment | (Length & TxLengthMask);
    Descriptor->Options1 = _byteswap_ulong(Options1);
    Descriptor->Options2 = 0;
    memcpy(reinterpret_cast<PBYTE>(Descriptor) + sizeof(RTL8153_TX_DESCRIPTOR), Frame, Length);

    *FramedLength = Length;
    *BytesWritten = Required;
    *HasTerminator = FALSE;
    return TRUE;
}

#include "stdafx.h"
#include "UsbEnetChipset.h"
#include "Ax88178.h"

#if USBENET_DEBUG
#define USBENET_LOG(_fmt, ...) DbgPrint("[Ax88178] " _fmt "\n", __VA_ARGS__)
#define USBENET_LOG0(_msg) DbgPrint("[Ax88178] " _msg "\n")
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

static const USHORT Ax88178EepromPhyModeWord = 0x0017;
static const USHORT Ax88178PhyModeMask = 0x007F;
static const USHORT Ax88178PhyModeMarvell = 0x0000;
static const USHORT Ax88178PhyModeCicadaV1 = 0x0001;
static const USHORT Ax88178PhyModeAgereA = 0x0002;
static const USHORT Ax88178PhyModeCicadaV2 = 0x0005;
static const USHORT Ax88178PhyModeAgereB = 0x0006;
static const USHORT Ax88178PhyModeCicadaV2Asix = 0x0009;
static const USHORT Ax88178PhyModeVitesse = 0x000A;
static const USHORT Ax88178PhyModeRealtek = 0x000C;
static const USHORT Ax88178PhyModeRealtekAlt = 0x000D;
static const DWORD Ax88178PhyReadyRetryLimit = 100;
static const DWORD Ax88178PhyReadyRetryPeriod = 5;
static const DWORD Ax88178TransportErrorWindow = 5000;
static const DWORD Ax88178TransportRecoveryDelay = 10;
static const DWORD Ax88178FullReinitDrainTimeout = 2000;
static const DWORD Ax88178TransmitTimeout = 5000;

static const AX88178_PHY_PATCH_ENTRY Ax88178CicadaV1Patch[] = {
    { 0x1F, 0x0001 }, { 0x17, 0x1C25 }, { 0x1F, 0x2A30 }, { 0x10, 0x234C },
    { 0x1F, 0x2A30 }, { 0x08, 0x0212 }, { 0x1F, 0x52B5 }, { 0x00, 0xA7FA },
    { 0x02, 0x0012 }, { 0x01, 0x3002 }, { 0x00, 0x87FA }, { 0x1F, 0x52B5 },
    { 0x00, 0xAFAC }, { 0x02, 0x000D }, { 0x01, 0x001C }, { 0x00, 0x8FAC },
    { 0x1F, 0x2A30 }, { 0x08, 0x0012 }, { 0x1F, 0x2A30 }, { 0x14, 0x0400 },
    { 0x1F, 0x2A30 }, { 0x08, 0x0212 }, { 0x1F, 0x52B5 }, { 0x00, 0xA760 },
    { 0x02, 0x0000 }, { 0x01, 0xFAFF }, { 0x00, 0x8760 }, { 0x1F, 0x52B5 },
    { 0x00, 0xA760 }, { 0x02, 0x0000 }, { 0x01, 0xFAFF }, { 0x00, 0x8760 },
    { 0x1F, 0x52B5 }, { 0x00, 0xAFAE }, { 0x02, 0x0004 }, { 0x01, 0x0671 },
    { 0x00, 0x8FAE }, { 0x1F, 0x2A30 }, { 0x08, 0x0012 }, { 0x1F, 0x0000 }
};

static const AX88178_PHY_PATCH_ENTRY Ax88178CicadaV2Patch[] = {
    { 0x1F, 0x2A30 }, { 0x08, 0x0212 }, { 0x1F, 0x52B5 }, { 0x02, 0x000F },
    { 0x01, 0x472A }, { 0x00, 0x8FA4 }, { 0x1F, 0x2A30 }, { 0x08, 0x0212 },
    { 0x1F, 0x0000 }
};

static const AX88178_PHY_PATCH_ENTRY Ax88178CicadaV2AsixPatch[] = {
    { 0x1F, 0x2A30 }, { 0x08, 0x0212 }, { 0x1F, 0x52B5 }, { 0x02, 0x0012 },
    { 0x01, 0x3002 }, { 0x00, 0x87FA }, { 0x1F, 0x52B5 }, { 0x02, 0x000F },
    { 0x01, 0x472A }, { 0x00, 0x8FA4 }, { 0x1F, 0x2A30 }, { 0x08, 0x0212 },
    { 0x1F, 0x0000 }
};

static const AX88178_PHY_PATCH_ENTRY Ax88178AgereV0Patch[] = {
    { 0x00, 0x0800 }, { 0x12, 0x0007 }, { 0x10, 0x8805 }, { 0x11, 0xB03E },
    { 0x10, 0x8808 }, { 0x11, 0xE110 }, { 0x10, 0x8806 }, { 0x11, 0xB03E },
    { 0x10, 0x8807 }, { 0x11, 0xFF00 }, { 0x10, 0x880E }, { 0x11, 0xB4D3 },
    { 0x10, 0x880F }, { 0x11, 0xB4D3 }, { 0x10, 0x8810 }, { 0x11, 0xB4D3 },
    { 0x10, 0x8817 }, { 0x11, 0x1C00 }, { 0x10, 0x300D }, { 0x11, 0x0001 },
    { 0x12, 0x0002 }
};

/* The Windows VSC8601 routine performs several read/modify/write operations.
 * These fixed writes are the recovered vendor programming sequence; standard
 * advertisement, BMCR and medium-mode setup still run afterward. */
static const AX88178_PHY_PATCH_ENTRY Ax88178Vitesse8601Patch[] = {
    { 0x1F, 0x52B5 }, { 0x12, 0x009E }, { 0x11, 0xDD39 }, { 0x10, 0x87AA },
    { 0x10, 0xA7B4 }, { 0x10, 0x87B4 }, { 0x10, 0xA794 }, { 0x10, 0x8794 },
    { 0x12, 0x00F7 }, { 0x11, 0xBE36 }, { 0x10, 0x879E }, { 0x10, 0xA7A0 },
    { 0x10, 0x87A0 }, { 0x12, 0x003C }, { 0x11, 0xF3CF }, { 0x10, 0x87A2 },
    { 0x12, 0x003C }, { 0x11, 0xF3CF }, { 0x10, 0x87A4 }, { 0x12, 0x003C },
    { 0x11, 0xD287 }, { 0x10, 0x87A6 }, { 0x10, 0xA7A8 }, { 0x10, 0x87A8 },
    { 0x10, 0xA7FA }, { 0x10, 0x87FA }, { 0x1F, 0x0000 }
};

CAx88178 g_Ax88178Chipset;

static const DWORD Ax88178InterruptPacketSize = 8;
static const DWORD Ax88178InterruptLinkByte = 2;
static const BYTE Ax88178InterruptLinkMask = 0x01;
static const DWORD Ax88178InterruptRetryPeriod = 200;
static const DWORD Ax88178InterruptIdlePeriod = 1000;
static const DWORD Ax88178FallbackPhyPollPeriod = 5000;


const char* CAx88178::GetName() const {
    return "AX88178";
}

USHORT CAx88178::GetVendorIdRaw() const {
    return 0x950B;
}

USHORT CAx88178::GetProductIdRaw() const {
    return 0x8017;
}

BOOL CAx88178::IsImplemented() const {
    return TRUE;
}

BOOL CAx88178::SupportsTransmitAggregation() const {
    return TRUE;
}

DWORD CAx88178::GetMaximumFrameSize() const {
    return 1518;
}

DWORD CAx88178::GetMaximumAggregateTransferSize() const {
    return MaximumAggregateSize;
}

DWORD CAx88178::GetTransmitHeaderSize() const {
    return sizeof(AX88178_RECV_HEADER);
}

DWORD CAx88178::GetTransmitTerminatorSize() const {
    return sizeof(DWORD);
}

BOOL CAx88178::IsReady(CUsbEnet* Device) {
    return Device->InitStage == UsbEnetInitComplete;
}

BOOL CAx88178::IsNodeIdAvailable(CUsbEnet* Device) {
    return Device->InitStage > UsbEnetInitReadNodeId;
}

VOID CAx88178::OnUnicastAddressChanged(CUsbEnet* Device) {
    if (Device->DeviceAttached == 0)
        return;

    if (Device->InitStage == UsbEnetInitWaitingForEthernetAddress)
        AdvanceInitStage(Device);
    else if (Device->InitStage > UsbEnetInitWaitingForEthernetAddress)
        WriteNodeId(Device);
}

VOID CAx88178::IncrementEthernetAddress(CEnetAddr* Address) {
    PBYTE Bytes = reinterpret_cast<PBYTE>(Address);

    for (INT Index = static_cast<INT>(sizeof(CEnetAddr)) - 1; Index >= 0; Index--) {
        Bytes[Index]++;

        if (Bytes[Index] != 0)
            break;
    }
}

VOID CAx88178::PrintLinkMode(DWORD State, USHORT Control, USHORT BasicStatus, USHORT Advertisement, USHORT LinkPartnerAbility, USHORT GigabitControl, USHORT GigabitStatus) {
    const char* Speed = "unknown";
    const char* Duplex = "unknown";

    if ((State & NIC_LINK_STATE_1000_MBPS) != 0)
        Speed = "1000";
    else if ((State & NIC_LINK_STATE_100_MBPS) != 0)
        Speed = "100";
    else if ((State & NIC_LINK_STATE_10_MBPS) != 0)
        Speed = "10";

    if ((State & NIC_LINK_STATE_FULL_DUPLEX) != 0)
        Duplex = "full";
    else if ((State & NIC_LINK_STATE_HALF_DUPLEX) != 0)
        Duplex = "half";

    DbgPrint("[usbenet]: Ethernet link mode: %s Mbps %s duplex, active=%u, autoneg=%u, state=0x%08X\n", Speed, Duplex, (State & NIC_LINK_STATE_ACTIVE) != 0, (Control & MII_BMCR_AUTONEG_ENABLE) != 0, State);
    DbgPrint("[usbenet]: PHY registers: BMCR=0x%04X BMSR=0x%04X ANAR=0x%04X LPA=0x%04X CTRL1000=0x%04X STAT1000=0x%04X\n", Control, BasicStatus, Advertisement, LinkPartnerAbility, GigabitControl, GigabitStatus);
}

DWORD CAx88178::EthernetCrc32(const BYTE* Data, DWORD Length) {
    DWORD Crc = 0xFFFFFFFF;

    for (DWORD Index = 0; Index < Length; Index++) {
        DWORD Value = static_cast<DWORD>(Data[Index]) << 24;

        for (DWORD Bit = 0; Bit < 8; Bit++) {
            BOOL Carry = ((Crc ^ Value) & 0x80000000) != 0;
            Crc <<= 1;
            Value <<= 1;

            if (Carry)
                Crc ^= 0x04C11DB7;
        }
    }

    return Crc;
}

BOOL CAx88178::IsRecognizedPhyIdentifier(USHORT Identifier1) {
    switch (Identifier1) {
        case 0x0007:
        case 0x000F:
        case 0x001C:
        case 0x004D:
        case 0x0141:
        case 0x0243:
        case 0x0282:
            return TRUE;

        default:
            return FALSE;
    }
}

BOOL CAx88178::InitializeReceiveParser(CUsbEnet* Device, PBYTE Buffer, DWORD Length, PUSBENET_RX_PARSE_CONTEXT Context) {
    UNREFERENCED_PARAMETER(Device);
    Context->Buffer = Buffer;
    Context->Length = Length;
    Context->DataOffset = 0;
    Context->MetadataOffset = 0;
    Context->MetadataIndex = 0;
    Context->MetadataCount = 0;
    return TRUE;
}

USBENET_RX_PARSE_RESULT CAx88178::GetNextReceiveFrame(CUsbEnet* Device, PUSBENET_RX_PARSE_CONTEXT Context, PUSBENET_RX_FRAME Frame) {
    UNREFERENCED_PARAMETER(Device);

    if (Context->DataOffset == Context->Length)
        return UsbEnetRxParseComplete;

    DWORD BytesRemaining = Context->Length - Context->DataOffset;

    if (BytesRemaining < sizeof(AX88178_RECV_HEADER)) {
        DbgPrint("[usbenet]: [DISCARD] AX88178 received %u bytes and no header\n", BytesRemaining);
        return UsbEnetRxParseError;
    }

    PAX88178_RECV_HEADER Header = reinterpret_cast<PAX88178_RECV_HEADER>(Context->Buffer + Context->DataOffset);
    USHORT FrameLength = _byteswap_ushort(Header->Length);
    USHORT FrameLengthComplement = _byteswap_ushort(Header->LengthComplement);
    DWORD Available = BytesRemaining - sizeof(AX88178_RECV_HEADER);

    if (static_cast<USHORT>(FrameLength ^ FrameLengthComplement) != 0xFFFF) {
        DbgPrint("[usbenet]: [DISCARD] AX88178 received invalid frame header (0x%04X 0x%04X, %u bytes available)\n", Header->Length, Header->LengthComplement, Available);
        return UsbEnetRxParseError;
    }

    DWORD AlignedFrameLength = (FrameLength + 1) & ~1;

    if (FrameLength > Available || AlignedFrameLength > Available) {
        DbgPrint("[usbenet]: [DISCARD] AX88178 received insufficient transfer size (packet size %u, available %u)\n", FrameLength, Available);
        return UsbEnetRxParseError;
    }

    Frame->Data = Context->Buffer + Context->DataOffset + sizeof(AX88178_RECV_HEADER);
    Frame->Length = FrameLength;
    Frame->Flags = 0;
    Context->DataOffset += sizeof(AX88178_RECV_HEADER) + AlignedFrameLength;
    return UsbEnetRxParseFrame;
}

BOOL CAx88178::AppendTransmitFrame(CUsbEnet* Device, PBYTE Buffer, DWORD Capacity, DWORD AggregateOffset, const PVOID Frame, DWORD Length, PDWORD FramedLength, PDWORD BytesWritten, PBOOL HasTerminator) {
    DWORD Required = sizeof(AX88178_RECV_HEADER) + Length;
    DWORD TerminatorLength = 0;

    if (Device->TransmitMaxPacketSize != 0 && ((AggregateOffset + Required) % Device->TransmitMaxPacketSize) == 0)
        TerminatorLength = sizeof(DWORD);

    if (Required + TerminatorLength > Capacity)
        return FALSE;

    PAX88178_RECV_HEADER Header = reinterpret_cast<PAX88178_RECV_HEADER>(Buffer);
    Header->Length = _byteswap_ushort(static_cast<USHORT>(Length));
    Header->LengthComplement = _byteswap_ushort(static_cast<USHORT>(~Length));
    memcpy(Buffer + sizeof(AX88178_RECV_HEADER), Frame, Length);

    if (TerminatorLength != 0)
        *reinterpret_cast<UNALIGNED ULONG*>(Buffer + Required) = 0x0000FFFF;

    *FramedLength = Required;
    *BytesWritten = Required + TerminatorLength;
    *HasTerminator = TerminatorLength != 0;
    return TRUE;
}

NTSTATUS CAx88178::QueueRealtekPhyInitStep(CUsbEnet* Device) {
    switch (m_RealtekPhyInitStep) {
        case RealtekPhyInitWritePage5:
            return BeginWritePhy(Device, 0x1F, 0x0005);

        case RealtekPhyInitClearRegister0C:
            return BeginWritePhy(Device, 0x0C, 0x0000);

        case RealtekPhyInitReadRegister01:
            return BeginReadPhy(Device, 0x01);

        case RealtekPhyInitSetRegister01Bit7:
            return BeginWritePhy(Device, 0x01, static_cast<USHORT>(Device->CurrentPhyValue | 0x0080));

        case RealtekPhyInitRestorePage0:
        case RealtekPhyInitRestorePage0AfterLed:
        case RealtekPhyInitRestorePage0AfterFailure:
            return BeginWritePhy(Device, 0x1F, 0x0000);

        case RealtekPhyInitWriteLedPage2:
            return BeginWritePhy(Device, 0x1F, 0x0002);

        case RealtekPhyInitWriteLedRegister:
            return BeginWritePhy(Device, 0x1A, m_LedMode == 0x0D ? 0x00CF : 0x00CB);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

BOOL CAx88178::AdvanceRealtekPhyInit(CUsbEnet* Device) {
    switch (m_RealtekPhyInitStep) {
        case RealtekPhyInitWritePage5:
            m_RealtekPhyInitStep = RealtekPhyInitClearRegister0C;
            break;

        case RealtekPhyInitClearRegister0C:
            m_RealtekPhyInitStep = RealtekPhyInitReadRegister01;
            break;

        case RealtekPhyInitReadRegister01:
            m_RealtekPhyInitStep = RealtekPhyInitSetRegister01Bit7;
            break;

        case RealtekPhyInitSetRegister01Bit7:
            m_RealtekPhyInitStep = RealtekPhyInitRestorePage0;
            break;

        case RealtekPhyInitRestorePage0:
            m_RealtekPhyInitStep = (m_LedMode == 0x0C || m_LedMode == 0x0D) ? RealtekPhyInitWriteLedPage2 : RealtekPhyInitComplete;
            break;

        case RealtekPhyInitWriteLedPage2:
            m_RealtekPhyInitStep = RealtekPhyInitWriteLedRegister;
            break;

        case RealtekPhyInitWriteLedRegister:
            m_RealtekPhyInitStep = RealtekPhyInitRestorePage0AfterLed;
            break;

        case RealtekPhyInitRestorePage0AfterLed:
            m_RealtekPhyInitStep = RealtekPhyInitComplete;
            break;

        case RealtekPhyInitRestorePage0AfterFailure:
            DbgPrint("[usbenet]: AX88178 Realtek PHY initialization failed; restored page 0 and continuing.\n");
            m_RealtekPhyInitStep = RealtekPhyInitIdle;
            return FALSE;

        default:
            DbgPrint("[usbenet]: AX88178 Realtek PHY initialization entered invalid step %u; continuing.\n", static_cast<DWORD>(m_RealtekPhyInitStep));
            m_RealtekPhyInitStep = RealtekPhyInitIdle;
            return FALSE;
    }

    if (m_RealtekPhyInitStep == RealtekPhyInitComplete) {
        DbgPrint("[usbenet]: AX88178 Realtek PHY initialization complete; LED mode=0x%02X.\n", m_LedMode);
        m_RealtekPhyInitStep = RealtekPhyInitIdle;
        return FALSE;
    }

    NTSTATUS Status = QueueRealtekPhyInitStep(Device);

    if (NT_SUCCESS(Status))
        return TRUE;

    DbgPrint("[usbenet]: Couldn't queue AX88178 Realtek PHY initialization step %u (err = 0x%08X)!\n", static_cast<DWORD>(m_RealtekPhyInitStep), Status);

    if (m_RealtekPhyInitStep != RealtekPhyInitWritePage5 && m_RealtekPhyInitStep != RealtekPhyInitRestorePage0 && m_RealtekPhyInitStep != RealtekPhyInitRestorePage0AfterLed && m_RealtekPhyInitStep != RealtekPhyInitRestorePage0AfterFailure) {
        m_RealtekPhyInitStep = RealtekPhyInitRestorePage0AfterFailure;
        Status = QueueRealtekPhyInitStep(Device);

        if (NT_SUCCESS(Status))
            return TRUE;

        DbgPrint("[usbenet]: Couldn't restore AX88178 Realtek PHY page 0 after queue failure (err = 0x%08X)! Continuing.\n", Status);
    }

    m_RealtekPhyInitStep = RealtekPhyInitIdle;
    return FALSE;
}

VOID CAx88178::BeginPhyPatch(CUsbEnet* Device, const AX88178_PHY_PATCH_ENTRY* Patch, DWORD Count, const char* Name) {
    m_PhyPatch = Patch;
    m_PhyPatchCount = Count;
    m_PhyPatchIndex = 0;
    m_PhyPatchName = Name;
    m_PhyPatchOperationFailed = FALSE;

    DbgPrint("[usbenet]: Applying AX88178 %s PHY patch (%u writes).\n", Name, Count);

    if (!QueueNextPhyPatch(Device)) {
        DbgPrint("[usbenet]: AX88178 %s PHY patch could not start; continuing initialization.\n", Name);
        m_PhyPatch = NULL;
        m_PhyPatchCount = 0;
        m_PhyPatchIndex = 0;
        m_PhyPatchName = NULL;
        AdvanceInitStage(Device);
    }
}

BOOL CAx88178::QueueNextPhyPatch(CUsbEnet* Device) {
    if (m_PhyPatch == NULL || m_PhyPatchIndex >= m_PhyPatchCount)
        return FALSE;

    const AX88178_PHY_PATCH_ENTRY& Entry = m_PhyPatch[m_PhyPatchIndex];
    NTSTATUS Status = BeginWritePhy(Device, Entry.Register, Entry.Value);

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: AX88178 %s PHY patch write %u/%u reg=0x%02X value=0x%04X could not queue (0x%08X).\n", m_PhyPatchName, m_PhyPatchIndex + 1, m_PhyPatchCount, Entry.Register, Entry.Value, Status);
        return FALSE;
    }

    return TRUE;
}

VOID CAx88178::BeginEepromConfig(CUsbEnet* Device, BOOL InitialRead) {
    m_PhyConfigInitialRead = InitialRead;
    m_PhyConfigStep = Ax88178PhyConfigEnableEeprom;

    NTSTATUS Status = QueuePhyConfigStep(Device);

    if (NT_SUCCESS(Status))
        return;

    DbgPrint("[usbenet]: Couldn't begin AX88178 EEPROM configuration read (err = 0x%08X).\n", Status);
    m_PhyConfigStep = Ax88178PhyConfigIdle;

    if (InitialRead)
        AdvanceInitStage(Device);
    else
        ContinueVendorPhyInitialization(Device);
}

NTSTATUS CAx88178::QueuePhyConfigStep(CUsbEnet* Device) {
    switch (m_PhyConfigStep) {
        case Ax88178PhyConfigEnableEeprom:
            return Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutinePhyConfig, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_ENABLE, 0, 0, 0, NULL);

        case Ax88178PhyConfigReadEepromWord17:
            return Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutinePhyConfig, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_READ_EEPROM, Ax88178EepromPhyModeWord, 0, sizeof(USHORT), NULL);

        case Ax88178PhyConfigDisableEeprom:
            return Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutinePhyConfig, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_DISABLE, 0, 0, 0, NULL);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

VOID CAx88178::ContinueVendorPhyInitialization(CUsbEnet* Device) {
    USHORT Identifier1 = Device->PhyRegisters[PhyRegisterIdentifier1];
    USHORT Identifier2 = Device->PhyRegisters[PhyRegisterIdentifier2];

    m_PhyConfigStep = Ax88178PhyConfigIdle;

    if (!m_EepromWord17Valid) {
        DbgPrint("[usbenet]: PHY ID 0x%04X:0x%04X; EEPROM word 0x17 could not be read. Skipping vendor-specific PHY initialization.\n", Identifier1, Identifier2);
        AdvanceInitStage(Device);
        return;
    }

    if (m_EepromWord17 == 0xFFFF) {
        m_PhyMode = Ax88178PhyModeMarvell;
        m_LedMode = 0;
    } else {
        m_PhyMode = m_EepromWord17 & Ax88178PhyModeMask;
        m_LedMode = m_EepromWord17 >> 8;
    }

    DbgPrint("[usbenet]: PHY ID 0x%04X:0x%04X, EEPROM[0x17]=0x%04X, PHY mode=0x%02X, LED mode=0x%02X.\n", Identifier1, Identifier2, m_EepromWord17, m_PhyMode, m_LedMode);

    switch (m_PhyMode) {
        case Ax88178PhyModeMarvell: {
            USHORT MarvellControl = 0x0082;

            if (m_LedMode != 0)
                MarvellControl |= 0x0004;

            DbgPrint("[usbenet]: Applying AX88178 Marvell PHY initialization.\n");
            NTSTATUS Status = BeginWritePhy(Device, 0x14, MarvellControl);

            if (!NT_SUCCESS(Status)) {
                DbgPrint("[usbenet]: Couldn't begin AX88178 Marvell PHY initialization (err = 0x%08X)! Continuing.\n", Status);
                AdvanceInitStage(Device);
            }
            return;
        }

        case Ax88178PhyModeRealtek:
        case Ax88178PhyModeRealtekAlt: {
            DbgPrint("[usbenet]: Applying AX88178 Realtek PHY initialization selected by EEPROM PHY mode 0x%02X.\n", m_PhyMode);
            m_RealtekPhyInitStep = RealtekPhyInitWritePage5;
            m_RealtekPhyInitOperationFailed = FALSE;
            NTSTATUS Status = QueueRealtekPhyInitStep(Device);

            if (!NT_SUCCESS(Status)) {
                DbgPrint("[usbenet]: Couldn't begin AX88178 Realtek PHY initialization (err = 0x%08X)! Continuing.\n", Status);
                m_RealtekPhyInitStep = RealtekPhyInitIdle;
                AdvanceInitStage(Device);
            }
            return;
        }

        case Ax88178PhyModeCicadaV1:
            BeginPhyPatch(Device, Ax88178CicadaV1Patch, ARRAYSIZE(Ax88178CicadaV1Patch), "Cicada V1");
            return;

        case Ax88178PhyModeCicadaV2:
            BeginPhyPatch(Device, Ax88178CicadaV2Patch, ARRAYSIZE(Ax88178CicadaV2Patch), "Cicada V2");
            return;

        case Ax88178PhyModeCicadaV2Asix:
            BeginPhyPatch(Device, Ax88178CicadaV2AsixPatch, ARRAYSIZE(Ax88178CicadaV2AsixPatch), "Cicada V2 ASIX");
            return;

        case Ax88178PhyModeAgereA:
        case Ax88178PhyModeAgereB:
            BeginPhyPatch(Device, Ax88178AgereV0Patch, ARRAYSIZE(Ax88178AgereV0Patch), "Agere V0");
            return;

        case Ax88178PhyModeVitesse:
            BeginPhyPatch(Device, Ax88178Vitesse8601Patch, ARRAYSIZE(Ax88178Vitesse8601Patch), "Vitesse VSC8601");
            return;

        default:
            DbgPrint("[usbenet]: EEPROM PHY mode 0x%02X has no recovered vendor patch; using standard IEEE PHY initialization.\n", m_PhyMode);
            AdvanceInitStage(Device);
            return;
    }
}

VOID CAx88178::CompletePhyConfigStep(CUsbEnet* Device, NTSTATUS Status) {
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    AX88178_PHY_CONFIG_STEP CompletedStep = m_PhyConfigStep;

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: AX88178 EEPROM PHY-mode step %u failed with status 0x%08X.\n", static_cast<DWORD>(CompletedStep), Status);

    if (CompletedStep == Ax88178PhyConfigEnableEeprom) {
        if (NT_SUCCESS(Status)) {
            m_PhyConfigStep = Ax88178PhyConfigReadEepromWord17;
            NTSTATUS QueueStatus = QueuePhyConfigStep(Device);

            if (NT_SUCCESS(QueueStatus))
                goto ReleaseLock;

            DbgPrint("[usbenet]: Couldn't queue AX88178 EEPROM word 0x17 read (err = 0x%08X).\n", QueueStatus);
        }

        m_PhyConfigStep = Ax88178PhyConfigDisableEeprom;
        if (NT_SUCCESS(QueuePhyConfigStep(Device)))
            goto ReleaseLock;
    } else if (CompletedStep == Ax88178PhyConfigReadEepromWord17) {
        if (NT_SUCCESS(Status) && Device->ControlRequest.Transfer.BytesTransferred >= sizeof(USHORT)) {
            USHORT RawValue = *reinterpret_cast<PUSHORT>(reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize);
            m_EepromWord17 = _byteswap_ushort(RawValue);
            m_EepromWord17Valid = TRUE;
            m_PhyMode = m_EepromWord17 == 0xFFFF ? Ax88178PhyModeMarvell : m_EepromWord17 & Ax88178PhyModeMask;
            m_LedMode = m_EepromWord17 == 0xFFFF ? 0 : m_EepromWord17 >> 8;
            DbgPrint("[usbenet]: AX88178 EEPROM[0x17]=0x%04X PHY mode=0x%02X LED mode=0x%02X.\n", m_EepromWord17, m_PhyMode, m_LedMode);
        } else if (NT_SUCCESS(Status)) {
            DbgPrint("[usbenet]: AX88178 EEPROM word 0x17 read returned only %u bytes.\n", Device->ControlRequest.Transfer.BytesTransferred);
        }

        m_PhyConfigStep = Ax88178PhyConfigDisableEeprom;
        if (NT_SUCCESS(QueuePhyConfigStep(Device)))
            goto ReleaseLock;
    } else if (CompletedStep != Ax88178PhyConfigDisableEeprom) {
        DbgPrint("[usbenet]: AX88178 EEPROM PHY-mode completion entered invalid step %u.\n", static_cast<DWORD>(CompletedStep));
    }

    m_PhyConfigStep = Ax88178PhyConfigIdle;

    if (m_PhyConfigInitialRead) {
        m_PhyConfigInitialRead = FALSE;
        AdvanceInitStage(Device);
    } else {
        ContinueVendorPhyInitialization(Device);
    }

ReleaseLock:
    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteReadNodeId(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteReadNodeId Device=%p status=0x%08X", Device, Status);
	PBYTE NodeBytes = NULL;

	NicBaseTakeLock(Device);

	Device->CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Reading node ID failed with status 0x%08X!\n", Status);
		goto ReleaseLock;
	}

	if (Device->ControlRequest.Transfer.BytesTransferred < sizeof(CEnetAddr)) {
		DbgPrint("[usbenet]: Reading node ID returned only %u bytes!\n", Device->ControlRequest.Transfer.BytesTransferred);
		goto ReleaseLock;
	}

	memcpy(&Device->NodeId, reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize, sizeof(CEnetAddr));

	NodeBytes = reinterpret_cast<PBYTE>(&Device->NodeId);
	USBENET_LOG("Read Device->NodeId %02X:%02X:%02X:%02X:%02X:%02X", NodeBytes[0], NodeBytes[1], NodeBytes[2], NodeBytes[3], NodeBytes[4], NodeBytes[5]);

	TRAP_ASSERT(Device->InitStage == UsbEnetInitReadNodeId);

	AdvanceInitStage(Device);

ReleaseLock:
	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);
	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteWriteNodeId(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteWriteNodeId Device=%p status=0x%08X", Device, Status);
	NicBaseTakeLock(Device);
	Device->CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Writing node ID failed with status 0x%08x!  Continuing.\n", Status);
	}

	if (Device->InitStage != UsbEnetInitComplete) {
		AdvanceInitStage(Device);
	}

	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);
	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteReadPhyAddressRegister(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteReadPhyAddressRegister Device=%p status=0x%08X", Device, Status);
	UNREFERENCED_PARAMETER(Status);

	NicBaseTakeLock(Device);
	Device->CompleteControlTransfer();

	USHORT value = _byteswap_ushort(*reinterpret_cast<PUSHORT>(reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize));

	Device->PhyAddress = (value >> 8) & 0x1F;

	TRAP_ASSERT(Device->InitStage == UsbEnetInitReadPhyAddressRegister);

	AdvanceInitStage(Device);

	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);
	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteWriteMediumStatus(CUsbEnet* Device, NTSTATUS Status) {
    USBENET_LOG("CompleteWriteMediumStatus Device=%p status=0x%08X", Device, Status);
    BOOL NotifyUsers = FALSE;

    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: Writing medium status register failed with status 0x%08x! Continuing.\n", Status);

    if (Device->InitStage != UsbEnetInitComplete) {
        AdvanceInitStage(Device);
    } else {
        UpdateMarvellLed(Device);
    }

    if ((Device->Flags & USBENET_STATE_NOTIFY_LINK_STATE) != 0) {
        Device->Flags &= ~USBENET_STATE_NOTIFY_LINK_STATE;
        NotifyUsers = TRUE;
    }

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);

    if (NotifyUsers)
        Device->NotifyLinkStateChangedToUsers();
}

VOID CAx88178::CompleteWriteGpio(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteWriteGpio Device=%p status=0x%08X", Device, Status);
	NicBaseTakeLock(Device);
	Device->CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Writing GPIO failed with status 0x%08x! Continuing.\n", Status);
	}

	TRAP_ASSERT(
		Device->InitStage == UsbEnetInitWriteGpio1 ||
		Device->InitStage == UsbEnetInitWriteGpio2 ||
		Device->InitStage == UsbEnetInitWriteGpio3 ||
		Device->InitStage == UsbEnetInitWriteGpio4);

	AdvanceInitStage(Device);

	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);
	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteSoftwareReset(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteSoftwareReset Device=%p status=0x%08X", Device, Status);
	NicBaseTakeLock(Device);
	Device->CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Writing software reset failed with status 0x%08x! Continuing.\n", Status);
	}

	if (Device->InitStage != UsbEnetInitComplete) {
		TRAP_ASSERT(Device->InitStage == UsbEnetInitSoftwareResetClear || Device->InitStage == UsbEnetInitSoftwareReset);
		AdvanceInitStage(Device);
	}

	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);
	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteWriteMiiControl(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteWriteMiiControl Device=%p status=0x%08X", Device, Status);
	NicBaseTakeLock(Device);
	Device->CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Writing MII control register failed with status 0x%08x! Continuing.\n", Status);
	}

	TRAP_ASSERT(Device->InitStage == UsbEnetInitWriteMiiControlRegister);

	AdvanceInitStage(Device);

	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);
	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::WriteIpg(CUsbEnet* Device) {
	USBENET_LOG("WriteIpg Device=%p", Device);
	NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineWriteIpg, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_IPG0, 0x0C15, 0x000E, 0, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to write IPG control register (err = 0x%08x)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::ReadNodeId(CUsbEnet* Device) {
	USBENET_LOG("ReadNodeId Device=%p", Device);
	TRAP_ASSERT(Device->InitStage == UsbEnetInitReadNodeId);
	NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineReadNodeId, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_READ_NODE_ID, 0, 0, 6, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to read node ID (err = 0x%08x)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::WriteNodeId(CUsbEnet* Device) {
	USBENET_LOG("Ax88178WriteNodeId Device=%p", Device);

	const CEnetAddr* Address = NULL;

	if (!Device->UnicastAddress.IsZero())
		Address = &Device->UnicastAddress;
	else if (!Device->AlternateUnicastAddress.IsZero())
		Address = &Device->AlternateUnicastAddress;

	if (Address == NULL) {
		DbgPrint("[usbenet]: No Ethernet address, not configuring USB Ethernet device node ID!\n");

		if (Device->InitStage != UsbEnetInitComplete)
			AdvanceInitStage(Device);

		return;
	}

	PBYTE AddressBytes = reinterpret_cast<PBYTE>(const_cast<CEnetAddr*>(Address));
	USBENET_LOG("Programming adapter address %02X:%02X:%02X:%02X:%02X:%02X", AddressBytes[0], AddressBytes[1], AddressBytes[2], AddressBytes[3], AddressBytes[4], AddressBytes[5]);

	NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineWriteNodeId, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_NODE_ID, 0, 0, sizeof(CEnetAddr), (PVOID)Address);

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to write node ID (err = 0x%08X)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::ReadPhyAddressRegister(CUsbEnet* Device) {
	USBENET_LOG("ReadPhyAddressRegister Device=%p", Device);
	TRAP_ASSERT(Device->InitStage == UsbEnetInitReadPhyAddressRegister);
	NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineReadPhyAddressRegister, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_READ_PHY_ID, 0, 0, 2, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to read PHY address register (err = 0x%08x)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::WriteMediumStatus(CUsbEnet* Device) {
    USBENET_LOG("WriteMediumStatus Device=%p", Device);
    USHORT MediumStatus = AX_MEDIUM_DEFAULT;

    if ((Device->LinkState & NIC_LINK_STATE_1000_MBPS) != 0)
        MediumStatus |= AX_MEDIUM_GM;
    else if ((Device->LinkState & NIC_LINK_STATE_100_MBPS) != 0)
        MediumStatus |= AX_MEDIUM_PS;

    if ((Device->LinkState & NIC_LINK_STATE_FULL_DUPLEX) != 0)
        MediumStatus |= AX_MEDIUM_FD;

    if ((Device->LinkState & NIC_LINK_STATE_TX_FLOW_CONTROL) != 0)
        MediumStatus |= AX_MEDIUM_RFC | AX_MEDIUM_TFC;

    NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineWriteMediumStatus, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_MEDIUM_MODE, MediumStatus, 0, 0, NULL);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: Can't queue control transfer to write medium status register (err = 0x%08x)!\n", Status);
        if (Device->InitStage != UsbEnetInitComplete)
            AdvanceInitStage(Device);
    }
}

VOID CAx88178::WriteGpio(CUsbEnet* Device, BYTE Value) {
	USBENET_LOG("WriteGpio Device=%p value=0x%02X", Device, Value);
	NTSTATUS Status;

	TRAP_ASSERT(Device->InitStage == UsbEnetInitWriteGpio1 || Device->InitStage == UsbEnetInitWriteGpio2 || Device->InitStage == UsbEnetInitWriteGpio3 || Device->InitStage == UsbEnetInitWriteGpio4);

	Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineWriteGpio, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_GPIOS, Value, 0, 0, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to write GPIO register (err = 0x%08x)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::SoftwareReset(CUsbEnet* Device, BYTE Value) {
	USBENET_LOG("SoftwareReset Device=%p value=0x%02X", Device, Value);
	NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineSoftwareReset, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_SW_RESET, Value, 0, 0, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to write software reset register (err = 0x%08x)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::WriteMiiControl(CUsbEnet* Device) {
	USBENET_LOG("WriteMiiControl Device=%p", Device);
	NTSTATUS Status;

	TRAP_ASSERT(Device->InitStage == UsbEnetInitWriteMiiControlRegister);

	Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineWriteMiiControl, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_SW_PHY_SELECT, 0, 0, 0, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to write software reset register (err = 0x%08x)!\n", Status);
		TRAP_ASSERT(FALSE);
	}
}

VOID CAx88178::UpdateReceiveFilter(CUsbEnet* Device) {
    USBENET_LOG("Ax88178UpdateRecvFilter Device=%p", Device);
    TRAP_THREAD(Device->LockOwnerThread);

    if (Device->InitStage < UsbEnetInitStartReceiving || (Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    if (m_FilterStep != Ax88178FilterIdle || (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0) {
        m_FilterUpdatePending = TRUE;
        return;
    }

    BeginFilterUpdate(Device, FALSE);
}

VOID CAx88178::UpdateLinkState(CUsbEnet* Device) {
    USBENET_LOG("UpdateLinkState Device=%p", Device);

    DWORD NewLinkState = 0;
    USHORT Control = 0;
    USHORT BasicStatus = 0;
    USHORT Advertisement = 0;
    USHORT LinkPartnerAbility = 0;
    USHORT GigabitControl = 0;
    USHORT GigabitStatus = 0;

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) == 0) {
        Control = Device->PhyRegisters[MII_BMCR];
        BasicStatus = Device->PhyRegisters[MII_BMSR];
        Advertisement = Device->PhyRegisters[MII_ADVERTISE];
        LinkPartnerAbility = Device->PhyRegisters[MII_LPA];
        GigabitControl = Device->PhyRegisters[MII_CTRL1000];
        GigabitStatus = Device->PhyRegisters[MII_STAT1000];
    }

    if (m_InterruptStatusSeen && m_InterruptLinkAuthoritative) {
        if (m_InterruptLinkUp)
            BasicStatus |= MII_BMSR_LINK_STATUS;
        else
            BasicStatus &= ~MII_BMSR_LINK_STATUS;
    }

    if ((Control & MII_BMCR_AUTONEG_ENABLE) == 0) {
        if ((Control & MII_BMCR_SPEED1000) != 0)
            NewLinkState = NIC_LINK_STATE_1000_MBPS;
        else if ((Control & MII_BMCR_SPEED100) != 0)
            NewLinkState = NIC_LINK_STATE_100_MBPS;
        else
            NewLinkState = NIC_LINK_STATE_10_MBPS;

        NewLinkState |= (Control & MII_BMCR_FULL_DUPLEX) != 0 ? NIC_LINK_STATE_FULL_DUPLEX : NIC_LINK_STATE_HALF_DUPLEX;
        NewLinkState |= NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    } else {
        USHORT CommonCapabilities = Advertisement & LinkPartnerAbility;

        if ((GigabitControl & MII_CTRL1000_FULL) != 0 && (GigabitStatus & MII_STAT1000_LP_FULL) != 0)
            NewLinkState = NIC_LINK_STATE_1000_MBPS | NIC_LINK_STATE_FULL_DUPLEX;
        else if ((GigabitControl & MII_CTRL1000_HALF) != 0 && (GigabitStatus & MII_STAT1000_LP_HALF) != 0)
            NewLinkState = NIC_LINK_STATE_1000_MBPS | NIC_LINK_STATE_HALF_DUPLEX;
        else if ((CommonCapabilities & MII_ADVERTISE_100_FULL) != 0)
            NewLinkState = NIC_LINK_STATE_100_MBPS | NIC_LINK_STATE_FULL_DUPLEX;
        else if ((CommonCapabilities & MII_ADVERTISE_100_HALF) != 0)
            NewLinkState = NIC_LINK_STATE_100_MBPS | NIC_LINK_STATE_HALF_DUPLEX;
        else if ((CommonCapabilities & MII_ADVERTISE_10_FULL) != 0)
            NewLinkState = NIC_LINK_STATE_10_MBPS | NIC_LINK_STATE_FULL_DUPLEX;
        else if ((CommonCapabilities & MII_ADVERTISE_10_HALF) != 0)
            NewLinkState = NIC_LINK_STATE_10_MBPS | NIC_LINK_STATE_HALF_DUPLEX;

        if ((BasicStatus & MII_BMSR_AUTONEG_COMPLETE) != 0 || (Device->LinkState & NIC_LINK_STATE_NEGOTIATION_COMPLETE) != 0 || KeTimeStampBundle->TickCount - Device->LinkPollTick >= 3500)
            NewLinkState |= NIC_LINK_STATE_NEGOTIATION_COMPLETE;

        if ((NewLinkState & NIC_LINK_STATE_FULL_DUPLEX) != 0 && (Advertisement & LinkPartnerAbility & 0x0400) != 0)
            NewLinkState |= NIC_LINK_STATE_TX_FLOW_CONTROL;
    }

    if ((BasicStatus & MII_BMSR_LINK_STATUS) != 0)
        NewLinkState |= NIC_LINK_STATE_ACTIVE;

    if (Device->LinkState != NewLinkState) {
        Device->LinkState = NewLinkState;
        Device->Flags |= USBENET_STATE_NOTIFY_LINK_STATE;
        PrintLinkMode(NewLinkState, Control, BasicStatus, Advertisement, LinkPartnerAbility, GigabitControl, GigabitStatus);
        WriteMediumStatus(Device);
    } else if (Device->InitStage != UsbEnetInitComplete) {
        AdvanceInitStage(Device);
    }

    m_InterruptPhyRefreshPending = FALSE;
    m_InterruptLinkAuthoritative = FALSE;
    m_LastFallbackPhyPollTick = KeTimeStampBundle->TickCount;
    TRAP_THREAD(Device->LockOwnerThread);
}

VOID CAx88178::FinishPhyTransaction(CUsbEnet* Device, NTSTATUS ReleaseStatus) {
    Device->Flags &= ~USBENET_STATE_SERIAL_MGMT_CONTROL;

    if (Device->InitStage == UsbEnetInitWaitPhyReady) {
        if (NT_SUCCESS(ReleaseStatus) && IsRecognizedPhyIdentifier(Device->CurrentPhyValue)) {
            Device->PhyRegisters[PhyRegisterIdentifier1] = Device->CurrentPhyValue;
            m_PhyReadyRetryPending = FALSE;
            DbgPrint("[usbenet]: AX88178 external PHY became ready after %u retries; PHYSID1=0x%04X.\n", m_PhyReadyRetryCount, Device->CurrentPhyValue);
            AdvanceInitStage(Device);
        } else if (m_PhyReadyRetryCount < Ax88178PhyReadyRetryLimit) {
            ++m_PhyReadyRetryCount;
            m_PhyReadyRetryPending = TRUE;
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178PhyReadyRetryPeriod);
        } else {
            DbgPrint("[usbenet]: AX88178 external PHY did not become ready after %u attempts; continuing with PHYSID1=0x%04X.\n", m_PhyReadyRetryCount, Device->CurrentPhyValue);
            m_PhyReadyRetryPending = FALSE;
            AdvanceInitStage(Device);
        }
        return;
    }

    if (m_PhyPatch != NULL) {
        if (!NT_SUCCESS(ReleaseStatus) || m_PhyPatchOperationFailed) {
            DbgPrint("[usbenet]: AX88178 %s PHY patch failed at write %u/%u; continuing initialization.\n", m_PhyPatchName, m_PhyPatchIndex + 1, m_PhyPatchCount);
            m_PhyPatch = NULL;
            m_PhyPatchCount = 0;
            m_PhyPatchIndex = 0;
            m_PhyPatchName = NULL;
            m_PhyPatchOperationFailed = FALSE;
            AdvanceInitStage(Device);
        } else {
            ++m_PhyPatchIndex;
            if (m_PhyPatchIndex < m_PhyPatchCount && QueueNextPhyPatch(Device))
                return;

            DbgPrint("[usbenet]: AX88178 %s PHY patch completed (%u writes).\n", m_PhyPatchName, m_PhyPatchCount);
            m_PhyPatch = NULL;
            m_PhyPatchCount = 0;
            m_PhyPatchIndex = 0;
            m_PhyPatchName = NULL;
            AdvanceInitStage(Device);
        }
        return;
    }

    if (m_MarvellLedStep == Ax88178MarvellLedRead) {
        USHORT Value = static_cast<USHORT>(Device->CurrentPhyValue & 0xFC0F);
        if ((Device->LinkState & NIC_LINK_STATE_1000_MBPS) != 0)
            Value |= 0x03E0;
        else if ((Device->LinkState & NIC_LINK_STATE_100_MBPS) != 0)
            Value |= 0x03B0;
        else
            Value |= 0x02F0;

        m_MarvellLedStep = Ax88178MarvellLedWrite;
        if (NT_SUCCESS(BeginWritePhy(Device, 0x19, Value)))
            return;
        m_MarvellLedStep = Ax88178MarvellLedIdle;
    } else if (m_MarvellLedStep == Ax88178MarvellLedWrite) {
        m_MarvellLedStep = Ax88178MarvellLedIdle;
        return;
    }

    if (Device->InitStage == UsbEnetInitWriteMarvellExtPhySpecificCtrl && m_RealtekPhyInitStep != RealtekPhyInitIdle) {
        if (m_RealtekPhyInitOperationFailed) {
            m_RealtekPhyInitOperationFailed = FALSE;
            if (m_RealtekPhyInitStep != RealtekPhyInitWritePage5 && m_RealtekPhyInitStep != RealtekPhyInitRestorePage0 && m_RealtekPhyInitStep != RealtekPhyInitRestorePage0AfterLed && m_RealtekPhyInitStep != RealtekPhyInitRestorePage0AfterFailure) {
                m_RealtekPhyInitStep = RealtekPhyInitRestorePage0AfterFailure;
                if (NT_SUCCESS(QueueRealtekPhyInitStep(Device)))
                    return;
            }
            m_RealtekPhyInitStep = RealtekPhyInitIdle;
            AdvanceInitStage(Device);
        } else if (!AdvanceRealtekPhyInit(Device)) {
            AdvanceInitStage(Device);
        }
    } else if ((Device->Flags & USBENET_STATE_REFRESH_PHY_REGISTERS) != 0) {
        Device->Flags &= ~USBENET_STATE_REFRESH_PHY_REGISTERS;
        if (!NT_SUCCESS(BeginReadAllPhy(Device)) && (Device->Flags & USBENET_STATE_LINK_STATE_UPDATE_PENDING) != 0) {
            Device->Flags &= ~USBENET_STATE_LINK_STATE_UPDATE_PENDING;
            UpdateLinkState(Device);
        }
    } else if ((Device->Flags & USBENET_STATE_LINK_STATE_UPDATE_PENDING) != 0) {
        Device->Flags &= ~USBENET_STATE_LINK_STATE_UPDATE_PENDING;
        UpdateLinkState(Device);
    } else if (Device->InitStage != UsbEnetInitComplete) {
        AdvanceInitStage(Device);
    }
}

VOID CAx88178::CompleteReleaseSerialMgmtControl(CUsbEnet* Device, NTSTATUS Status) {
    USBENET_LOG("CompleteReleaseSerialMgmtControl Device=%p status=0x%08X", Device, Status);
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: Releasing serial management control failed with status 0x%08x! Continuing.\n", Status);

    FinishPhyTransaction(Device, Status);

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

BOOL CAx88178::UsesInterruptLinkStatus() const {
    return TRUE;
}

BOOL CAx88178::ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length) {
    TRAP_THREAD(Device->LockOwnerThread);

    if (Data == NULL || Length < Ax88178InterruptPacketSize)
        return FALSE;

    BOOL LinkUp = (Data[Ax88178InterruptLinkByte] & Ax88178InterruptLinkMask) != 0;
    BOOL FirstStatus = !m_InterruptStatusSeen;
    BOOL LinkChanged = FirstStatus || LinkUp != m_InterruptLinkUp;
    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    m_InterruptStatusSeen = TRUE;
    m_InterruptLinkUp = LinkUp;
    ++m_InterruptEventCount;

    if (LinkChanged || m_InterruptEventCount <= 4 || (m_InterruptEventCount & (m_InterruptEventCount - 1)) == 0) {
        DbgPrint("[usbenet]: AX88178 interrupt status #%u bytes=%02X %02X %02X %02X %02X %02X %02X %02X link=%u.\n", m_InterruptEventCount, Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7], LinkUp);
    }

    if (LinkChanged || (LinkUp && (Device->LinkState & NIC_LINK_STATE_ACTIVE) == 0)) {
        m_InterruptPhyRefreshPending = TRUE;
        m_InterruptLinkAuthoritative = TRUE;
        Device->Flags |= USBENET_STATE_LINK_STATE_UPDATE_PENDING;

        if ((Device->Flags & (ControlOutstanding | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) == 0) {
            NTSTATUS Status = BeginReadAllPhy(Device);

            if (!NT_SUCCESS(Status) && Status != static_cast<NTSTATUS>(0x80000011)) {
                DbgPrint("[usbenet]: AX88178 interrupt-triggered PHY refresh could not start (0x%08X); timer will retry.\n", Status);
            }
        }
    }

    if (LinkUp)
        return FALSE;

    DWORD NewLinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    BOOL Notify = Device->LinkState != NewLinkState;
    Device->LinkState = NewLinkState;
    Device->LinkPollTick = CurrentTick;

    if (Notify)
        DbgPrint("[usbenet]: AX88178 interrupt reported physical link down; notifying users immediately.\n");

    return Notify;
}

VOID CAx88178::RestartReceiving(CUsbEnet* Device) {
	USBENET_LOG("Ax88178StopAndRestartReceiving Device=%p", Device);
	TRAP_THREAD(Device->LockOwnerThread);

	if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || (Device->Flags & ReceiveRunning) == 0)
		return;

	Device->Flags &= ~ReceiveRunning;
	Device->NoteReceiveRestart();

	for (ULONG Index = 0; Index < RECV_PACKET_COUNT; Index++) {
		PRECV_TRANSFER Packet = &Device->RecvPackets[Index];

		if (Packet->InFlight != 0) {
			TRAP_IRQL(DISPATCH_LEVEL);
			UsbdCancelAsyncTransfer(&Packet->Transfer);
		}
	}

	NTSTATUS Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)CUsbEnet::AsyncCompletionRoutineStopReceiving, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_RX_CTL, 0, 0, 0, NULL);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Can't queue control transfer to clear RX control register (err = 0x%08x)! Restoring receive submissions.\n", Status);
		Device->Flags |= ReceiveRunning;
		Device->SubmitReceive();
	}
}

VOID CAx88178::CompleteReadPhy(CUsbEnet* Device, NTSTATUS Status) {
    USBENET_LOG("CompleteReadPhy Device=%p status=0x%08X phyAddress=0x%02X register=0x%04X bytesTransferred=%u buffer=%p", Device, Status, Device->PhyAddress, Device->CurrentPhyRegister, Device->ControlRequest.Transfer.BytesTransferred, Device->ControlRequest.Transfer.Buffer);
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
    BOOL RealtekPagedRead = Device->InitStage == UsbEnetInitWriteMarvellExtPhySpecificCtrl && m_RealtekPhyInitStep == RealtekPhyInitReadRegister01;

    if (!NT_SUCCESS(Status)) {
        if (RealtekPagedRead)
            m_RealtekPhyInitOperationFailed = TRUE;
        if (Device->InitStage == UsbEnetInitWaitPhyReady)
            Device->CurrentPhyValue = 0xFFFF;
        DbgPrint("[usbenet]: Reading PHY register failed with status 0x%08x! Continuing.\n", Status);
    } else {
        USHORT PhyValue = _byteswap_ushort(*reinterpret_cast<PUSHORT>(reinterpret_cast<PBYTE>(Device->PhysicalMemory) + DmaBufferSize));
        Device->CurrentPhyValue = PhyValue;

        if (!RealtekPagedRead && Device->CurrentPhyRegister <= PhyRegisterExtendedStatus) {
            if (Device->PhyRegisters[Device->CurrentPhyRegister] != PhyValue) {
                Device->PhyRegisters[Device->CurrentPhyRegister] = PhyValue;
                switch (Device->CurrentPhyRegister) {
                    case PhyRegisterControl:
                    case PhyRegisterStatus:
                    case PhyRegisterAnar:
                    case PhyRegisterLinkPartner:
                    case PhyRegisterExpansion:
                    case PhyRegister1000TControl:
                    case PhyRegister1000TStatus:
                        Device->Flags |= USBENET_STATE_LINK_STATE_UPDATE_PENDING;
                        break;
                }
                if (Device->InitStage == UsbEnetInitComplete && Device->CurrentPhyRegister == PhyRegisterStatus && (Device->Flags & USBENET_STATE_READ_ALL_PHY_REGISTERS) == 0)
                    Device->Flags |= USBENET_STATE_REFRESH_PHY_REGISTERS;
            }
        }
    }

    if ((Device->Flags & USBENET_STATE_READ_ALL_PHY_REGISTERS) != 0 && Device->CurrentPhyRegister < PhyRegisterExtendedStatus) {
        NTSTATUS ReadStatus = BeginReadPhy(Device, static_cast<USHORT>(Device->CurrentPhyRegister + 1));
        if (!NT_SUCCESS(ReadStatus)) {
            Device->Flags &= ~USBENET_STATE_READ_ALL_PHY_REGISTERS;
            if (Device->InitStage != UsbEnetInitComplete)
                AdvanceInitStage(Device);
        }
    } else {
        Device->Flags &= ~USBENET_STATE_READ_ALL_PHY_REGISTERS;
        NTSTATUS ReleaseStatus = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineReleaseSerialMgmtControl, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_SET_HW_MII, 0, 0, 0, NULL);
        if (!NT_SUCCESS(ReleaseStatus)) {
            DbgPrint("[usbenet]: Can't queue control transfer to release serial management control (err = 0x%08x)!\n", ReleaseStatus);
            FinishPhyTransaction(Device, ReleaseStatus);
        }
    }

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::CompleteWritePhy(CUsbEnet* Device, NTSTATUS Status) {
    USBENET_LOG("CompleteWritePhy Device=%p status=0x%08X", Device, Status);
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;

    if (!NT_SUCCESS(Status)) {
        if (Device->InitStage == UsbEnetInitWriteMarvellExtPhySpecificCtrl && m_RealtekPhyInitStep != RealtekPhyInitIdle)
            m_RealtekPhyInitOperationFailed = TRUE;
        if (m_PhyPatch != NULL)
            m_PhyPatchOperationFailed = TRUE;
        DbgPrint("[usbenet]: Writing PHY register failed with status 0x%08x! Continuing.\n", Status);
    }

    NTSTATUS ReleaseStatus = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineReleaseSerialMgmtControl, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_SET_HW_MII, 0, 0, 0, NULL);
    if (!NT_SUCCESS(ReleaseStatus)) {
        DbgPrint("[usbenet]: Can't queue control transfer to release serial management control (err = 0x%08x)!\n", ReleaseStatus);
        FinishPhyTransaction(Device, ReleaseStatus);
    }

    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

NTSTATUS CAx88178::BeginWritePhy(CUsbEnet* Device, USHORT Register, USHORT Value) {
	USBENET_LOG("PhyBeginWrite Device=%p phyAddress=0x%02X register=0x%04X value=0x%04X", Device, Device->PhyAddress, Register, Value);
	if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
		return STATUS_DEVICE_REMOVED;
	}

	if ((Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0) {
		DbgPrint("[usbenet]: PHY read (%i) or write (%i) already pending!\n", (Device->Flags & USBENET_STATE_PHY_READ_IN_PROGRESS) != 0, (Device->Flags & USBENET_STATE_PHY_WRITE_IN_PROGRESS) != 0);
		return STATUS_DEVICE_BUSY;
	}

	Device->CurrentPhyRegister = Register;
	Device->CurrentPhyValue = Value;
	Device->Flags |= USBENET_STATE_PHY_WRITE_IN_PROGRESS;

	NTSTATUS Status;

	if ((Device->Flags & USBENET_STATE_SERIAL_MGMT_CONTROL) != 0) {
		USHORT WriteValue = _byteswap_ushort(Value);
		Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineWritePhy, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_MII_REG, Device->PhyAddress, Register, sizeof(WriteValue), &WriteValue);

		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Can't queue control transfer to write PHY register (err = 0x%08x)!\n", Status);
		}
	} else {
		Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineAcquireSerialMgmtControl, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_SET_SW_MII, 0, 0, 0, NULL);

		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Can't queue control transfer to acquire serial management control (err = 0x%08x)!\n", Status);
		}
	}

	if (!NT_SUCCESS(Status)) {
		Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;
	}

	return Status;
}

VOID CAx88178::CompleteAcquireSerialMgmtControl(CUsbEnet* Device, NTSTATUS Status) {
    USBENET_LOG("CompleteAcquireSerialMgmtControl Device=%p status=0x%08X", Device, Status);
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();

    if (!NT_SUCCESS(Status)) {
        Device->Flags &= ~(USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS);
        DbgPrint("[usbenet]: Acquiring serial management control failed with status 0x%08x.\n", Status);

        if (Device->InitStage == UsbEnetInitWaitPhyReady) {
            Device->CurrentPhyValue = 0xFFFF;
            if (m_PhyReadyRetryCount < Ax88178PhyReadyRetryLimit) {
                ++m_PhyReadyRetryCount;
                m_PhyReadyRetryPending = TRUE;
                CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178PhyReadyRetryPeriod);
            } else {
                AdvanceInitStage(Device);
            }
        } else if (m_PhyPatch != NULL) {
            m_PhyPatchOperationFailed = TRUE;
            m_PhyPatch = NULL;
            m_PhyPatchCount = 0;
            m_PhyPatchIndex = 0;
            m_PhyPatchName = NULL;
            AdvanceInitStage(Device);
        } else if (m_MarvellLedStep != Ax88178MarvellLedIdle) {
            m_MarvellLedStep = Ax88178MarvellLedIdle;
        } else if (Device->InitStage == UsbEnetInitWriteMarvellExtPhySpecificCtrl && m_RealtekPhyInitStep != RealtekPhyInitIdle) {
            m_RealtekPhyInitStep = RealtekPhyInitIdle;
            m_RealtekPhyInitOperationFailed = FALSE;
            AdvanceInitStage(Device);
        } else if (Device->InitStage != UsbEnetInitComplete) {
            AdvanceInitStage(Device);
        }
        goto ReleaseLock;
    }

    Device->Flags |= USBENET_STATE_SERIAL_MGMT_CONTROL;
    if ((Device->Flags & USBENET_STATE_PHY_READ_IN_PROGRESS) != 0) {
        Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
        NTSTATUS OperationStatus = BeginReadPhy(Device, Device->CurrentPhyRegister);
        if (!NT_SUCCESS(OperationStatus)) {
            if (Device->InitStage == UsbEnetInitWaitPhyReady)
                Device->CurrentPhyValue = 0xFFFF;
            FinishPhyTransaction(Device, OperationStatus);
        }
    } else if ((Device->Flags & USBENET_STATE_PHY_WRITE_IN_PROGRESS) != 0) {
        Device->Flags &= ~USBENET_STATE_PHY_WRITE_IN_PROGRESS;
        NTSTATUS OperationStatus = BeginWritePhy(Device, Device->CurrentPhyRegister, Device->CurrentPhyValue);
        if (!NT_SUCCESS(OperationStatus))
            FinishPhyTransaction(Device, OperationStatus);
    }

ReleaseLock:
    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

NTSTATUS CAx88178::BeginReadPhy(CUsbEnet* Device, USHORT Register) {
	USBENET_LOG("PhyBeginRead Device=%p phyAddress=0x%02X register=0x%04X", Device, Device->PhyAddress, Register);
	if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
		return static_cast<NTSTATUS>(0xC00002B6);
	}

	if ((Device->Flags & (USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0) {
		DbgPrint("[usbenet]: PHY read (%i) or write (%i) already pending!\n", (Device->Flags & USBENET_STATE_PHY_READ_IN_PROGRESS) != 0, (Device->Flags & USBENET_STATE_PHY_WRITE_IN_PROGRESS) != 0);
		return static_cast<NTSTATUS>(0x80000011);
	}

	Device->CurrentPhyRegister = Register;
	Device->CurrentPhyValue = 0;
	Device->Flags |= USBENET_STATE_PHY_READ_IN_PROGRESS;

	NTSTATUS Status;

	if ((Device->Flags & USBENET_STATE_SERIAL_MGMT_CONTROL) != 0) {
		Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineReadPhy, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_READ_MII_REG, Device->PhyAddress, Register, sizeof(USHORT), NULL);

		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Can't queue control transfer to read PHY register (err = 0x%08x)!\n", Status);
		}
	} else {
		Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineAcquireSerialMgmtControl, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_SET_SW_MII, 0, 0, 0, NULL);

		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Can't queue control transfer to acquire serial management control (err = 0x%08x)!\n", Status);
		}
	}

	if (!NT_SUCCESS(Status)) {
		Device->Flags &= ~USBENET_STATE_PHY_READ_IN_PROGRESS;
	}

	return Status;
}

NTSTATUS CAx88178::BeginReadAllPhy(CUsbEnet* Device) {
	USBENET_LOG("PhyBeginReadAll Device=%p", Device);
	NTSTATUS Status = BeginReadPhy(Device, 0);

	if (NT_SUCCESS(Status)) {
		Device->Flags |= USBENET_STATE_READ_ALL_PHY_REGISTERS;
	}

	return Status;
}

USHORT CAx88178::BuildReceiveControl(CUsbEnet* Device, BYTE Filter[8], PBOOL WriteFilter) const {
    memset(Filter, 0, 8);
    USHORT ReceiveControl = AX_RX_CTL_START | AX_RX_CTL_MFB_2048;
    DWORD MulticastCount = 0;

    // AX88178 has one perfect unicast address in the node-ID register. The
    // eight-byte hash table is for multicast addresses and cannot accept the
    // Xbox debug/XBDM alternate unicast MAC. Receive all unicast in hardware
    // while an alternate address is attached, then let CompleteReceive route
    // only the title and debug addresses to their respective users.
    if ((Device->AggregateReceiveFilter & (NIC_RECV_DEST_FLAG_PROMISCUOUS | NIC_RECV_DEST_FLAG_ALTERNATE_UNICAST)) != 0)
        ReceiveControl |= AX_RX_CTL_PRO;
    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_BROADCAST) != 0)
        ReceiveControl |= AX_RX_CTL_AB;

    for (DWORD Index = 0; Index < ARRAYSIZE(Device->MulticastTable); Index++) {
        const NIC_MCAST_ENTRY& Entry = Device->MulticastTable[Index];
        if (Entry.SubscriptionCount == 0 || !Entry.Address.IsMulticast())
            continue;

        const BYTE* Address = reinterpret_cast<const BYTE*>(&Entry.Address);
        DWORD Hash = EthernetCrc32(Address, sizeof(CEnetAddr)) >> 26;
        Filter[Hash >> 3] |= static_cast<BYTE>(1 << (Hash & 7));
        ++MulticastCount;
    }

    if ((Device->AggregateReceiveFilter & NIC_RECV_DEST_FLAG_MULTICAST) != 0) {
        if (MulticastCount == 0)
            ReceiveControl |= AX_RX_CTL_AMALL;
        else
            ReceiveControl |= AX_RX_CTL_AM;
    }

    *WriteFilter = memcmp(Filter, m_MulticastFilter, 8) != 0;
    return ReceiveControl;
}

VOID CAx88178::BeginFilterUpdate(CUsbEnet* Device, BOOL SubmitReceiveRing) {
    TRAP_THREAD(Device->LockOwnerThread);
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;

    m_FilterSubmitReceiveRing = m_FilterSubmitReceiveRing || SubmitReceiveRing;
    if (m_FilterStep != Ax88178FilterIdle || (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0) {
        m_FilterUpdatePending = TRUE;
        return;
    }

    BOOL WriteFilter = FALSE;
    m_PendingReceiveControl = BuildReceiveControl(Device, m_PendingMulticastFilter, &WriteFilter);
    m_FilterStep = WriteFilter ? Ax88178FilterWriteHash : Ax88178FilterWriteRxControl;
    m_FilterUpdatePending = FALSE;

    NTSTATUS Status;
    if (WriteFilter)
        Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineFilterUpdate, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_MULTI_FILTER, 0, 0, sizeof(m_PendingMulticastFilter), m_PendingMulticastFilter);
    else
        Status = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineFilterUpdate, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_RX_CTL, m_PendingReceiveControl, 0, 0, NULL);

    if (!NT_SUCCESS(Status)) {
        DbgPrint("[usbenet]: AX88178 receive-filter update could not queue (0x%08X); retrying.\n", Status);
        m_FilterStep = Ax88178FilterIdle;
        m_FilterUpdatePending = TRUE;
        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178TransportRecoveryDelay);
    }
}

VOID CAx88178::CompleteFilterUpdate(CUsbEnet* Device, NTSTATUS Status) {
    NicBaseTakeLock(Device);
    Device->CompleteControlTransfer();
    AX88178_FILTER_STEP CompletedStep = m_FilterStep;

    if (!NT_SUCCESS(Status))
        DbgPrint("[usbenet]: AX88178 receive-filter step %u failed with 0x%08X.\n", static_cast<DWORD>(CompletedStep), Status);

    if (CompletedStep == Ax88178FilterWriteHash) {
        if (NT_SUCCESS(Status))
            memcpy(m_MulticastFilter, m_PendingMulticastFilter, sizeof(m_MulticastFilter));
        else
            m_FilterUpdatePending = TRUE;

        m_FilterStep = Ax88178FilterWriteRxControl;
        NTSTATUS QueueStatus = Device->QueueControlTransfer((PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineFilterUpdate, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX_CMD_WRITE_RX_CTL, m_PendingReceiveControl, 0, 0, NULL);
        if (NT_SUCCESS(QueueStatus))
            goto ReleaseLock;
        Status = QueueStatus;
        DbgPrint("[usbenet]: AX88178 RX-control update could not queue after multicast hash (0x%08X).\n", Status);
    }

    m_FilterStep = Ax88178FilterIdle;
    if (!NT_SUCCESS(Status)) {
        m_FilterUpdatePending = TRUE;
        CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178TransportRecoveryDelay);
        goto ReleaseLock;
    }

    if (m_FilterSubmitReceiveRing && (Device->Flags & ReceiveRunning) == 0) {
        Device->Flags |= ReceiveRunning;
        Device->SubmitReceive();
    }
    m_FilterSubmitReceiveRing = FALSE;

    if (Device->InitStage == UsbEnetInitStartReceiving)
        AdvanceInitStage(Device);
    else if (m_FilterUpdatePending && (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0)
        BeginFilterUpdate(Device, FALSE);

ReleaseLock:
    TRAP_ASSERT(Device->PreviousIrql != 0xEE);
    TRAP_THREAD(Device->LockOwnerThread);
    NULL_OWNER_THREAD(Device);
    KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::UpdateMarvellLed(CUsbEnet* Device) {
    if (m_PhyMode != Ax88178PhyModeMarvell || m_LedMode == 0 || (Device->LinkState & NIC_LINK_STATE_ACTIVE) == 0 || m_MarvellLedStep != Ax88178MarvellLedIdle)
        return;

    DWORD LedState = (Device->LinkState & NIC_LINK_STATE_1000_MBPS) != 0 ? 1000 : (Device->LinkState & NIC_LINK_STATE_100_MBPS) != 0 ? 100 : 10;
    if (LedState == m_LastMarvellLedState)
        return;

    m_LastMarvellLedState = LedState;
    m_MarvellLedStep = Ax88178MarvellLedRead;
    if (!NT_SUCCESS(BeginReadPhy(Device, 0x19)))
        m_MarvellLedStep = Ax88178MarvellLedIdle;
}

VOID CAx88178::ResetRecoveryCounters(USBENET_TRANSPORT_PIPE Pipe) {
    DWORD Index = static_cast<DWORD>(Pipe);
    if (Index < ARRAYSIZE(m_RecoveryErrorCount)) {
        m_RecoveryErrorCount[Index] = 0;
        m_RecoveryLastErrorTick[Index] = 0;
    }
}

VOID CAx88178::HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe) {
    if (Pipe == UsbEnetTransportTransmit && Device->PendingXmitCount == 1)
        m_TxOutstandingSinceTick = KeTimeStampBundle->TickCount;
}

VOID CAx88178::HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status) {
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
        if (m_RecoveryLastErrorTick[Index] != 0 && CurrentTick - m_RecoveryLastErrorTick[Index] > Ax88178TransportErrorWindow)
            ResetRecoveryCounters(Pipe);
        return;
    }

    if (m_RecoveryLastErrorTick[Index] == 0 || CurrentTick - m_RecoveryLastErrorTick[Index] > Ax88178TransportErrorWindow)
        m_RecoveryErrorCount[Index] = 0;
    m_RecoveryLastErrorTick[Index] = CurrentTick;
    DWORD Count = ++m_RecoveryErrorCount[Index];

    AX88178_RECOVERY_LEVEL Level = Count >= 5 ? Ax88178RecoveryResetDevice : Count >= 3 ? Ax88178RecoveryReinitialize : Ax88178RecoveryResetPipe;
    if (Level > m_RecoveryLevel) {
        m_RecoveryLevel = Level;
        m_RecoveryPipe = Pipe;
        m_RecoveryRequestTick = CurrentTick;
        m_RecoveryReason = 0x100 + (Index << 8) + Count;
        DbgPrint("[usbenet-recovery]: AX88178 transport error pipe=%u status=0x%08X count=%u level=%u.\n", Index, Status, Count, static_cast<DWORD>(Level));
    }
}

NTSTATUS CAx88178::RequestFullReinitialize(CUsbEnet* Device, DWORD Reason) {
    TRAP_THREAD(Device->LockOwnerThread);
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || Device->InitStage != UsbEnetInitComplete)
        return STATUS_DEVICE_NOT_READY;
    if (m_FullReinitializePending || m_FullReinitializeActive)
        return STATUS_DEVICE_BUSY;

    m_FullReinitializePending = TRUE;
    m_ReinitializeDrainStarted = TRUE;
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

    DbgPrint("[usbenet-recovery]: AX88178 full hardware reinitialization requested reason=%u count=%u RX=%u TX=%u.\n", Reason, m_RecoveryCount, Device->GetReceiveInFlightCount(), Device->PendingXmitCount);
    return STATUS_SUCCESS;
}

VOID CAx88178::ProcessRecovery(CUsbEnet* Device, DWORD CurrentTick) {
    if (m_RecoveryLevel != Ax88178RecoveryIdle && !m_FullReinitializePending && !m_FullReinitializeActive) {
        AX88178_RECOVERY_LEVEL Level = m_RecoveryLevel;
        USBENET_TRANSPORT_PIPE Pipe = m_RecoveryPipe;
        DWORD Reason = m_RecoveryReason;
        m_RecoveryLevel = Ax88178RecoveryIdle;

        if (Level == Ax88178RecoveryResetPipe) {
            Device->ResetTransportEndpoint(Pipe);
            if (Pipe == UsbEnetTransportReceive && (Device->Flags & ReceiveRunning) != 0)
                RestartReceiving(Device);
            DbgPrint("[usbenet-recovery]: AX88178 reset transport endpoint %u%s.\n", static_cast<DWORD>(Pipe), Pipe == UsbEnetTransportReceive ? " and restarted the RX engine" : "");
        } else if (Level == Ax88178RecoveryReinitialize) {
            RequestFullReinitialize(Device, Reason);
        } else {
            Device->PauseInterruptLinkStatus();
            Device->Flags &= ~ReceiveRunning;
            Device->LinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
            m_DeviceResetPending = TRUE;
            DbgPrint("[usbenet-recovery]: AX88178 scheduling USB device reset after repeated transport failures.\n");
        }
    }

    if (!m_FullReinitializePending)
        return;

    DWORD Age = CurrentTick - m_RecoveryRequestTick;
    if (Device->GetReceiveInFlightCount() == 0 && (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0 && Age >= Ax88178FullReinitDrainTimeout)
        UsbdCancelAsyncTransfer(&Device->ControlRequest.Transfer);
    if ((Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0 && Age >= Ax88178FullReinitDrainTimeout)
        Device->Flags &= ~(USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS | USBENET_STATE_SERIAL_MGMT_CONTROL);
    if (Age >= Ax88178FullReinitDrainTimeout) {
        Device->ResetTransportEndpoint(UsbEnetTransportReceive);
        Device->ResetTransportEndpoint(UsbEnetTransportTransmit);
        Device->ResetTransportEndpoint(UsbEnetTransportInterrupt);
    }

    if (Device->GetReceiveInFlightCount() != 0 || Device->PendingXmitCount != 0 || (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0)
        return;

    DWORD Reason = m_RecoveryReason;
    DWORD Count = m_RecoveryCount;
    DWORD RequestTick = m_RecoveryRequestTick;
    ResetState(Device);
    m_FullReinitializePending = FALSE;
    m_FullReinitializeActive = TRUE;
    m_RecoveryReason = Reason;
    m_RecoveryCount = Count;
    m_RecoveryRequestTick = RequestTick;
    Device->Flags &= ~(ReceiveRunning | USBENET_STATE_00080000 | USBENET_STATE_NOTIFY_LINK_STATE | USBENET_STATE_REFRESH_PHY_REGISTERS | USBENET_STATE_READ_ALL_PHY_REGISTERS | USBENET_STATE_LINK_STATE_UPDATE_PENDING);
    Device->LinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;
    Device->TimerTick = CurrentTick;
    Device->InitStage = UsbEnetInitBeginning;
    DbgPrint("[usbenet-recovery]: AX88178 transports drained; replaying EEPROM, GPIO, PHY, MAC, multicast, medium, interrupt and RX initialization.\n");
    AdvanceInitStage(Device);
}

VOID CAx88178::RunTimer(CUsbEnet* Device) {
    USBENET_LOG("Ax88178DoTimerRunning Device=%p", Device);
    NicBaseTakeLockAtRaisedIrql(Device);

    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    DWORD CurrentTick = KeTimeStampBundle->TickCount;

    if (Device->InitStage == UsbEnetInitWaitPhyReady) {
        if (m_PhyReadyRetryPending && (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) == 0) {
            m_PhyReadyRetryPending = FALSE;
            NTSTATUS Status = BeginReadPhy(Device, PhyRegisterIdentifier1);
            if (!NT_SUCCESS(Status)) {
                m_PhyReadyRetryPending = TRUE;
                CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178PhyReadyRetryPeriod);
            }
        }
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    if (Device->InitStage == UsbEnetInitStartReceiving && m_FilterStep == Ax88178FilterIdle) {
        BeginFilterUpdate(Device, TRUE);
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    if (Device->PendingXmitCount != 0 && m_TxOutstandingSinceTick != 0 && CurrentTick - m_TxOutstandingSinceTick >= Ax88178TransmitTimeout && m_RecoveryLevel == Ax88178RecoveryIdle && !m_FullReinitializePending && !m_FullReinitializeActive) {
        DWORD TimeoutCount = ++m_TxTimeoutCount;
        m_RecoveryLevel = TimeoutCount >= 5 ? Ax88178RecoveryResetDevice : TimeoutCount >= 3 ? Ax88178RecoveryReinitialize : Ax88178RecoveryResetPipe;
        m_RecoveryPipe = UsbEnetTransportTransmit;
        m_RecoveryRequestTick = CurrentTick;
        m_RecoveryReason = 0x200 + TimeoutCount;
        m_TxOutstandingSinceTick = CurrentTick;
        DbgPrint("[usbenet-recovery]: AX88178 TX timeout with %u pending transfers; count=%u level=%u.\n", Device->PendingXmitCount, TimeoutCount, static_cast<DWORD>(m_RecoveryLevel));
    }

    ProcessRecovery(Device, CurrentTick);
    BOOL ResetDevice = m_DeviceResetPending;
    m_DeviceResetPending = FALSE;
    if (ResetDevice) {
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        Device->ResetUsbDevice();
        return;
    }
    if (Device->InitStage != UsbEnetInitComplete) {
        NULL_OWNER_THREAD(Device);
        KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
        return;
    }

    DWORD TimerPeriod = Ax88178InterruptIdlePeriod;
    Device->PrintThroughputStats(CurrentTick);

    if ((Device->LinkState & NIC_LINK_STATE_ACTIVE) != 0 && (Device->Flags & ReceiveRunning) != 0 && (Device->Flags & USBENET_STATE_00080000) != 0 && CurrentTick - Device->TimerTick > 5000) {
        RestartReceiving(Device);
        Device->TimerTick = CurrentTick;
        TimerPeriod = Ax88178InterruptRetryPeriod;
    } else if (m_InterruptStatusSeen) {
        BOOL PhyBusy = (Device->Flags & (USBENET_STATE_CAN_USER_TRANSFER | USBENET_STATE_PHY_READ_IN_PROGRESS | USBENET_STATE_PHY_WRITE_IN_PROGRESS)) != 0;
        if (m_InterruptPhyRefreshPending) {
            TimerPeriod = Ax88178InterruptRetryPeriod;
            if (!PhyBusy)
                BeginReadAllPhy(Device);
        } else if (CurrentTick - m_LastFallbackPhyPollTick >= Ax88178FallbackPhyPollPeriod) {
            if (!PhyBusy && NT_SUCCESS(BeginReadPhy(Device, MII_BMSR)))
                m_LastFallbackPhyPollTick = CurrentTick;
        } else if (m_InterruptLinkUp && (Device->LinkState & NIC_LINK_STATE_ACTIVE) == 0) {
            m_InterruptPhyRefreshPending = TRUE;
            m_InterruptLinkAuthoritative = TRUE;
            Device->Flags |= USBENET_STATE_LINK_STATE_UPDATE_PENDING;
            TimerPeriod = Ax88178InterruptRetryPeriod;
        }
    } else {
        NTSTATUS Status = (Device->LinkState & NIC_LINK_STATE_NEGOTIATION_COMPLETE) != 0 ? BeginReadPhy(Device, MII_BMSR) : BeginReadAllPhy(Device);
        TimerPeriod = NT_SUCCESS(Status) && (Device->LinkState & NIC_LINK_STATE_NEGOTIATION_COMPLETE) != 0 ? Ax88178InterruptIdlePeriod : Ax88178InterruptRetryPeriod;
        if (CurrentTick - Device->LinkPollTick >= 3500)
            Device->Flags |= USBENET_STATE_LINK_STATE_UPDATE_PENDING;
    }

    if (m_FilterUpdatePending && m_FilterStep == Ax88178FilterIdle && (Device->Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0) {
        m_FilterUpdatePending = FALSE;
        BeginFilterUpdate(Device, FALSE);
    }

    CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, TimerPeriod);
    NULL_OWNER_THREAD(Device);
    KeReleaseSpinLockFromRaisedIrql(&Device->NicLock);
}

VOID CAx88178::AdvanceInitStage(CUsbEnet* Device) {
    USBENET_LOG("Ax88178AdvanceInitStage Device=%p currentStage=%d", Device, Device->InitStage);
    TRAP_THREAD(Device->LockOwnerThread);

    USBENET_INIT_STAGE NextStage = static_cast<USBENET_INIT_STAGE>(Device->InitStage + 1);
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
        Device->InitStage = UsbEnetInitComplete;
        return;
    }

    Device->InitStage = NextStage;
    BOOL Gpio0 = m_EepromWord17 == 0xFFFF || (m_EepromWord17 & 0x0080) == 0;
    BYTE Gpio0Enable = Gpio0 ? 0x01 : 0x00;

    switch (NextStage) {
        case UsbEnetInitReadEepromConfig:
            BeginEepromConfig(Device, TRUE);
            break;

        case UsbEnetInitWriteGpio1:
            WriteGpio(Device, static_cast<BYTE>(0x8C | Gpio0Enable));
            break;

        case UsbEnetInitPause1:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 40);
            break;

        case UsbEnetInitWriteGpio2:
            CNicBase::NicBaseShutdown(Device);
            WriteGpio(Device, static_cast<BYTE>((m_LedMode == 1 ? 0x04 : 0x3C) | Gpio0Enable));
            break;

        case UsbEnetInitPause2:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 30);
            break;

        case UsbEnetInitWriteGpio3:
            CNicBase::NicBaseShutdown(Device);
            WriteGpio(Device, static_cast<BYTE>((m_LedMode == 1 ? 0x0C : 0x1C) | Gpio0Enable));
            break;

        case UsbEnetInitPause3:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, m_LedMode == 1 ? 30 : 300);
            break;

        case UsbEnetInitWriteGpio4:
            CNicBase::NicBaseShutdown(Device);
            WriteGpio(Device, static_cast<BYTE>((m_LedMode == 1 ? 0x0C : 0x3C) | Gpio0Enable));
            break;

        case UsbEnetInitPause4:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 30);
            break;

        case UsbEnetInitSoftwareResetClear:
            CNicBase::NicBaseShutdown(Device);
            SoftwareReset(Device, 0x00);
            break;

        case UsbEnetInitPauseResetClear:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 150);
            break;

        case UsbEnetInitSoftwareReset:
            CNicBase::NicBaseShutdown(Device);
            SoftwareReset(Device, 0x48);
            break;

        case UsbEnetInitPause5:
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerAdvanceInitStageDpc, Device, 150);
            break;

        case UsbEnetInitReadNodeId:
            CNicBase::NicBaseShutdown(Device);
            ReadNodeId(Device);
            break;

        case UsbEnetInitWaitingForEthernetAddress: {
            CEnetAddr* TitleAddress = Main::GetKernelTitleEthernetAddress();
            CEnetAddr* DebugAddress = Main::GetKernelDebugEthernetAddress();
            BOOL TitleAddressValid = TitleAddress != NULL && !TitleAddress->IsZero() && !TitleAddress->IsMulticast();
            BOOL DebugAddressValid = DebugAddress != NULL && !DebugAddress->IsZero() && !DebugAddress->IsMulticast();

            if (TitleAddressValid)
                memcpy(&Device->UnicastAddress, TitleAddress, sizeof(CEnetAddr));

            if (Main::Devkit) {
                if (DebugAddressValid)
                    memcpy(&Device->AlternateUnicastAddress, DebugAddress, sizeof(CEnetAddr));
                else if (TitleAddressValid) {
                    memcpy(&Device->AlternateUnicastAddress, TitleAddress, sizeof(CEnetAddr));
                    IncrementEthernetAddress(&Device->AlternateUnicastAddress);
                }
            } else {
                // Retail/free has only the title XNet instance. Do not synthesize a
                // second address because no debug user exists to receive it.
                Device->AlternateUnicastAddress.SetZero();
            }

            if (!Device->UnicastAddress.IsZero())
                AdvanceInitStage(Device);
            break;
        }

        case UsbEnetInitWriteNodeId:
            WriteNodeId(Device);
            break;

        case UsbEnetInitReadPhyAddressRegister:
            ReadPhyAddressRegister(Device);
            break;

        case UsbEnetInitWaitPhyReady:
            m_PhyReadyRetryCount = 0;
            m_PhyReadyRetryPending = FALSE;
            if (!NT_SUCCESS(BeginReadPhy(Device, PhyRegisterIdentifier1))) {
                m_PhyReadyRetryPending = TRUE;
                CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178PhyReadyRetryPeriod);
            }
            break;

        case UsbEnetInitReadPhyRegisters:
            if (!NT_SUCCESS(BeginReadAllPhy(Device)))
                AdvanceInitStage(Device);
            break;

        case UsbEnetInitWriteMediumStatusRegister:
            WriteMediumStatus(Device);
            break;

        case UsbEnetInitWriteMiiControlRegister:
            WriteMiiControl(Device);
            break;

        case UsbEnetInitWritePhyAnar: {
            USHORT Value = static_cast<USHORT>((Device->PhyRegisters[PhyRegisterAnar] & 0xF01F) | 0x05E0);
            if (!NT_SUCCESS(BeginWritePhy(Device, PhyRegisterAnar, Value)))
                AdvanceInitStage(Device);
            break;
        }

        case UsbEnetInitWritePhyExpansion:
            if (!NT_SUCCESS(BeginWritePhy(Device, PhyRegisterExpansion, 4)))
                AdvanceInitStage(Device);
            break;

        case UsbEnetInitWritePhy1000TControl: {
            USHORT Value = static_cast<USHORT>((Device->PhyRegisters[PhyRegister1000TControl] & ~0x0300) | MII_CTRL1000_FULL);
            if (!NT_SUCCESS(BeginWritePhy(Device, PhyRegister1000TControl, Value)))
                AdvanceInitStage(Device);
            break;
        }

        case UsbEnetInitWriteMarvellExtPhySpecificCtrl:
            if (m_EepromWord17Valid)
                ContinueVendorPhyInitialization(Device);
            else
                BeginEepromConfig(Device, FALSE);
            break;

        case UsbEnetInitWritePhyControl:
            if (!NT_SUCCESS(BeginWritePhy(Device, PhyRegisterControl, MII_BMCR_RESET | MII_BMCR_AUTONEG_ENABLE)))
                AdvanceInitStage(Device);
            break;

        case UsbEnetInitWriteIpg:
            WriteIpg(Device);
            break;

        case UsbEnetInitStartReceiving:
            StartReceiving(Device);
            break;

        case UsbEnetInitReady:
            Device->InitStage = UsbEnetInitComplete;
            Device->StartInterruptLinkStatus();
            if (m_FullReinitializeActive) {
                m_FullReinitializeActive = FALSE;
                DbgPrint("[usbenet-recovery]: AX88178 full hardware reinitialization completed reason=%u count=%u.\n", m_RecoveryReason, m_RecoveryCount);
            }
            CNicBase::NicBaseTimerStart(Device, (PKDEFERRED_ROUTINE)CUsbEnet::NicTimerRunningDpc, Device, Ax88178InterruptRetryPeriod);
            DbgPrint("[usbenet]: AX88178 interrupt-driven link monitoring started; MII polling retained as a %u ms fallback.\n", Ax88178FallbackPhyPollPeriod);
            break;

        default:
            TRAP_ASSERT(FALSE);
            break;
    }
}

VOID CAx88178::CompleteWriteIpg(CUsbEnet* Device, NTSTATUS Status) {
	USBENET_LOG("CompleteWriteIpg Device=%p status=0x%08X", Device, Status);
	UNREFERENCED_PARAMETER(Status);

	NicBaseTakeLock(Device);
	Device->CompleteControlTransfer();

	AdvanceInitStage(Device);

	TRAP_ASSERT(Device->PreviousIrql != 0xEE);
	TRAP_THREAD(Device->LockOwnerThread);

	NULL_OWNER_THREAD(Device);

	KfReleaseSpinLock(&Device->NicLock, Device->PreviousIrql);
}

VOID CAx88178::ResetState(CUsbEnet* Device) {
    UNREFERENCED_PARAMETER(Device);
    m_RealtekPhyInitStep = RealtekPhyInitIdle;
    m_RealtekPhyInitOperationFailed = FALSE;
    m_PhyConfigStep = Ax88178PhyConfigIdle;
    m_PhyConfigInitialRead = FALSE;
    m_EepromWord17 = 0;
    m_EepromWord17Valid = FALSE;
    m_PhyMode = Ax88178PhyModeMarvell;
    m_LedMode = 0;
    m_PhyReadyRetryCount = 0;
    m_PhyReadyRetryPending = FALSE;
    m_PhyPatch = NULL;
    m_PhyPatchCount = 0;
    m_PhyPatchIndex = 0;
    m_PhyPatchName = NULL;
    m_PhyPatchOperationFailed = FALSE;
    m_FilterStep = Ax88178FilterIdle;
    memset(m_MulticastFilter, 0, sizeof(m_MulticastFilter));
    memset(m_PendingMulticastFilter, 0, sizeof(m_PendingMulticastFilter));
    m_PendingReceiveControl = 0;
    m_FilterSubmitReceiveRing = FALSE;
    m_FilterUpdatePending = FALSE;
    m_InterruptStatusSeen = FALSE;
    m_InterruptLinkUp = FALSE;
    m_InterruptPhyRefreshPending = FALSE;
    m_InterruptLinkAuthoritative = FALSE;
    m_InterruptEventCount = 0;
    m_LastFallbackPhyPollTick = KeTimeStampBundle->TickCount;
    m_LastMarvellLedState = 0xFFFFFFFF;
    m_MarvellLedStep = Ax88178MarvellLedIdle;
    m_RecoveryLevel = Ax88178RecoveryIdle;
    m_RecoveryPipe = UsbEnetTransportReceive;
    m_RecoveryRequestTick = 0;
    memset(m_RecoveryLastErrorTick, 0, sizeof(m_RecoveryLastErrorTick));
    memset(m_RecoveryErrorCount, 0, sizeof(m_RecoveryErrorCount));
    m_RecoveryReason = 0;
    m_TxOutstandingSinceTick = 0;
    m_TxTimeoutCount = 0;
    m_FullReinitializePending = FALSE;
    m_FullReinitializeActive = FALSE;
    m_ReinitializeDrainStarted = FALSE;
    m_DeviceResetPending = FALSE;
    m_RecoveryCount = 0;
}

VOID __fastcall CAx88178::AsyncCompletionRoutineReadNodeId(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteReadNodeId(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineWriteNodeId(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteWriteNodeId(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineReadPhyAddressRegister(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteReadPhyAddressRegister(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineWriteMediumStatus(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteWriteMediumStatus(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineWriteGpio(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteWriteGpio(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineSoftwareReset(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteSoftwareReset(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineWriteMiiControl(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteWriteMiiControl(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineWriteIpg(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteWriteIpg(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineReadPhy(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteReadPhy(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineWritePhy(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteWritePhy(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineAcquireSerialMgmtControl(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteAcquireSerialMgmtControl(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineReleaseSerialMgmtControl(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteReleaseSerialMgmtControl(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutinePhyConfig(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompletePhyConfigStep(Device, Status);
}

VOID __fastcall CAx88178::AsyncCompletionRoutineFilterUpdate(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
    CUsbEnet* Device = static_cast<CUsbEnet*>(Request->Context);
    TRAP_ASSERT(Device != NULL);
    g_Ax88178Chipset.CompleteFilterUpdate(Device, Status);
}

VOID CAx88178::StartReceiving(CUsbEnet* Device) {
    USBENET_LOG("StartReceiving Device=%p", Device);
    if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
        return;
    BeginFilterUpdate(Device, TRUE);
}

#include "stdafx.h"
#include "UsbEnetChipset.h"

#if USBENET_DEBUG
#define USBENET_LOG(_fmt, ...) DbgPrint("[CUsbEnet] " _fmt "\n", __VA_ARGS__)
#define USBENET_LOG0(_msg) DbgPrint("[CUsbEnet] " _msg "\n")
#else
#define USBENET_LOG(_fmt, ...) ((void)0)
#define USBENET_LOG0(_msg) ((void)0)
#endif

#if USBENET_CONNECTION_LOGGING
#define USBENET_CONN(_fmt, ...) USBENET_CONNECTION_PRINT("[usbenet-conn] " _fmt "\n", __VA_ARGS__)
#else
#define USBENET_CONN(_fmt, ...) ((void)0)
#endif

#if DEVKIT_ONLY
#define NULL_OWNER_THREAD(_this) {  _this->LockOwnerThread = NULL; }
#define SET_NIC_STATS_PRINT_PENDING(_this) {  _this->StatsPrintPending = 1; }
#else
#define NULL_OWNER_THREAD(val) { __noop; }
#define SET_NIC_STATS_PRINT_PENDING(val) { __noop; }
#endif

#if _TRAP && DEVKIT_ONLY
#define TRAP_ASSERT(Expression) if (!(Expression)) {  __asm { twi 31, r0, 0x19 } }
#define TRAP_IRQL(val) { TRAP_ASSERT(KeGetCurrentIrql() == val); }
#define TRAP_THREAD(val) { TRAP_ASSERT(KeGetCurrentThread() == val); }
#else
#define TRAP_ASSERT(Expression) { __noop; }
#define TRAP_IRQL(val) { __noop; }
#define TRAP_THREAD(val) { __noop; }
#endif

CUsbEnet g_UsbEnet;

static PVOID g_UsbEnetInterruptEndpoint;
static USBD_TRANSFER_REQUEST g_UsbEnetInterruptTransfer;
static volatile LONG g_UsbEnetInterruptInFlight;
static volatile LONG g_UsbEnetInterruptPaused;
static DWORD g_UsbEnetInterruptSequence;
static DWORD g_UsbEnetInterruptErrorCount;
static NTSTATUS g_UsbEnetInterruptLastCompletionStatus;
static BOOL g_UsbEnetInterruptLastCompletionStatusValid;
static NTSTATUS g_UsbEnetBulkRecvLastCompletionStatus;
static BOOL g_UsbEnetBulkRecvLastCompletionStatusValid;

static const DWORD UsbEnetUserAttachLogSlotCount = 4;

typedef struct _USBENET_USER_ATTACH_LOG_STATE {
	CNicUser* User;
	BOOL Attached;
	BOOL Valid;
} USBENET_USER_ATTACH_LOG_STATE;

static USBENET_USER_ATTACH_LOG_STATE g_UsbEnetUserAttachLogState[UsbEnetUserAttachLogSlotCount];

static DWORD ThroughputStatsTick;
static DWORD TxUsbTransferCount;
static DWORD TxUsbBytes;
static DWORD TxFrameCount;
static DWORD TxFrameBytes;
static DWORD TxTitleFrameCount;
static DWORD TxDebugFrameCount;
static DWORD TxPacketInUseCount;
static DWORD TxPacketHighWater;
static DWORD TxPacketDropCount;
static DWORD TxTrackerHighWater;
static DWORD TxBufferDropCount;
static DWORD RxUsbCompletionCount;
static DWORD RxUsbBytes;
static DWORD RxInFlightCount;
static DWORD RxRestartCount;
static DWORD RxCancelledCompletionCount;
static DWORD RxFrameCount;
static DWORD RxFrameBytes;
static DWORD RxParsedFrameCount;
static DWORD RxInvalidFrameCount;
static DWORD RxMulticastFilteredCount;
static DWORD RxTitleUnicastFrameCount;
static DWORD RxDebugUnicastFrameCount;
static DWORD RxMulticastFrameCount;
static DWORD RxPromiscuousFrameCount;
static DWORD RxBroadcastFrameCount;
static DWORD RxTitleDeliveredCount;
static DWORD RxDebugDeliveredCount;
static DWORD RxNoDeliveryFrameCount;
static DWORD DiagnosticStatsPeriodsRemaining;
static DWORD DiagnosticControlQueueCount;
static DWORD DiagnosticTxSubmitCount;
static DWORD DiagnosticTxSubmitSequence;
static DWORD DiagnosticTxCompleteCount;
static DWORD DiagnosticTxCompleteSequence;
static DWORD DiagnosticTxErrorCount;
static NTSTATUS DiagnosticTxLastStatus;
#if USBENET_PERF_LOGGING
static DWORD TxDispatchEntryCount;
static DWORD TxOtherIrqlEntryCount;
static DWORD TxDirectSubmitCount;
static DWORD TxDirectPipeIdleSubmitCount;
static DWORD TxDispatchWaitCount;
static DWORD TxDispatchWrongProcessorCount;
static DWORD TxDpcQueueCount;
static DWORD TxDpcRunCount;
static DWORD TxCompletionRefillCount;
static DWORD TxCompletionRefillTransferCount;
static DWORD TxPendingHighWater;
#endif
static DWORD DiagnosticRxSubmitCount;
static DWORD DiagnosticRxSubmitSequence;
static DWORD DiagnosticRxSubmitErrorCount;
static DWORD DiagnosticRxCompleteCount;
static DWORD DiagnosticRxCompleteSequence;
static DWORD DiagnosticRxErrorCount;
static DWORD DiagnosticRxZeroLengthCount;
static DWORD DiagnosticRxParserFailureCount;


static BOOL ShouldLogDrop(DWORD Count) {
	return Count == 1 || (Count & (Count - 1)) == 0;
}

static BOOL ShouldLogDiagnosticEvent(DWORD Count) {
	return Count <= 16 || ShouldLogDrop(Count);
}

static USHORT ReadNetworkU16(const BYTE* Buffer) {
	return static_cast<USHORT>((static_cast<USHORT>(Buffer[0]) << 8) | Buffer[1]);
}

static BOOL IsDebugNicUser(CNicUser* User) {
	return User != NULL && User->AttachInfo.ReceiveFilterMask == DebugReceiveFilterMask;
}

static BOOL IsRetailTitleNicUser(CNicUser* User) {
	return !Main::Devkit && User != NULL && User->AttachInfo.ReceiveFilterMask == TitleReceiveFilterMask;
}

static BOOL IsLowLatencyNicUser(CNicUser* User) {
	return IsDebugNicUser(User) || IsRetailTitleNicUser(User);
}

static BOOL IsDebugXmitTracker(PXMIT_TRACKER Tracker) {
	return Tracker != NULL && Tracker->FirstPacket != NULL && IsDebugNicUser(Tracker->FirstPacket->User);
}

static BOOL IsImmediateLowLatencyXmitTracker(PXMIT_TRACKER Tracker) {
	return Tracker != NULL && Tracker->FirstPacket != NULL && IsLowLatencyNicUser(Tracker->FirstPacket->User) && g_UsbEnetChipset != NULL && Tracker->FirstPacket->Length <= DebugImmediateFrameSize + g_UsbEnetChipset->GetTransmitHeaderSize();
}

static DWORD CalculateMbpsX100(DWORD Bytes, DWORD ElapsedMilliseconds) {
	if (ElapsedMilliseconds == 0)
		return 0;

	ULONGLONG ScaledBits = static_cast<ULONGLONG>(Bytes) * 800;
	ULONGLONG Divisor = static_cast<ULONGLONG>(ElapsedMilliseconds) * 1000;
	return static_cast<DWORD>((ScaledBits + Divisor / 2) / Divisor);
}


static VOID PrintThroughputStats(DWORD CurrentTick, PXMIT_TRACKER Trackers, DWORD ActiveXmitCount, DWORD PendingXmitCount) {
	if (ThroughputStatsTick == 0) {
		ThroughputStatsTick = CurrentTick;
		return;
	}

	DWORD ElapsedMilliseconds = CurrentTick - ThroughputStatsTick;

	if (ElapsedMilliseconds < UsbEnetPerfPeriodMilliseconds())
		return;

	DWORD TxFrameMbpsX100 = CalculateMbpsX100(TxFrameBytes, ElapsedMilliseconds);
	DWORD TxUsbMbpsX100 = CalculateMbpsX100(TxUsbBytes, ElapsedMilliseconds);
	DWORD RxFrameMbpsX100 = CalculateMbpsX100(RxFrameBytes, ElapsedMilliseconds);
	DWORD RxUsbMbpsX100 = CalculateMbpsX100(RxUsbBytes, ElapsedMilliseconds);
	DWORD TxFramesPerTransferX100 = TxUsbTransferCount != 0 ? TxFrameCount * 100 / TxUsbTransferCount : 0;
	DWORD TxBytesPerTransfer = TxUsbTransferCount != 0 ? TxUsbBytes / TxUsbTransferCount : 0;
	DWORD RxFramesPerCompletionX100 = RxUsbCompletionCount != 0 ? RxFrameCount * 100 / RxUsbCompletionCount : 0;
	DWORD RxBytesPerCompletion = RxUsbCompletionCount != 0 ? RxUsbBytes / RxUsbCompletionCount : 0;
	DWORD WaitingTrackerCount = 0;
	DWORD QueuedTrackerCount = 0;
	DWORD BusyTrackerCount = 0;

	for (DWORD Index = 0; Index < XMIT_BUFFER_COUNT; Index++) {
		if ((Trackers[Index].Flags & XMIT_FLAG_WAITING) != 0)
			++WaitingTrackerCount;

		if ((Trackers[Index].Flags & XMIT_FLAG_QUEUED) != 0)
			++QueuedTrackerCount;

		if ((Trackers[Index].Flags & XMIT_FLAG_BUSY) != 0)
			++BusyTrackerCount;
	}


	BOOL ForceDiagnostics = DiagnosticStatsPeriodsRemaining != 0;
	if (ForceDiagnostics)
		--DiagnosticStatsPeriodsRemaining;

	BOOL HasDataActivity = TxUsbTransferCount != 0 || RxUsbCompletionCount != 0 || TxFrameCount != 0 || RxFrameCount != 0;
	BOOL HasErrors = DiagnosticTxErrorCount != 0 || DiagnosticRxSubmitErrorCount != 0 || DiagnosticRxErrorCount != 0 || DiagnosticRxParserFailureCount != 0;
	BOOL PrintStats = HasErrors || TxFrameMbpsX100 > 1000 || RxFrameMbpsX100 > 1000 || (ForceDiagnostics && HasDataActivity);

	if (PrintStats) {
		USBENET_PERF_PRINT("[usbenet]: Throughput stats: TX transfers=%u frames=%u frameBytes=%u usbBytes=%u avg=%u.%02u frames/%u bytes title=%u debug=%u packets=%u high=%u trackers=%u high=%u packetDrops=%u bufferDrops=%u waiting=%u queued=%u busy=%u pending=%u/%u; RX inFlight=%u restarts=%u cancelled=%u completions=%u frames=%u frameBytes=%u usbBytes=%u avg=%u.%02u frames/%u bytes\n", TxUsbTransferCount, TxFrameCount, TxFrameBytes, TxUsbBytes, TxFramesPerTransferX100 / 100, TxFramesPerTransferX100 % 100, TxBytesPerTransfer, TxTitleFrameCount, TxDebugFrameCount, TxPacketInUseCount, TxPacketHighWater, ActiveXmitCount, TxTrackerHighWater, TxPacketDropCount, TxBufferDropCount, WaitingTrackerCount, QueuedTrackerCount, BusyTrackerCount, PendingXmitCount, MaximumPendingXmitCount, RxInFlightCount, RxRestartCount, RxCancelledCompletionCount, RxUsbCompletionCount, RxFrameCount, RxFrameBytes, RxUsbBytes, RxFramesPerCompletionX100 / 100, RxFramesPerCompletionX100 % 100, RxBytesPerCompletion);
		USBENET_PERF_PRINT("[usbenet]: Throughput rate: elapsed=%u ms TX frame=%u.%02u Mbps usb=%u.%02u Mbps; RX frame=%u.%02u Mbps usb=%u.%02u Mbps\n", ElapsedMilliseconds, TxFrameMbpsX100 / 100, TxFrameMbpsX100 % 100, TxUsbMbpsX100 / 100, TxUsbMbpsX100 % 100, RxFrameMbpsX100 / 100, RxFrameMbpsX100 % 100, RxUsbMbpsX100 / 100, RxUsbMbpsX100 % 100);
		USBENET_PERF_PRINT("[usbenet]: RX routing: parsed=%u invalid=%u mcastFiltered=%u title=%u debug=%u multicast=%u promisc=%u broadcast=%u deliveredTitle=%u deliveredDebug=%u deliveredNone=%u\n", RxParsedFrameCount, RxInvalidFrameCount, RxMulticastFilteredCount, RxTitleUnicastFrameCount, RxDebugUnicastFrameCount, RxMulticastFrameCount, RxPromiscuousFrameCount, RxBroadcastFrameCount, RxTitleDeliveredCount, RxDebugDeliveredCount, RxNoDeliveryFrameCount);
		USBENET_PERF_PRINT("[usbenet]: Transport diagnostics: controlQueued=%u TX submit=%u complete=%u errors=%u lastStatus=0x%08X; RX submit=%u submitErrors=%u complete=%u errors=%u zero=%u parserFailures=%u inFlight=%u flags=0x%08X\n", DiagnosticControlQueueCount, DiagnosticTxSubmitCount, DiagnosticTxCompleteCount, DiagnosticTxErrorCount, DiagnosticTxLastStatus, DiagnosticRxSubmitCount, DiagnosticRxSubmitErrorCount, DiagnosticRxCompleteCount, DiagnosticRxErrorCount, DiagnosticRxZeroLengthCount, DiagnosticRxParserFailureCount, RxInFlightCount, g_UsbEnet.Flags);
#if USBENET_PERF_LOGGING
		USBENET_PERF_PRINT("[usbenet]: TX scheduling: entryDispatch=%u entryOther=%u direct=%u directPipeIdle=%u dispatchWait=%u deferredCpu=%u dpcQueued=%u dpcRuns=%u completionRefills=%u refillTransfers=%u pendingHigh=%u/%u directEnabled=%u\n", TxDispatchEntryCount, TxOtherIrqlEntryCount, TxDirectSubmitCount, TxDirectPipeIdleSubmitCount, TxDispatchWaitCount, TxDispatchWrongProcessorCount, TxDpcQueueCount, TxDpcRunCount, TxCompletionRefillCount, TxCompletionRefillTransferCount, TxPendingHighWater, MaximumPendingXmitCount, UsbEnetDirectXmitEnabled());
#endif
	}

	ThroughputStatsTick = CurrentTick;
	TxUsbTransferCount = 0;
	TxUsbBytes = 0;
	TxFrameCount = 0;
	TxFrameBytes = 0;
	TxTitleFrameCount = 0;
	TxDebugFrameCount = 0;
	TxPacketHighWater = TxPacketInUseCount;
	TxPacketDropCount = 0;
	TxTrackerHighWater = ActiveXmitCount;
	TxBufferDropCount = 0;
	RxRestartCount = 0;
	RxCancelledCompletionCount = 0;
	RxUsbCompletionCount = 0;
	RxUsbBytes = 0;
	RxFrameCount = 0;
	RxFrameBytes = 0;
	RxParsedFrameCount = 0;
	RxInvalidFrameCount = 0;
	RxMulticastFilteredCount = 0;
	RxTitleUnicastFrameCount = 0;
	RxDebugUnicastFrameCount = 0;
	RxMulticastFrameCount = 0;
	RxPromiscuousFrameCount = 0;
	RxBroadcastFrameCount = 0;
	RxTitleDeliveredCount = 0;
	RxDebugDeliveredCount = 0;
	RxNoDeliveryFrameCount = 0;
	DiagnosticControlQueueCount = 0;
	DiagnosticTxSubmitCount = 0;
	DiagnosticTxCompleteCount = 0;
	DiagnosticTxErrorCount = 0;
	DiagnosticTxLastStatus = STATUS_SUCCESS;
#if USBENET_PERF_LOGGING
	TxDispatchEntryCount = 0;
	TxOtherIrqlEntryCount = 0;
	TxDirectSubmitCount = 0;
	TxDirectPipeIdleSubmitCount = 0;
	TxDispatchWaitCount = 0;
	TxDispatchWrongProcessorCount = 0;
	TxDpcQueueCount = 0;
	TxDpcRunCount = 0;
	TxCompletionRefillCount = 0;
	TxCompletionRefillTransferCount = 0;
	TxPendingHighWater = PendingXmitCount;
#endif
	DiagnosticRxSubmitCount = 0;
	DiagnosticRxSubmitErrorCount = 0;
	DiagnosticRxCompleteCount = 0;
	DiagnosticRxErrorCount = 0;
	DiagnosticRxZeroLengthCount = 0;
	DiagnosticRxParserFailureCount = 0;
}

static DWORD CountXmitTrackerFrames(PXMIT_TRACKER Tracker, PXMIT_PACKET PacketBase) {
	DWORD Remaining = Tracker->Transfer.BufferLength;
	DWORD FrameCount = 0;
	PXMIT_PACKET Packet = Tracker->FirstPacket;

	if ((Tracker->Flags & XMIT_FLAG_CRC_PADDING) != 0) {
		TRAP_ASSERT(g_UsbEnetChipset != NULL);
		Remaining -= g_UsbEnetChipset->GetTransmitTerminatorSize();
	}

	while (Remaining != 0 && FrameCount < XMIT_PACKET_COUNT) {
		TRAP_ASSERT(Packet != NULL);
		TRAP_ASSERT(Packet->User != NULL);
		TRAP_ASSERT(Packet->Tracker == Tracker);
		TRAP_ASSERT(Packet->Length != 0);
		TRAP_ASSERT(Packet->Length <= Remaining);

		Remaining -= Packet->Length;
		++FrameCount;
		++Packet;

		if (Packet >= &PacketBase[XMIT_PACKET_COUNT])
			Packet = &PacketBase[0];
	}

	TRAP_ASSERT(Remaining == 0);
	return FrameCount;
}

static VOID ReleaseSubmittedXmitPackets(PXMIT_TRACKER Tracker, PXMIT_PACKET PacketBase) {
	DWORD FrameCount = CountXmitTrackerFrames(Tracker, PacketBase);
	PXMIT_PACKET Packet = Tracker->FirstPacket;

	for (DWORD Index = 0; Index < FrameCount; Index++) {
		TRAP_ASSERT(Packet != NULL);
		TRAP_ASSERT(Packet->User != NULL);
		TRAP_ASSERT(Packet->Tracker == Tracker);
		TRAP_ASSERT(TxPacketInUseCount != 0);

		--TxPacketInUseCount;
		Packet->User = NULL;
		Packet->CompletionContext = NULL;
		Packet->Tracker = NULL;
		Packet->Length = 0;
		++Packet;

		if (Packet >= &PacketBase[XMIT_PACKET_COUNT])
			Packet = &PacketBase[0];
	}
}

static VOID QueueXmitTrackerDpc(PXMIT_TRACKER Tracker) {
	TRAP_ASSERT(Tracker != NULL);
	TRAP_ASSERT(Tracker->FirstPacket != NULL);
	TRAP_ASSERT((Tracker->Flags & XMIT_FLAG_BUSY) == 0);

	if ((Tracker->Flags & XMIT_FLAG_QUEUED) != 0)
		return;

	Tracker->Flags &= ~XMIT_FLAG_WAITING;
	Tracker->Flags |= XMIT_FLAG_QUEUED;
#if USBENET_PERF_LOGGING
	++TxDpcQueueCount;
#endif

	/* FALSE means the DPC was already queued; keep the software flag set. */
	KeInsertQueueDpc(&Tracker->CompletionDpc, NULL, NULL);
}

static BOOL CanSubmitXmitTracker(CUsbEnet* Device, PXMIT_TRACKER Tracker) {
	DWORD PendingLimit = IsDebugXmitTracker(Tracker) ? MaximumPendingXmitCount : MaximumTitlePendingXmitCount;
	return Device->PendingXmitCount < PendingLimit;
}

static VOID SubmitXmitTrackerLocked(CUsbEnet* Device, PXMIT_TRACKER Tracker) {
	TRAP_ASSERT(Device != NULL);
	TRAP_ASSERT(Tracker != NULL);
	TRAP_ASSERT(Tracker->FirstPacket != NULL);
	TRAP_ASSERT((Tracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_BUSY | XMIT_FLAG_QUEUED)) == 0);
	TRAP_ASSERT(CanSubmitXmitTracker(Device, Tracker));

	/* The frame data has already been copied into the tracker's DMA buffer and
	 * NicXmit reports completion immediately, so packet descriptors are no
	 * longer needed once the USB transfer is submitted. */
	ReleaseSubmittedXmitPackets(Tracker, Device->XmitPackets);
	Tracker->Flags |= XMIT_FLAG_BUSY;
	++Device->PendingXmitCount;
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->HandleTransportSubmission(Device, UsbEnetTransportTransmit);

#if USBENET_PERF_LOGGING
	if (Device->PendingXmitCount > TxPendingHighWater)
		TxPendingHighWater = Device->PendingXmitCount;
#endif

	++TxUsbTransferCount;
	TxUsbBytes += Tracker->Transfer.BufferLength;
	++DiagnosticTxSubmitCount;
	++DiagnosticTxSubmitSequence;

	PBYTE UsbBuffer = static_cast<PBYTE>(Tracker->Transfer.Buffer);
	DWORD HeaderSize = g_UsbEnetChipset != NULL ? g_UsbEnetChipset->GetTransmitHeaderSize() : 0;
	PBYTE Frame = Tracker->Transfer.BufferLength >= HeaderSize + 14 ? UsbBuffer + HeaderSize : NULL;
	USHORT EtherType = Frame != NULL ? ReadNetworkU16(Frame + 12) : 0;

	if (ShouldLogDiagnosticEvent(DiagnosticTxSubmitSequence) || EtherType == 0x0806) {
		DbgPrint("[usbenet]: TX submit #%u tracker=%u usbLength=%u pending=%u flags=0x%08X header=%02X %02X %02X %02X %02X %02X %02X %02X etherType=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X\n", DiagnosticTxSubmitSequence, static_cast<DWORD>(Tracker - Device->XmitTrackers), Tracker->Transfer.BufferLength, Device->PendingXmitCount, Tracker->Flags, UsbBuffer[0], UsbBuffer[1], UsbBuffer[2], UsbBuffer[3], UsbBuffer[4], UsbBuffer[5], UsbBuffer[6], UsbBuffer[7], EtherType, Frame != NULL ? Frame[0] : 0, Frame != NULL ? Frame[1] : 0, Frame != NULL ? Frame[2] : 0, Frame != NULL ? Frame[3] : 0, Frame != NULL ? Frame[4] : 0, Frame != NULL ? Frame[5] : 0, Frame != NULL ? Frame[6] : 0, Frame != NULL ? Frame[7] : 0, Frame != NULL ? Frame[8] : 0, Frame != NULL ? Frame[9] : 0, Frame != NULL ? Frame[10] : 0, Frame != NULL ? Frame[11] : 0);
	}


	/* Publish all descriptor and frame-buffer stores before the USB controller
	 * is allowed to consume this physical buffer.  Verbose DbgPrint calls were
	 * accidentally providing enough ordering/delay to hide this race on the
	 * direct DISPATCH_LEVEL path. */
	__sync();

	/* UsbdQueueAsyncTransfer completes asynchronously. Its recovered export
	 * prototype previously declared an NTSTATUS return, but the kernel leaves
	 * an internal/opaque value in r3. Only the completion callback status is
	 * meaningful. */
	UsbdQueueAsyncTransfer(Device->DeviceNode, &Tracker->Transfer);
}

static VOID QueueOrSubmitXmitTrackerLocked(CUsbEnet* Device, PXMIT_TRACKER Tracker, KIRQL EntryIrql) {
	TRAP_ASSERT(Device != NULL);
	TRAP_ASSERT(Tracker != NULL);
	TRAP_ASSERT(Tracker->FirstPacket != NULL);
	TRAP_ASSERT((Tracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_BUSY | XMIT_FLAG_QUEUED)) == 0);

	/* The checked kernel requires UsbdQueueAsyncTransfer to run on hardware
	 * processor 2 at DISPATCH_LEVEL or above. NicXmit can be called by an XAM
	 * DPC on another processor, so IRQL alone is not sufficient. The tracker
	 * DPC remains the fallback path. */
	DWORD CurrentProcessor = GetCurrentProcessorNumber();
	BOOL CanDirectSubmit = UsbEnetDirectXmitEnabled() && EntryIrql == DISPATCH_LEVEL && CurrentProcessor == 2 && (Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) == 0;

	if (CanDirectSubmit) {
		if (CanSubmitXmitTracker(Device, Tracker)) {
#if USBENET_PERF_LOGGING
			if (Device->PendingXmitCount == 0)
				++TxDirectPipeIdleSubmitCount;

			++TxDirectSubmitCount;
#endif
			SubmitXmitTrackerLocked(Device, Tracker);
		} else {
			Tracker->Flags |= XMIT_FLAG_WAITING;
#if USBENET_PERF_LOGGING
			++TxDispatchWaitCount;
#endif
		}

		return;
	}

#if USBENET_PERF_LOGGING
	if (UsbEnetDirectXmitEnabled() && EntryIrql == DISPATCH_LEVEL && CurrentProcessor != 2)
		++TxDispatchWrongProcessorCount;
#endif

	QueueXmitTrackerDpc(Tracker);
}

static PXMIT_TRACKER FindReadyXmitTracker(CUsbEnet* Device, PXMIT_TRACKER StartAfter, BOOL DebugTracker, BOOL WaitingTracker) {
	PXMIT_TRACKER Candidate = StartAfter;

	for (DWORD Index = 0; Index < XMIT_BUFFER_COUNT; Index++) {
		++Candidate;

		if (Candidate >= &Device->XmitTrackers[XMIT_BUFFER_COUNT])
			Candidate = &Device->XmitTrackers[0];

		if (Candidate->FirstPacket == NULL)
			continue;

		if ((Candidate->Flags & (XMIT_FLAG_BUSY | XMIT_FLAG_QUEUED)) != 0)
			continue;

		if (((Candidate->Flags & XMIT_FLAG_WAITING) != 0) != WaitingTracker)
			continue;

		if (IsDebugXmitTracker(Candidate) != DebugTracker)
			continue;

		return Candidate;
	}

	return NULL;
}

static DWORD ScheduleReadyXmitTrackers(CUsbEnet* Device, PXMIT_TRACKER StartAfter) {
	if ((Device->Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
		return 0;

	DWORD SubmittedCount = 0;
	PXMIT_TRACKER SearchStart = StartAfter;

	for (;;) {
		PXMIT_TRACKER Selected = NULL;

		if (Device->PendingXmitCount < MaximumPendingXmitCount) {
			Selected = FindReadyXmitTracker(Device, SearchStart, TRUE, TRUE);

			/* A non-waiting debug tracker is a partial large-frame aggregate.
			 * Leave it open while other transfers are pending and flush it when
			 * the debug TX queue drains. */
			if (Selected == NULL && Device->PendingXmitCount == 0)
				Selected = FindReadyXmitTracker(Device, SearchStart, TRUE, FALSE);
		}

		if (Selected == NULL && Device->PendingXmitCount < MaximumTitlePendingXmitCount) {
			Selected = FindReadyXmitTracker(Device, SearchStart, FALSE, TRUE);

			if (Selected == NULL)
				Selected = FindReadyXmitTracker(Device, SearchStart, FALSE, FALSE);
		}

		if (Selected == NULL)
			break;

		Selected->Flags &= ~XMIT_FLAG_WAITING;
		SubmitXmitTrackerLocked(Device, Selected);
		++SubmittedCount;
		SearchStart = Selected;
	}

	return SubmittedCount;
}

VOID __fastcall CUsbEnet::AbortUserControlTransfer() {
	USBENET_LOG("AbortUserControlTransfer this=%p", this);
	TRAP_THREAD(LockOwnerThread);
	TRAP_ASSERT((Flags & USBENET_STATE_ACTIVE) != 0);

	Flags &= ~USBENET_STATE_ACTIVE;
}

NTSTATUS __fastcall CUsbEnet::NicUpdateMcastMembership(CEnetAddr* Address, DWORD Add) {
	USBENET_LOG("ENTER NicUpdateMcastMembership this=%p address=%p add=0x%08X", this, Address, Add);
	// this function only logged Xwpp info originally
	return STATUS_SUCCESS;
}

DWORD __fastcall CUsbEnet::NicGetLinkState() {
#if USBENET_CONNECTION_LOGGING
	static DWORD GetLinkCallCount;
	static DWORD LastFlags = 0xFFFFFFFF;
	static DWORD LastLinkState = 0xFFFFFFFF;
	static LONG LastDeviceAttached = -1;
	static DWORD LastInitStage = 0xFFFFFFFF;

	++GetLinkCallCount;
	if (GetLinkCallCount <= 4 || ShouldLogDrop(GetLinkCallCount) || LastFlags != Flags || LastLinkState != LinkState || LastDeviceAttached != DeviceAttached || LastInitStage != InitStage) {
		USBENET_CONN("get-link count=%u this=%p flags=0x%08X link=0x%08X attached=%ld stage=%u irql=%u cpu=%u", GetLinkCallCount, this, Flags, LinkState, DeviceAttached, InitStage, KeGetCurrentIrql(), GetCurrentProcessorNumber());
		LastFlags = Flags;
		LastLinkState = LinkState;
		LastDeviceAttached = DeviceAttached;
		LastInitStage = InitStage;
	}
#endif
	return LinkState;
}

VOID __fastcall CUsbEnet::NicDoTimerWaitForDeviceAdd() {
	USBENET_LOG("NicDoTimerWaitForDeviceAdd this=%p", this);
	NicBaseTakeLock(this);

	LinkState |= 0x00010000;

	NicBaseShutdown(this);

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);
	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	NotifyLinkStateChangedToUsers();
}

VOID __fastcall CUsbEnet::NicDoTimerAdvanceInitStage() {
	USBENET_LOG("NicDoTimerAdvanceInitStage this=%p", this);
	NicBaseTakeLock(this);

	AdvanceInitStage();

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);
	NULL_OWNER_THREAD(this);

	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

NTSTATUS __fastcall CUsbEnet::PrepareForUserControlTransfer() {
	USBENET_LOG("PrepareForUserControlTransfer this=%p", this);
	TRAP_THREAD(LockOwnerThread);

	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
		TRAP_THREAD(LockOwnerThread);
		return STATUS_DEVICE_REMOVED;
	}

	while ((Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0) {

		if ((Flags & USBENET_STATE_TRANSFER_IN_PROGRESS) != 0) {
			DbgPrint("[usbenet]: Can't intiate another user control transfer, one already in progress!\n");
			TRAP_THREAD(LockOwnerThread);
			return STATUS_DEVICE_BUSY;
		}

		Flags |= USBENET_STATE_TRANSFER_IN_PROGRESS;

		KeEnterCriticalRegion();

		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);

		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);

		TRAP_IRQL(PASSIVE_LEVEL);

		KeWaitForSingleObject(&ControlEvent, Executive, PROC_IDLE, FALSE, nullptr);

		NicBaseTakeLock(this);
		KeLeaveCriticalRegion();

		TRAP_ASSERT((Flags & USBENET_STATE_TRANSFER_IN_PROGRESS) != 0);

		Flags &= ~USBENET_STATE_TRANSFER_IN_PROGRESS;

		if (!DeviceAttached) {
			DbgPrint("[usbenet]: USB Ethernet device unloaded, aborting user control transfer!\n");

			TRAP_ASSERT((Flags & USBENET_STATE_TRANSFER_IN_PROGRESS) != 0);

			TRAP_THREAD(LockOwnerThread);
			return STATUS_DEVICE_BUSY;
		}

		if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
			TRAP_THREAD(LockOwnerThread);
			return STATUS_DEVICE_BUSY;
		}

		if ((Flags & USBENET_STATE_CAN_USER_TRANSFER) == 0)
			break;
	}

	TRAP_ASSERT((Flags & USBENET_STATE_ACTIVE) == 0);

	Flags |= USBENET_STATE_ACTIVE;

	TRAP_THREAD(LockOwnerThread);

	return STATUS_SUCCESS;
}

VOID __fastcall CUsbEnet::WaitForUserControlTransferResult() {
	USBENET_LOG("WaitForUserControlTransferResult this=%p", this);
	TRAP_THREAD(LockOwnerThread);
	TRAP_ASSERT((Flags & USBENET_STATE_ACTIVE) != 0);

	KeEnterCriticalRegion();

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	TRAP_IRQL(PASSIVE_LEVEL);

	KeWaitForSingleObject(&ControlEvent, Executive, PROC_IDLE, FALSE, NULL);

	NicBaseTakeLock(this);

	KeLeaveCriticalRegion();

	TRAP_ASSERT((Flags & USBENET_STATE_ACTIVE) != 0);

	Flags &= ~USBENET_STATE_ACTIVE;
}

NTSTATUS __fastcall CUsbEnet::QueueControlTransfer(PUSBD_ASYNC_COMPLETION_ROUTINE CompletionRoutine, BYTE RequestType, BYTE Request, WORD Value, WORD Index, WORD Length, const PVOID Buffer) {
	TRAP_THREAD(LockOwnerThread);

	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
		return STATUS_DEVICE_REMOVED;

	if ((Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0) {
		DbgPrint("[usbenet]: Internal control transfer already in use!\n");
		return STATUS_DEVICE_BUSY;
	}

	Flags |= USBENET_STATE_CAN_USER_TRANSFER;
	++DiagnosticControlQueueCount;

	if (ShouldLogDiagnosticEvent(DiagnosticControlQueueCount)) {
		BYTE Data[6] = { 0, 0, 0, 0, 0, 0 };
		if ((RequestType & USB_DIR_IN) == 0 && Buffer != NULL && Length != 0)
			memcpy(Data, Buffer, Length < sizeof(Data) ? Length : sizeof(Data));
		DbgPrint("[usbenet]: Control queue #%u type=0x%02X request=0x%02X value=0x%04X index=0x%04X length=%u completion=%p out=%02X %02X %02X %02X %02X %02X flags=0x%08X\n", DiagnosticControlQueueCount, RequestType, Request, Value, Index, Length, CompletionRoutine, Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Flags);
	}

	if ((Flags & (USBENET_STATE_TRANSFER_IN_PROGRESS | USBENET_STATE_ACTIVE)) != 0)
		KeResetEvent(&ControlEvent);

	ControlRequest.Transfer.Request.Context = this;
	ControlRequest.Transfer.Request.CompletionRoutine = CompletionRoutine;
	ControlRequest.Transfer.Request.EndpointHandle = DefaultEndpoint;
	ControlRequest.Transfer.Request.TransferFlags = 0;

	if (Length != 0) {
		TRAP_ASSERT(PhysicalMemory != NULL);
		TRAP_ASSERT(Length <= ControlScratchBufferSize);

		if (RequestType & 0x80) {
			TRAP_ASSERT(Buffer == NULL);
			memset((PBYTE)PhysicalMemory + DmaBufferSize, 0, Length);
		} else {
			TRAP_ASSERT(Buffer != NULL);
			memcpy((PBYTE)PhysicalMemory + DmaBufferSize, Buffer, Length);
		}

		ControlRequest.Transfer.Buffer = (PBYTE)PhysicalMemory + DmaBufferSize;
	} else {
		TRAP_ASSERT(Buffer == NULL);
		ControlRequest.Transfer.Buffer = NULL;
	}

	ControlRequest.Transfer.BufferLength = Length;

	ControlRequest.Setup.RequestType = RequestType;
	ControlRequest.Setup.Request = Request;
	ControlRequest.Setup.Value = _byteswap_ushort(Value);
	ControlRequest.Setup.Index = _byteswap_ushort(Index);
	ControlRequest.Setup.Length = _byteswap_ushort(Length);

	KeInsertQueueDpc(&ControlDpc, NULL, NULL);

	return STATUS_SUCCESS;
}

VOID __fastcall CUsbEnet::CompleteControlTransfer() {
	USBENET_LOG("CompleteControlTransfer this=%p", this);
	TRAP_THREAD(LockOwnerThread);
	TRAP_ASSERT((Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0);

	Flags &= ~USBENET_STATE_CAN_USER_TRANSFER;

	if ((Flags & (USBENET_STATE_TRANSFER_IN_PROGRESS | USBENET_STATE_ACTIVE)) != 0)
		KeSetEvent(&ControlEvent, 1, FALSE);
}

VOID __fastcall CUsbEnet::ControlSwitchProcs() {
	USBENET_LOG("ControlSwitchProcs this=%p", this);
	NicBaseTakeLock(this);

	TRAP_ASSERT((Flags & USBENET_STATE_CAN_USER_TRANSFER) != 0);

	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) == 0) {
		UsbdQueueAsyncTransfer(DeviceNode, &ControlRequest.Transfer);

		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);

		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);
	} else {
		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);

		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);

		ControlRequest.Transfer.Request.CompletionRoutine(&ControlRequest.Transfer, STATUS_CANCELLED);
	}
}

VOID __fastcall CUsbEnet::CompleteReceive(PRECV_TRANSFER Packet, NTSTATUS Status) {
	// Successful receive completions are intentionally not logged here. The
	// sampled [usbenet]: RX complete diagnostic below already records the first
	// completions, powers of two, and every error without flooding the console.
	DWORD FramesProcessed = 0;
	DWORD FrameBytesProcessed = 0;
	BOOL ResubmitPacket = TRUE;
	BOOL NotifyLinkAfterReceive = FALSE;
	USBENET_RX_PARSE_CONTEXT ParseContext;

	NicBaseTakeLockAtRaisedIrql(this);

	TimerTick = KeTimeStampBundle->TickCount;

	TRAP_ASSERT(Packet->InFlight != 0);
	TRAP_ASSERT(RxInFlightCount != 0);
	Packet->InFlight = 0;
	--RxInFlightCount;
	++DiagnosticRxCompleteCount;
	++DiagnosticRxCompleteSequence;

	if (!NT_SUCCESS(Status))
		++DiagnosticRxErrorCount;
	else if (Packet->Transfer.BytesTransferred == 0)
		++DiagnosticRxZeroLengthCount;

	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->HandleTransportCompletion(this, UsbEnetTransportReceive, Status);

	if (!NT_SUCCESS(Status) || ShouldLogDiagnosticEvent(DiagnosticRxCompleteSequence)) {
		PBYTE Data = static_cast<PBYTE>(Packet->Transfer.Buffer);
		DWORD Bytes = Packet->Transfer.BytesTransferred;
		DbgPrint("[usbenet]: RX complete #%u packet=%u status=0x%08X bytes=%u requested=%u inFlightAfter=%u first=%02X %02X %02X %02X %02X %02X %02X %02X last=%02X %02X %02X %02X\n", DiagnosticRxCompleteSequence, static_cast<DWORD>(Packet - RecvPackets), Status, Bytes, Packet->Transfer.BufferLength, RxInFlightCount, Bytes > 0 ? Data[0] : 0, Bytes > 1 ? Data[1] : 0, Bytes > 2 ? Data[2] : 0, Bytes > 3 ? Data[3] : 0, Bytes > 4 ? Data[4] : 0, Bytes > 5 ? Data[5] : 0, Bytes > 6 ? Data[6] : 0, Bytes > 7 ? Data[7] : 0, Bytes > 3 ? Data[Bytes - 4] : 0, Bytes > 2 ? Data[Bytes - 3] : 0, Bytes > 1 ? Data[Bytes - 2] : 0, Bytes > 0 ? Data[Bytes - 1] : 0);
	}

	if (Status < 0) {
		switch (Status) {
			case STATUS_CANCELLED:
				++RxCancelledCompletionCount;
				ResubmitPacket = (Flags & ReceiveRunning) != 0;
				goto Complete;

			case (NTSTATUS)0xC0051011:
				SET_NIC_STATS_PRINT_PENDING(this);
				++Stats.ReceiveOtherErrorCount;
				goto Complete;

			case STATUS_DATA_OVERRUN:
				SET_NIC_STATS_PRINT_PENDING(this);
				++Stats.ReceiveOverrunErrorCount;
				goto Complete;

			default:
				SET_NIC_STATS_PRINT_PENDING(this);
				++Stats.ReceiveOtherErrorCount;
				goto Complete;
		}
	}

	++RxUsbCompletionCount;
	RxUsbBytes += Packet->Transfer.BytesTransferred;

	TRAP_ASSERT(g_UsbEnetChipset != NULL);

	if (!g_UsbEnetChipset->InitializeReceiveParser(this, static_cast<PBYTE>(Packet->Transfer.Buffer), Packet->Transfer.BytesTransferred, &ParseContext)) {
		++DiagnosticRxParserFailureCount;
		SET_NIC_STATS_PRINT_PENDING(this);
		++Stats.ReceiveOtherErrorCount;
		++RxInvalidFrameCount;
		goto Complete;
	}

	for (;;) {
		USBENET_RX_FRAME ParsedFrame;
		USBENET_RX_PARSE_RESULT ParseResult = g_UsbEnetChipset->GetNextReceiveFrame(this, &ParseContext, &ParsedFrame);

		if (ParseResult == UsbEnetRxParseComplete)
			break;

		if (ParseResult == UsbEnetRxParseSkip) {
			SET_NIC_STATS_PRINT_PENDING(this);
			++Stats.ReceiveOtherErrorCount;
			++RxInvalidFrameCount;
			continue;
		}

		if (ParseResult == UsbEnetRxParseError) {
			SET_NIC_STATS_PRINT_PENDING(this);
			++Stats.ReceiveOtherErrorCount;
			++RxInvalidFrameCount;
			break;
		}

		PBYTE frame = ParsedFrame.Data;
		DWORD frameLength = ParsedFrame.Length;

		if (frameLength < MinimumReceiveFrameSize) {
			DbgPrint("[usbenet]: [DISCARD] Received short frame of %u bytes\n", frameLength);
			SET_NIC_STATS_PRINT_PENDING(this);
			++Stats.ReceiveOtherErrorCount;
			++RxInvalidFrameCount;
			continue;
		}

		USHORT typeOrLength = *reinterpret_cast<UNALIGNED USHORT*>(frame + 12);

		if (typeOrLength <= 0x05DC) {
			DWORD snapHeader = *reinterpret_cast<UNALIGNED DWORD*>(frame + 14);
			USHORT snapReserved = *reinterpret_cast<UNALIGNED USHORT*>(frame + 18);

			if (snapHeader != 0xAAAA0300 || snapReserved != 0) {
				++RxInvalidFrameCount;
				continue;
			}

			BYTE addresses[12];
			USHORT snapType = *reinterpret_cast<UNALIGNED USHORT*>(frame + 20);

			frameLength -= 8;
			memcpy(addresses, frame, sizeof(addresses));
			frame += 8;
			memcpy(frame, addresses, sizeof(addresses));
			*reinterpret_cast<UNALIGNED USHORT*>(frame + 12) = snapType;
		}

		NIC_RECV_DEST destination;

		if (*reinterpret_cast<UNALIGNED DWORD*>(frame) == 0xFFFFFFFF && *reinterpret_cast<UNALIGNED USHORT*>(frame + 4) == 0xFFFF) {
			destination = NIC_RECV_DEST_BROADCAST;
			++RxBroadcastFrameCount;
		} else if ((frame[0] & 1) != 0) {
			destination = NIC_RECV_DEST_MULTICAST;
			++RxMulticastFrameCount;

			Stats.ReceiveBytes[destination] += frameLength;
			++Stats.ReceiveFrameCount[destination];
			SET_NIC_STATS_PRINT_PENDING(this);

			if (!NicBaseIsSubscribedMcastAddr(this, reinterpret_cast<CEnetAddr*>(frame)) && (AggregateReceiveFilter & NIC_RECV_DEST_FLAG_PROMISCUOUS) == 0) {
				++RxMulticastFilteredCount;
				continue;
			}
		} else if (memcmp(frame, &UnicastAddress, sizeof(CEnetAddr)) == 0) {
			destination = NIC_RECV_DEST_UNICAST;
			++RxTitleUnicastFrameCount;
		} else if (memcmp(frame, &AlternateUnicastAddress, sizeof(CEnetAddr)) == 0) {
			destination = NIC_RECV_DEST_ALTERNATE_UNICAST;
			++RxDebugUnicastFrameCount;
		} else {
			destination = NIC_RECV_DEST_PROMISCUOUS;
			++RxPromiscuousFrameCount;
		}

		if (destination != NIC_RECV_DEST_MULTICAST) {
			Stats.ReceiveBytes[destination] += frameLength;
			++Stats.ReceiveFrameCount[destination];
			SET_NIC_STATS_PRINT_PENDING(this);
		}

		Flags |= USBENET_STATE_00080000;
		++FramesProcessed;
		++RxParsedFrameCount;
		FrameBytesProcessed += frameLength;

		DWORD DestinationFlag = 1u << static_cast<DWORD>(destination);
		CNicUser* NotifyUsers[16];
		DWORD NotifyUserCount = 0;
		KIRQL UserListIrql = KfAcquireSpinLock(&m_UserListLock);

		for (PLIST_ENTRY Entry = m_UserList.Flink; Entry != &m_UserList && NotifyUserCount < ARRAYSIZE(NotifyUsers); Entry = Entry->Flink) {
			PUSB_ENET_USER_ENTRY UserEntry = CONTAINING_RECORD(Entry, USB_ENET_USER_ENTRY, Link);
			CNicUser* User = UserEntry->User;

			if ((User->AttachInfo.ReceiveFilterMask & DestinationFlag) != 0)
				NotifyUsers[NotifyUserCount++] = User;
		}

		KfReleaseSpinLock(&m_UserListLock, UserListIrql);

		if (NotifyUserCount == 0) {
			++RxNoDeliveryFrameCount;
			continue;
		}

		/* A link-state callback may be running on another processor. Do not enter
		 * XAM concurrently; discard this frame rather than racing its IP reset. */
		if ((Flags & USBENET_STATE_00100000) != 0) {
			++RxNoDeliveryFrameCount;
			continue;
		}

		for (DWORD Index = 0; Index < NotifyUserCount; Index++) {
			if (IsDebugNicUser(NotifyUsers[Index]))
				++RxDebugDeliveredCount;
			else
				++RxTitleDeliveredCount;
		}

		Flags |= USBENET_STATE_CALLBACK_IN_PROGRESS;

		TRAP_ASSERT(PreviousIrql == 0xEE);
		TRAP_THREAD(LockOwnerThread);

		NULL_OWNER_THREAD(this);
		KeReleaseSpinLockFromRaisedIrql(&NicLock);

		for (DWORD Index = 0; Index < NotifyUserCount; Index++)
			NotifyUsers[Index]->NotifyReceive(frame, frameLength, destination);

		NicBaseTakeLockAtRaisedIrql(this);

		TRAP_ASSERT((Flags & USBENET_STATE_CALLBACK_IN_PROGRESS) != 0);
		Flags &= ~USBENET_STATE_CALLBACK_IN_PROGRESS;

		if ((Flags & USBENET_STATE_NOTIFY_LINK_STATE) != 0)
			NotifyLinkAfterReceive = TRUE;
	}

Complete:
	RxFrameCount += FramesProcessed;
	RxFrameBytes += FrameBytesProcessed;

	if ((Flags & USBENET_STATE_NOTIFY_LINK_STATE) != 0)
		NotifyLinkAfterReceive = TRUE;

	/* Hard RX recovery clears ReceiveRunning before cancelling the ring.  The
	 * completion that detected the wedge may still have STATUS_SUCCESS, so it
	 * must obey ReceiveRunning too or it will keep one request alive forever
	 * and prevent the ring from reaching the quiescent state. */
	if (ResubmitPacket && (Flags & ReceiveRunning) != 0 && (Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) == 0) {
		SubmitReceivePacket(Packet);
	}

	TRAP_ASSERT(PreviousIrql == 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KeReleaseSpinLockFromRaisedIrql(&NicLock);

	if (NotifyLinkAfterReceive)
		NotifyLinkStateChangedToUsers();
}

VOID __fastcall CUsbEnet::CompleteStartReceiving(NTSTATUS Status) {
	USBENET_LOG("CompleteStartReceiving this=%p status=0x%08X", this, Status);
	NicBaseTakeLock(this);

	CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Attempting to start receiving failed with status 0x%08X! Continuing.\n", Status);
	}

	TRAP_ASSERT(g_UsbEnetChipset != NULL);

	if (!g_UsbEnetChipset->IsReady(this)) {
		AdvanceInitStage();
	}

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

VOID __fastcall CUsbEnet::CompleteTransmit(PXMIT_TRACKER Tracker, NTSTATUS Status) {
	USBENET_LOG("CompleteTransmit this=%p tracker=%p status=0x%08X", this, Tracker, Status);
	NicBaseTakeLockAtRaisedIrql(this);
	++DiagnosticTxCompleteCount;
	++DiagnosticTxCompleteSequence;
	DiagnosticTxLastStatus = Status;

	if (!NT_SUCCESS(Status))
		++DiagnosticTxErrorCount;

	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->HandleTransportCompletion(this, UsbEnetTransportTransmit, Status);

	if (!NT_SUCCESS(Status) || ShouldLogDiagnosticEvent(DiagnosticTxCompleteSequence))
		DbgPrint("[usbenet]: TX complete #%u tracker=%u status=0x%08X bytesTransferred=%u bufferLength=%u pendingBefore=%u flags=0x%08X\n", DiagnosticTxCompleteSequence, static_cast<DWORD>(Tracker - XmitTrackers), Status, Tracker->Transfer.BytesTransferred, Tracker->Transfer.BufferLength, PendingXmitCount, Tracker->Flags);

	TRAP_ASSERT(Tracker->FirstPacket != NULL);
	Tracker->FirstPacket = NULL;

	TRAP_ASSERT((Tracker->Flags & XMIT_FLAG_BUSY) != 0);
	TRAP_ASSERT((Tracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_QUEUED)) == 0);
	Tracker->Flags &= ~(XMIT_FLAG_BUSY | XMIT_FLAG_CRC_PADDING);

	TRAP_ASSERT(PendingXmitCount != 0);
	--PendingXmitCount;

	TRAP_ASSERT(ActiveXmitCount != 0);
	--ActiveXmitCount;

	/* Refill USB slots directly from the completion DPC. This is already the
	 * lowest-latency path and should not queue another transmit DPC. */
	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) == 0) {
		DWORD Refilled = ScheduleReadyXmitTrackers(this, Tracker);

#if USBENET_PERF_LOGGING
		if (Refilled != 0) {
			++TxCompletionRefillCount;
			TxCompletionRefillTransferCount += Refilled;
		}
#else
		UNREFERENCED_PARAMETER(Refilled);
#endif
	}

	TRAP_ASSERT(PreviousIrql == 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KeReleaseSpinLockFromRaisedIrql(&NicLock);
}

VOID __fastcall CUsbEnet::BulkXmitSwitchProcs(PXMIT_TRACKER Tracker) {
	USBENET_LOG("BulkXmitSwitchProcs this=%p tracker=%p", this, Tracker);
	NicBaseTakeLock(this);
#if USBENET_PERF_LOGGING
	++TxDpcRunCount;
#endif

	TRAP_ASSERT(Tracker->FirstPacket != NULL);
	TRAP_ASSERT((Tracker->Flags & XMIT_FLAG_QUEUED) != 0);

	Tracker->Flags &= ~XMIT_FLAG_QUEUED;
	TRAP_ASSERT((Tracker->Flags & XMIT_FLAG_BUSY) == 0);

	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
		Tracker->Flags |= XMIT_FLAG_WAITING;
	} else if (!CanSubmitXmitTracker(this, Tracker)) {
		Tracker->Flags |= XMIT_FLAG_WAITING;
		USBENET_LOG("BulkXmitSwitchProcs deferring tracker=%p debug=%u active=%u pending=%u", Tracker, IsDebugXmitTracker(Tracker), ActiveXmitCount, PendingXmitCount);
	} else {
		Tracker->Flags &= ~XMIT_FLAG_WAITING;
		SubmitXmitTrackerLocked(this, Tracker);
	}

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

CUsbEnet* __fastcall CUsbEnet::AllocDeviceExtension() {
	USBENET_LOG("AllocDeviceExtension this=%p", this);
	LONG PreviousAttached = InterlockedExchange(&DeviceAttached, 1);
	if (PreviousAttached != 0) {
		DbgPrint("[usbenet]: Only one USB Ethernet device allowed! attached=%ld\n", PreviousAttached);
		return NULL;
	}
	return this;
}

VOID __fastcall CUsbEnet::FreeDeviceExtension(PVOID DeviceExtension) {
	USBENET_LOG("FreeDeviceExtension this=%p deviceExtension=%p", this, DeviceExtension);
	TRAP_ASSERT(DeviceExtension == this);
	TRAP_ASSERT(g_UsbEnetInterruptEndpoint == NULL);
	TRAP_ASSERT(TransmitEndpoint == NULL);
	TRAP_ASSERT(ReceiveEndpoint == NULL);
	TRAP_ASSERT(PhysicalMemory == NULL);


	LONG PreviousAttached = InterlockedExchange(&DeviceAttached, 0);

	DbgPrint("[CUsbEnet] FreeDeviceExtension this=%p previousAttached=%ld flags=%08X node=%p\n",
		this, PreviousAttached, Flags, DeviceNode);

	TRAP_ASSERT(PreviousAttached == 1);
}

VOID __fastcall CUsbEnet::NicFlushXmitQueue(CNicUser* User) {
	USBENET_LOG("NicFlushXmitQueue this=%p user=%p", this, User);
	UNREFERENCED_PARAMETER(User);
	// Xwpp debug on dev
}

VOID __fastcall CUsbEnet::NicXmit(CNicUser* User, DWORD Unknown, PVOID Buffer, DWORD Length, PVOID CompletionContext) {
	USBENET_LOG("NicXmit this=%p user=%p unknown=0x%08X buffer=%p length=%u completionContext=%p", this, User, Unknown, Buffer, Length, CompletionContext);
	PXMIT_PACKET Packet;
	PXMIT_PACKET PreviousPacket;
	PXMIT_TRACKER Tracker;
	PXMIT_TRACKER SearchTracker;
	PBYTE TrackerBuffer;
	DWORD TrackerBufferLength;
	DWORD TrackerFrameCount;
	DWORD ScheduledXmitCount;
	DWORD WaitingXmitCount;
	DWORD AggregateOffset;
	DWORD FramedLength;
	DWORD BytesWritten;
	DWORD SubmitFrameLimit;
	BOOL SubmitTracker;
	BOOL SequentialPacket;
	BOOL HasTerminator;
	BOOL FrameAppended;
	BOOL DebugUser = IsDebugNicUser(User);
	BOOL LowLatencyUser = IsLowLatencyNicUser(User);
	BOOL ImmediateLowLatency = LowLatencyUser && Length <= DebugImmediateFrameSize;
	KIRQL EntryIrql = KeGetCurrentIrql();
	CUsbEnetChipset* Chipset = g_UsbEnetChipset;

	UNREFERENCED_PARAMETER(Unknown);

	if (Chipset == NULL) {
		DbgPrint("[usbenet]: No USB Ethernet chipset backend selected! Not sending.\n");
		goto Complete;
	}

	if (Length > Chipset->GetMaximumFrameSize()) {
		DbgPrint("[usbenet]: Frame size %u is larger than %s max allowed (%u)! Not sending.\n", Length, Chipset->GetName(), Chipset->GetMaximumFrameSize());
		goto Complete;
	}

	TRAP_ASSERT(Length <= 0xFFFF);
	TrackerBufferLength = Length + Chipset->GetTransmitHeaderSize();

	NicBaseTakeLock(this);


#if USBENET_PERF_LOGGING
	if (EntryIrql == DISPATCH_LEVEL)
		++TxDispatchEntryCount;
	else
		++TxOtherIrqlEntryCount;
#endif

	if (!DeviceAttached || !Chipset->IsReady(this) || (Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
		DbgPrint("[usbenet]: Device unavailable, initializing, or shutting down (attached=%i stage=%i flags=0x%08X)!  Not sending.\n", DeviceAttached, InitStage, Flags);
		goto ReleaseLock;
	}

	Packet = NextXmitPacket;
	SequentialPacket = Packet->User == NULL;

	if (!SequentialPacket) {
		PXMIT_PACKET SearchPacket = Packet;

		for (DWORD Index = 0; Index < XMIT_PACKET_COUNT; Index++) {
			++SearchPacket;

			if (SearchPacket >= &XmitPackets[XMIT_PACKET_COUNT])
				SearchPacket = &XmitPackets[0];

			if (SearchPacket->User == NULL) {
				Packet = SearchPacket;
				break;
			}
		}
	}

	if (Packet->User != NULL) {
		++TxPacketDropCount;

		if (ShouldLogDrop(TxPacketDropCount))
			DbgPrint("[usbenet]: Transmit packet ring full! packets=%u/%u packetHighWater=%u trackers=%u/%u trackerHighWater=%u pending=%u packetDrops=%u\n", TxPacketInUseCount, XMIT_PACKET_COUNT, TxPacketHighWater, ActiveXmitCount, XMIT_BUFFER_COUNT, TxTrackerHighWater, PendingXmitCount, TxPacketDropCount);

		goto ReleaseLock;
	}

	NextXmitPacket = Packet;
	PreviousPacket = Packet - 1;

	if (PreviousPacket < &XmitPackets[0])
		PreviousPacket = &XmitPackets[XMIT_PACKET_COUNT - 1];

	Tracker = NULL;
	TrackerBuffer = NULL;
	TrackerFrameCount = 0;
	SubmitTracker = FALSE;

	/* Never mix users in one USB transfer. Title traffic may aggregate up to
	 * MaximumFramesPerXmit. The devkit debug user and the 17559 retail title
	 * user both carry latency-sensitive XBDM control traffic, so frames at or
	 * below DebugImmediateFrameSize remain immediate. Larger frames still
	 * aggregate. Queue an open large-frame aggregate before an immediate frame
	 * so USB submission order remains consistent. */
	if (SequentialPacket && PreviousPacket->User == User) {
		Tracker = PreviousPacket->Tracker;

		TRAP_ASSERT(Tracker != NULL);
		TRAP_ASSERT(Tracker->FirstPacket != NULL);

		if ((Tracker->Flags & XMIT_FLAG_BUSY) == 0) {
			ULONG CurrentLength = Tracker->Transfer.BufferLength;
			TrackerFrameCount = CountXmitTrackerFrames(Tracker, XmitPackets);

			if ((Tracker->Flags & XMIT_FLAG_CRC_PADDING) != 0)
				CurrentLength -= Chipset->GetTransmitTerminatorSize();

			TrackerBufferLength = CurrentLength + Length + Chipset->GetTransmitHeaderSize();
			TrackerBuffer = static_cast<PBYTE>(Tracker->Transfer.Buffer) + CurrentLength;

			BOOL CanAppend = Chipset->SupportsTransmitAggregation() && TrackerBufferLength <= Chipset->GetMaximumAggregateTransferSize() && TrackerFrameCount < MaximumFramesPerXmit;

			if (LowLatencyUser)
				CanAppend = CanAppend && !ImmediateLowLatency && !IsImmediateLowLatencyXmitTracker(Tracker) && TrackerFrameCount < MaximumDebugFramesPerXmit;

			if (CanAppend) {
				SubmitTracker = FALSE;
				goto FillFrame;
			}

			if ((Tracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_QUEUED)) == 0)
				QueueOrSubmitXmitTrackerLocked(this, Tracker, EntryIrql);
		}
	}

	SearchTracker = NextXmitTracker;
	Tracker = NULL;

	for (DWORD Index = 0; Index < XMIT_BUFFER_COUNT; Index++) {
		if (SearchTracker->FirstPacket == NULL && (SearchTracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_BUSY | XMIT_FLAG_QUEUED)) == 0) {
			Tracker = SearchTracker;
			break;
		}

		++SearchTracker;

		if (SearchTracker >= &XmitTrackers[XMIT_BUFFER_COUNT])
			SearchTracker = &XmitTrackers[0];
	}

	if (Tracker == NULL) {
		++TxBufferDropCount;

		if (ShouldLogDrop(TxBufferDropCount))
			DbgPrint("[usbenet]: Transmit tracker pool full! trackers=%u/%u trackerHighWater=%u packets=%u/%u packetHighWater=%u pending=%u bufferDrops=%u\n", ActiveXmitCount, XMIT_BUFFER_COUNT, TxTrackerHighWater, TxPacketInUseCount, XMIT_PACKET_COUNT, TxPacketHighWater, PendingXmitCount, TxBufferDropCount);

		goto ReleaseLock;
	}

	Tracker->FirstPacket = Packet;
	TrackerFrameCount = 0;
	TrackerBufferLength = Length + Chipset->GetTransmitHeaderSize();
	TrackerBuffer = static_cast<PBYTE>(Tracker->Transfer.Buffer);
	NextXmitTracker = Tracker + 1;

	if (NextXmitTracker >= &XmitTrackers[XMIT_BUFFER_COUNT])
		NextXmitTracker = &XmitTrackers[0];

	TRAP_ASSERT(ActiveXmitCount < XMIT_BUFFER_COUNT);

	ScheduledXmitCount = PendingXmitCount;
	WaitingXmitCount = 0;

	for (DWORD Index = 0; Index < XMIT_BUFFER_COUNT; Index++) {
		if ((XmitTrackers[Index].Flags & XMIT_FLAG_QUEUED) != 0)
			++ScheduledXmitCount;

		if ((XmitTrackers[Index].Flags & XMIT_FLAG_WAITING) != 0)
			++WaitingXmitCount;
	}

	SubmitTracker = ImmediateLowLatency || (ScheduledXmitCount == 0 && WaitingXmitCount == 0);
	++ActiveXmitCount;

	if (ActiveXmitCount > TxTrackerHighWater)
		TxTrackerHighWater = ActiveXmitCount;

FillFrame:
	AggregateOffset = static_cast<DWORD>(TrackerBuffer - static_cast<PBYTE>(Tracker->Transfer.Buffer));
	FramedLength = 0;
	BytesWritten = 0;
	HasTerminator = FALSE;
	FrameAppended = Chipset->AppendTransmitFrame(this, TrackerBuffer, XmitBufferSize - AggregateOffset, AggregateOffset, Buffer, Length, &FramedLength, &BytesWritten, &HasTerminator);
	TRAP_ASSERT(FrameAppended);

	if (!FrameAppended) {
		DbgPrint("[usbenet]: %s transmit framing failed for %u byte frame.\n", Chipset->GetName(), Length);

		if (TrackerFrameCount == 0) {
			Tracker->FirstPacket = NULL;
			TRAP_ASSERT(ActiveXmitCount != 0);
			--ActiveXmitCount;
		}

		goto ReleaseLock;
	}

	TrackerBufferLength = AggregateOffset + BytesWritten;

	if (HasTerminator)
		Tracker->Flags |= XMIT_FLAG_CRC_PADDING;
	else
		Tracker->Flags &= ~XMIT_FLAG_CRC_PADDING;

	TRAP_ASSERT(TrackerBufferLength <= XmitBufferSize);

	Tracker->Transfer.BufferLength = TrackerBufferLength;
	++TrackerFrameCount;

	SubmitFrameLimit = LowLatencyUser ? MaximumDebugFramesPerXmit : MaximumFramesPerXmit;

	if (ImmediateLowLatency)
		SubmitTracker = TRUE;
	else if (!SubmitTracker && (Tracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_BUSY | XMIT_FLAG_QUEUED)) == 0 && (TrackerBufferLength >= XmitSubmitThreshold || TrackerFrameCount >= SubmitFrameLimit))
		SubmitTracker = TRUE;

	TRAP_ASSERT(User != NULL);

	Packet->User = User;
	Packet->CompletionContext = CompletionContext;
	Packet->Tracker = Tracker;
	Packet->Length = FramedLength;
	++TxPacketInUseCount;

	if (TxPacketInUseCount > TxPacketHighWater)
		TxPacketHighWater = TxPacketInUseCount;

	++TxFrameCount;
	TxFrameBytes += Length;

	if (DebugUser)
		++TxDebugFrameCount;
	else
		++TxTitleFrameCount;

	++NextXmitPacket;

	if (NextXmitPacket >= &XmitPackets[XMIT_PACKET_COUNT])
		NextXmitPacket = &XmitPackets[0];

	if (SubmitTracker && (Tracker->Flags & (XMIT_FLAG_WAITING | XMIT_FLAG_BUSY | XMIT_FLAG_QUEUED)) == 0)
		QueueOrSubmitXmitTrackerLocked(this, Tracker, EntryIrql);

ReleaseLock:
	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

Complete:
	User->NotifyXmitComplete(CompletionContext);
}

VOID __fastcall CUsbEnet::NicTimerWaitForDeviceAddDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
	USBENET_LOG("NicTimerWaitForDeviceAddDpc dpc=%p deferredContext=%p systemArgument1=%p systemArgument2=%p", Dpc, DeferredContext, SystemArgument1, SystemArgument2);
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);

	static_cast<CUsbEnet*>(DeferredContext)->NicDoTimerWaitForDeviceAdd();
}

VOID __fastcall CUsbEnet::NicTimerAdvanceInitStageDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
	USBENET_LOG("NicTimerAdvanceInitStageDpc dpc=%p deferredContext=%p systemArgument1=%p systemArgument2=%p", Dpc, DeferredContext, SystemArgument1, SystemArgument2);
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);

	static_cast<CUsbEnet*>(DeferredContext)->NicDoTimerAdvanceInitStage();
}

VOID __fastcall CUsbEnet::ContinueAsyncCloseAndDropLock() {
	USBENET_LOG("ContinueAsyncCloseAndDropLock this=%p", this);
	TRAP_THREAD(LockOwnerThread);

	if (!(Flags & USBENET_STATE_RESETTING)) {
		Flags |= USBENET_STATE_RESETTING;
	}

	if (g_UsbEnetInterruptEndpoint != NULL) {
		if (InterlockedCompareExchange(&g_UsbEnetInterruptInFlight, 0, 0) != 0)
			UsbdCancelAsyncTransfer(&g_UsbEnetInterruptTransfer);

		CloseRequest.EndpointHandle = g_UsbEnetInterruptEndpoint;
		g_UsbEnetInterruptEndpoint = NULL;
		UsbdQueueCloseEndpoint(DeviceNode, &CloseRequest);

		goto ReleaseLock;
	}

	if (TransmitEndpoint != NULL) {
		CloseRequest.EndpointHandle = TransmitEndpoint;
		TransmitEndpoint = NULL;
		UsbdQueueCloseEndpoint(DeviceNode, &CloseRequest);

		goto ReleaseLock;
	}

	if (ReceiveEndpoint != NULL) {
		CloseRequest.EndpointHandle = ReceiveEndpoint;
		ReceiveEndpoint = NULL;
		UsbdQueueCloseEndpoint(DeviceNode, &CloseRequest);

		goto ReleaseLock;
	}

	if (DefaultEndpoint != NULL) {
		CloseRequest.EndpointHandle = DefaultEndpoint;
		DefaultEndpoint = NULL;
		UsbdQueueCloseDefaultEndpoint(DeviceNode, &CloseRequest);

		goto ReleaseLock;
	}

	Flags &= ~USBENET_STATE_RESETTING;
	TRAP_ASSERT(!(Flags & USBENET_STATE_CAN_USER_TRANSFER));


	if (!(Flags & USBENET_STATE_STOPPING))
		goto ReleaseLock;

	if (PhysicalMemory != NULL) {
		MmFreePhysicalMemory(2, (DWORD)PhysicalMemory);
		PhysicalMemory = NULL;
	}

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	FreeDeviceExtension(this);

	DeviceNode->DeviceExtension = NULL;
	UsbdRemoveDeviceComplete(DeviceNode);

	return;

ReleaseLock:
	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

VOID __fastcall CUsbEnet::DpcControlSwitchProcsRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
	USBENET_LOG("DpcControlSwitchProcsRoutine dpc=%p deferredContext=%p systemArgument1=%p systemArgument2=%p", Dpc, DeferredContext, SystemArgument1, SystemArgument2);
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);

	CUsbEnet* Context = static_cast<CUsbEnet*>(DeferredContext);
	TRAP_ASSERT(Context != NULL);
	Context->ControlSwitchProcs();
}

VOID __fastcall CUsbEnet::AsyncCompletionRoutineBulkRecv(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
	if (!g_UsbEnetBulkRecvLastCompletionStatusValid || g_UsbEnetBulkRecvLastCompletionStatus != Status) {
		USBENET_LOG("AsyncCompletionRoutineBulkRecv request=%p status=0x%08X completion=%u", Request, Status, DiagnosticRxCompleteSequence + 1);
		g_UsbEnetBulkRecvLastCompletionStatus = Status;
		g_UsbEnetBulkRecvLastCompletionStatusValid = TRUE;
	}

	CUsbEnet* Context = static_cast<CUsbEnet*>(Request->Context);
	TRAP_ASSERT(Context != NULL);
	Context->CompleteReceive(reinterpret_cast<PRECV_TRANSFER>(Request), Status);
}

VOID __fastcall CUsbEnet::AsyncCompletionRoutineInterruptStatus(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
	if (!g_UsbEnetInterruptLastCompletionStatusValid || g_UsbEnetInterruptLastCompletionStatus != Status) {
		USBENET_LOG("AsyncCompletionRoutineInterruptStatus request=%p status=0x%08X completion=%u", Request, Status, g_UsbEnetInterruptSequence + 1);
		g_UsbEnetInterruptLastCompletionStatus = Status;
		g_UsbEnetInterruptLastCompletionStatusValid = TRUE;
	}

	CUsbEnet* Context = static_cast<CUsbEnet*>(Request->Context);
	TRAP_ASSERT(Context != NULL);
	Context->CompleteInterruptLinkStatus(Status);
}

VOID __fastcall CUsbEnet::ResetTransportEndpoint(USBENET_TRANSPORT_PIPE Pipe) {
	typedef VOID(__fastcall* PUSBD_RESET_ENDPOINT)(PUSBD_DEVICE_NODE DeviceNode, PVOID EndpointHandle, BOOL ResetDataToggle);
	static PUSBD_RESET_ENDPOINT ResetEndpoint = reinterpret_cast<PUSBD_RESET_ENDPOINT>(Utilities::ResolveFunction("xboxkrnl.exe", 892));
	PVOID Endpoint = NULL;

	if (Pipe == UsbEnetTransportReceive)
		Endpoint = ReceiveEndpoint;
	else if (Pipe == UsbEnetTransportTransmit)
		Endpoint = TransmitEndpoint;
	else if (Pipe == UsbEnetTransportInterrupt)
		Endpoint = g_UsbEnetInterruptEndpoint;

	if (ResetEndpoint != NULL && DeviceNode != NULL && Endpoint != NULL)
		ResetEndpoint(DeviceNode, Endpoint, TRUE);
}

VOID __fastcall CUsbEnet::ResetUsbDevice() {
	typedef VOID(__fastcall* PUSBD_RESET_DEVICE)(PUSBD_DEVICE_NODE DeviceNode);
	static PUSBD_RESET_DEVICE ResetDevice = reinterpret_cast<PUSBD_RESET_DEVICE>(Utilities::ResolveFunction("xboxkrnl.exe", 758));

	if (ResetDevice != NULL && DeviceNode != NULL)
		ResetDevice(DeviceNode);
}

VOID __fastcall CUsbEnet::StartInterruptLinkStatus() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);

	if (!g_UsbEnetChipset->UsesInterruptLinkStatus() || g_UsbEnetInterruptEndpoint == NULL || PhysicalMemory == NULL)
		return;

	InterlockedExchange(&g_UsbEnetInterruptPaused, 0);
	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0 || InterlockedCompareExchange(&g_UsbEnetInterruptInFlight, 1, 0) != 0)
		return;

	g_UsbEnetInterruptTransfer.BytesTransferred = 0;
	g_UsbEnetInterruptTransfer.BufferLength = InterruptStatusBufferSize;
	UsbdQueueAsyncTransfer(DeviceNode, &g_UsbEnetInterruptTransfer);
}

VOID __fastcall CUsbEnet::PauseInterruptLinkStatus() {
	InterlockedExchange(&g_UsbEnetInterruptPaused, 1);
	if (InterlockedCompareExchange(&g_UsbEnetInterruptInFlight, 0, 0) != 0)
		UsbdCancelAsyncTransfer(&g_UsbEnetInterruptTransfer);
}

BOOL __fastcall CUsbEnet::IsInterruptLinkStatusInFlight() {
	return InterlockedCompareExchange(&g_UsbEnetInterruptInFlight, 0, 0) != 0;
}

VOID __fastcall CUsbEnet::CompleteInterruptLinkStatus(NTSTATUS Status) {
	BOOL Notify = FALSE;
	BOOL Resubmit = FALSE;

	NicBaseTakeLockAtRaisedIrql(this);
	InterlockedExchange(&g_UsbEnetInterruptInFlight, 0);
	++g_UsbEnetInterruptSequence;

	BOOL InterruptPaused = InterlockedCompareExchange(&g_UsbEnetInterruptPaused, 0, 0) != 0;
	if (NT_SUCCESS(Status) && !InterruptPaused) {
		DWORD BytesTransferred = g_UsbEnetInterruptTransfer.BytesTransferred;

		if (BytesTransferred != 0) {
			TRAP_ASSERT(g_UsbEnetChipset != NULL);
			Notify = g_UsbEnetChipset->ProcessInterruptLinkStatus(this, static_cast<const BYTE*>(g_UsbEnetInterruptTransfer.Buffer), BytesTransferred);
		} else if (g_UsbEnetInterruptSequence <= 8 || (g_UsbEnetInterruptSequence & (g_UsbEnetInterruptSequence - 1)) == 0) {
			DbgPrint("[usbenet]: Interrupt status completion #%u returned zero bytes.\n", g_UsbEnetInterruptSequence);
		}
	} else if (Status != STATUS_CANCELLED) {
		++g_UsbEnetInterruptErrorCount;

		if (g_UsbEnetInterruptErrorCount <= 8 || (g_UsbEnetInterruptErrorCount & (g_UsbEnetInterruptErrorCount - 1)) == 0)
			DbgPrint("[usbenet]: Interrupt status completion failed: status=0x%08X errors=%u.\n", Status, g_UsbEnetInterruptErrorCount);
	}

	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->HandleTransportCompletion(this, UsbEnetTransportInterrupt, Status);

	Resubmit = g_UsbEnetInterruptEndpoint != NULL && InterlockedCompareExchange(&g_UsbEnetInterruptPaused, 0, 0) == 0 && (Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) == 0;

	TRAP_ASSERT(PreviousIrql == 0xEE);
	TRAP_THREAD(LockOwnerThread);
	NULL_OWNER_THREAD(this);
	KeReleaseSpinLockFromRaisedIrql(&NicLock);

	if (Notify)
		NotifyLinkStateChangedToUsers();

	if (Resubmit)
		StartInterruptLinkStatus();
}

VOID __fastcall CUsbEnet::SubmitReceivePacket(PRECV_TRANSFER Packet) {
	TRAP_ASSERT(Packet >= &RecvPackets[0]);
	TRAP_ASSERT(Packet < &RecvPackets[RECV_PACKET_COUNT]);
	TRAP_ASSERT(Packet->InFlight == 0);

	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
		return;

	TRAP_ASSERT((Flags & USBENET_STATE_CALLBACK_IN_PROGRESS) == 0);

	ULONG Index = static_cast<ULONG>(Packet - &RecvPackets[0]);
	ULONG BufferOffset = Index * ReceiveBufferSize;

	TRAP_ASSERT(Packet->Transfer.Request.Context == this);
	TRAP_ASSERT(Packet->Transfer.Request.CompletionRoutine == (PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineBulkRecv);
	TRAP_ASSERT(Packet->Transfer.Request.EndpointHandle == ReceiveEndpoint);
	TRAP_ASSERT(Packet->Transfer.Request.TransferFlags != 0);
	TRAP_ASSERT(PhysicalMemory != NULL);
	TRAP_ASSERT(Packet->Transfer.Buffer == static_cast<PUCHAR>(PhysicalMemory) + BufferOffset);
	TRAP_IRQL(DISPATCH_LEVEL);

	Packet->Transfer.BufferLength = ReceiveBufferSize;
	Packet->Transfer.BytesTransferred = 0;
	Packet->InFlight = 1;
	++RxInFlightCount;
	++DiagnosticRxSubmitCount;
	++DiagnosticRxSubmitSequence;
	UsbdQueueAsyncTransfer(DeviceNode, &Packet->Transfer);

	if (ShouldLogDiagnosticEvent(DiagnosticRxSubmitSequence))
		DbgPrint("[usbenet]: RX submit #%u packet=%u inFlight=%u endpoint=%p buffer=%p length=%u\n", DiagnosticRxSubmitSequence, Index, RxInFlightCount, ReceiveEndpoint, Packet->Transfer.Buffer, Packet->Transfer.BufferLength);
}

VOID __fastcall CUsbEnet::SubmitReceive() {
	USBENET_LOG("SubmitReceive this=%p", this);

	if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0)
		return;

	for (ULONG Index = 0; Index < RECV_PACKET_COUNT; Index++) {
		PRECV_TRANSFER Packet = &RecvPackets[Index];

		if (Packet->InFlight == 0)
			SubmitReceivePacket(Packet);
	}
}

VOID __fastcall CUsbEnet::AsyncCompletionRoutineStartReceiving(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
	USBENET_LOG("AsyncCompletionRoutineStartReceiving request=%p status=0x%08X", Request, Status);
	CUsbEnet* Context = static_cast<CUsbEnet*>(Request->Context);
	TRAP_ASSERT(Context != NULL);
	Context->CompleteStartReceiving(Status);
}

VOID __fastcall CUsbEnet::AsyncCompletionRoutineBulkXmit(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
	USBENET_LOG("AsyncCompletionRoutineBulkXmit request=%p status=0x%08X", Request, Status);
	CUsbEnet* Context = static_cast<CUsbEnet*>(Request->Context);
	TRAP_ASSERT(Context != NULL);
	Context->CompleteTransmit(reinterpret_cast<PXMIT_TRACKER>(Request), Status);
}

VOID __fastcall CUsbEnet::DpcBulkXmitSwitchProcsRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
	USBENET_LOG("DpcBulkXmitSwitchProcsRoutine dpc=%p deferredContext=%p systemArgument1=%p systemArgument2=%p", Dpc, DeferredContext, SystemArgument1, SystemArgument2);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	CUsbEnet* Context = static_cast<CUsbEnet*>(DeferredContext);
	TRAP_ASSERT(Context != NULL);
	Context->BulkXmitSwitchProcs(CONTAINING_RECORD(Dpc, XMIT_TRACKER, CompletionDpc));
}

VOID __fastcall CUsbEnet::BeginPowerDownAndClose() {
	USBENET_LOG("BeginPowerDownAndClose this=%p attached=%ld flags=0x%08X inFlight=%u pendingXmit=%u", this, DeviceAttached, Flags, RxInFlightCount, PendingXmitCount);

	if (DeviceAttached == 0)
		return;

	NicBaseTakeLock(this);

	if ((Flags & (USBENET_STATE_STOPPING | USBENET_STATE_RESETTING)) == (USBENET_STATE_STOPPING | USBENET_STATE_RESETTING)) {
		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);
		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);
		return;
	}

	/* An earlier notification may have quiesced submissions by setting STOPPING.
	 * Continue into the close sequence unless RESETTING shows that endpoint
	 * closure is already in progress. */
	Flags |= USBENET_STATE_STOPPING;
	Flags &= ~(ReceiveRunning | USBENET_STATE_LINK_STATE_UPDATE_PENDING);
	NicBaseShutdown(this);
	LinkState = NIC_LINK_STATE_NEGOTIATION_COMPLETE;

	for (PLIST_ENTRY Entry = m_UserList.Flink; Entry != &m_UserList; Entry = Entry->Flink) {
		PUSB_ENET_USER_ENTRY UserEntry = CONTAINING_RECORD(Entry, USB_ENET_USER_ENTRY, Link);
		UserEntry->User->NotifyLinkStateChanged();
	}

	DWORD CancelledReceiveCount = 0;
	DWORD CancelledTransmitCount = 0;

	if (InterlockedCompareExchange(&g_UsbEnetInterruptInFlight, 0, 0) != 0)
		UsbdCancelAsyncTransfer(&g_UsbEnetInterruptTransfer);

	for (DWORD Index = 0; Index < RECV_PACKET_COUNT; Index++) {
		PRECV_TRANSFER Packet = &RecvPackets[Index];

		if (Packet->InFlight == 0)
			continue;

		UsbdCancelAsyncTransfer(&Packet->Transfer);
		++CancelledReceiveCount;
	}

	for (DWORD Index = 0; Index < XMIT_TRACKER_COUNT; Index++) {
		PXMIT_TRACKER Tracker = &XmitTrackers[Index];

		if ((Tracker->Flags & XMIT_FLAG_BUSY) == 0)
			continue;

		UsbdCancelAsyncTransfer(&Tracker->Transfer);
		++CancelledTransmitCount;
	}

	DbgPrint("[usbenet]: Power-down close started: RX inFlight=%u cancelled=%u TX pending=%u cancelled=%u flags=0x%08X.\n", RxInFlightCount, CancelledReceiveCount, PendingXmitCount, CancelledTransmitCount, Flags);

	ContinueAsyncCloseAndDropLock();
}

BOOL __fastcall CUsbEnet::IsPowerDownComplete() {
	return DeviceAttached == 0;
}

DWORD __fastcall CUsbEnet::GetReceiveInFlightCount() {
	return RxInFlightCount;
}

VOID __fastcall CUsbEnet::BeginRemove() {
	USBENET_LOG("BeginRemove this=%p", this);

	NicBaseTakeLock(this);

	if (Flags & USBENET_STATE_STOPPING) {
		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);

		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);
		return;
	}

	Flags |= USBENET_STATE_STOPPING;

	NicBaseShutdown(this);

	LinkState = 0x10000;

	TRAP_THREAD(LockOwnerThread);

	PLIST_ENTRY Entry = m_UserList.Flink;
	while (Entry != &m_UserList) {
		PUSB_ENET_USER_ENTRY UserEntry = CONTAINING_RECORD(Entry, USB_ENET_USER_ENTRY, Link);
		CNicUser* User = UserEntry->User;
		User->NotifyLinkStateChanged();
		Entry = Entry->Flink;
	}

	ContinueAsyncCloseAndDropLock();
}

VOID __fastcall CUsbEnet::DriverEntry() {
	USBENET_LOG("DriverEntry this=%p", this);

	ZeroMemory(&g_UsbEnet, sizeof(CUsbEnet));
	g_UsbEnetInterruptEndpoint = NULL;
	ZeroMemory(&g_UsbEnetInterruptTransfer, sizeof(g_UsbEnetInterruptTransfer));
	g_UsbEnetInterruptInFlight = 0;
	g_UsbEnetInterruptPaused = 0;
	g_UsbEnetInterruptSequence = 0;
	g_UsbEnetInterruptErrorCount = 0;
	g_UsbEnetInterruptLastCompletionStatus = STATUS_SUCCESS;
	g_UsbEnetInterruptLastCompletionStatusValid = FALSE;
	ZeroMemory(g_UsbEnetUserAttachLogState, sizeof(g_UsbEnetUserAttachLogState));

	TRAP_ASSERT(DeviceAttached == 0);
	TRAP_ASSERT(Flags == 0);

	NicBaseInitialize(this);

	InitializeListHead(&m_UserList);
	m_UserListLock = 0;

	ControlEvent.Header.Type = 1;
	ControlEvent.Header.SignalState = 0;
	InitializeListHead(&ControlEvent.Header.WaitListHead);

	NicBaseTimerStart(this, (PKDEFERRED_ROUTINE)NicTimerWaitForDeviceAddDpc, this, 750);
}

VOID __fastcall CUsbEnet::AsyncCompletionRoutineClose(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
	USBENET_LOG("AsyncCompletionRoutineClose request=%p status=0x%08X", Request, Status);
	UNREFERENCED_PARAMETER(Status);
	CUsbEnet* Context = static_cast<CUsbEnet*>(Request->Context);
	TRAP_ASSERT(Context != NULL);
	NicBaseTakeLock(Context);
	Context->ContinueAsyncCloseAndDropLock();
}

VOID __fastcall CUsbEnet::StartReceiving() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->StartReceiving(this);
}

VOID __fastcall CUsbEnet::CompleteStopReceiving(NTSTATUS Status) {
	USBENET_LOG("CompleteStopReceiving this=%p status=0x%08X", this, Status);
	NicBaseTakeLock(this);
	CompleteControlTransfer();

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Attempting to stop receiving failed with status 0x%08x!  Continuing.\n", Status);
	}

	Flags &= ~USBENET_STATE_00080000;

	StartReceiving();

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

VOID __fastcall CUsbEnet::WriteNodeId() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->WriteNodeId(this);
}

VOID __fastcall CUsbEnet::BeginRemoveDeviceExtension(PVOID DeviceExtension) {
	USBENET_LOG("BeginRemoveDeviceExtension deviceExtension=%p", DeviceExtension);
	static_cast<CUsbEnet*>(DeviceExtension)->BeginRemove();
}

VOID __fastcall CUsbEnet::UpdateRecvFilter() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->UpdateReceiveFilter(this);
}

VOID __fastcall CUsbEnet::AsyncCompletionRoutineStopReceiving(PUSBD_ASYNC_REQUEST Request, NTSTATUS Status) {
	USBENET_LOG("AsyncCompletionRoutineStopReceiving request=%p status=0x%08X", Request, Status);
	CUsbEnet* Context = static_cast<CUsbEnet*>(Request->Context);

	TRAP_ASSERT(Context != NULL);

	Context->CompleteStopReceiving(Status);
}

VOID __fastcall CUsbEnet::NicAttachUser(CNicUser* User) {
	USBENET_LOG("NicAttachUser this=%p user=%p", this, User);

	if (User == NULL || !MmIsAddressValid(User) || !MmIsAddressValid(reinterpret_cast<PBYTE>(User) + sizeof(CNicUser) - 1)) {
		DbgPrint("[CUsbEnet] Rejecting invalid CNicUser pointer during attach: user=%p.\n", User);
		return;
	}

	DWORD ReceiveFilter = User->OriginalAttachInfo.ReceiveFilterMask;
	if (ReceiveFilter != XNET_STANDARD_RECEIVE_FILTER && ReceiveFilter != XNET_XBDM_RECEIVE_FILTER)
		ReceiveFilter = User->AttachInfo.ReceiveFilterMask;

	if ((ReceiveFilter != XNET_STANDARD_RECEIVE_FILTER && ReceiveFilter != XNET_XBDM_RECEIVE_FILTER) || User->AttachInfo.ReceiveCallback == NULL || User->AttachInfo.LinkStateCallback == NULL || !MmIsAddressValid(reinterpret_cast<PVOID>(User->AttachInfo.ReceiveCallback)) || !MmIsAddressValid(reinterpret_cast<PVOID>(User->AttachInfo.LinkStateCallback))) {
		DbgPrint("[CUsbEnet] Rejecting malformed CNicUser during attach: user=%p flags=0x%08X filter=0x%08X receive=%p link=%p.\n", User, User->AttachInfo.DeviceFlags, ReceiveFilter, User->AttachInfo.ReceiveCallback, User->AttachInfo.LinkStateCallback);
		return;
	}

	USBENET_CONN("attach this=%p user=%p mask=0x%08X callback=%p flags=0x%08X link=0x%08X irql=%u cpu=%u", this, User, ReceiveFilter, User->AttachInfo.LinkStateCallback, Flags, LinkState, KeGetCurrentIrql(), GetCurrentProcessorNumber());

	/* Allocation and user-list attachment are PASSIVE_LEVEL operations. More
	 * importantly, an XNet callback can re-enter NicAttach while the receive
	 * callback flag is set. Waiting or retiring DPCs from that path recursively
	 * enters the dispatcher and eventually switches/overflows the DPC stack. */
	if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
		DbgPrint("[CUsbEnet] Deferring CNicUser attach from IRQL %u user=%p.\n", KeGetCurrentIrql(), User);
		return;
	}

	PUSB_ENET_USER_ENTRY NewEntry = static_cast<PUSB_ENET_USER_ENTRY>(ExAllocatePoolWithTag(sizeof(USB_ENET_USER_ENTRY), 'tenU'));

	USBENET_LOG("NicAttachUser allocation user=%p entry=%p size=0x%X", User, NewEntry, sizeof(USB_ENET_USER_ENTRY));

	if (NewEntry == NULL) {
		USBENET_LOG("NicAttachUser allocation failed user=%p", User);
		return;
	}

	NewEntry->User = User;
	NewEntry->Link.Flink = NULL;
	NewEntry->Link.Blink = NULL;

	NicBaseTakeLock(this);

	/* Do not wait for a callback from an attach operation. The attach may have
	 * been requested reentrantly by that callback, so waiting would deadlock;
	 * KeRetireDpcList was even worse because it recursively entered the DPC
	 * dispatcher. A later PASSIVE_LEVEL hook will retry lazy attachment. */
	if ((Flags & (USBENET_STATE_CALLBACK_IN_PROGRESS | USBENET_STATE_00100000)) != 0) {
		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);
		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);
		ExFreePool(NewEntry);
		USBENET_LOG("NicAttachUser deferred while callback active user=%p flags=0x%08X", User, Flags);
		return;
	}

	BOOL AlreadyAttached = FALSE;
	KIRQL UserListIrql = KfAcquireSpinLock(&m_UserListLock);

	for (PLIST_ENTRY Link = m_UserList.Flink; Link != &m_UserList; Link = Link->Flink) {
		PUSB_ENET_USER_ENTRY Entry = CONTAINING_RECORD(Link, USB_ENET_USER_ENTRY, Link);

		if (Entry->User == User) {
			AlreadyAttached = TRUE;
			break;
		}
	}

	if (!AlreadyAttached)
		InsertTailList(&m_UserList, &NewEntry->Link);

	KfReleaseSpinLock(&m_UserListLock, UserListIrql);

	if (AlreadyAttached) {
		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);

		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);
		ExFreePool(NewEntry);

		USBENET_LOG("NicAttachUser already attached user=%p linkState=0x%08X", User, LinkState);

		if ((LinkState & NIC_LINK_STATE_ACTIVE) != 0) {
			USBENET_LOG("Notifying already attached active user=%p linkState=0x%08X", User, LinkState);
			User->NotifyLinkStateChanged();
		}

		return;
	}
	User->AttachInfo.DeviceFlags |= NicInterfaceUsbEnet;
	User->OriginalAttachInfo.DeviceFlags |= NicInterfaceUsbEnet;

	DWORD UserReceiveDestinationMask = User->AttachInfo.ReceiveFilterMask;

	if ((AggregateReceiveFilter & UserReceiveDestinationMask) != UserReceiveDestinationMask) {
		AggregateReceiveFilter |= UserReceiveDestinationMask;
		UpdateRecvFilter();
	}

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	USBENET_LOG("NicAttachUser attached user=%p deviceFlags=0x%08X receiveMask=0x%08X linkState=0x%08X", User, User->AttachInfo.DeviceFlags, User->AttachInfo.ReceiveFilterMask, LinkState);

	if ((LinkState & NIC_LINK_STATE_ACTIVE) != 0) {
		USBENET_LOG("Notifying newly attached active user=%p linkState=0x%08X", User, LinkState);
		User->NotifyLinkStateChanged();
	}
}

VOID __fastcall CUsbEnet::NotifyLinkStateChangedToUsers() {
	USBENET_CONN("link-notify enter this=%p flags=0x%08X link=0x%08X attached=%ld stage=%u irql=%u cpu=%u", this, Flags, LinkState, DeviceAttached, InitStage, KeGetCurrentIrql(), GetCurrentProcessorNumber());
	for (;;) {
		CNicUser* Users[16];
		DWORD UserCount = 0;
		NicBaseTakeLock(this);

		if ((Flags & (USBENET_STATE_RESETTING | USBENET_STATE_STOPPING)) != 0) {
			Flags &= ~USBENET_STATE_NOTIFY_LINK_STATE;

			TRAP_ASSERT(PreviousIrql != 0xEE);
			TRAP_THREAD(LockOwnerThread);
			NULL_OWNER_THREAD(this);
			KfReleaseSpinLock(&NicLock, PreviousIrql);
			return;
		}

		if ((Flags & (USBENET_STATE_CALLBACK_IN_PROGRESS | USBENET_STATE_00100000)) != 0) {
			Flags |= USBENET_STATE_NOTIFY_LINK_STATE;

			TRAP_ASSERT(PreviousIrql != 0xEE);
			TRAP_THREAD(LockOwnerThread);
			NULL_OWNER_THREAD(this);
			KfReleaseSpinLock(&NicLock, PreviousIrql);
			return;
		}

		Flags &= ~USBENET_STATE_NOTIFY_LINK_STATE;
		KIRQL UserListIrql = KfAcquireSpinLock(&m_UserListLock);

		for (PLIST_ENTRY Link = m_UserList.Flink; Link != &m_UserList && UserCount < ARRAYSIZE(Users); Link = Link->Flink) {
			PUSB_ENET_USER_ENTRY Entry = CONTAINING_RECORD(Link, USB_ENET_USER_ENTRY, Link);

			if (Entry->User != NULL && Entry->User->AttachInfo.LinkStateCallback != NULL) {
				USBENET_CONN("link-notify user=%p mask=0x%08X callback=%p deviceFlags=0x%08X", Entry->User, Entry->User->AttachInfo.ReceiveFilterMask, Entry->User->AttachInfo.LinkStateCallback, Entry->User->AttachInfo.DeviceFlags);
				Users[UserCount++] = Entry->User;
			}
		}

		KfReleaseSpinLock(&m_UserListLock, UserListIrql);

		if (UserCount == 0) {
			TRAP_ASSERT(PreviousIrql != 0xEE);
			TRAP_THREAD(LockOwnerThread);
			NULL_OWNER_THREAD(this);
			KfReleaseSpinLock(&NicLock, PreviousIrql);
			return;
		}

		Flags |= USBENET_STATE_00100000;

		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);
		NULL_OWNER_THREAD(this);
		KIRQL CallbackIrql = PreviousIrql;
		KfReleaseSpinLock(&NicLock, CallbackIrql);

		for (DWORD Index = 0; Index < UserCount; Index++)
			Users[Index]->NotifyLinkStateChanged();

		NicBaseTakeLock(this);
		TRAP_ASSERT((Flags & USBENET_STATE_00100000) != 0);
		Flags &= ~USBENET_STATE_00100000;
		BOOL RepeatNotification = (Flags & USBENET_STATE_NOTIFY_LINK_STATE) != 0;

		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);
		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);

		if (!RepeatNotification)
			return;
	}
}

BOOL __fastcall CUsbEnet::NicDetachUser(CNicUser* User) {
	USBENET_LOG("NicDetachUser this=%p user=%p", this, User);
	USBENET_CONN("detach this=%p user=%p mask=0x%08X callback=%p flags=0x%08X link=0x%08X irql=%u cpu=%u", this, User, User != NULL ? User->AttachInfo.ReceiveFilterMask : 0, User != NULL ? User->AttachInfo.LinkStateCallback : NULL, Flags, LinkState, KeGetCurrentIrql(), GetCurrentProcessorNumber());
	if (User == NULL)
		return TRUE;

	PUSB_ENET_USER_ENTRY RemovedEntry = NULL;

	NicBaseTakeLock(this);

	while ((Flags & (USBENET_STATE_CALLBACK_IN_PROGRESS | USBENET_STATE_00100000)) != 0) {
		TRAP_ASSERT(PreviousIrql != 0xEE);
		TRAP_THREAD(LockOwnerThread);
		NULL_OWNER_THREAD(this);
		KfReleaseSpinLock(&NicLock, PreviousIrql);

		/* Detach normally arrives at PASSIVE_LEVEL. Never recurse into the DPC
		 * dispatcher if an unexpected elevated-IRQL detach occurs. */
		if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
			DbgPrint("[CUsbEnet] Deferring CNicUser detach from IRQL %u user=%p while callback is active.\n", KeGetCurrentIrql(), User);
			return FALSE;
		}

		LARGE_INTEGER DelayInterval;
		DelayInterval.QuadPart = -10000;
		KeDelayExecutionThread(PROC_USER, FALSE, &DelayInterval);
		NicBaseTakeLock(this);
	}

	DWORD ReceiveMask = 0;
	KIRQL UserListIrql = KfAcquireSpinLock(&m_UserListLock);

	for (PLIST_ENTRY Link = m_UserList.Flink; Link != &m_UserList; Link = Link->Flink) {
		PUSB_ENET_USER_ENTRY Entry = CONTAINING_RECORD(Link, USB_ENET_USER_ENTRY, Link);

		if (Entry->User == User) {
			RemoveEntryList(&Entry->Link);
			RemovedEntry = Entry;
			break;
		}
	}

	for (PLIST_ENTRY Link = m_UserList.Flink; Link != &m_UserList; Link = Link->Flink) {
		PUSB_ENET_USER_ENTRY Entry = CONTAINING_RECORD(Link, USB_ENET_USER_ENTRY, Link);
		ReceiveMask |= Entry->User->AttachInfo.ReceiveFilterMask;
	}

	KfReleaseSpinLock(&m_UserListLock, UserListIrql);

	if (AggregateReceiveFilter != ReceiveMask) {
		AggregateReceiveFilter = ReceiveMask;
		UpdateRecvFilter();
	}

	if (RemovedEntry != NULL) {
		User->AttachInfo.DeviceFlags &= ~NicInterfaceUsbEnet;
		NicFlushXmitQueue(User);
	}

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	if (RemovedEntry != NULL)
		ExFreePool(RemovedEntry);

	return TRUE;
}

VOID __fastcall CUsbEnet::StopAndRestartReceiving() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->RestartReceiving(this);
}

NTSTATUS __fastcall CUsbEnet::NicDoSetOpt(CNicUser* User, DWORD Option, const PBYTE OptionValue, DWORD OptionLength, PDWORD Result) {
	USBENET_CONN("setopt user=%p option=0x%08X length=%u value=0x%08X flags=0x%08X link=0x%08X", User, Option, OptionLength, OptionValue != NULL && OptionLength >= sizeof(DWORD) ? *reinterpret_cast<const DWORD*>(OptionValue) : 0, Flags, LinkState);
	USBENET_LOG("NicDoSetOpt this=%p user=%p option=0x%08X optionValue=%p optionLength=%u result=%p", this, User, Option, OptionValue, OptionLength, Result);
	UNREFERENCED_PARAMETER(User);

	NTSTATUS Status;
	DWORD Value;
	USHORT Register;

	NicBaseTakeLock(this);

	if (Option < 0x2EE0 || Option > 0x2EFF) {
		Status = STATUS_NOT_IMPLEMENTED;
		*Result = 1;
		goto Exit;
	}

	if (OptionLength < sizeof(DWORD)) {
		*Result = 0;
		Status = STATUS_INVALID_PARAMETER;
		goto Exit;
	}

	if (!DeviceAttached) {
		Status = STATUS_DEVICE_DOES_NOT_EXIST;
		goto Exit;
	}

	Value = *reinterpret_cast<const DWORD*>(OptionValue);
	Register = static_cast<USHORT>(Option - 0x2EE0);

	TRAP_ASSERT(Value <= SOL_SOCKET);

	Status = PrepareForUserControlTransfer();
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Couldn't prepare user control transfer!\n");
		goto Exit;
	}

	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	Status = g_UsbEnetChipset->BeginWritePhy(this, Register, static_cast<USHORT>(Value));
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Couldn't write PHY reg %u!\n", Register);
		AbortUserControlTransfer();
		*Result = 1;
		goto Exit;
	}

	WaitForUserControlTransferResult();
	*Result = 0;

Exit:
	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	return Status;
}

NTSTATUS __fastcall CUsbEnet::NicDoGetOpt(CNicUser* User, DWORD Option, PBYTE OptionValue, DWORD* OptionLength, PDWORD Result) {
	USBENET_CONN("getopt user=%p option=0x%08X length=%u flags=0x%08X link=0x%08X", User, Option, OptionLength != NULL ? *OptionLength : 0, Flags, LinkState);
	USBENET_LOG("NicDoGetOpt this=%p user=%p option=0x%08X optionValue=%p optionLength=%p result=%p", this, User, Option, OptionValue, OptionLength, Result);
	UNREFERENCED_PARAMETER(User);

	NTSTATUS Status;

	NicBaseTakeLock(this);

	if (Option >= 0x2EE0 && Option <= 0x2EFF) {
		if (*OptionLength < sizeof(DWORD)) {
			Status = STATUS_BUFFER_TOO_SMALL;
			*Result = 0;
			*OptionLength = sizeof(DWORD);
			goto ReleaseLock;
		}

		USHORT Register = static_cast<USHORT>(Option - 0x2EE0);

		if (DeviceAttached == 0) {
			Status = STATUS_DEVICE_DOES_NOT_EXIST;
			goto ReleaseLock;
		}

		Status = PrepareForUserControlTransfer();
		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Couldn't prepare user control transfer!\n");
			goto ReleaseLock;
		}

		TRAP_ASSERT(g_UsbEnetChipset != NULL);
		Status = g_UsbEnetChipset->BeginReadPhy(this, Register);
		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Couldn't read PHY reg %u!\n", Register);
			AbortUserControlTransfer();
			*Result = 1;
			*OptionLength = sizeof(DWORD);
			goto ReleaseLock;
		}

		WaitForUserControlTransferResult();

		TRAP_ASSERT(CurrentPhyRegister == Register);

		*reinterpret_cast<DWORD*>(OptionValue) = CurrentPhyValue;
		*Result = 0;
		*OptionLength = sizeof(DWORD);
	} else if (Option == 0x2F00) {
		if (*OptionLength < sizeof(CEnetAddr)) {
			Status = STATUS_BUFFER_TOO_SMALL;
			*Result = 0;
			*OptionLength = sizeof(DWORD);
			goto ReleaseLock;
		}

		if (DeviceAttached == 0) {
			Status = STATUS_DEVICE_DOES_NOT_EXIST;
			goto ReleaseLock;
		}

		TRAP_ASSERT(g_UsbEnetChipset != NULL);

		if (!g_UsbEnetChipset->IsNodeIdAvailable(this)) {
			Status = STATUS_PENDING;
		} else {
			memcpy(OptionValue, &NodeId, sizeof(CEnetAddr));
			Status = STATUS_SUCCESS;
		}

		*Result = 0;
		*OptionLength = sizeof(DWORD);
	} else {
		*Result = 1;
		Status = STATUS_NOT_IMPLEMENTED;
	}

ReleaseLock:
	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);

	return Status;
}

VOID __fastcall CUsbEnet::PrintThroughputStats(DWORD CurrentTick) {
	::PrintThroughputStats(CurrentTick, XmitTrackers, ActiveXmitCount, PendingXmitCount);
}

VOID __fastcall CUsbEnet::NoteReceiveRestart() {
	++RxRestartCount;
	DbgPrint("[usbenet]: RX watchdog restarting receive path after %u ms without a completion (inFlight=%u).\n", KeTimeStampBundle->TickCount - TimerTick, RxInFlightCount);
}

VOID __fastcall CUsbEnet::NicDoTimerRunning() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->RunTimer(this);
}

VOID __fastcall CUsbEnet::NicTimerRunningDpc(PKDPC Dpc, PVOID Context, PVOID SystemArgument1, PVOID SystemArgument2) {
	USBENET_LOG("NicTimerRunningDpc dpc=%p context=%p systemArgument1=%p systemArgument2=%p", Dpc, Context, SystemArgument1, SystemArgument2);
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	static_cast<CUsbEnet*>(Context)->NicDoTimerRunning();
}

VOID __fastcall CUsbEnet::AdvanceInitStage() {
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->AdvanceInitStage(this);
}

VOID __fastcall CUsbEnet::Init(PUSBD_DEVICE_NODE DeviceNode, const PUSB_ENDPOINT_DESCRIPTOR InterruptEndpointDescriptor, const PUSB_ENDPOINT_DESCRIPTOR ReceiveEndpointDescriptor, const PUSB_ENDPOINT_DESCRIPTOR TransmitEndpointDescriptor) {
	USBENET_LOG("Init this=%p deviceNode=%p interruptEndpointDescriptor=%p receiveEndpointDescriptor=%p transmitEndpointDescriptor=%p", this, DeviceNode, InterruptEndpointDescriptor, ReceiveEndpointDescriptor, TransmitEndpointDescriptor);

	NicBaseTakeLock(this);
	NicBaseShutdown(this);

	this->DeviceNode = DeviceNode;
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	InitStage = 0;
	DbgPrint("[usbenet]: Selected chipset backend: %s.\n", g_UsbEnetChipset->GetName());

	USHORT ReceiveMaxPacketSize = _byteswap_ushort(ReceiveEndpointDescriptor->wMaxPacketSize) & 0x07FF;
	TransmitMaxPacketSize = _byteswap_ushort(TransmitEndpointDescriptor->wMaxPacketSize) & 0x07FF;

	if (ReceiveMaxPacketSize == 0x200 && TransmitMaxPacketSize == 0x200) {
		DbgPrint("[usbenet]: USB bus mode: high-speed (480 Mbps), RX packet=%u, TX packet=%u\n", ReceiveMaxPacketSize, TransmitMaxPacketSize);
	} else if (ReceiveMaxPacketSize == 0x40 && TransmitMaxPacketSize == 0x40) {
		DbgPrint("[usbenet]: USB bus mode: full-speed (12 Mbps), RX packet=%u, TX packet=%u\n", ReceiveMaxPacketSize, TransmitMaxPacketSize);
	} else {
		DbgPrint("[usbenet]: USB bus mode: unknown, RX packet=%u, TX packet=%u\n", ReceiveMaxPacketSize, TransmitMaxPacketSize);
	}

	if (TransmitMaxPacketSize != 0x200) {
		DbgPrint("[usbenet]: Unexpected max packet size %u for transmit bulk transfer endpoint!  Continuing.\n", TransmitMaxPacketSize);
	}

	DefaultEndpoint = NULL;
	ReceiveEndpoint = NULL;
	TransmitEndpoint = NULL;
	g_UsbEnetInterruptEndpoint = NULL;
	ZeroMemory(&g_UsbEnetInterruptTransfer, sizeof(g_UsbEnetInterruptTransfer));
	g_UsbEnetInterruptInFlight = 0;
	g_UsbEnetInterruptPaused = 0;
	g_UsbEnetInterruptSequence = 0;
	g_UsbEnetInterruptErrorCount = 0;
	g_UsbEnetInterruptLastCompletionStatusValid = FALSE;
	g_UsbEnetBulkRecvLastCompletionStatus = STATUS_SUCCESS;
	g_UsbEnetBulkRecvLastCompletionStatusValid = FALSE;
	ReceiveEndpointAddress = ReceiveEndpointDescriptor->bEndpointAddress;

	memset(&CloseRequest, 0, sizeof(CloseRequest));
	CloseRequest.Context = this;
	CloseRequest.CompletionRoutine = (PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineClose;

	KeInitializeDpc(&ControlDpc, DpcControlSwitchProcsRoutine, this);
	ControlDpc.TargetNumber = 3;

	memset(&ControlRequest, 0, sizeof(ControlRequest));

	PhyAddress = 0;
	memset(PhyRegisters, 0, sizeof(PhyRegisters));

	LinkPollTick = KeTimeStampBundle->TickCount;
	LinkState = 0;

	memset(XmitPackets, 0, sizeof(XmitPackets));
	memset(XmitTrackers, 0, sizeof(XmitTrackers));

	NextXmitPacket = XmitPackets;
	NextXmitTracker = XmitTrackers;
	ActiveXmitCount = 0;
	PendingXmitCount = 0;
	ThroughputStatsTick = KeTimeStampBundle->TickCount;
	TxUsbTransferCount = 0;
	TxUsbBytes = 0;
	TxFrameCount = 0;
	TxFrameBytes = 0;
	TxTitleFrameCount = 0;
	TxDebugFrameCount = 0;
	TxPacketInUseCount = 0;
	TxPacketHighWater = 0;
	TxPacketDropCount = 0;
	TxTrackerHighWater = 0;
	TxBufferDropCount = 0;
	DiagnosticStatsPeriodsRemaining = 20;
	DiagnosticControlQueueCount = 0;
	DiagnosticTxSubmitCount = 0;
	DiagnosticTxSubmitSequence = 0;
	DiagnosticTxCompleteCount = 0;
	DiagnosticTxCompleteSequence = 0;
	DiagnosticTxErrorCount = 0;
	DiagnosticTxLastStatus = STATUS_SUCCESS;
#if USBENET_PERF_LOGGING
	TxDispatchEntryCount = 0;
	TxOtherIrqlEntryCount = 0;
	TxDirectSubmitCount = 0;
	TxDirectPipeIdleSubmitCount = 0;
	TxDispatchWaitCount = 0;
	TxDispatchWrongProcessorCount = 0;
	TxDpcQueueCount = 0;
	TxDpcRunCount = 0;
	TxCompletionRefillCount = 0;
	TxCompletionRefillTransferCount = 0;
	TxPendingHighWater = PendingXmitCount;
#endif
	DiagnosticRxSubmitCount = 0;
	DiagnosticRxSubmitSequence = 0;
	DiagnosticRxSubmitErrorCount = 0;
	DiagnosticRxCompleteCount = 0;
	DiagnosticRxCompleteSequence = 0;
	DiagnosticRxErrorCount = 0;
	DiagnosticRxZeroLengthCount = 0;
	DiagnosticRxParserFailureCount = 0;
	TRAP_ASSERT(g_UsbEnetChipset != NULL);
	g_UsbEnetChipset->ResetState(this);
	RxUsbCompletionCount = 0;
	RxUsbBytes = 0;
	RxFrameCount = 0;
	RxFrameBytes = 0;
	RxParsedFrameCount = 0;
	RxInvalidFrameCount = 0;
	RxMulticastFilteredCount = 0;
	RxTitleUnicastFrameCount = 0;
	RxDebugUnicastFrameCount = 0;
	RxMulticastFrameCount = 0;
	RxPromiscuousFrameCount = 0;
	RxBroadcastFrameCount = 0;
	RxTitleDeliveredCount = 0;
	RxDebugDeliveredCount = 0;
	RxNoDeliveryFrameCount = 0;

	memset(RecvPackets, 0, sizeof(RecvPackets));

	TimerTick = KeTimeStampBundle->TickCount;
	NodeId.SetZero();
	Flags = 0;

	NTSTATUS Status = UsbdOpenDefaultEndpoint(DeviceNode, &DefaultEndpoint);

	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Couldn't open default control endpoint (err = 0x%08x)!\n", Status);
		ContinueAsyncCloseAndDropLock();
		return;
	}

	if (g_UsbEnetChipset->UsesInterruptLinkStatus()) {
		USHORT InterruptMaxPacketSize = _byteswap_ushort(InterruptEndpointDescriptor->wMaxPacketSize) & 0x07FF;
		Status = UsbdOpenEndpoint(DeviceNode, UsbTransferInterrupt, InterruptEndpointDescriptor->bEndpointAddress, InterruptMaxPacketSize, InterruptEndpointDescriptor->bInterval, &g_UsbEnetInterruptEndpoint);

		if (!NT_SUCCESS(Status)) {
			DbgPrint("[usbenet]: Couldn't open interrupt status endpoint (err = 0x%08X)!\n", Status);
			ContinueAsyncCloseAndDropLock();
			return;
		}

		DbgPrint("[usbenet]: Interrupt link endpoint opened: address=0x%02X packet=%u interval=%u handle=%p.\n", InterruptEndpointDescriptor->bEndpointAddress, InterruptMaxPacketSize, InterruptEndpointDescriptor->bInterval, g_UsbEnetInterruptEndpoint);
	}

	Status = UsbdOpenEndpoint(DeviceNode, UsbTransferBulk, ReceiveEndpointDescriptor->bEndpointAddress, ReceiveMaxPacketSize, 0, &ReceiveEndpoint);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Couldn't open receive bulk endpoint (err = 0x%08x)!\n", Status);
		ContinueAsyncCloseAndDropLock();
		return;
	}

	Status = UsbdOpenEndpoint(DeviceNode, UsbTransferBulk, TransmitEndpointDescriptor->bEndpointAddress, TransmitMaxPacketSize, 0, &TransmitEndpoint);
	if (!NT_SUCCESS(Status)) {
		DbgPrint("[usbenet]: Couldn't open transmit bulk endpoint (err = 0x%08x)!\n", Status);
		ContinueAsyncCloseAndDropLock();
		return;
	}

	TRAP_ASSERT(PhysicalMemory == NULL);
	PhysicalMemory = MmAllocatePhysicalMemory(2, PhysicalBufferSize, 4);
	if (PhysicalMemory == NULL) {
		DbgPrint("[usbenet]: Couldn't allocate physical memory for transfer buffers!\n");
		ContinueAsyncCloseAndDropLock();
		return;
	}

	memset(PhysicalMemory, 0, PhysicalBufferSize);
	TRAP_ASSERT(PhysicalMemory != NULL);

	BYTE* PhysicalBuffer = static_cast<BYTE*>(PhysicalMemory);

	if (g_UsbEnetInterruptEndpoint != NULL) {
		g_UsbEnetInterruptTransfer.Request.Context = this;
		g_UsbEnetInterruptTransfer.Request.CompletionRoutine = (PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineInterruptStatus;
		g_UsbEnetInterruptTransfer.Request.EndpointHandle = g_UsbEnetInterruptEndpoint;
		g_UsbEnetInterruptTransfer.Request.TransferFlags = 1;
		g_UsbEnetInterruptTransfer.Buffer = PhysicalBuffer + InterruptStatusBufferOffset;
		g_UsbEnetInterruptTransfer.BufferLength = InterruptStatusBufferSize;
	}

	DbgPrint("[usbenet]: Buffer layout: RX=%u x %u bytes, TX=%u x %u bytes, TX aggregate=%u bytes, submit=%u bytes/%u title frames, pending=%u titlePending=%u, XBDM <=%u bytes immediate/>%u bytes aggregate up to %u frames, DMA=%u bytes\n", RECV_PACKET_COUNT, ReceiveBufferSize, XMIT_BUFFER_COUNT, XmitBufferSize, MaximumAggregateSize, XmitSubmitThreshold, MaximumFramesPerXmit, MaximumPendingXmitCount, MaximumTitlePendingXmitCount, DebugImmediateFrameSize, DebugImmediateFrameSize, MaximumDebugFramesPerXmit, DmaBufferSize);

	for (ULONG Index = 0; Index < XMIT_BUFFER_COUNT; Index++) {
		ULONG BufferOffset = XmitBufferOffset + Index * XmitBufferSize;
		TRAP_ASSERT(BufferOffset + XmitBufferSize <= DmaBufferSize);
		XMIT_TRACKER& Tracker = XmitTrackers[Index];

		Tracker.Transfer.Request.Context = this;
		Tracker.Transfer.Request.CompletionRoutine = (PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineBulkXmit;
		Tracker.Transfer.Request.EndpointHandle = TransmitEndpoint;

		TRAP_ASSERT(Tracker.Transfer.BytesTransferred == 0);

		Tracker.Transfer.Buffer = PhysicalBuffer + BufferOffset;

		KeInitializeDpc(&Tracker.CompletionDpc, DpcBulkXmitSwitchProcsRoutine, this);
		Tracker.CompletionDpc.TargetNumber = 3;

		TRAP_ASSERT(Tracker.FirstPacket == 0);
		TRAP_ASSERT((Tracker.Flags & XMIT_FLAG_WAITING) == 0);
		TRAP_ASSERT((Tracker.Flags & 0x80000000) == 0);
		TRAP_ASSERT((Tracker.Flags & 0x40000000) == 0);
		TRAP_ASSERT((Tracker.Flags & 0x20000000) == 0);
	}

	for (ULONG Index = 0; Index < RECV_PACKET_COUNT; Index++) {
		ULONG BufferOffset = Index * ReceiveBufferSize;
		RECV_TRANSFER& Packet = RecvPackets[Index];

		Packet.Transfer.Request.Context = this;
		Packet.Transfer.Request.CompletionRoutine = (PUSBD_ASYNC_COMPLETION_ROUTINE)AsyncCompletionRoutineBulkRecv;
		Packet.Transfer.Request.EndpointHandle = ReceiveEndpoint;
		Packet.Transfer.Request.TransferFlags = 1;
		Packet.Transfer.Buffer = PhysicalBuffer + BufferOffset;
		Packet.Transfer.BufferLength = ReceiveBufferSize;
	}

	AdvanceInitStage();

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

VOID __fastcall CUsbEnet::NicSetUnicastAddress(CEnetAddr* Address, DWORD AddressSlot) {
	const BYTE* AddressBytes = reinterpret_cast<const BYTE*>(Address);
	USBENET_CONN("set-address slot=%u address=%02X:%02X:%02X:%02X:%02X:%02X flags=0x%08X link=0x%08X", AddressSlot, AddressBytes[0], AddressBytes[1], AddressBytes[2], AddressBytes[3], AddressBytes[4], AddressBytes[5], Flags, LinkState);
	USBENET_LOG("NicSetUnicastAddress this=%p address=%p addressSlot=0x%08X", this, Address, AddressSlot);
	TRAP_ASSERT(!Address->IsMulticast() && !Address->IsZero());

	NicBaseTakeLock(this);

	CEnetAddr* TargetAddress;

	if (AddressSlot != 0)
		TargetAddress = &UnicastAddress;
	else
		TargetAddress = &AlternateUnicastAddress;

	if (!TargetAddress->IsEqual(*Address)) {
		memcpy(TargetAddress, Address, sizeof(CEnetAddr));
		TRAP_ASSERT(g_UsbEnetChipset != NULL);
		g_UsbEnetChipset->OnUnicastAddressChanged(this);
	}

	TRAP_ASSERT(PreviousIrql != 0xEE);
	TRAP_THREAD(LockOwnerThread);

	NULL_OWNER_THREAD(this);
	KfReleaseSpinLock(&NicLock, PreviousIrql);
}

VOID __fastcall CUsbEnet::InitDeviceExtension(CUsbEnet* DeviceExtension, PUSBD_DEVICE_NODE DeviceNode, const PUSB_ENDPOINT_DESCRIPTOR DefaultEndpoint, const PUSB_ENDPOINT_DESCRIPTOR ReceiveEndpoint, const PUSB_ENDPOINT_DESCRIPTOR TransmitEndpoint) {
	USBENET_LOG("InitDeviceExtension deviceExtension=%p deviceNode=%p defaultEndpoint=%p receiveEndpoint=%p transmitEndpoint=%p", DeviceExtension, DeviceNode, DefaultEndpoint, ReceiveEndpoint, TransmitEndpoint);
	DeviceExtension->Init(DeviceNode, DefaultEndpoint, ReceiveEndpoint, TransmitEndpoint);
}

BOOL CUsbEnet::IsUserAttached(CNicUser* User) {
	if (User == NULL)
		return FALSE;

	BOOL Attached = FALSE;
	BOOL LogStateChange = FALSE;
	DWORD LogSlot = UsbEnetUserAttachLogSlotCount;
	KIRQL OldIrql = KfAcquireSpinLock(&m_UserListLock);

	for (PLIST_ENTRY Link = m_UserList.Flink; Link != &m_UserList; Link = Link->Flink) {
		PUSB_ENET_USER_ENTRY Entry = CONTAINING_RECORD(Link, USB_ENET_USER_ENTRY, Link);

		if (Entry->User == User) {
			Attached = TRUE;
			break;
		}
	}

	for (DWORD Index = 0; Index < UsbEnetUserAttachLogSlotCount; Index++) {
		if (g_UsbEnetUserAttachLogState[Index].Valid && g_UsbEnetUserAttachLogState[Index].User == User) {
			LogSlot = Index;
			break;
		}

		if (LogSlot == UsbEnetUserAttachLogSlotCount && !g_UsbEnetUserAttachLogState[Index].Valid)
			LogSlot = Index;
	}

	if (LogSlot < UsbEnetUserAttachLogSlotCount) {
		USBENET_USER_ATTACH_LOG_STATE& State = g_UsbEnetUserAttachLogState[LogSlot];
		LogStateChange = !State.Valid || State.Attached != Attached;
		State.User = User;
		State.Attached = Attached;
		State.Valid = TRUE;
	}

	KfReleaseSpinLock(&m_UserListLock, OldIrql);

	if (LogStateChange)
		USBENET_LOG("IsUserAttached this=%p user=%p attached=%u", this, User, Attached);

	return Attached;
}

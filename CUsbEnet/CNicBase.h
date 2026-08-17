#pragma once
#include "stdafx.h"

#pragma pack(push, 1)
struct NIC_MCAST_ENTRY {
    WORD SubscriptionCount;
    CEnetAddr Address;
};
static_assert(sizeof(NIC_MCAST_ENTRY) == 0x08, "NIC_MCAST_ENTRY size mismatch");

typedef struct _NIC_STATS {
    QWORD TransmitBytes;                // 0x00
    QWORD ReceiveBytes[5];              // 0x08
    DWORD TransmitFrameCount;           // 0x30
    DWORD ReceiveFrameCount[5];         // 0x34
    DWORD TransmitUnderrunErrorCount;   // 0x48
    DWORD TransmitOtherErrorCount;      // 0x4C
    DWORD ReceiveOverrunErrorCount;     // 0x50
    DWORD ReceiveShortOrCollisionCount; // 0x54
    DWORD ReceiveOtherErrorCount;       // 0x58
} NIC_STATS, *PNIC_STATS;
static_assert(sizeof(NIC_STATS) == 0x5C, "NIC_STATS size mismatch");

class CNicBase
{
public:                                     //  DEV    RET
    LIST_ENTRY UserListHead;                // 0x000  0x000

    DWORD AggregateReceiveFilter;           // 0x008  0x008
    CEnetAddr UnicastAddress;		        // 0x00C  0x00C
    CEnetAddr AlternateUnicastAddress;      // 0x012  0x012

    NIC_MCAST_ENTRY MulticastTable[12];     // 0x018  0x018
    NIC_STATS Stats;                        // 0x07C  0x07C

#if DEVKIT_ONLY
    DWORD IsrInvocationCount;               // 0x0D4
    DWORD DpcInvocationCount;               // 0x0D8
#endif

    DWORD QuiesceState;                     // 0x0DC  0x0D4     0 running, 2 quiesced

#if DEVKIT_ONLY
    DWORD StatsPrintPending;                // 0x0E0
    CHAR AddressStringBuffers[2][0x12];     // 0x0E4
#endif

    KSPIN_LOCK NicLock;                     // 0x108  0x0D8
    KIRQL PreviousIrql;                     // 0x10C  0x0DC
    BYTE Reserved10D[3];                    // 0x10D  0x0DD

    KDPC TimerDpc;                          // 0x110  0x0E0
    DWORD AlignmentPadding;                 // 0x12C  0x0FC
    KTIMER Timer;                           // 0x130  0x100

#if DEVKIT_ONLY
    PKTHREAD LockOwnerThread;               // 0x158
    DWORD LastStatsPrintTick;               // 0x15C
#endif


    typedef VOID(__fastcall* fnNicBaseThisVoid)(CNicBase* This);
    typedef VOID(__fastcall* fnNicBaseThisTimerStart)(CNicBase* This, PKDEFERRED_ROUTINE Routine, PVOID DeferredContext, DWORD PeriodMilliseconds);
    typedef VOID(__fastcall* fnNicBaseThisStats)(CNicBase* This, PNIC_STATS Stats);
    typedef BOOL(__fastcall* fnNicBaseThisCEnetAddr)(CNicBase* This, CEnetAddr* Address);
    typedef VOID(__fastcall* fnNicBaseThisPrintStats)(CNicBase* This, DWORD ReceivedFrames, DWORD ReceivedBytes, DWORD TransmittedFrames, DWORD TransmittedBytes);
    static fnNicBaseThisVoid NicBaseInitialize;
    static fnNicBaseThisVoid NicBaseShutdown;
    static fnNicBaseThisTimerStart NicBaseTimerStart;
    static fnNicBaseThisStats NicBaseAddInternalStats;
    static fnNicBaseThisCEnetAddr NicBaseIsSubscribedMcastAddr;
    static fnNicBaseThisPrintStats NicBasePrintStats;
    static fnNicBaseThisVoid NicBaseTakeLock;
    static fnNicBaseThisVoid NicBaseTakeLockAtRaisedIrql;
    static VOID ResolveFunctions();

};
#pragma pack(pop)

#if DEVKIT_ONLY
static_assert(sizeof(CNicBase) == 0x160, "CNicBase size mismatch");
#else
static_assert(sizeof(CNicBase) == 0x128, "CNicBase size mismatch");
#endif


#if DEVKIT_ONLY
#define NicBaseTakeLock(_this) { CNicBase::NicBaseTakeLock(_this); }
#else
#define NicBaseTakeLock(_this) { _this->PreviousIrql = KfAcquireSpinLock(&_this->NicLock); }
#endif

#if DEVKIT_ONLY
#define NicBaseTakeLockAtRaisedIrql(_this) { CNicBase::NicBaseTakeLockAtRaisedIrql(_this); }
#else
#define NicBaseTakeLockAtRaisedIrql(_this) { KeAcquireSpinLockAtRaisedIrql(&_this->NicLock); }
#endif
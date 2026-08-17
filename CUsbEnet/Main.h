#pragma once
#include "stdafx.h"

#ifndef VERSION_MAJOR
#define VERSION_MAJOR 1
#endif

#ifndef VERSION_MINOR
#define VERSION_MINOR 0
#endif

#ifndef DEVKIT_ONLY
#define DEVKIT_ONLY 1
#endif

#ifndef _TRAP
#define _TRAP 0
#endif

#ifndef USBENET_DEBUG
#define USBENET_DEBUG 0
#endif

#ifndef USBENET_PERF_LOGGING
#define USBENET_PERF_LOGGING 0
#endif

#ifndef USBENET_CONNECTION_LOGGING
#define USBENET_CONNECTION_LOGGING 0
#endif

// A long kernel-debugger break pauses the Xbox USB/timer servicing while the
// AX88179 continues running independently. Reinitialize the chipset when the
// running timer resumes after an abnormally large gap.
#ifndef USBENET_RECOVER_AFTER_TIMER_GAP
#define USBENET_RECOVER_AFTER_TIMER_GAP 1
#endif

#ifndef USBENET_TIMER_GAP_RECOVERY_MS
#define USBENET_TIMER_GAP_RECOVERY_MS 5000
#endif

enum USBENET_RECOVERY_STATE {
    UsbEnetRecoveryIdle          = 0,
    UsbEnetRecoveryRequested     = 1,
    UsbEnetRecoveryDrainingRx    = 2,
    UsbEnetRecoveryReinitializing= 3,
    UsbEnetRecoveryComplete      = 4,
    UsbEnetRecoverySuppressed    = 5,
    UsbEnetRecoveryFailed        = 6
};

enum USBENET_RECOVERY_REASON {
    UsbEnetRecoveryReasonNone                 = 0,
    UsbEnetRecoveryReasonKdCommand            = 1,
    UsbEnetRecoveryReasonInvalidFullAggregate = 2,
    UsbEnetRecoveryReasonTrailerOnlyStorm     = 3,
    UsbEnetRecoveryReasonTimerGap             = 4
};

#define UsbEnetDirectXmitEnabled() (EnableDispatchLevelDirectXmit != 0)
#define UsbEnetPerfPeriodMilliseconds() ThroughputStatsPeriod

// USBENET_DEBUG is the compile-time master switch for ordinary driver output.
// Performance reporting bypasses this macro so its low-overhead five-second
// summary remains independently controlled by USBENET_PERF_LOGGING.
#if !USBENET_DEBUG
#define DbgPrint(...) ((void)0)
#endif

#if USBENET_PERF_LOGGING
#define USBENET_PERF_PRINT(...) (DbgPrint)(__VA_ARGS__)
#else
#define USBENET_PERF_PRINT(...) ((void)0)
#endif

#if USBENET_CONNECTION_LOGGING
#define USBENET_CONNECTION_PRINT(...) (DbgPrint)(__VA_ARGS__)
#else
#define USBENET_CONNECTION_PRINT(...) ((void)0)
#endif

namespace Main {
    extern HANDLE mHandle;
    extern BOOL Devkit;
    extern BOOL Exiting;
    extern volatile LONG InitState;
    CEnetAddr* GetKernelTitleEthernetAddress();
    CEnetAddr* GetKernelDebugEthernetAddress();
    VOID Init();
    VOID Shutdown();
}

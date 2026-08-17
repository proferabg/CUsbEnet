#include "stdafx.h"

CNicBase::fnNicBaseThisVoid CNicBase::NicBaseInitialize = NULL;
CNicBase::fnNicBaseThisVoid CNicBase::NicBaseShutdown = NULL;
CNicBase::fnNicBaseThisTimerStart CNicBase::NicBaseTimerStart = NULL;
CNicBase::fnNicBaseThisStats CNicBase::NicBaseAddInternalStats = NULL;
CNicBase::fnNicBaseThisCEnetAddr CNicBase::NicBaseIsSubscribedMcastAddr = NULL;
CNicBase::fnNicBaseThisPrintStats CNicBase::NicBasePrintStats = NULL;
CNicBase::fnNicBaseThisVoid CNicBase::NicBaseTakeLock = NULL;
CNicBase::fnNicBaseThisVoid CNicBase::NicBaseTakeLockAtRaisedIrql = NULL;

VOID CNicBase::ResolveFunctions() {
    NicBaseInitialize = reinterpret_cast<fnNicBaseThisVoid>(Main::Devkit ? 0x8010A260 : 0x800D5B00);
    NicBaseShutdown = reinterpret_cast<fnNicBaseThisVoid>(Main::Devkit ? 0x8010A2D0 : 0x800D5B60);
    NicBaseTimerStart = reinterpret_cast<fnNicBaseThisTimerStart>(Main::Devkit ? 0x8010A170 : 0x800D5AD8);
    NicBaseAddInternalStats = reinterpret_cast<fnNicBaseThisStats>(Main::Devkit ? 0x80104BF8 : 0x800D1FB8);
    NicBaseIsSubscribedMcastAddr = reinterpret_cast<fnNicBaseThisCEnetAddr>(Main::Devkit ? 0x8010A308 : 0x800D5B98);

    if (Main::Devkit) {
        NicBasePrintStats = reinterpret_cast<fnNicBaseThisPrintStats>(0x8010A398);
        NicBaseTakeLock = reinterpret_cast<fnNicBaseThisVoid>(0x80106768);
        NicBaseTakeLockAtRaisedIrql = reinterpret_cast<fnNicBaseThisVoid>(0x80106858);
    }
}
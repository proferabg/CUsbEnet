#include "stdafx.h"

#ifndef USBENET_LOG
#if USBENET_DEBUG
#define USBENET_LOG(_fmt, ...) DbgPrint("[CUsbEnet/Hooks] " _fmt "\n", __VA_ARGS__)
#define USBENET_LOG0(_msg) DbgPrint("[CUsbEnet/Hooks] " _msg "\n")
#else
#define USBENET_LOG(_fmt, ...) ((void)0)
#define USBENET_LOG0(_msg) ((void)0)
#endif
#endif

namespace Hooks {

	Detour<VOID> NicAttach_Detour;
	Detour<VOID> NicDetach_Detour;
	Detour<VOID> NicGetStats_Detour;
    Detour<VOID> NicSetUnicastAddress_Detour;
    Detour<VOID> NicXmit_Detour;
    Detour<NTSTATUS> NicUpdateMcastMembership_Detour;
    Detour<VOID> NicFlushXmitQueue_Detour;
    Detour<DWORD> NicGetLinkState_Detour;
    Detour<NTSTATUS> NicGetOpt_Detour;
    Detour<NTSTATUS> NicSetOpt_Detour; 

    static Detour<VOID> UsbdEhciShutdown_Detour;

    static DWORD OriginalStandardNicDeviceFlags;
    static DWORD OriginalXbdmNicDeviceFlags;
    static BOOL InterfaceFlagsPatched;
    static BOOL ShutdownCalled;


    static BOOL IsPlausibleNicUser(CNicUser* User) {
        if (User == NULL || !MmIsAddressValid(User) || !MmIsAddressValid(reinterpret_cast<PBYTE>(User) + sizeof(CNicUser) - 1))
            return FALSE;

        DWORD Filter = User->OriginalAttachInfo.ReceiveFilterMask;
        if (Filter != XNET_STANDARD_RECEIVE_FILTER && Filter != XNET_XBDM_RECEIVE_FILTER)
            Filter = User->AttachInfo.ReceiveFilterMask;

        if (Filter != XNET_STANDARD_RECEIVE_FILTER && Filter != XNET_XBDM_RECEIVE_FILTER)
            return FALSE;

        if (User->AttachInfo.ReceiveCallback == NULL || User->AttachInfo.LinkStateCallback == NULL)
            return FALSE;

        return MmIsAddressValid(reinterpret_cast<PVOID>(User->AttachInfo.ReceiveCallback)) && MmIsAddressValid(reinterpret_cast<PVOID>(User->AttachInfo.LinkStateCallback));
    }

    static BOOL EnsureUsbEnetUser(CNicUser* User, const char* Source) {
        if (!IsPlausibleNicUser(User)) {
            USBENET_LOG("Rejected invalid/lifecycle NIC user=%p source=%s", User, Source != NULL ? Source : "unknown");
            return FALSE;
        }

        if (!g_UsbEnet.IsUserAttached(User)) {
            /* NicGetOpt, multicast and other NIC callbacks can run from an XNet
             * receive/DPC callback. Attaching there can re-enter NicAttachUser
             * while USBENET_STATE_CALLBACK_IN_PROGRESS is set. Never wait or
             * retire another DPC list from that context; a later PASSIVE_LEVEL
             * call can perform the fallback attachment. */
            if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
                USBENET_LOG("Deferring NIC user attach at IRQL %u user=%p source=%s", KeGetCurrentIrql(), User, Source != NULL ? Source : "unknown");
                return FALSE;
            }

            USBENET_LOG("Lazily attaching existing NIC user=%p source=%s mask=0x%08X flags=0x%08X", User, Source != NULL ? Source : "unknown", User->AttachInfo.ReceiveFilterMask, User->AttachInfo.DeviceFlags);
            UsbEnetNicAttachUser(User);
        }

        return g_UsbEnet.IsUserAttached(User);
    }

    static VOID AttachExistingRetailTitleUser() {
        static const DWORD KernelNicEmac17559 = 0x801864B8;
        PLIST_ENTRY Head = reinterpret_cast<PLIST_ENTRY>(KernelNicEmac17559);

        if (!MmIsAddressValid(Head) || !MmIsAddressValid(reinterpret_cast<PBYTE>(Head) + sizeof(LIST_ENTRY) - 1)) {
            DbgPrint("[CUsbEnet/Hooks] Retail g_NicEmac user-list head is invalid: head=%p.\n", Head);
            return;
        }

        PLIST_ENTRY Link = Head->Flink;
        DbgPrint("[CUsbEnet/Hooks] Retail g_NicEmac=%p user-list head=%p links=%p/%p.\n", reinterpret_cast<PVOID>(KernelNicEmac17559), Head, Head->Flink, Head->Blink);

        for (DWORD Index = 0; Link != Head && Index < 16; Index++) {
            if (Link == NULL || !MmIsAddressValid(Link) || !MmIsAddressValid(reinterpret_cast<PBYTE>(Link) + sizeof(LIST_ENTRY) - 1)) {
                DbgPrint("[CUsbEnet/Hooks] Retail NIC user-list entry is invalid: index=%u link=%p.\n", Index, Link);
                return;
            }

            PLIST_ENTRY Next = Link->Flink;
            CNicUser* User = CONTAINING_RECORD(Link, CNicUser, NicBaseListEntry);

            if (!IsPlausibleNicUser(User)) {
                USBENET_LOG("Skipping non-title or malformed retail NIC user=%p index=%u", User, Index);
                Link = Next;
                continue;
            }

            DWORD ReceiveFilter = User->OriginalAttachInfo.ReceiveFilterMask;
            if (ReceiveFilter != XNET_STANDARD_RECEIVE_FILTER)
                ReceiveFilter = User->AttachInfo.ReceiveFilterMask;

            DbgPrint("[CUsbEnet/Hooks] Retail existing NIC user=%p index=%u flags=0x%08X filter=0x%08X originalFlags=0x%08X originalFilter=0x%08X.\n", User, Index, User->AttachInfo.DeviceFlags, User->AttachInfo.ReceiveFilterMask, User->OriginalAttachInfo.DeviceFlags, User->OriginalAttachInfo.ReceiveFilterMask);

            if (ReceiveFilter == XNET_STANDARD_RECEIVE_FILTER) {
                if (EnsureUsbEnetUser(User, "retail-existing-title"))
                    DbgPrint("[CUsbEnet/Hooks] Attached existing 17559 retail title CNicUser=%p from g_NicEmac list.\n", User);
                return;
            }

            Link = Next;
        }

        if (Link != Head)
            DbgPrint("[CUsbEnet/Hooks] Retail g_NicEmac user-list walk reached the 16-entry safety limit.\n");
        else
            DbgPrint("[CUsbEnet/Hooks] No existing 17559 retail title CNicUser was present in g_NicEmac. Lazy hook attachment remains enabled.\n");
    }

    VOID AttachExistingXamUsers() {
        if (!Main::Devkit) {
            AttachExistingRetailTitleUser();
            return;
        }

        volatile PCXN_BASE_PARTIAL* XnInstances = reinterpret_cast<volatile PCXN_BASE_PARTIAL*>(0x81D6AF78);
        PCXN_BASE_PARTIAL StandardXn = const_cast<PCXN_BASE_PARTIAL>(XnInstances[0]);
        PCXN_BASE_PARTIAL XbdmXn = const_cast<PCXN_BASE_PARTIAL>(XnInstances[1]);

        DbgPrint("[CUsbEnet/Hooks] Devkit existing users table=%p standard=%p/%p xbdm=%p/%p.\n", XnInstances, StandardXn, StandardXn != NULL ? StandardXn->NicUser : NULL, XbdmXn, XbdmXn != NULL ? XbdmXn->NicUser : NULL);

        if (StandardXn != NULL)
            EnsureUsbEnetUser(StandardXn->NicUser, "devkit-existing-standard");
        if (XbdmXn != NULL)
            EnsureUsbEnetUser(XbdmXn->NicUser, "devkit-existing-xbdm");
    }

    static BOOL IsUsbEnetUser(CNicUser* User) {
        if (User == NULL)
            return FALSE;

        return g_UsbEnet.IsUserAttached(User);
    }
    
    VOID __fastcall NicSetUnicastAddress_Hook(DWORD DeviceFlags, CEnetAddr* Address, INT AddressIndex) {
        if (Address != NULL) {
            PBYTE AddressBytes = reinterpret_cast<PBYTE>(Address);
            USBENET_LOG("NicSetUnicastAddress_Hook deviceFlags=0x%08X address=%p value=%02X:%02X:%02X:%02X:%02X:%02X addressIndex=%d", DeviceFlags, Address, AddressBytes[0], AddressBytes[1], AddressBytes[2], AddressBytes[3], AddressBytes[4], AddressBytes[5], AddressIndex);
        } else {
            USBENET_LOG("NicSetUnicastAddress_Hook deviceFlags=0x%08X address=%p addressIndex=%d", DeviceFlags, Address, AddressIndex);
        }

        NicSetUnicastAddress_Detour.CallOriginal(DeviceFlags, Address, AddressIndex);

        if ((DeviceFlags & NicInterfaceUsbEnet) != 0 && Address != NULL)
            UsbEnetNicSetUnicastAddress(Address, AddressIndex);
    }

    VOID __fastcall NicAttach_Hook(PNIC_ATTACH_INFO AttachInfo, CNicUser** UserOut) {
        DWORD RequestedFlags = AttachInfo != NULL ? AttachInfo->DeviceFlags : 0;
        USBENET_LOG("NicAttach_Hook attachInfo=%p userOut=%p requestedFlags=0x%08X", AttachInfo, UserOut, RequestedFlags);

        NicAttach_Detour.CallOriginal(AttachInfo, UserOut);

        CNicUser* User = UserOut != NULL ? *UserOut : NULL;
        USBENET_LOG("NicAttach_Hook original complete attachInfo=%p userOut=%p user=%p requestedFlags=0x%08X", AttachInfo, UserOut, User, RequestedFlags);

        if (User == NULL)
            return;

        if ((RequestedFlags & NicInterfaceUsbEnet) == 0)
            return;

        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            DbgPrint("[CUsbEnet/Hooks] Refusing USB NIC attach from IRQL %u user=%p; existing-user/lazy PASSIVE attachment remains available.\n", KeGetCurrentIrql(), User);
            return;
        }

        USBENET_LOG("NicAttach_Hook forwarding to USB Ethernet user=%p", User);
		UsbEnetNicAttachUser(User);
    }

    VOID __fastcall NicDetach_Hook(CNicUser* User) {
        DWORD DeviceFlags = User != NULL ? User->AttachInfo.DeviceFlags : 0;
        USBENET_LOG("NicDetach_Hook user=%p deviceFlags=0x%08X", User, DeviceFlags);

        if (User != NULL && (DeviceFlags & NicInterfaceUsbEnet) != 0) {
            USBENET_LOG("NicDetach_Hook forwarding to USB Ethernet user=%p", User);
            if (!UsbEnetNicDetachUser(User)) {
                DbgPrint("[CUsbEnet/Hooks] Deferring kernel NIC detach for user=%p at IRQL %u until the active USB callback completes.\n", User, KeGetCurrentIrql());
                return;
            }
        }

        NicDetach_Detour.CallOriginal(User);
    }

    VOID __fastcall NicXmit_Hook(CNicUser* User, DWORD QueueIndex, PBYTE Frame, DWORD FrameLength, PVOID CompletionCookie) {
        // Never allocate/attach from the transmit path; NicXmit may run at DISPATCH_LEVEL.
        BOOL UsbAttached = IsUsbEnetUser(User);

        if (UsbAttached && (UsbEnetNicGetLinkState() & NIC_LINK_STATE_ACTIVE) != 0) {
            UsbEnetNicXmit(User, QueueIndex, Frame, FrameLength, CompletionCookie);
            return;
        }

        NicXmit_Detour.CallOriginal(User, QueueIndex, Frame, FrameLength, CompletionCookie);
    }

    NTSTATUS __fastcall NicUpdateMcastMembership_Hook(CNicUser* User, CEnetAddr* Address, INT Add) {
        DWORD DeviceFlags = IsPlausibleNicUser(User) ? User->AttachInfo.DeviceFlags : 0;
        if (Address != NULL) {
            PBYTE AddressBytes = reinterpret_cast<PBYTE>(Address);
            USBENET_LOG("NicUpdateMcastMembership_Hook user=%p flags=0x%08X address=%p value=%02X:%02X:%02X:%02X:%02X:%02X add=%d", User, DeviceFlags, Address, AddressBytes[0], AddressBytes[1], AddressBytes[2], AddressBytes[3], AddressBytes[4], AddressBytes[5], Add);
        } else {
            USBENET_LOG("NicUpdateMcastMembership_Hook user=%p flags=0x%08X address=%p add=%d", User, DeviceFlags, Address, Add);
        }

        NTSTATUS Status = NicUpdateMcastMembership_Detour.CallOriginal(User, Address, Add);
        USBENET_LOG("NicUpdateMcastMembership_Hook original status=0x%08X", Status);
        if (!NT_SUCCESS(Status))
            return Status;

        if (EnsureUsbEnetUser(User, "multicast") && Address != NULL) {
            NTSTATUS UsbStatus = UsbEnetNicUpdateMcastMembership(Address, Add);
            USBENET_LOG("NicUpdateMcastMembership_Hook USB status=0x%08X", UsbStatus);
            if (!NT_SUCCESS(UsbStatus))
                return UsbStatus;
        }

        return Status;
    }

    VOID __fastcall NicFlushXmitQueue_Hook(CNicUser* User) {
        BOOL UsbAttached = IsUsbEnetUser(User);
        USBENET_LOG("NicFlushXmitQueue_Hook user=%p usbAttached=%d", User, UsbAttached);

        NicFlushXmitQueue_Detour.CallOriginal(User);

        if (UsbAttached) {
            USBENET_LOG("NicFlushXmitQueue_Hook forwarding to USB Ethernet user=%p", User);
            UsbEnetNicFlushXmitQueue(User);
        }
    }

    DWORD __fastcall NicGetLinkState_Hook(DWORD DeviceFlags) {
        DWORD RemainingFlags = DeviceFlags & ~NicInterfaceUsbEnet;

        if ((DeviceFlags & NicInterfaceUsbEnet) != 0) {
            DWORD UsbLinkState = UsbEnetNicGetLinkState();

            if ((UsbLinkState & NIC_LINK_STATE_ACTIVE) != 0)
                return UsbLinkState;

            if (RemainingFlags == 0)
                return UsbLinkState;
        }

        return NicGetLinkState_Detour.CallOriginal(RemainingFlags);
    }

	VOID __fastcall NicGetStats_Hook(DWORD Interface, PNIC_STATS Stats) {
        USBENET_LOG("NicGetStats_Hook interface=0x%08X stats=%p", Interface, Stats);
        NicGetStats_Detour.CallOriginal(Interface, Stats);

		if ((Interface & NicInterfaceUsbEnet) != 0) {
            USBENET_LOG("NicGetStats_Hook adding USB Ethernet stats stats=%p", Stats);
			UsbEnetNicAddInternalStats(Stats);
		}
	}

    NTSTATUS __fastcall NicGetOpt_Hook(CNicUser* User, DWORD Option, PBYTE Value, PDWORD ValueLength) {
        EnsureUsbEnetUser(User, "get-opt");

        NTSTATUS Status = NicGetOpt_Detour.CallOriginal(User, Option, Value, ValueLength);

        if (!IsUsbEnetUser(User))
            return Status;

        /* USB-backed GetOpt can wait for an asynchronous control transfer. The
         * kernel NIC export may also be reached from DPC context, where waiting
         * or switching stacks is illegal. Preserve the original NIC result in
         * that case. */
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            USBENET_LOG("Skipping USB NicGetOpt at IRQL %u user=%p option=0x%08X", KeGetCurrentIrql(), User, Option);
            return Status;
        }

        DWORD Result = 1;
        NTSTATUS UsbStatus = UsbEnetNicDoGetOpt(User, Option, Value, ValueLength, &Result);
        DWORD UsbLength = ValueLength != NULL ? *ValueLength : 0;
        USBENET_LOG("NicGetOpt_Hook USB status=0x%08X result=%u outputLength=%u", UsbStatus, Result, UsbLength);

        if (!Result)
            return UsbStatus;

        return Status;
    }

    NTSTATUS __fastcall NicSetOpt_Hook(CNicUser* User, DWORD Option, const PBYTE Value, DWORD ValueLength) {
        EnsureUsbEnetUser(User, "set-opt");
        USBENET_LOG("NicSetOpt_Hook user=%p option=0x%08X value=%p valueLength=%u", User, Option, Value, ValueLength);

        NTSTATUS Status = NicSetOpt_Detour.CallOriginal(User, Option, Value, ValueLength);
        USBENET_LOG("NicSetOpt_Hook original status=0x%08X", Status);

        if (!IsUsbEnetUser(User))
            return Status;

        /* See NicGetOpt_Hook: USB control operations are PASSIVE_LEVEL only. */
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            USBENET_LOG("Skipping USB NicSetOpt at IRQL %u user=%p option=0x%08X", KeGetCurrentIrql(), User, Option);
            return Status;
        }

        DWORD Result = TRUE;
        NTSTATUS UsbStatus = UsbEnetNicDoSetOpt(User, Option, Value, ValueLength, &Result);
        USBENET_LOG("NicSetOpt_Hook USB status=0x%08X result=%u", UsbStatus, Result);

        if (!Result)
            return UsbStatus;

        return Status;
    }

    static BOOL DrainUsbEnetPowerDown() {
        const DWORD MaximumPassiveDrainPasses = 250;
        DWORD DrainPasses = 0;
        KIRQL EntryIrql = KeGetCurrentIrql();

        /* Never call KeRetireDpcList here. UsbdEhciShutdown may already execute
         * on a DPC stack; recursively retiring DPCs causes PANIC_STACK_SWITCH
         * and ATTEMPTED_SWITCH_FROM_DPC. At PASSIVE_LEVEL, allow asynchronous
         * close completions a short bounded interval. At elevated IRQL the
         * close remains best-effort and EHCI shutdown continues immediately. */
        if (EntryIrql == PASSIVE_LEVEL) {
            LARGE_INTEGER DelayInterval;
            DelayInterval.QuadPart = -10000;

            while (!UsbEnetIsPowerDownComplete() && DrainPasses < MaximumPassiveDrainPasses) {
                KeDelayExecutionThread(PROC_USER, FALSE, &DelayInterval);
                ++DrainPasses;
            }
        }

        BOOL Complete = UsbEnetIsPowerDownComplete();
        DbgPrint("[CUsbEnet/Hooks] USB power-down drain complete=%d passes=%u entryIrql=%u attached=%ld RX=%u TX=%u flags=0x%08X endpoints=%p/%p/%p.\n", Complete, DrainPasses, EntryIrql, g_UsbEnet.DeviceAttached, UsbEnetGetReceiveInFlightCount(), g_UsbEnet.PendingXmitCount, g_UsbEnet.Flags, g_UsbEnet.DefaultEndpoint, g_UsbEnet.ReceiveEndpoint, g_UsbEnet.TransmitEndpoint);
        return Complete;
    }

    VOID __fastcall UsbdEhciShutdown_Hook(PVOID UsbPage) {
        if (!ShutdownCalled) {
            ShutdownCalled = TRUE;
            DbgPrint("[CUsbEnet/Hooks] UsbdEhciShutdown received; closing USB Ethernet before EHCI shutdown.\n");

            UsbEnetBeginPowerDownAndClose();
            DbgPrint("[CUsbEnet/Hooks] Leaving USBD driver registration untouched during EHCI shutdown.\n");

            if (!DrainUsbEnetPowerDown())
                DbgPrint("[CUsbEnet/Hooks] WARNING: USB Ethernet did not fully drain before EHCI shutdown.\n");
        }

        UsbdEhciShutdown_Detour.CallOriginal(UsbPage);
    }

	VOID Install() {
        ShutdownCalled = FALSE;
        NicSetUnicastAddress_Detour.SetupDetour(MODULE_KERNEL, 513, NicSetUnicastAddress_Hook);
		NicAttach_Detour.SetupDetour(MODULE_KERNEL, 514, NicAttach_Hook);
		NicDetach_Detour.SetupDetour(MODULE_KERNEL, 515, NicDetach_Hook);
        NicXmit_Detour.SetupDetour(MODULE_KERNEL, 516, NicXmit_Hook);
        NicUpdateMcastMembership_Detour.SetupDetour(MODULE_KERNEL, 517, NicUpdateMcastMembership_Hook);
        NicFlushXmitQueue_Detour.SetupDetour(MODULE_KERNEL, 518, NicFlushXmitQueue_Hook);
        NicGetLinkState_Detour.SetupDetour(MODULE_KERNEL, 520, NicGetLinkState_Hook);
        NicGetStats_Detour.SetupDetour(MODULE_KERNEL, 521, NicGetStats_Hook);
        NicGetOpt_Detour.SetupDetour(MODULE_KERNEL, 522, NicGetOpt_Hook);
        NicSetOpt_Detour.SetupDetour(MODULE_KERNEL, 523, NicSetOpt_Hook);

        UsbdEhciShutdown_Detour.SetupDetour(Main::Devkit ? 0x80116388 : 0x800E06D0, UsbdEhciShutdown_Hook);
        DbgPrint("[CUsbEnet/Hooks] USB power-down hook installed.\n");

        // Patch Interface Flags
        DbgPrint("Patching interface flags.\n");

        PDWORD StandardNicDeviceFlags = reinterpret_cast<PDWORD>(Main::Devkit ? 0x81D27614 : 0x81A86C2C);

        OriginalStandardNicDeviceFlags = *StandardNicDeviceFlags;
        *StandardNicDeviceFlags = OriginalStandardNicDeviceFlags | NicInterfaceUsbEnet;

        DbgPrint("[CUsbEnet/Hooks] Standard NIC flags old=0x%08X new=0x%08X\n", OriginalStandardNicDeviceFlags, *StandardNicDeviceFlags);

        if (Main::Devkit) {
	        PDWORD XbdmNicDeviceFlags = reinterpret_cast<PDWORD>(0x81D27618);

	        OriginalXbdmNicDeviceFlags = *XbdmNicDeviceFlags;
	        *XbdmNicDeviceFlags = OriginalXbdmNicDeviceFlags | NicInterfaceUsbEnet;

	        DbgPrint("[CUsbEnet/Hooks] XBDM NIC flags old=0x%08X new=0x%08X\n", OriginalXbdmNicDeviceFlags, *XbdmNicDeviceFlags);
        }

        InterfaceFlagsPatched = TRUE;

        // Hook Existing CNicUser's
        DbgPrint("Attaching to existing xam users.\n");
        AttachExistingXamUsers();
	}

	VOID Remove() {
        // Unpatch Interface Flags
        DbgPrint("Unpatching interface flags.\n");

        if (InterfaceFlagsPatched) {
            PDWORD StandardNicDeviceFlags = reinterpret_cast<PDWORD>(Main::Devkit ? 0x81D27614 : 0x81A86C2C);

            *StandardNicDeviceFlags = OriginalStandardNicDeviceFlags;

            if (Main::Devkit) {
                PDWORD XbdmNicDeviceFlags = reinterpret_cast<PDWORD>(0x81D27618);
                *XbdmNicDeviceFlags = OriginalXbdmNicDeviceFlags;
            }

            InterfaceFlagsPatched = FALSE;
        }

        NicSetUnicastAddress_Detour.TakeDownDetour();
        NicAttach_Detour.TakeDownDetour();
        NicDetach_Detour.TakeDownDetour();
        NicXmit_Detour.TakeDownDetour();
        NicUpdateMcastMembership_Detour.TakeDownDetour();
        NicFlushXmitQueue_Detour.TakeDownDetour();
        NicGetLinkState_Detour.TakeDownDetour();
        NicGetStats_Detour.TakeDownDetour();
        NicGetOpt_Detour.TakeDownDetour();
        NicSetOpt_Detour.TakeDownDetour();
        UsbdEhciShutdown_Detour.TakeDownDetour();

	}
}

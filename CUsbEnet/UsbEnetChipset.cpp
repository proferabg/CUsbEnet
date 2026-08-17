#include "stdafx.h"
#include "UsbEnetChipset.h"
#include "Ax88178.h"
#include "Ax88179x_178A_772D.h"
#include "Rtl8153.h"

CUsbEnetChipset* g_UsbEnetChipset = &g_Ax88178Chipset;

NTSTATUS CUsbEnetChipset::RequestFullReinitialize(CUsbEnet* Device, DWORD Reason) {
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Reason);
    return STATUS_NOT_SUPPORTED;
}

VOID CUsbEnetChipset::HandleTransportSubmission(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe) {
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Pipe);
}

VOID CUsbEnetChipset::HandleTransportCompletion(CUsbEnet* Device, USBENET_TRANSPORT_PIPE Pipe, NTSTATUS Status) {
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Pipe);
    UNREFERENCED_PARAMETER(Status);
}

BOOL CUsbEnetChipset::UsesInterruptLinkStatus() const {
    return FALSE;
}

BOOL CUsbEnetChipset::ProcessInterruptLinkStatus(CUsbEnet* Device, const BYTE* Data, DWORD Length) {
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Length);
    return FALSE;
}

CUsbEnetChipset* UsbEnetFindChipset(USHORT VendorIdRaw, USHORT ProductIdRaw, USHORT DeviceRevision) {
    if (VendorIdRaw == g_Rtl8153Chipset.GetVendorIdRaw() && ProductIdRaw == g_Rtl8153Chipset.GetProductIdRaw())
        return &g_Rtl8153Chipset;

    if (VendorIdRaw == 0x950B && ProductIdRaw == 0x9017) {
        switch (DeviceRevision) {
            case 0x0100: return &g_Ax88179Chipset;
            case 0x0200: return &g_Ax88179ABChipset;
            case 0x0300: return &g_Ax88772DChipset;
            case 0x0400: return &g_Ax88279Chipset;
            default: return NULL;
        }
    }

    CUsbEnetChipset* Chipsets[] = {
        &g_Ax88178Chipset,
        &g_Ax88178AChipset
    };

    for (DWORD Index = 0; Index < ARRAYSIZE(Chipsets); Index++) {
        if (Chipsets[Index]->GetVendorIdRaw() == VendorIdRaw && Chipsets[Index]->GetProductIdRaw() == ProductIdRaw)
            return Chipsets[Index];
    }

    return NULL;
}

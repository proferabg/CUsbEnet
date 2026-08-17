// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once
#pragma warning (disable:4482)
#pragma warning (disable:4172)

#include <xtl.h>
#include <stdio.h>
#include <fstream>
#include <time.h>
#include <xam.h>
#include <xbdm.h>
#include "../libraries/xkelib/xkelib.h"
#include <string>
#include <xhttp.h>
#include <xui.h>

#include "Utils/ntstatus.h"
#include "Utils/Utilities.h"
#include "Utils/Detour.h"
#include "Main.h"
#include "Usbd.h"
#include "CNicUser.h"
#include "CNicBase.h"
#include "CUsbEnet.h"
#include "UsbEnet.h"
#include "Hooks.h"

#define NAME_MOUNT	"usbenet:\\"
#define NAME_HDD	"\\Device\\Harddisk0\\Partition1"
#define NAME_USB	"\\Device\\Mass0"
#define NAME_INI	"usbenet.ini"
#define NAME_LOG	"usbenet.log"
#define PATH_INI	NAME_MOUNT NAME_INI
#define PATH_LOG	NAME_MOUNT NAME_LOG

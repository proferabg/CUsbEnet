# CUsbEnet  [![Github All Releases](https://img.shields.io/github/downloads/proferabg/CUsbEnet/total.svg)]()

## Overview

CUsbEnet is a USB Ethernet driver for modified Xbox 360 systems. Microsoft
originally added the driver to various early Xbox 360 recoveries for use with
advanced Xbox 360 development kits. The hardware project was later cancelled and 
the driver was thus removed from the Xbox 360 kernel. This is a port of that 
driver with support for additional chipsets outside the original AX88178 driver
and better XNet registration. CUsbEnet registers supported ASIX and Realtek 
adapters with the Xbox 360 USB stack and integrates them with the console's 
existing XNet and NIC infrastructure.

## Features

The driver supports title networking and checked/devkit XBDM traffic, including
separate title and debug unicast addresses where required. It provides link-state
reporting, receive filtering, USB RX/TX buffering, transmit aggregation for
capable chipsets, and recovery for failed or stalled transfers.

CUsbEnet uses private Xbox 360 kernel interfaces and build-specific structure
layouts. Kernel addresses, ABI assumptions, and hook targets must be verified
before using the driver with a kernel version other than the one for which it
was built. This is experimental kernel-level software and should be tested
carefully on each adapter and console configuration.

## Installation

Copy `CUsbEnet.xex` to the console, then configure it as the first plugin so
the USB Ethernet driver loads before any other plugins.

- **DashLaunch:** Add `CUsbEnet.xex` as the first plugin entry
  in `launch.ini`, before `xbdm.xex`.
- **RGLoader:** Add `CUsbEnet.xex` as the first plugin entry in `rgloader.ini`.

Alternatively, the plugin can be loaded and unloaded after boot but may encounter issues.

## Supported chipsets

| Chipset |
| --- |
| ASIX AX88178 |
| ASIX AX88178A |
| ASIX AX88179 |
| ASIX AX88179A/B |
| ASIX AX88772D |
| ASIX AX88279 |
| Realtek RTL8153 v3-v6 |
| Realtek RTL8153B v8/v9 |

## License

CUsbEnet is licensed under the GNU General Public License, version 2 only
(`GPL-2.0-only`). The complete license text is provided in [LICENSE](LICENSE).

Hardware initialization and descriptor handling were developed with reference
to GPL-2.0-licensed Linux USB Ethernet drivers, including the Linux `r8152`
driver. GPL notices, SPDX identifiers, attribution, and provenance comments must
be preserved when modifying or redistributing this project.

This project is not affiliated with or endorsed by Microsoft, ASIX Electronics,
or Realtek Semiconductor.

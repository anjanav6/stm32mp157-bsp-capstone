# STM32MP157F-DK2 Linux BSP and QR Code Based Access Control System

**Anjana**
**April – June 2026**

## Project Overview

This project implements a complete Linux BSP built from source for the STM32MP157F-DK2 Discovery Kit using Buildroot.

The final system demonstrates a QR-code based access control application running on Embedded Linux with:

* USB camera capture using V4L2
* QR code detection using ZBar
* Whitelist-based access validation
* Audit logging
* LCD camera preview
* GPIO-based status indication
* Cortex-M4 bring-up using RemoteProc

---

## Hardware

* STM32MP157F-DK2 Discovery Kit
* USB Webcam (UVC / V4L2)
* Onboard LCD Display (480 × 800)
* onboard Red LED (PA13)
* microSD Card

---

## Software Stack

* TF-A (Trusted Firmware-A)
* U-Boot
* OP-TEE
* Linux Kernel 6.1.28 (ST)
* BusyBox Root Filesystem
* Buildroot 2023.02.10
* V4L2
* ZBar
* RemoteProc
* RPMsg Infrastructure

---

## Implemented Features

### BSP Development

* Buildroot BSP generation
* TF-A build and integration
* U-Boot build and configuration
* Linux kernel build
* BusyBox root filesystem generation
* SD card image creation
* Linux boot verification

### GPIO Control

* Userspace GPIO control
* Green LED indication
* Red LED indication

### Camera Integration

* USB webcam detection
* V4L2 camera capture
* YUYV frame acquisition
* MMAP buffer handling

### QR Access Control

* Real-time QR code detection
* ZBar integration
* Whitelist validation
* Duplicate scan cooldown protection
* Audit logging

### LCD Bring-Up

* DSI LCD initialization
* Framebuffer verification
* RGB565 rendering
* YUYV → RGB565 conversion
* Frame scaling (640×480 → 480×800)
* Live camera preview
* Framebuffer stride debugging and correction

### Cortex-M4 Experiments


* Cortex-M4 firmware loading
* Successful M4 boot from Linux


---

## Build Instructions

### Prerequisites

```bash
sudo apt install -y build-essential gcc g++ git wget unzip \
libssl-dev libncurses-dev python3 rsync bc cpio
```

### Clone Sources

```bash
mkdir -p ~/stm32mp157-bsp
cd ~/stm32mp157-bsp

git clone https://github.com/bootlin/buildroot.git \
-b st/2023.02.10 buildroot

git clone https://github.com/bootlin/buildroot-external-st.git \
-b st/2023.02.10 buildroot-external-st
```

### Build BSP

```bash
cd buildroot

make BR2_EXTERNAL=../buildroot-external-st \
st_stm32mp157f_dk2_defconfig

make -j$(nproc)
```

### Flash SD Card

```bash
sudo dd if=output/images/sdcard.img \
of=/dev/sdX \
bs=8M conv=fdatasync status=progress

sync
```

---

## Project Status

| Phase   | Status      | Description                       |
| ------- | ----------- | --------------------------------- |
| Phase 1 | Complete    | Development environment setup     |
| Phase 2 | Complete    | BSP build and Linux bring-up      |
| Phase 3 | Complete    | GPIO control and V4L2 camera      |
| Phase 4 | Complete    | QR access control application     |
| Phase 5 | Complete    | LCD bring-up and live preview     |
| Phase 6 | In Progress | Testing and documentation         |

---

## System Specifications

| Parameter          | Value       |
| ------------------ | ----------- |
| Linux Boot Time    | ~10 seconds |
| QR Detection Time  | < 2 seconds |
| Camera Resolution  | 640 × 480   |
| LCD Resolution     | 480 × 800   |
| Framebuffer Format | RGB565      |
| Whitelist Capacity | 100 entries |
| QR Scanner Library | ZBar        |
| Camera Interface   | V4L2        |

---

## Verified Results

* Linux boots successfully on STM32MP157F-DK2
* GPIO control functional
* USB camera capture functional
* QR code detection functional
* Whitelist validation functional
* Audit logging functional
* LCD framebuffer functional
* Live camera preview functional
* Cortex-M4 firmware boots through RemoteProc


---

## Repository Structure

```text
stm32mp157-bsp-capstone/
├── README.md
├── buildroot/
├── buildroot-external-st/
├── qr_app/
│   ├── complete_qrscan_display.c
│   └── testcam.c
└── docs/
```

---

## Author

**Anjana**
M.Tech Electronics Design Technology
NIELIT Calicut
April – June 2026

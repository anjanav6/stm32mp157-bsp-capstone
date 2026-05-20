# STM32MP157 DK2 Linux BSP — Capstone Project

**Anjana | M.Tech Electronics Design Technology — Semester 4**
**April – June 2026**

## Project Overview
Complete Linux BSP built from source for STM32MP157F-DK2 board.
Dual-core access control system using Cortex-A7 (Linux) + Cortex-M4 (bare-metal).

## System Architecture
- **TF-A** — Trusted Firmware-A (BL2 stage)
- **U-Boot** — Bootloader
- **Linux Kernel** — ST fork, custom Device Tree
- **BusyBox rootfs** — Minimal userspace via Buildroot
- **Cortex-M4 firmware** — EXTI IR interrupt, WFI sleep (Phase 5)
- **OpenAMP RPMsg** — Inter-processor communication (Phase 6)

## Hardware
- STM32MP157F-DK2 Discovery Kit
- USB Webcam (V4L2)
- IR proximity sensor (PE3, EXTI)
- Relay module (PA14)
- Green LED (PA13), Red LED (PA15)
- Buzzer (PA16)

## Build Instructions

### Prerequisites
```bash
sudo apt install -y build-essential gcc g++ git wget unzip \
  libssl-dev libncurses-dev python3 rsync bc cpio
```

### Clone and Build
```bash
mkdir -p ~/stm32mp157-bsp && cd ~/stm32mp157-bsp
git clone https://github.com/bootlin/buildroot.git -b st/2023.02.10 buildroot
git clone https://github.com/bootlin/buildroot-external-st.git -b st/2023.02.10 buildroot-external-st
cd buildroot
make BR2_EXTERNAL=../buildroot-external-st st_stm32mp157f_dk2_defconfig
make -j$(nproc)
```

### Flash SD Card
```bash
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=8M conv=fdatasync status=progress
sync
```

## Project Phases
| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | ✅ | Development environment, cross-compiler, boot theory |
| Phase 2 | 🔄 | BSP build complete, board bring-up in progress |
| Phase 3 | ⏳ | GPIO, relay, LED, buzzer, V4L2 camera |
| Phase 4 | ⏳ | ZBar QR scan, whitelist, audit log, systemd |
| Phase 5 | ⏳ | Cortex-M4 firmware, EXTI, WFI |
| Phase 6 | ⏳ | OpenAMP RPMsg IPC |
| Phase 7 | ⏳ | KPI measurement, 15 test cases |

## KPI Targets
| Metric | Target |
|--------|--------|
| Linux Boot Time | < 10s |
| IR Wake Latency | < 500ms |
| QR Detection Speed | < 2s |
| Door Response Time | < 200ms |
| M4 Power Reduction | > 60% |
| Test Cases | 15/15 |

## Author
Anjana | M.Tech EDT Sem 4 | April–June 2026

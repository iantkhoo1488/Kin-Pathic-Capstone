# Capstone Project – Kin:Pathic CAREBand

## Overview
This repository implements a heart rate monitor to scan for heart rate, then report back to an application on a mobile device, which then performs calculations on the values and creates alerts based on the heart rate.

## Team
| Name | Email | Primary responsibility |
|---|---|---|
| Ian Khoo | Ian_Khoo@student.uml.edu | Coding the device |
| Khang Pham | Khang_Pham@student.uml.edu | Creating the PCB |
| Elle Underhill | Elle_Underhill@student.uml.edu | Coding the mobile app |

## Repository Structure 

```
prototypecode/ --- stores the code for the various prototypes
README.md
```

- `requirements.txt` VSCode + NRFConnect + ZephyrRTOS

---

## Requirements
- Language/runtime: (VSCode + NRFConnect + ZephyrRTOS)
- OS tested: ( Windows )
- Dependencies: (mainly for Zephyr RTOS)
  - CMake
  - Ninja-Build
  - gperf
  - Python3
  - Git
  - DTC (Device Tree Compiler)
  - wget
  - 7-Zip
  - winget

---

**Notes**
- No current notes

---

## Quick Start (Run Locally)
prototype: plug-in power source, powers on automatically, and will start taking down values.
  - Power off by unplugging the battery
  - data is not stored
---

## Known Issues / Limitations
  - Current prototype does not take in values into memory
  - Bluetooth range about 10 meters

---

## Academic Integrity / External Tools
VSCode
ChatGPT/Claude.ai (troubleshooting)
StackOverflow
Zephyr manual: https://docs.zephyrproject.org/latest/develop/getting_started/index.html

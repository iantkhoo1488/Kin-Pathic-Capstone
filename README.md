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

---

## Requirements
- Language/runtime: (Arduino IDE)
- OS tested: ( Windows )
- Dependencies:
  - ADAFruit Libraries for the MAX30102 HRM
  - ADAFruit Libraries for the OLED screen

---

**Notes**
- No current notes

---

## Quick Start (Run Locally)
prototype: plug-in power source, powers on automatically, and will start taking down values.
Open the application, connect to the target device, and wait for new values to come in

---

## Known Issues / Limitations
  - Current prototype does not take in values into memory
  - Bluetooth range is about 25 meters
  - Current sensor is not entirely accurate

---

## Academic Integrity / External Tools
Arduino IDE
ChatGPT/Claude.ai (troubleshooting)

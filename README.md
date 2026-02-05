# Capstone Project – Kin:Pathic CAREBand

## Overview
This repository implements a heartrate monitor to scan for heartrate, then report back to an application on a mobile device whihc then does calculations on the values and creates alerts based on the value of the heartrate.

## Team
| Name | Email | Primary responsibility |
|---|---|---|
| Ian Khoo | Ian_Khoo@student.uml.edu | Coding the device |
| Khang Pham | --- | --- |
| Elle Underhill | --- | --- |

## Repository Structure 

```
prototypecode/ --- stores the code fior the various prototypes
README.md
```

- `tests/` --- tests to be run on the device
- `data/` --- stores the data collected
- `requirements.txt` VSCode + NRFConnect + ZephyrRTOS

---

## Requirements
- Language/runtime: (VSCode + NRFConnect + ZephyrRTOS)
- OS tested: ( Windows )
- Dependencies:
  - ADAFruit Libraries for the MAX30102 HRM
  - ADAFruit Libraries for the OLED screen

---

**Notes**
- No current notes

---

## Quick Start (Run Locally)
prototype: plug in power source, powers on automatically, and will start taking down values.
  - Power off by unplugging battery
  - data is not stored
---

## Known Issues / Limitations
  - Current prototype does not take in values into memory
  - Connects to bluetooth, but does not send values yet
  - Current sensor not entirely accurate

---

## Academic Integrity / External Tools
VSCode
ChatGPT/Claude.ai (troubleshooting)
StackOverflow
Zephyr manual: https://docs.zephyrproject.org/latest/develop/getting_started/index.html

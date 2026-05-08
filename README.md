# Capstone Project – Kin:Pathic CAREBand

## Overview
This repository implements a heart rate monitor to scan for heart rate, then report back to an application on a mobile device, which then performs calculations on the values and creates alerts based on the heart rate.
This branch is for the HTML implementation of the application

| Version | Files Changed | Changes / Improvements |
|---------|--------------|------------------------|
| Beta 0.1.0 *(careband-dashboard)* | All files (initial) | Initial release. ECG canvas waveform with animated beat-flash effect. 5-minute sampling window that locks the average. Heart rate zone chip. Basic connect/disconnect handling. Log grid layout. No alert system. |
| Beta 0.2.0 | All files (rewrite) | Stripped ECG/visual effects. Rebuilt around a practical alert model. Added alert banner with dismiss. Added threshold card showing current alert BPM threshold. Added `maybeRearm()` logic — alert re-arms automatically when HR drops back below threshold. Introduced 22% above-average alert threshold. Changed log from grid to table with `tbody`. |
| Beta 0.3.0 | main logic, UI | Introduced confirmation pips to reduce false alerts. Alert no longer fires on a single elevated beat — requires 2 consecutive beats above threshold (`CONFIRM_REQUIRED = 2`). Added 2 pip indicators that light up as the breach streak builds. Removed `maybeRearm()` in favour of streak-reset approach. Log row status changed from boolean flag to descriptive status string. Switched from exponential weighted moving average to Z-score moving average. |
| Beta 0.4.0 | All files | Switched from locked 5-minute average to rolling average. Added 2-minute alert warm-up period: alerts suppressed until warm-up completes, with dedicated progress bar (`WARMUP_S = 120`). Added `onReadyNotify()` for new BLE `ready` characteristic. Added avg-status chip (`Warming up` / `Active`). Added inline sample count chip. Alert text updated to reference 'rolling average'. Log rows gain cell class for styling. |
| Beta 0.5.0 | main logic, UI | Tightened false-alert prevention. Bumped `CONFIRM_REQUIRED` from 2 to 3 — now requires 3 consecutive beats above threshold before an alert fires. Added third confirmation pip to match. |
| Beta 0.6.0 *(current)* | main logic, UI, header | Added runtime Settings panel. `CONFIRM_REQUIRED` changed from `const` to `let` so it can be adjusted live. Settings button added to header bar (visible after connect). Settings panel provides +/− controls to change the required consecutive-beat confirmation count (range 1–10) without reloading. Pip row made dynamic to reflect current setting. |


# Network Health Monitor – Requirements

## Problem Statement
The internet at home feels unreliable and sometimes can be slow, but we lack objective data to understand the problem or hold our ISP accountable. This project will continuously monitor connection quality and latency, packet loss, and outages in order to provide a factual record of network performance over time. With this data, we can diagnose issues, correlate slow experiences with actual drops, and generate reports for ISP support.

## Functional Requirements

### Latency Monitoring
- Measure round-trip time (RTT) in milliseconds to a configurable target (default: `8.8.8.8`).
- Check once every 10 seconds (interval configurable).
- Each check sends 1 ICMP ping with a 2-second timeout.
- Record: timestamp, target, latency_ms (floating point), success (1 = reply received, 0 = timeout/failure).

### Outage Detection
- An outage is defined as **3 consecutive failed checks** (≥30 seconds without a reply).
- Log a separate event when an outage starts and when it ends (allows counting total outages and average duration).

### Packet Loss
- Calculate packet loss as a percentage over a rolling window of the last 100 checks (~17 minutes at 10s intervals).
- Display this percentage on the dashboard.

### Historical Data & Dashboard
- Time ranges: last 24 hours, 7 days, 30 days.
- Dashboard elements:
  - Current status indicator (green/yellow/red based on recent latency and loss thresholds).
  - Latency graph (line chart, per-check or averaged).
  - Packet loss graph (rolling loss percentage).
  - Outage log (start time, end time, duration).
  - Summary statistics: total outages, average latency, maximum latency, total downtime.
- Dashboard auto-refreshes every 10 seconds without full page reload.

### Device Discovery (Future)
- Detect new devices joining the local network (passive or active scanning).
- Log device appearance with timestamp and MAC address.

### Alerts (Future)
- Initial: dashboard visual notification during active outages.
- Later: SMS, Discord, or email alerts for outages exceeding 2 minutes.

## Non-Functional Requirements
1. **Reliability:** The collector shall continue to run and log failed pings even when internet connectivity is lost, without crashing or requiring manual restart.
2. **Usability:** The web dashboard shall render correctly on mobile and desktop devices with responsive design.
3. **Security:** The dashboard and API shall be accessible only from the local network (private IP range); no external exposure.
4. **Data Retention:** Raw ping measurements shall be retained for at least 30 days. After that, data may be aggregated.
5. **Maintainability:** The codebase shall separate concerns into distinct modules: data collection (C++), database operations, web backend (Python/Flask), and frontend (HTML/JS).

## Constraints
- Hardware: Raspberry Pi 4 Model B, 64GB Lexar A1 microSD.
- Operating System: Raspberry Pi OS Lite (headless).
- Must run 24/7 without degradation.
- Network traffic generated must be negligible (a single ping every 10 seconds).
- No cloud dependencies; all components run locally.
- Development environment: SSH from Windows PC, Git for version control.

## Future Roadmap
- Speed test (upload/download bandwidth measurement).
- Physical status LED or OLED display.
- SMS/Discord alert integration.
- Automated ISP outage reports.
- Multi-target ping (latency to multiple hosts).
- Device fingerprinting (OS detection via TTL, MAC OUI lookup).

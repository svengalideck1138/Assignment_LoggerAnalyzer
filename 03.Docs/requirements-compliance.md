# Requirements Compliance Matrix

Source: the assignment brief *"Large-scale log analysis and transfer system"*.

> Every "Evidence" cell points at something that can be verified directly in
> this repository — a test name, a CI job, or a measured number reported by
> the running server.

---

## 1. Requirements and How They Are Met

Legend — ✅ met / ⭐ exceeded

### 1.1 Environment & Technology Stack

| ID | Requirement | Status | Implementation · Evidence |
|---|---|:--:|---|
| E1 | Client built with one of MFC / .NET / Qt / open source (e.g. Dear ImGui) | ⭐ | **Two** clients from the allowed list: C# WinForms (`01.Sources/CLIENT/CSHARP`) and cross-platform Dear ImGui (`01.Sources/CLIENT/CPP`), sharing one wire protocol |
| E2 | Server in Linux C++, Modern C++17 or newer required | ✅ | `SERVER/CMakeLists.txt`: `CXX_STANDARD 17`, `EXTENSIONS OFF`; builds warning-free under GCC (Linux) and MSVC (test project) in CI |
| E3 | TCP/IP sockets (Berkeley sockets, libuv, Boost.Asio allowed) | ✅ | Server: libuv 1.51.0 (allowed list). C++ client: raw Berkeley/Winsock sockets |

### 1.2 Strict Coding Rules

| ID | Requirement | Status | Implementation · Evidence |
|---|---|:--:|---|
| S1 | No `new`/`delete`/`malloc`/`free`/`calloc`/`realloc` anywhere in the sources | ⭐ | **Zero** tokens across all first-party C++ sources, comments included. `tools/check_forbidden.sh` runs as the CI job `Forbidden keyword scan` — **re-verified on every push**. (The C# client's `new` is garbage-collected managed allocation, not the raw-pointer manual allocation this rule forbids) |
| S2 | All dynamic memory via smart pointers / STL, zero leaks guaranteed by RAII | ⭐ | `std::unique_ptr` / `std::vector` / `std::string` plus RAII wrappers for sockets, the event loop and the PID file. CI re-runs all 36 unit tests under **ASan (leak detection included) + UBSan** on every push. The **client** side is covered by the headless engine probe (`CLIENT/CPP/tests`): the full connect → upload → cancel → re-upload → disconnect cycle ran under ASan + LeakSanitizer against a live server with **zero leaks reported** |

### 1.3 Client Application

| ID | Requirement | Status | Implementation · Evidence |
|---|---|:--:|---|
| C1 | File picker, upload button, real-time progress bar, result download button | ⭐ | Both clients implement all four. The C++ client adds a status LED (blink while connected, PWM-style breathe on failure), progress driven by server-analyzed bytes, and a live per-module count table |
| C2 | Transfer on a worker thread; the UI must never freeze during the 500MB transfer | ✅ | C#: `async/await` end to end. C++: dedicated worker thread + non-blocking sockets (250ms cancel slices, 20s write-stall detection); the UI copies a mutex-guarded snapshot per frame. Two demo GIFs of real 500MB transfers in `03.Docs/` |

### 1.4 Server Application (Linux C++)

| ID | Requirement | Status | Implementation · Evidence |
|---|---|:--:|---|
| V1 | Runs as a stable Linux background daemon/service | ⭐ | Both a self-daemonizing `--daemon` mode (double fork + flocked pidfile) **and** a systemd unit with an auto-registration script (`03.DaemonRegistration.sh`: substitutes paths/account, start at boot, restart within 5s) |
| V2 | Parse while receiving or in buffered units; loading the whole 500MB is forbidden (≤50MB peak recommended) | ⭐ | Each 1MiB chunk is parsed the moment it arrives, then discarded — nothing is spooled to disk either. **Measured peak RSS 2.8 MiB ≈ 1/17 of the budget** (section 2). The no-newline flood guard is pinned by the `oversize_line_is_dropped_not_accumulated` test |
| V3 | Parse with `std::string` operations or regex | ⭐ | `std::string_view` fixed-offset checks — allocation-free, exception-free, far faster than regex on 3.48M lines. `01.Sources/SERVER/src/parse/LogLine.cpp` |
| V4 | Task 1 — module occurrence counts grouped by time slot | ✅ | Hourly buckets (YYYY-MM-DD HH:00) × module → CSV section `HOURLY_MODULE_COUNTS`. Measured: 5 modules × 25 hourly buckets. Pinned by `buckets_group_by_hour` |
| V5 | Task 2 — extract the speed from `spd` lines, compute the average | ⭐ | Exact-anchor matching (rejects `xspd[` tails) + Neumaier compensated summation. Measured: **average 137500.000000** over 580,661 samples. Pinned by `spd_anchor_is_exact` / `spd_boundaries` |
| V6 | Produce a structured result.csv and send it to the client | ✅ | RFC 4180 quoting + UTF-8 BOM; disposition counters are mutually exclusive and sum exactly to total_lines. The e2e job verifies size and content on every push |

### 1.5 Core Evaluation Criteria

| ID | Requirement | Status | Implementation · Evidence |
|---|---|:--:|---|
| P1 | ~0.001% corrupted lines must never crash the server; skip, record, finish every valid line | ⭐ | Measured: **26 corrupted lines (0.00075%) skipped, 3,483,502 lines completed, zero crashes**. Failures are classified into 8 reasons with first-occurrence excerpts reported in the CSV. Every reason, the module whitelist (`BeyondLimit` kill) and the speed range gate are pinned by unit tests and re-run under ASan/UBSan |
| P2 | On a forced disconnect mid-transfer, both sides must survive and release resources | ⭐ | **Server side** — CI job `Server build + disconnect e2e`: injects a **TCP RST** mid-upload, then a follow-up session must complete with exact expected statistics; a cancel-mid-upload session must be answered with `ERROR(Cancelled)` and allow a re-upload on the same connection; the server's clean shutdown (all libuv handles released) is asserted too. **Client side** — the headless engine probe (`CLIENT/CPP/tests`, `abort` mode) uploads to a live server that is then `kill -9`-ed mid-transfer: the engine must transition to a failed state without crashing and its worker thread must exit and release the socket (verified under ASan + LeakSanitizer). The GUI clients were additionally hand-verified against the live aarch64 server: 500MB upload, mid-upload cancel with same-connection re-upload, local-file errors, window close during transfer, and a server hard-killed (`SIGKILL`) at ~26% of an upload — the client reported the error without crashing and completed a reconnect + full re-upload |

### 1.6 Deliverables

| ID | Requirement | Status | Implementation · Evidence |
|---|---|:--:|---|
| D1 | Full source on a personal GitHub | ✅ | https://github.com/svengalideck1138/Assignment_LoggerAnalyzer (public) |
| D2 | Compiled executables — Windows .exe and Linux binary | ✅ | `02.Release/`: Windows C# client .exe, Linux (aarch64) server and client. CI additionally produces x86_64 server/client artifacts on every push |
| D3 | English README (build steps · network architecture · memory strategy · corrupted-data algorithm) | ✅ | `README.md`: Building from Source / Network Architecture (two diagrams) / Memory Optimization Strategy / Corrupted Data Handling, plus a Verification section |

---

## 2. Measured Results (the provided 500MB log)

Environment: server on a **Raspberry Pi CM4 (eMMC 32GB, 8GB RAM, aarch64
Linux)**, client on Windows 11 x64, wired LAN (~112MiB/s effective).
Numbers are self-reported by the running server in `ANALYZE_DONE`
(elapsed time; peak RSS read from `/proc/self/status` VmHWM).

| Metric | Required / recommended | Measured | Notes |
|---|---|---|---|
| Peak server memory | ≤ 50MB recommended | **2.8 MiB** | ≈ **1/17 of the budget**; 2.3 MiB on a Wi-Fi re-run |
| Whole-file load | forbidden | **never** | 1MiB streaming chunks; nothing written to disk |
| Lines completed | finish every valid line | **3,483,502 / 3,483,528** | 26 corrupted (0.00075%) skipped, zero crashes |
| Average speed (task 2) | — | **137500.000000** (580,661 samples) | without the whitelist it would read 9.18×10¹⁵ |
| Upload + analysis | — | **~4.3s** on LAN | streaming: analysis ends with the upload; 110s on 4.4MiB/s Wi-Fi — time scales with bandwidth, memory does not |
| Memory leaks | zero (S2) | **0** | ASan/LeakSanitizer on every push in CI |

## 3. Continuous Verification (CI)

Five jobs run on every push (badge at the top of the README):
forbidden keyword scan (S1) · 36 unit tests + ASan/UBSan (S2·P1) ·
server build + forced-disconnect e2e (P2) · Linux and Windows client
builds (E1·C2).

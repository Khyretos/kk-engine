# Crash reports: what a game leaves behind when it dies

When a game dies (a bad pointer, an `abort()`, a divide by zero, an
exception nobody caught) `kke::crash` ([kke/CrashReporter.h](../engine/include/kke/CrashReporter.h))
writes one text file to the game's crash folder before the process goes.
It is on in every game, costs nothing while the game runs, and **never sends
anything by itself.**

```
kke crash report
app: My Game
engine: kke 0.1
reason: SIGSEGV (invalid memory access) at 0x0
time: 1793139208 (unix seconds)
language: nl
gpu: AMD Radeon RX 9070 XT (RADV GFX1201)
hardware tier: desktop
modules: Input, UI, OrbitCamera, RigidBodies, Models, Audio, ...

-- backtrace (most recent call first; use addr2line -e <binary> to read the addresses) --
./my_game(+0x27ab19) [0x557c64e6bb19]
...
-- last log lines --
[2026-10-27 23:13:27.652][engine][Application][My Game][info]: ...
```

Where: `crashes/` in the user data folder (`~/.local/share/KreativeKompas/crashes/`
on Linux), named `crash-<unix time>-<pid>.txt`. One crash, one file.

## What it holds, and what it doesn't

- **Reason and place:** the signal, the faulting address, a backtrace (addresses
  and, where the binary exports them, names: `addr2line -e game 0x...` reads the
  rest), or the message of an uncaught exception.
- **What the engine was doing:** the GPU, the hardware tier, the modules, the
  language, anything a game adds with `crash::setContext("scene", "level 3")`
  (16 entries), and the last 128 log lines (lines still in the logger's queue
  are not in it).
- **Not in it:** screenshots, save files, the command line, the environment.
- **Read it before sending.** The log lines can hold file paths and device
  names (a controller's serial number, say): the file is plain text for exactly
  this reason.

## Sending is the player's choice

`EngineSettings::privacy.sendCrashReports` is off by default and is a switch on
the Settings page (**Privacy**). The engine ships no server. A game that has
one tells the engine how to reach it:

```cpp
kke::crash::setUploader([](const std::string& path, const std::string& text) {
    return myServer.post("/crash", text); // true when it took it
});
```

At the next start (and when the switch is turned on) the pending reports go,
one at a time, only with the switch on and an uploader set. A sent report is
renamed `<name>.sent` and kept; one the server refused stays pending.

## Fitting it in

- **Developers:** `KKE_NO_CRASH_REPORTS=1` leaves your debugger's own handler in
  place. `KKE_TEST_CRASH=segv|abort|throw` makes a game die on its 20th frame,
  to see a report.
- **Signal safety:** the handler uses `open`, `write` and
  `backtrace_symbols_fd` only, on its own 64 KB stack (a stack overflow is
  reported too), then restores the default action and re-raises the signal, so
  the process ends as it would have: a core dump, the same exit status.
- **Platforms:** Linux, macOS and Android (no backtrace on Android's libc
  without `execinfo.h`: the report says so). **Not yet on Windows:**
  `install()` returns false there; a Windows backend is an
  `SetUnhandledExceptionFilter` with `CaptureStackBackTrace`.
- **Not done:** symbolised backtraces in the report itself (needs the binary's
  debug info shipped), a viewer for the folder, grouping similar crashes (a
  server's job).

Tested in `tests/test_crash_reporter.cpp`: the log ring, the sending rules,
and children that segfault, abort and throw, whose reports and exit statuses
the test reads.

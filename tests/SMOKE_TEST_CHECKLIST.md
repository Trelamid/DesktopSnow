# Native acceptance checklist — NOT EXECUTED

These tests require an actual Mac with Apple Silicon and a Windows PC.
No GUI test in this file should be marked passed unless a tester saw the result.

| ID | macOS M1 | Windows x64 | Pass criterion | Status |
|---|---|---|---|---|
| A1 | macOS 12+ | Windows 10/11 | Native build succeeds; app starts without visible console or focus stealing | PENDING |
| A2 | Retina 1x/2x | DPI 100/125/150/200% | Snow covers entire viewport, including corners, with correct scaling | PENDING |
| A3 | Finder/Safari | Explorer/Chrome | All clicks, double-clicks, drags and scrolling reach underlying windows | PENDING |
| A4 | Two physical displays | Two physical displays | Both displays have snowfall without black/opaque background | PENDING |
| A5 | External monitor at left/above | External monitor at left/above | Negative desktop coordinates render correctly | PENDING |
| A6 | Safari and Finder | Chrome and Explorer | Window movement/resize preserves local snow profiles; top edges stay aligned | PENDING |
| A7 | Overlapping windows | Overlapping windows | Hidden rear-window snow does not show through front windows | PENDING |
| A8 | Dock | Taskbar | Snow settles on visible supported system element | PENDING |
| A9 | Brush cursor mode | Brush cursor mode | Moving cursor along snow removes it | PENDING |
| A10 | Shift held and released | Shift held and released | Snowball formed; repeated releases near ball stack into snowman | PENDING |
| A11 | System status icon | System tray icon | Snowfall, wind, FPS, clear/pause and exit menu commands work | PENDING |
| A12 | Fullscreen app | Borderless fullscreen game | Overlay becomes transparent and physics pauses; resumes on exit | PENDING |
| A13 | Battery low-power | Battery saver | Frame rate reduces to 15 FPS or less and recovers | PENDING |
| A14 | Sleep/wake | Sleep/wake | No crashes, huge physics jumps or blank screen after resume | PENDING |
| A15 | Hot-plug external monitor | Hot-plug external monitor | No crash/leaked renderer; monitor list updates | PENDING |
| A16 | Spotlight/widget | Widgets/system UI | Unsupported OS surfaces degrade gracefully without blocking input | PENDING |
| A17 | ≥30 min idle test | ≥30 min idle test | CPU/RAM/GPU stable; no runaway memory, no accelerated CPU loop | PENDING |
| A18 | 30 min heavy snowfall | 30 min heavy snowfall | Frame pacing predictable and app interactions remain responsive | PENDING |

## Performance telemetry (record on each target device)

Record system version, display resolution/scale/count, AC vs battery,
quality (15/30/60), snow intensity (light/normal/blizzard) and the following:

1. CPU % (30-second mean and worst second) in Activity Monitor / Task Manager.
2. GPU % (or GPU time via Xcode Instruments / PresentMon) and compositor time.
3. Resident memory after launch, 5 min, and 30 min.
4. Total energy impact / battery discharge over a measured interval.
5. Interactive click-through latency and scene geometry alignment while dragging.
6. Confirm work in fullscreen game mode and correct idle after pause.

**Release gate:** No black overlays, unresponsive input, crash or continuous
memory growth. Set numeric CPU/GPU budgets after first measurements on a
real M1 and reference Windows PC; a simulation microbenchmark is not sufficient.
# QA report — Desktop Snow (2026-11-12)

## Проверки, выполненные в текущей среде

| Проверка | Метод | Результат |
|---|---|---|
| C++17 engine, GCC 14.2, x86_64 Linux | CMake Ninja Release | PASS |
| 13 групп функциональных тестов | `ctest` Release | PASS |
| Физика и геометрия сугробов | окно / перенос / ресайз / Z-order | PASS |
| Отрицательные координаты экранов | Unit test | PASS |
| Фиксированный seed и ветер | Unit test | PASS |
| Жёсткий лимит 1800 частиц на монитор | Unit test | PASS |
| Некорректные/слишком большие прямоугольники | Unit + randomized stress | PASS |
| 2200 кадров рандомизированных сценариев | Unit test, seed 20261112 | PASS |
| AddressSanitizer + UndefinedBehaviorSanitizer | Debug build + test suite | PASS |
| Синтаксис общих частей desktop (`main.cpp`/`renderer.cpp`) | Clang C++17 + GLFW API declaration stub | PASS |
| CPU benchmark (2 simulated monitors, 75 windows, 3000 frames per monitor) | Release x86_64 Linux | ~0.031–0.036 ms/monitor/frame |

Performance figure is **CPU simulation + mesh construction only**.

Audit update: 250-window scene benchmark (3840x2160, 100 repeated snapshots,
100 moving snapshots, Linux x86-64): ~0.014 ms per unchanged snapshot,
~0.17 ms per moving snapshot in this container. Previous implementation took
~0.49 ms for both cases. These values exclude the native API query, GPU and compositor.
Fixed the GLFW native-header include order on both platforms, fixed simulation
slowdown at 15 FPS, and added unchanged-scene caching and interval-sweep occlusion.
Native compilation remains **UNVERIFIED** until build logs pass.

It excludes OS window enumeration, GPU driver, OpenGL rendering and OS compositor.
It cannot establish real macOS M1 performance.

## Проверки, НЕ выполненные

| Проверка | Статус | Причина |
|---|---|---|
| Компиляция `.app` arm64 под macOS 12+ | NOT RUN | В окружении нет Xcode/macOS |
| Запуск на реальном Mac M1 | NOT RUN | Отсутствует устройство |
| Компиляция `.exe` для Windows x64 | NOT RUN | Нет Windows SDK/MSVC |
| GPU/WindowServer/DWM нагрузка | NOT RUN | Нет нативного графического композитора целевой ОС |
| Click-through на реальных приложениях | NOT RUN | Нет целевых ОС |
| Качество на Retina / HiDPI / нескольких физических мониторах | NOT RUN | Нет целевых устройств |
| Функции Dock, Spotlight, Win taskbar, fullscreen | NOT RUN | Нужен нативный GUI тест |

## Порог готовности к публичному релизу

1. Native CI compile & unit tests on `macos-15-arm64` and `windows-2022`.
2. Manual execution of every relevant row in `tests/SMOKE_TEST_CHECKLIST.md`.
3. Real performance measurement at 15/30/60 FPS with multiple physical displays.
4. Crash-free and stable-resident-memory overnight testing.
5. macOS signing/notarization and Windows signed distribution if publicly released.

Until those conditions are met, call this version **source prototype**, not a
verified production app.

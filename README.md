# Desktop Snow — macOS Apple Silicon / Windows

Independent desktop snowfall overlay, written in C++17. This project is an
original implementation of the functionality category; it does not include
Flake assets, binaries or reverse-engineered proprietary code.

**Current stage: testable source code, not a production release.** The physics
engine was compiled, unit-tested and benchmarked on Linux. The macOS and Windows
UI/native parts are supplied but have **not** been built or visually tested on
those operating systems in this environment. A GitHub Actions workflow is
included to build native macOS arm64 and Windows x64 artifacts once the code is
placed in a repository with Actions enabled. A passing build does not replace
hands-on GUI testing.

## Функции

- Снегопад поверх рабочего стола: лёгкий, обычный, метель.
- Накопление снега на верхних границах окон, панели задач Windows, некоторых
  системных элементах macOS и у нижней границы экрана.
- Сугробы следуют за окнами при перемещении и переживают изменение ширины.
- Геометрия перекрывающихся окон учитывается в порядке Z-order: снег за
  передними окнами скрывается.
- Смахивание снега курсором (режим Brush).
- Снежки и снеговики: в меню переключить Cursor tool → Snowballs, удерживать
  **Shift** и вести курсор по сугробу, затем отпустить Shift. Повторите рядом
  для второго/третьего шара. Клики при этом остаются доступными приложениям.
- Ветер влево/вправо, очистка всего снега, пауза, частота 15/30/60 FPS.
- Несколько мониторов, включая экраны с отрицательными координатами.
- Автоматическая пауза для распознанных полноэкранных приложений.
- Нативная иконка и меню в строке меню macOS / системном трее Windows.
- Сохранение настроек между запусками.
- Нет аккаунта, аналитики, лицензирования, сетевого обмена или захвата пикселей экрана.

## macOS M1 (macOS 12+)

Потребуются Xcode Command Line Tools, CMake и Ninja. Установка:

```bash
xcode-select --install
brew install cmake ninja
bash scripts/build_mac.sh && open build-native/desktop_snow.app
```

Первый запуск CMake загружает исходники **GLFW 3.4** с GitHub. Нужен интернет.
Результат компилируется в нативную архитектуру `arm64`, не через Rosetta.
GLFW использует OpenGL 3.3 Core на macOS (через системный драйвер Apple).
OpenGL устарел в API Apple, но macOS 12+ продолжает его поддерживать;
нативный Metal backend — возможное дальнейшее улучшение.

Приложение без значка в Dock, управляется через снежинку в строке меню.
Некоторые защищённые окна/системные панели могут отсутствовать в списке
CoreGraphics или иметь неточную геометрию — это зависит от macOS и прав доступа.
Приложение не делает скриншотов и не запрашивает доступ к содержимому экрана.

## Windows 10/11 x64

Потребуются Visual Studio 2022 с workload **Desktop development with C++**,
CMake (доступен в VS) и интернет для первого получения GLFW:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_windows.ps1
.\build-native\Release\desktop_snow.exe
```

Программа находится в системном трее (в скрытых значках панели задач).
Окна создаются как always-on-top + no-activate + mouse-pass-through.
Для координат при разном DPI включён `PerMonitorV2`.

## Как использовать

1. Запустите приложение. Снег начнёт падать автоматически.
2. Кликните на иконке снежинки (macOS — в строке меню; Windows — в трее).
3. Настройте `Snowfall`, `Wind` и `Frame rate`.
4. `Brush`: курсор автоматически очищает сугробы рядом с границами окон.
5. `Snowballs`: удерживайте Shift и проведите курсором по сугробу.
   Отпустите Shift, чтобы положить шар. Повторите рядом, чтобы сделать снеговика.
6. `Clear all snow` сбрасывает все сугробы, шары и частицы.

Настройки: macOS `~/Library/Application Support/DesktopSnow/settings.ini`,
Windows `%APPDATA%\DesktopSnow\settings.ini`.

## Оптимизация

- Максимум **1800 живых снежинок на монитор**, меньше в обычном режиме.
- Одна динамическая точечная GPU-отрисовка и одна отрисовка меша сугробов
  на монитор за кадр (всего до **2 draw calls на экран**).
- CPU-физика с индексированием коллизий по пространственным столбцам.
- Сугробы хранятся в 1D-профилях (9 экранных единиц на ячейку);
  не используется пиксельная карта всей поверхности окон.
- GPU-буфер сугробов обновляется только при изменении геометрии/высоты снега.
- Приостановленное приложение отрисовывает один пустой кадр и далее не
  выполняет симуляцию. Без busy-loop: `glfwWaitEventsTimeout`.
- Перечисление окон: Windows ~110 мс, macOS ~180 мс; в энергосбережении ~650 мс.
  Это ограничивает CPU, но при быстром перетаскивании окно может опережать сугроб.
- По умолчанию **30 FPS**, при энергосбережении или высокой CPU-нагрузке — 15 FPS.
  Пользователь может выбрать 60 FPS, но это повысит расход энергии.

### Что именно измерено

Проверен C++ движок на контейнере Linux x86-64. Пример локального прогона
после реализации отсечения скрытых поверхностей: 2 виртуальных монитора,
75 окон, 3000 кадров/монитор, метель; **0,033 мс CPU/кадр/монитор**
для физики + построения меша, усреднённо ~793 частицы на экран.
Это только вычислительная часть без GPU, оконной системы, композитора и
macOS/Windows; данные **не являются измерением M1**, не предсказывают напрямую
FPS в реальном приложении и не гарантируют энергопотребление.

Запустить тесты и бенчмарк (Linux/macOS/Windows):

```bash
cmake -S . -B build-tests -DSNOW_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
./build-tests/snow_benchmark 3000
```

Для AddressSanitizer/UBSan на Clang/GCC:

```bash
cmake -S . -B build-sanitized -DSNOW_BUILD_DESKTOP=OFF -DSNOW_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

Отдельный синтаксический анализ C++ UI-ядра без GLFW установленного в Linux:

```bash
bash scripts/check_frontend_syntax.sh
```

Отдельно измеряется обновление оконной геометрии: `./build-tests/snow_scene_benchmark`
(потребление ресурсов оконной системы этот тест не включает).

**Проверить реальные FPS, GPU %, CPU %, RAM, энергопотребление и click-through
надо на целевых машинах.** См. `tests/SMOKE_TEST_CHECKLIST.md`.

## Известные ограничения

- Нативные `.app` и `.exe` в данном архиве **не приложены**. Их надо собрать
  на целевых ОС или с помощью предоставленного CI. Открытие macOS-приложения
  после сторонней загрузки может потребовать обычной процедуры Gatekeeper;
  автоматической Apple notarization / Microsoft code signing нет.
- Накопление моделируется по верхним краям прямоугольных окон; объекты
  внутри окна, сложные контуры Dock/виджетов, физика у наклонных поверхностей
  и детальная модель снежного покрова пока не реализованы.
- macOS-специфичные Spotlight и виджеты отслеживаются только если они доступны
  через публичную CoreGraphics-геометрию.
- Полноэкранный режим определяется эвристически по размеру/стилю окна,
  поэтому некоторые игры могут требовать ручной паузы.
- Отдельная проверка Apple Silicon / Windows x64 в GUI обязательна до релиза.
- Возможна задержка сугробов при быстром перемещении окон: используются
  периодические обновления геометрии. Для устранения потребуется подписка
  на системные события перемещения окон (дополнительные разрешения на macOS).
- Автозапуска, установщика, обновлятора и пользовательских текстур/скинов нет.

## Структура

- `include/engine.hpp`, `src/engine.cpp` — детерминированная физика и меши.
- `src/renderer.cpp` — OpenGL 3.3 GPU-renderer.
- `src/main.cpp` — цикл, энергосбережение, мониторы, настройки.
- `src/platform_mac.mm` — AppKit + CoreGraphics.
- `src/platform_win.cpp` — Win32 + DWM + system tray.
- `tests/tests.cpp` — 13 групп юнит-тестов, включая коллизии и Z-order.
- `tests/benchmark.cpp` — CPU-микробенчмарк.
- `tests/scene_benchmark.cpp` — стоимость обновления сцены для 250 окон.
- `.github/workflows/build.yml` — будущая нативная CI-сборка и пакеты.

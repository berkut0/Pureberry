# Изменения в build_firmware.py (рефакторинг логгирования и clean)

Временный файл с описанием изменений. Можно удалить после ознакомления.

## Цели рефакторинга

- Упростить и сделать прозрачной логику чистки build-директории.
- Вернуть поведение логгирования «как было»: тихий консоль в normal, полный вывод в файл.
- Сохранить устойчивость к блокировкам файлов на Windows без лишней сложности.

---

## 1. Логгирование (`run_cmd` и вывод в консоль/файл)

**Было:**
- Параметр `inherit_stdio=True` для шага сборки прошивки — cmake/ninja писали напрямую в терминал, из-за чего в normal режиме шёл большой дамп.
- Две ветки: capture (quiet/normal) и streaming (verbose/debug), плюс отдельная ветка inherit_stdio.

**Стало:**
- Параметр `inherit_stdio` удалён. Всегда две ветки:
  - **quiet / normal**: `subprocess.run(..., capture_output=True)` → вывод подпроцессов захватывается, в консоль через `_emit_filtered_output` попадают только строки с ERROR/WARNING (и скриптовые INFO).
  - **verbose / debug**: `_run_cmd_streaming` — потоковый вывод в реальном времени.
- В файл пишется то, что проходит по уровню file handler: в **verbose/debug** уровень файла DEBUG — в лог попадает полный stdout/stderr подпроцессов (они логируются как `logger.debug`); в **normal** уровень файла INFO — в файл идут только сообщения скрипта уровня INFO и выше; в **quiet** уровень файла ERROR — в файл только ошибки.

**Итог по режимам:**
- **quiet**: консоль — только ошибки (и предупреждения из подпроцессов через `_emit_filtered_output`); файл — только сообщения уровня ERROR и выше.
- **normal**: консоль — INFO скрипта + WARN/ERROR из cmake/ninja/компилятора; файл — INFO и выше (построчный вывод подпроцессов в файл не попадает).
- **verbose / debug**: консоль — потоковый вывод подпроцессов; файл — полный лог (уровень DEBUG).

---

## 2. Чистка build-директории (clean)

**Было:**
- Попытка удалять всю базовую папку (`out/` или `build/`) или весь каталог патча — на Windows часто WinError 5 из-за открытого `build_firmware.log`.
- Fallback на `firmware-build-<timestamp>` при неудачном удалении `firmware-build`.
- Чистка вызывалась в разных местах (в т.ч. при создании build dir и перед шагом firmware).

**Стало:**
- Одна точка входа: `_clean_patch_build_dir(build_dir, logger)`.
  - Удаляет только `build_dir / "c"` (Heavy) и все `firmware-build*` (включая `firmware-build-*`).
  - Не удаляет корень `build_dir` и не трогает `build_firmware.log`.
- Вызов только в `main`, сразу после `create_build_dir` и **до** `attach_file_logger` — чтобы лог-файл ещё не был открыт.
- `create_build_dir` только выбирает базовую папку (`build/` по умолчанию или `--output`) и создаёт `build_base_dir / patch_name`, без чистки.
- Fallback на `firmware-build-<timestamp>` сохранён: если после clean каталог `firmware-build` всё ещё существует (заблокирован), используется новый каталог с меткой времени.

**Упрощение `clean_build_directory`:**
- Оставлены retry (до 3 попыток), снятие read-only через `onerror` и аккуратная обработка PermissionError/OSError.
- Один короткий комментарий про блокировки (indexer/AV/IDE), без лишних отладочных формулировок.

---

## 3. Дефолтная папка сборки

- По умолчанию используется **`build/`** в корне проекта (функция `_default_build_base_dir`).
- Раньше временно был `out/` — возврат к `build/` как к основной папке артефактов.

---

## 4. Удалённый и упрощённый код

- Удалён параметр и ветка `inherit_stdio` в `run_cmd`.
- Убраны комментарии про deadlock/IDE; в docstring `run_cmd` кратко описано поведение по режимам (capture+фильтрация vs streaming).
- В `build_firmware` короткий комментарий о том, что вывод cmake/ninja идёт через capture/фильтрацию и что сборку лучше запускать из обычного терминала (без песочницы), чтобы CMake мог удалять временные файлы.

---

## 5. Причина «ничего не билдилось» / зависаний

- Основная причина — **режим песочницы** в Cursor: в нём CMake не может удалять временные файлы в `CMakeFiles/CMakeScratch/TryCompile-*` (access denied).
- Без песочницы (обычный терминал или запуск с отключённой песочницей) та же версия скрипта успешно собирает прошивку.
- Рефакторинг не меняет эту зависимость от окружения; он лишь возвращает предсказуемое отображение логов и упрощает логику чистки.

---

## 6. Проверка

- Сборка проверена в режимах: quiet, normal, verbose (debug по структуре аналогичен verbose).
- В normal консоль снова «тихая»: шаги + предупреждения компилятора, без дампа cmake/ninja.
- UF2 генерируется; лог-файл `build/<patch>/build_firmware.log` — в verbose/debug в нём полный вывод подпроцессов, в normal/quiet только сообщения скрипта уровня INFO/ERROR и выше.

---

## 7. Рефакторинг I2S‑конфигов и пинов

### 7.1. Переход на стандартные `PICO_AUDIO_I2S_*`

**Было:**
- В разных местах использовались проектные макросы `I2S_DATA_PIN`, `I2S_CLOCK_PIN_BASE` и отдельный мост `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED` (через `#define` в `main.c` или через CMake).
- Источник истины по I2S был размазан: часть дефайнов в `core/src/main.c`, часть в `config.h`, часть в CMake.

**Стало:**
- Весь I2S‑конфиг теперь базируется на **стандартных макросах pico-extras**:
  - `PICO_AUDIO_I2S_DATA_PIN`
  - `PICO_AUDIO_I2S_CLOCK_PIN_BASE`
  - `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED`
- Эти макросы задаются в:
  - `core/src/config.h` — дефолты для репозитория;
  - `core/src/config_local.h` (локальный, не в git) — переопределения под конкретную плату;
  - `core/src/config_local.h.example` — пример и документация по пинам.
- Проектные `I2S_*` макросы удалены: код и pico-extras работают напрямую с `PICO_AUDIO_I2S_*`.

### 7.2. `pico_config.h` и единый источник для pico-sdk/pico-extras

**Было:**
- Отдельный “мост” в `pico_config.h`, который перетаскивал проектные макросы в `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED` и т.п.
- Риск расхождений между тем, что видит приложение (`main.c`), и тем, что видят исходники pico-extras (I2S PIO-программы).

**Стало:**
- `core/src/pico_config.h` упрощён до:
  - `#include "pico/config.h"`
  - `#include "config.h"`
- Глобальный заголовок для pico-sdk/pico-extras задаётся через `PICO_CONFIG_HEADER=pico_config.h` в `core/CMakeLists.txt`.
- Вся конфигурация (включая I2S) теперь приходит из одного места — `config.h`/`config_local.h`, и **одинаково видна** и приложению, и pico-extras.

### 7.3. `config.h` / `config_local.h` и фактические пины

**Было:**
- Исторические дефолты под старую разводку пинов (например, DIN=26, BCK=27, LCK=28), плюс ручные `#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED` в коде.

**Стало:**
- В `config.h`:
  - Дефолты I2S описаны только через `PICO_AUDIO_I2S_*`.
  - Явное пояснение, что `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED=0` означает:
    - `CLOCK_PIN_BASE = LRCLK (LCK)`
    - `CLOCK_PIN_BASE+1 = BCLK (BCK)`.
- В `config_local.h.example` зафиксирован рабочий пример для текущей платы:
  - `PICO_AUDIO_I2S_DATA_PIN = 5`  (DIN)
  - `PICO_AUDIO_I2S_CLOCK_PIN_BASE = 6` (LRCLK на 6, BCLK на 7)
  - `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED = 0`
- Реальный `config_local.h` (игнорируется git) используется для локальной настройки этих значений, без правок tracked‑файлов.

### 7.4. Использование макросов в `main.c` и Errata 9

**Было:**
- В `core/src/main.c` жёстко захардкожены пины через `I2S_DATA_PIN`/`I2S_CLOCK_PIN_BASE` и локальный `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED`.
- Код Errata 9 (конфигурация GPIO для I2S‑пинов) опирался на те же локальные макросы, что ломало единый источник истины.

**Стало:**
- В `main.c` явно используются **только** `PICO_AUDIO_I2S_DATA_PIN` и `PICO_AUDIO_I2S_CLOCK_PIN_BASE` (для `i2s_config` и Errata 9). `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED` задаётся в config и используется pico-extras при сборке I2S, в `main.c` не фигурирует.
- Errata 9:
  - `gpio_disable_pulls(...)`, `gpio_set_drive_strength(...)`, `gpio_set_slew_rate(...)` используют `PICO_AUDIO_I2S_*`, так что любые изменения пинов в конфиге автоматически учитываются и здесь.

### 7.5. Инварианты и sanity‑check

При текущей рабочей конфигурации (локальный `config_local.h`):
- `DIN = GPIO5`
- `LRCLK = GPIO6`
- `BCLK = GPIO7`
- `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED = 0` → `CLOCK_PIN_BASE = LRCLK`, `CLOCK_PIN_BASE+1 = BCLK`
- pico-extras берёт эти же макросы через `PICO_CONFIG_HEADER`, так что порядок пинов в PIO‑программе и в твоём коде полностью согласован.

---

## 8. Прочие существенные изменения

### 8.1. `core/CMakeLists.txt`

- Добавлено: `target_compile_definitions(..., PICO_CONFIG_HEADER=pico_config.h)` — чтобы pico-sdk и pico-extras подхватывали проектный конфиг.
- Убрана опция/дефайн для I2S swap из CMake: порядок пинов задаётся только в `config.h` / `config_local.h` (`PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED`), а не через `-D` при сборке.

### 8.2. `.gitignore`

- В игнор добавлены: `out/`, `core/src/config_local.h` (локальный конфиг не коммитится).

### 8.3. `core/src/config.h`

- Сильно почищен от длинных комментариев и документации; оставлены только рабочие вещи: условное подключение `config_local.h`, флаги/опции (USB MIDI, I2S), дефолты `PICO_AUDIO_I2S_*` с краткими пояснениями.

### 8.4. Репозиторий и локальный конфиг

- В репо хранится только `config_local.h.example`; сам `config_local.h` не трекается (пользователь копирует пример и правит локально).

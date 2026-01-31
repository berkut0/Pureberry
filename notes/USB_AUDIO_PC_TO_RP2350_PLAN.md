# План внедрения: USB Audio (ПК → RP2350) как вход для `adc~`

Цель: сделать RP2350 USB-аудио **устройством-приёмником** (“speaker”/OUT со стороны хоста), чтобы звук с ПК попадал в аудио-вход Heavy/`hvcc`, а в Pd-патче был доступен через `adc~`.

Контекст проекта:
- Сейчас аудио-движок работает в **core1**: `hv_processInlineInterleaved(..., NULL, out, nFrames)` и выдаёт звук в I2S. См. `core/src/main.c`.
- USB уже используется через TinyUSB (опционально) для **CDC+MIDI** (`ENABLE_USB_MIDI`) и собственных дескрипторов. См. `core/src/usb/*`.

## 0) Требования/ограничения (зафиксировать до кода)

1. Формат и режим:
   - Sample rate: **48 kHz** (совпадает с текущим I2S и Heavy).
   - Каналы: **2 (stereo)** (соответствует текущей конфигурации буферов).
   - Формат: **PCM S16LE** (напрямую конвертируется в float для Heavy).
2. ОС хоста:
   - Целевой хост — Windows/macOS/Linux; для максимальной совместимости стоит решить: **UAC2 (TinyUSB умеет)** или UAC1 (иногда проще для старых ОС).
   - Для стабильного приёма “ПК → девайс” почти наверняка нужен **feedback endpoint** (см. TinyUSB `uac2_speaker_fb`) — иначе со временем будет дрейф/переполнение/недобор.
3. Что остаётся без изменений (non-goals первой итерации):
   - Без “RP → ПК” (микрофон) и без USB-аудио-выхода на ПК.
   - Вывод остаётся на **I2S** как сейчас.

## 1) Флаги сборки и режимы USB

Сделать новый флаг CMake:
- `ENABLE_USB_AUDIO` (по умолчанию OFF).

Правило выбора стека:
- Если включён **любой** из `ENABLE_USB_MIDI` или `ENABLE_USB_AUDIO`, использовать **custom TinyUSB device** (как сейчас для MIDI), а не `pico_stdio_usb`.
- CDC (для логов) лучше оставлять включенным в этом режиме (удобно для отладки).

Файлы:
- `core/CMakeLists.txt` — добавить option и объединить условие выбора TinyUSB.
- `core/src/config.h` — при необходимости добавить имя устройства/строки для USB audio.

## 2) TinyUSB конфиг (tusb_config)

Задача: включить класс Audio и описать параметры аудио-функции.

1. В `core/src/tusb_config.h`:
   - `#define CFG_TUD_AUDIO 1`
   - Описать `CFG_TUD_AUDIO_FUNC_1_*` (частота, каналы RX, bytes/sample, размеры EP, буферы).
2. Брать за основу **TinyUSB example**:
   - `sdk/pico-sdk/lib/tinyusb/examples/device/uac2_speaker_fb`
   - или `sdk/pico-sdk/lib/tinyusb/examples/device/cdc_uac2` (если нужен и микрофон, но нам пока не нужен).

Критично:
- Для режима speaker с feedback нужны макросы/параметры для **EP OUT** и **EP feedback (IN)**.

## 3) USB дескрипторы (композитное устройство)

Задача: расширить текущие CDC+MIDI дескрипторы, добавив UAC2 speaker (+ feedback).

1. Добавить интерфейсы Audio Control + Audio Streaming (speaker) и эндпоинты:
   - ISO OUT (аудио данные с ПК → RP)
   - ISO IN (feedback)
2. Убедиться, что:
   - номера интерфейсов не конфликтуют с текущими CDC/MIDI;
   - endpoint addresses не конфликтуют (сейчас заняты 0x81, 0x02, 0x82, 0x03, 0x83).
3. Источник макросов:
   - использовать `TUD_AUDIO_*_DESCRIPTOR` макросы из TinyUSB примеров.

Файлы:
- `core/src/usb/usb_descriptors.c` — расширить конфигурационный дескриптор + строки.

## 4) Приём USB-аудио и ring-buffer (core0 → core1)

Задача: принять isochronous OUT данные на core0, положить в ring-buffer, а core1 будет читать их как вход для Heavy.

1. Добавить модуль:
   - `core/src/usb/usb_audio.h/.c`
2. В `usb_audio.c` реализовать колбэки TinyUSB Audio:
   - обработка alt setting (старт/стоп стрима): `tud_audio_set_itf_cb(...)`
   - приём пакетов: `tud_audio_rx_done_pre_read_cb(...)` + `tud_audio_read(...)`
   - feedback: вернуть текущий feedback (зависит от TinyUSB API; ориентироваться на `uac2_speaker_fb`).
3. Ring-buffer:
   - single-producer (core0) / single-consumer (core1).
   - хранить **кадры** (frames) `int16_t` interleaved L/R.
   - ёмкость: например 10–50 мс (в RAM), цель — держать заполнение около середины.
   - политика на underrun: выдавать **silence**.
   - политика на overrun: дропать **старое** (чтобы не раздувать задержку) и корректировать feedback.
4. Синхронизация:
   - индексы head/tail как `uint32_t` с атомиками/барьерами (`__atomic` или `__sync_synchronize()`).
   - без `queue_t` (слишком тяжело для аудио-потока).

## 5) Подключение входа в Heavy (`adc~`)

Задача: прокинуть входной буфер в `hv_processInlineInterleaved`.

1. В `core/src/main.c` (core1, `audio_core_main`):
   - завести `audio_in_buffer[AUDIO_BUFFER_SIZE]` (float interleaved).
   - для каждого блока:
     - прочитать N frames из ring-buffer (int16 interleaved),
     - конвертировать в float [-1..1],
     - вызвать `hv_processInlineInterleaved(heavy_context, audio_in_buffer, audio_out_buffer, block_size)`.
2. Совместимость каналов:
   - если host шлёт stereo, а патч/Heavy ожидает mono — подавать L в оба или L-only (решить правилом и зафиксировать).
   - по возможности, при старте вывести в лог (core0) количество in/out каналов Heavy (если API позволяет; иначе — фиксируем 2/2).

## 6) Feedback (стабильная синхронизация)

Задача: чтобы поток “ПК → девайс” не уплывал, host должен подстраивать скорость через feedback endpoint.

1. Выбрать модель:
   - асинхронный speaker с feedback (как `uac2_speaker_fb`).
2. Алгоритм:
   - измерять заполнение ring-buffer (frames) и стремиться к `target_fill`.
   - простая PI-регуляция: `fb = nominal_rate + kP*err + kI*err_int`.
   - ограничить диапазон коррекции (ppm), чтобы не вызывать артефакты.
3. Представление feedback:
   - для Full-Speed обычно используется формат 10.14 (см. пример TinyUSB).
   - реализацию копировать/адаптировать строго по примеру.

## 7) План тестирования (итеративно)

1. Enumerate/дескрипторы:
   - подключить к ПК, убедиться что определяется как аудио-устройство (speaker) + CDC (и MIDI если включено).
2. Поток:
   - воспроизвести с ПК тестовый сигнал (1 кГц sine).
   - на RP2350 проверить:
     - что ring-buffer заполняется,
     - что core1 читает (без underrun/overrun в steady-state).
3. Heavy/adc~:
   - сделать Pd-патч: `adc~` → `*~` → `dac~` (или любой простой обработчик).
   - убедиться, что звук с ПК проходит через патч и слышен на I2S.
4. Долгий прогон:
   - 10–30 минут, контролируя отсутствие накопления задержки и редких “щелчков”.

## 8) Риски и “план Б”

1. Если feedback окажется слишком сложным на старте:
   - временно сделать большой ring-buffer (латентность), чтобы “жило” несколько минут без drift-артефактов;
   - затем вернуться к полноценному feedback.
2. Если UAC2 будет проблемным на Windows:
   - рассмотреть UAC1 (но тогда дескрипторы/реализация будут другими).


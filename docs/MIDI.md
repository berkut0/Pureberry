# USB MIDI Integration Guide

## Overview

The RP2350 Pure Data firmware supports USB MIDI input, allowing your Pure Data patches to receive MIDI messages directly from USB hosts (computers, DAWs, MIDI controllers). The implementation uses a **composite USB device** that provides both CDC (debug serial) and MIDI interfaces simultaneously.

## Building with MIDI Support

USB MIDI support is **disabled by default** to maintain compatibility with existing workflows. To enable it, you have two options:

### Option 1: Build Script (Recommended)

Use the build script with the `-DENABLE_USB_MIDI=ON` flag:

```bash
python scripts/build_firmware.py your_patch.pd -DENABLE_USB_MIDI=ON
```

**Note:** The build script does not flash the firmware automatically. After building, flash the generated UF2 file manually using picotool or by dragging it to the RP2350's mass storage device.

### Option 2: CMake Configuration

If building manually with CMake:

```bash
cmake -DENABLE_USB_MIDI=ON ..
make
```

### Option 3: Edit Configuration (Not Recommended)

You can also manually define `ENABLE_USB_MIDI` in `core/src/config.h`, but this is not recommended as it requires modifying source files. The CMake option is preferred.

**Note:** When `ENABLE_USB_MIDI` is enabled, the firmware uses a custom TinyUSB implementation instead of `pico_stdio_usb`. Debug output via `printf` still works through the CDC interface.

## MIDI Message Protocol

The firmware uses **Pure Data standard MIDI object format** for maximum compatibility. This allows you to easily port patches from desktop Pure Data to the RP2350 without modification.

### Message Format

MIDI messages are sent to Pure Data patches as **lists** via receive objects. The format matches standard Pure Data MIDI objects (`[notein]`, `[ctlin]`, `[bendin]`, etc.), ensuring compatibility with existing Pure Data patches.

### Supported MIDI Messages

#### 1. Note Messages (`notein`)

**Receive Object:** `[r notein]`

**Message Format:** `[note, velocity, channel]`

- **note**: MIDI note number (0-127)
- **velocity**: Note velocity (0-127)
  - `velocity > 0`: Note On
  - `velocity = 0`: Note Off
- **channel**: MIDI channel (0-15)

**Example Usage:**
```pd
[r notein]
|
[unpack f f f]    # Unpack: note, velocity, channel
|  |  |
|  |  [print channel]
|  |
|  [/ 127]        # Normalize velocity to 0-1
|  |
[mtof]            # Convert note to frequency
```

**Implementation Details:**
- Note On messages (velocity > 0) are sent as `[note, velocity, channel]`
- Note Off messages are sent as `[note, 0, channel]` (velocity = 0)
- Both Note On and Note Off use the same `notein` receiver
- Velocity is always included, even for Note Off (unlike some MIDI implementations)

#### 2. Control Change (`ctlin`)

**Receive Object:** `[r ctlin]`

**Message Format:** `[controller, value, channel]`

- **controller**: CC number (0-127)
- **value**: CC value (0-127)
- **channel**: MIDI channel (0-15)

**Example Usage:**
```pd
[r ctlin]
|
[unpack f f f]    # Unpack: controller, value, channel
|  |  |
|  |  [print channel]
|  |
|  [/ 127]        # Normalize value to 0-1
```

#### 3. Pitch Bend (`bendin`)

**Receive Object:** `[r bendin]`

**Message Format:** `[bend, channel]`

- **bend**: Pitch bend value (0-16383, center = 8192)
- **channel**: MIDI channel (0-15)

**Example Usage:**
```pd
[r bendin]
|
[unpack f f]      # Unpack: bend, channel
|  |
|  [- 8192]       # Center at 0
|  |
[/ 8192]          # Normalize to -1.0 to +1.0
```

**Implementation Details:**
- Pitch bend is 14-bit (combines LSB and MSB from MIDI message)
- Range: 0-16383 (center = 8192 = no bend)
- Format matches Pure Data's `[bendin]` object

#### 4. Program Change (`pgmin`)

**Receive Object:** `[r pgmin]`

**Message Format:** `[program, channel]`

- **program**: Program number (0-127)
- **channel**: MIDI channel (0-15)

**Example Usage:**
```pd
[r pgmin]
|
[unpack f f]      # Unpack: program, channel
|  |
|  [print program]
```

#### 5. Channel Aftertouch (`touchin`)

**Receive Object:** `[r touchin]`

**Message Format:** `[pressure, channel]`

- **pressure**: Aftertouch pressure (0-127)
- **channel**: MIDI channel (0-15)

#### 6. Poly Aftertouch (`polytouchin`)

**Receive Object:** `[r polytouchin]`

**Message Format:** `[note, pressure, channel]`

- **note**: MIDI note number (0-127)
- **pressure**: Aftertouch pressure (0-127)
- **channel**: MIDI channel (0-15)

## Message Flow Architecture

```
USB Host (Computer/DAW/MIDI Controller)
    |
    v
[USB MIDI Class Interface]
    |
    v
[TinyUSB Stack] ──> tud_midi_packet_read()
    |
    v
[usb_midi.c] ──> process_midi_packet()
    |              Parse USB MIDI packet
    |              Extract: status, data1, data2, channel
    |
    v
[Heavy Context] ──> hv_sendMessageToReceiverFFF/FF()
    |                 Send list message to Pure Data
    |
    v
[Pure Data Patch]
    |
    v
[r notein] / [r ctlin] / etc.
    |
    v
[unpack f f f] ──> Extract individual values
```

## Technical Implementation

### USB MIDI Packet Format

USB MIDI uses a 4-byte packet format:

```
Byte 0: Cable Number (high nibble) + Code Index Number (low nibble)
Byte 1: MIDI Status Byte
Byte 2: MIDI Data Byte 1
Byte 3: MIDI Data Byte 2
```

The firmware extracts:
- **Message Type**: From status byte high nibble (0x80-0xF0)
- **Channel**: From status byte low nibble (0x0F)
- **Data**: From data bytes 1 and 2

### Heavy Message API

The firmware uses Heavy's message API to send lists to Pure Data:

- **`hv_sendMessageToReceiverFFF()`**: Sends 3-float list `[f1, f2, f3]`
  - Used for: `notein`, `ctlin`, `polytouchin`
  
- **`hv_sendMessageToReceiverFF()`**: Sends 2-float list `[f1, f2]`
  - Used for: `bendin`, `pgmin`, `touchin`

**Message Format String:**
- Heavy uses format strings similar to printf
- `"f f f"` = three floats
- `"f f"` = two floats
- Delay is set to `0.0` for immediate processing

### Receiver Object Names

The firmware sends messages to receive objects using these standard Pure Data names:

| MIDI Message Type | Receive Object | List Format |
|-------------------|----------------|-------------|
| Note On/Off | `notein` | `[note, velocity, channel]` |
| Control Change | `ctlin` | `[controller, value, channel]` |
| Pitch Bend | `bendin` | `[bend, channel]` |
| Program Change | `pgmin` | `[program, channel]` |
| Channel Aftertouch | `touchin` | `[pressure, channel]` |
| Poly Aftertouch | `polytouchin` | `[note, pressure, channel]` |

**Important:** These names match Pure Data's standard MIDI objects, allowing direct porting of desktop Pure Data patches.

## Example Patch

```pd
#N canvas 0 0 800 600 12;
#X obj 20 100 r notein;
#X obj 20 140 unpack f f f;
#X obj 20 200 mtof;
#X obj 20 260 hv.osc~ sine;
#X obj 20 346 dac~;
#X obj 113 200 / 127;
#X obj 20 307 *~;
#X obj 113 260 sig~;
#X obj 20 231 sig~;
#X connect 2 0 3 0;
#X connect 3 0 4 0;
#X connect 3 1 7 0;
#X connect 4 0 10 0;
#X connect 5 0 8 0;
#X connect 7 0 9 0;
#X connect 8 0 6 0;
#X connect 8 0 6 1;
#X connect 9 0 8 1;
#X connect 10 0 5 0;
```

## Debugging

### Debug Output

When MIDI is enabled, debug messages are printed via the CDC interface:

```
[MIDI] notein: note=60 vel=127 ch=0
[MIDI] ctlin: cc=1 val=64 ch=0
[MIDI] bendin: bend=8192 ch=0
```

### Message Queue

If the Heavy message queue is full, you'll see:

```
[MIDI] notein: note=60 vel=127 ch=0 (queue full)
```

This indicates the audio processing thread is busy. The message will be dropped.

### Verifying MIDI Reception

1. Check debug output via CDC (COM port on Windows, `/dev/ttyACM*` on Linux)
2. Look for `[MIDI]` log messages
3. Ensure receive objects in your patch match the names above
4. Use `[print]` objects to verify messages are received

## Compatibility Notes

### Pure Data Desktop Compatibility

The message format matches Pure Data's standard MIDI objects:
- `[notein]` → `[r notein]` with `[unpack f f f]`
- `[ctlin]` → `[r ctlin]` with `[unpack f f f]`
- `[bendin]` → `[r bendin]` with `[unpack f f]`
- `[pgmin]` → `[r pgmin]` with `[unpack f f]`

This means you can:
1. Develop patches in desktop Pure Data using standard MIDI objects
2. Replace `[notein]` with `[r notein]` and add `[unpack f f f]`
3. Flash to RP2350 - behavior should be identical

### Velocity Handling

Unlike some MIDI implementations, **velocity is always sent**, even for Note Off (where velocity = 0). This matches Pure Data's `[notein]` behavior:
- Note On: `[note, velocity>0, channel]`
- Note Off: `[note, 0, channel]`

You can filter Note Off messages in your patch if needed:
```pd
[r notein]
|
[unpack f f f]
|  |
|  [> 0]          # Filter: velocity > 0 (Note On only)
|  |
[spigot]          # Gate: only pass Note On
```

## Limitations

- **MIDI Output**: Currently only MIDI input is supported. MIDI output (sending from Pure Data to host) is not yet implemented.
- **Multiple Ports**: Only one MIDI port is supported.
- **SysEx**: System Exclusive messages are not supported.
- **MIDI Clock**: MIDI clock synchronization is not implemented.

## References

- [USB MIDI Specification](https://www.usb.org/sites/default/files/midi10.pdf)
- [TinyUSB Documentation](https://docs.tinyusb.org/)
- [Pure Data MIDI Objects](https://puredata.info/docs/manuals/pd/x3.htm#s4)
- [Heavy Compiler Documentation](https://enzienaudio.com/docs/)

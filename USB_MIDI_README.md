# USB MIDI Integration for RP2350 Pure Data Firmware

## Overview

USB MIDI support allows your RP2350 Pure Data firmware to receive MIDI messages directly from USB hosts (computers, DAWs, MIDI controllers) and use them to control Pure Data patches in real-time.

## Features

- **Composite USB Device**: CDC (debug serial) + MIDI (music input)
- **MIDI Message Support**: Note On/Off, Control Change, Pitch Bend, Program Change, Aftertouch
- **Pure Data Integration**: MIDI data automatically sent to receive objects in your patches
- **Debug Output**: printf/debug via USB CDC still works
- **Optional**: Can be disabled to use standard pico_stdio_usb

## Enabling USB MIDI

USB MIDI is **disabled by default** to maintain compatibility with existing workflows.

To enable USB MIDI, build with:

```bash
python scripts/build_firmware.py your_patch.pd -DENABLE_USB_MIDI=ON
```

Or manually in CMake:

```cmake
cmake -DENABLE_USB_MIDI=ON ..
```

## Using MIDI in Pure Data Patches

When MIDI is enabled, MIDI messages are sent to Pure Data using **standard Pure Data MIDI object format** for maximum compatibility. This allows you to easily port patches from desktop Pure Data to the RP2350.

### Note Messages (notein)

```pd
[r notein]           # Receives [note, velocity, channel] list
                     # Note On: velocity > 0
                     # Note Off: velocity = 0
```

Example usage:
```pd
[r notein]
|
[unpack f f f]       # Unpack: note, velocity, channel
|  |  |
|  |  [print channel]
|  |
|  [/ 127]           # Normalize velocity to 0-1
|  |
[mtof]               # Convert note to frequency
```

### Control Change (ctlin)

```pd
[r ctlin]            # Receives [controller, value, channel] list
```

Example usage:
```pd
[r ctlin]
|
[unpack f f f]       # Unpack: controller, value, channel
|  |  |
|  |  [print channel]
|  |
|  [/ 127]           # Normalize value to 0-1
```

### Pitch Bend (bendin)

```pd
[r bendin]           # Receives [bend, channel] list
                     # bend: 0-16383 (center = 8192)
```

Example usage:
```pd
[r bendin]
|
[unpack f f]         # Unpack: bend, channel
|  |
|  [- 8192]          # Center at 0
|  |
[/ 8192]             # Normalize to -1.0 to +1.0
```

### Program Change (pgmin)

```pd
[r pgmin]            # Receives [program, channel] list
```

### Channel Aftertouch (touchin)

```pd
[r touchin]          # Receives [pressure, channel] list
```

### Poly Aftertouch (polytouchin)

```pd
[r polytouchin]      # Receives [note, pressure, channel] list
```

## Example Pure Data Patch

See `pd-patches/midi_synth_example.pd` for a complete example of a MIDI-controlled synthesizer.

### Simple MIDI-controlled oscillator:

```pd
#N canvas 0 0 450 300 12;
#X obj 50 50 r notein;
#X obj 50 100 unpack f f f;
#X obj 50 150 mtof;
#X obj 50 200 hv.osc~ sine;
#X obj 50 250 *~;
#X obj 150 100 / 127;
#X obj 50 300 dac~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 1 1 5 0;
#X connect 2 0 3 0;
#X connect 3 0 4 0;
#X connect 5 0 4 1;
#X connect 4 0 6 0;
#X connect 4 0 6 1;
```

This patch:
1. Receives MIDI note messages via `[r notein]` as [note, velocity, channel] list
2. Unpacks the list to get note, velocity, and channel separately
3. Converts MIDI note number to frequency with `[mtof]`
4. Generates a sine wave at that frequency
5. Scales output by velocity (normalized to 0-1)
6. Outputs to `[dac~]` for audio

**Key points:**
- Use `[unpack f f f]` to extract note, velocity, channel from `[r notein]`
- Velocity = 0 means Note Off (you can filter these if needed)
- This format matches standard Pure Data `[notein]` object behavior

## USB Device Information

When connected to a computer:

- **Vendor ID**: 0xCafe (test VID)
- **Product ID**: 0x4009 (CDC+MIDI)
- **Device Name**: "Pure Data Audio Device"
- **CDC Interface**: "Pure Data Debug"
- **MIDI Interface**: "Pure Data MIDI"

## Testing MIDI

### On Windows

1. Build and flash firmware with `-DENABLE_USB_MIDI=ON`
2. Connect RP2350 via USB
3. Open Device Manager → Sound, video and game controllers
4. You should see "Pure Data Audio Device"
5. Use a MIDI host (MIDI-OX, Ableton Live, etc.) to send MIDI
6. Debug output still available via COM port (CDC)

### On macOS

1. Open Audio MIDI Setup
2. You should see "Pure Data MIDI" device
3. Use any MIDI software to send messages
4. Debug output via `/dev/cu.usbmodem*`

### On Linux

1. Check with `aconnect -l` to see MIDI devices
2. You should see "Pure Data MIDI"
3. Use `aconnect` to route MIDI from other software
4. Debug output via `/dev/ttyACM*`

## Technical Details

### Architecture

```
USB Host (Computer/DAW)
    |
    v
[USB MIDI Class] ─────> [usb_midi.c] ─────> [Heavy Context]
    |                        |                      |
    |                   Parse MIDI              Send to PD
    |                                               |
[USB CDC Class] ──> printf/debug                  v
                                          [Pure Data Patch]
                                          [r midi_note_on] etc.
```

### Initialization Sequence

1. `tusb_init()` - Initialize TinyUSB stack
2. Service USB for enumeration (100ms)
3. `usb_cdc_init()` - Initialize CDC for printf
4. Initialize Heavy context
5. `usb_midi_init(heavy_context)` - Connect MIDI to Heavy

### Main Loop

```c
while (true) {
    tud_task();              // Service USB stack (critical!)
    usb_midi_task();         // Process MIDI messages
    audio_producer_task();   // Generate audio
}
```

## Troubleshooting

### Device not recognized

- Ensure MIDI is enabled: check CMake output for "USB MIDI enabled"
- Try different USB cable/port
- Check Device Manager/System Information for enumeration errors
- Windows may cache old driver - try different PID or uninstall device

### No MIDI messages received

- Verify MIDI is being sent (test with MIDI monitor software)
- Check firmware debug output via CDC for "[MIDI]" log messages
- Ensure receive objects in Pure Data patch use standard names: `notein`, `ctlin`, `bendin`, `pgmin`
- Use `[unpack]` to extract values from lists (e.g., `[unpack f f f]` for `[r notein]`)
- Try simple test patch (see examples)
- Note: Velocity = 0 in `notein` means Note Off - filter these if you only want Note On

### printf not working

- CDC should still work alongside MIDI
- Check COM port/ttyACM/ttyUSB in terminal
- Baud rate doesn't matter (USB CDC ignores it)
- May need to open CDC port before printf appears

### Compilation errors

- Ensure pico-sdk is up to date (2.0.0+)
- Check TinyUSB is available in SDK
- Verify `tusb_config.h` is found (include path)
- Clean build directory and rebuild

## Performance Notes

- USB servicing (`tud_task()`) must be called frequently
- MIDI processing is non-blocking and very fast
- No impact on audio latency when MIDI not used
- CDC debug adds minimal overhead

## Disabling USB MIDI

To revert to standard CDC-only mode:

```bash
python scripts/build_firmware.py your_patch.pd
# (without -DENABLE_USB_MIDI flag)
```

Or:

```bash
cmake -DENABLE_USB_MIDI=OFF ..
```

This restores `pico_stdio_usb` behavior with automatic USB management.

## Future Enhancements

Potential additions:
- MIDI output (send from Pure Data to host)
- Multiple MIDI ports
- MIDI clock sync
- SysEx support
- MIDI learn functionality

## References

- [USB MIDI Specification](https://www.usb.org/sites/default/files/midi10.pdf)
- [TinyUSB Documentation](https://docs.tinyusb.org/)
- [Pure Data MIDI Objects](https://puredata.info/docs/manuals/pd/x3.htm#s4)

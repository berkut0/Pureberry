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

When MIDI is enabled, the following receive objects automatically receive MIDI data:

### Note Messages

```pd
[r midi_note_on]     # Receives note number (0-127) when note pressed
[r midi_note_off]    # Receives note number (0-127) when note released
[r midi_velocity]    # Receives velocity (0-127) for note on
[r midi_channel]     # Receives MIDI channel (0-15)
```

### Control Change

```pd
[r midi_cc_num]      # Receives CC number (0-127)
[r midi_cc_val]      # Receives CC value (0-127)
[r midi_channel]     # Receives MIDI channel (0-15)
```

### Pitch Bend

```pd
[r midi_pitchbend]   # Receives pitch bend (-1.0 to +1.0)
[r midi_channel]     # Receives MIDI channel (0-15)
```

### Other Messages

```pd
[r midi_program]           # Receives program change (0-127)
[r midi_aftertouch]        # Receives channel aftertouch (0-127)
[r midi_poly_note]         # Receives note for poly aftertouch
[r midi_poly_pressure]     # Receives pressure for poly aftertouch
```

## Example Pure Data Patch

See `pd-patches/midi_synth_example.pd` for a complete example of a MIDI-controlled synthesizer.

### Simple MIDI-controlled oscillator:

```pd
#N canvas 0 0 450 300 12;
#X obj 50 50 r midi_note_on;
#X obj 50 100 mtof;
#X obj 50 150 hv.osc~ sine;
#X obj 50 200 *~;
#X obj 150 50 r midi_velocity;
#X obj 150 100 / 127;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
#X connect 4 0 5 0;
#X connect 5 0 3 1;
```

This patch:
1. Receives MIDI note on messages
2. Converts MIDI note number to frequency
3. Generates a sine wave at that frequency
4. Scales output by velocity (0-1)

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
- Ensure receive objects in Pure Data patch match names above
- Try simple test patch (see examples)

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

# ESP32 Firmware — AI Pet Robot V1

PlatformIO + Arduino C++ firmware for the robot body. Modular: each
hardware peripheral and each FSM lives in its own folder; `main.cpp` only
wires them together.

## Build & flash

```bash
# one-time
cp include/secrets.h.example include/secrets.h    # edit Wi-Fi + laptop IP

pio run                # compile (env esp32dev)
pio run -t upload      # flash over USB
pio device monitor     # serial logs @115200

# native protocol unit tests (no hardware required)
bash test/test_protocol/run_native_tests.sh
```

Environment: `platform = espressif32`, `board = esp32dev`, ArduinoJson v7,
WebSockets 2.3.x, Adafruit GFX/SSD1306/NeoPixel. Verified build: RAM 14.4 %,
Flash 74 %, no warnings with `-Wall`.

## Module map

| Module | Responsibility | Key safety/design rule |
|---|---|---|
| `config.h` | every pin + timing constant | no magic numbers elsewhere |
| `motor/` | TB6612, LEDC PWM, motion commands | duration-capped, watchdog 1.5 s, STOP default |
| `display/` | SSD1306 faces + one-shot animations | redraw on change only, millis() deadlines |
| `touch/` | TTP223 sampling | 60 ms debounce → exactly one event per touch |
| `sound/` | buzzer tone sequences | non-blocking sequencer |
| `led/` | WS2812 modes (solid/pulse/thinking/happy/error) | 25 fps frame cap, brightness ceiling 160 |
| `protocol/` | JSON whitelist filter + enum tables | host-testable (no `Arduino.h`) |
| `connection/` | Wi-Fi + WS + heartbeat + backoff | FSM separate from behavior |
| `behavior/` | personality + reaction to AI commands | works 100 % offline |

## Serial log tags

`[motor] [display] [touch] [sound] [led] [conn] [behavior] [main]` — grep
the monitor output with these tags while debugging.

## Adding a V2 sensor (sketch)

1. New folder `src/<sensor>/` with the same header/impl split.
2. Register its event in `protocol/` (serialize + docs).
3. Feed it into `behavior/` as an input like `touch`.
No changes to `connection/` or the dashboard are required unless you add
new wire fields.

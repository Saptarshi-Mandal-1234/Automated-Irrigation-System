# IoT-Based Automated Smart Irrigation System Using ESP32

An academic IoT project: an ESP32 controller reads soil moisture, soil pH,
temperature, and humidity, and automatically controls a water pump using
crop-specific thresholds and hysteresis logic. A mobile web app connects
over Bluetooth Low Energy (BLE) to select the crop, monitor live sensor
data, and issue manual/emergency commands.

## ⚠️ Current project status (read this first)

This repository contains a **working software design and simulation**, but
has **not yet been verified against real hardware end-to-end**. Specifically:

| Component | Status |
|---|---|
| Project documentation (37-section spec, DFDs, algorithms) | ✅ Complete |
| Logic simulator (browser, no hardware) | ✅ Complete, runs standalone |
| Circuit diagram + visual simulation (browser) | ✅ Complete, runs standalone |
| Tinkercad simulation sketch (Arduino Uno substitute sensors) | ✅ Complete, tested logic only in Tinkercad's simulator |
| ESP32 BLE firmware (`firmware/esp32-irrigation-ble`) | ✅ Written, implements the full BLE contract and control logic — **not yet flashed/tested on real hardware** |
| Mobile web app (`mobile-app/index.html`) | ✅ UI + BLE client code complete, includes a no-hardware Demo Mode — **BLE path never tested against a real ESP32** |
| Sensor calibration | ⚠️ Calibration utility provided, but the constants in firmware are placeholders until you run it on your own sensors |
| End-to-end test (app ↔ ESP32 ↔ pump) | ❌ Not yet performed |

If you're using this as a portfolio piece, be upfront in interviews/demos
that the firmware and app are implemented and internally consistent (same
UUIDs, same JSON contract) but not yet hardware-verified — that's an honest
and normal state for an in-progress embedded project.

## Repository structure

```
docs/                          Full project documentation (spec, DFDs, algorithms, test plan)
firmware/
  esp32-irrigation-ble/        Real ESP32 + BLE firmware (target hardware)
    src/main.cpp
    calibration/calibrate_sensors.ino   Run first, to get real ADC calibration values
    platformio.ini              PlatformIO build config
    libraries.txt                Arduino IDE library list
  tinkercad-uno-sim/            Arduino Uno + potentiometer substitute sketch, for Tinkercad only
mobile-app/
  index.html                    Web Bluetooth mobile app (Chrome/Android/desktop only)
circuit/
  circuit-visual-simulation.html  Animated circuit diagram (browser, no hardware needed)
  logic-simulator.html            Pure irrigation-logic simulator (browser, no hardware needed)
LICENSE
README.md
```

## Getting started

### 1. Try it with no hardware
Open `circuit/logic-simulator.html` or `mobile-app/index.html` (which
auto-starts Demo Mode) in a browser. This exercises the same irrigation,
pH, and safety logic that runs on the ESP32, with simulated sensor drift.

### 2. Calibrate your sensors
Flash `firmware/esp32-irrigation-ble/calibration/calibrate_sensors.ino` to
your ESP32, follow the Serial Monitor prompts, and record the raw ADC
values for dry air / water / pH 4.0 / pH 7.0.

### 3. Flash the real firmware
Update the calibration constants at the top of
`firmware/esp32-irrigation-ble/src/main.cpp` with the values from step 2,
then flash it (Arduino IDE using `libraries.txt`, or PlatformIO using
`platformio.ini`).

### 4. Connect the mobile app
Open `mobile-app/index.html` in Chrome on Android or desktop (Web
Bluetooth is not supported on iOS Safari). Tap **Scan & Connect** and
select your ESP32 device.

### 5. Wire the circuit
See `docs/PROJECT_DOCUMENTATION.md` (Hardware Requirements / Hardware
Architecture sections) and the comment header in `main.cpp` for the pin
list.

## BLE contract

| Characteristic | UUID | Direction |
|---|---|---|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | — |
| CropConfig | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | Phone → ESP32 (write) |
| SensorData | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | ESP32 → Phone (notify) |
| ManualControl | `6e400004-b5a3-f393-e0a9-e50e24dcca9e` | Phone → ESP32 (write) |

## License

MIT — see `LICENSE`.

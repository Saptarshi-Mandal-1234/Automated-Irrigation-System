/*
  Sensor Calibration Utility — Soil Moisture + pH
  ------------------------------------------------
  Run THIS sketch by itself (separate upload from main firmware) to
  determine the raw ADC values needed in main.cpp's calibration constants:
      MOIST_DRY_RAW, MOIST_WET_RAW, PH_RAW_AT_PH4, PH_RAW_AT_PH7

  HOW TO USE:
    1. Flash this sketch to your ESP32.
    2. Open Serial Monitor at 115200 baud.
    3. Follow the on-screen prompts for each step below.
    4. Record the printed raw values.
    5. Paste them into main.cpp's calibration constants, then re-flash
       the main BLE firmware.

  STEPS:
    A. Soil moisture — dry: hold the sensor in open air (no soil/water).
       Note the "Moisture RAW" value after it stabilizes -> this is MOIST_DRY_RAW.
    B. Soil moisture — wet: submerge the sensor fully in a glass of water.
       Note the value -> this is MOIST_WET_RAW.
    C. pH — buffer 4.0: rinse the probe, dip in pH 4.0 calibration buffer,
       wait ~30s for the reading to settle. Note the value -> PH_RAW_AT_PH4.
    D. pH — buffer 7.0: rinse the probe, dip in pH 7.0 calibration buffer,
       wait ~30s. Note the value -> PH_RAW_AT_PH7.
       (A pH 10.0 buffer point can be added for a 3-point calibration if
       your probe's response is non-linear near the extremes — not required
       for typical soil pH ranges of 4-9.)
*/

#define MOISTURE_PIN 34
#define PH_PIN       35

const int SAMPLES = 20;

int averagedRead(int pin) {
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(pin);
    delay(20);
  }
  return sum / SAMPLES;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // 0-4095 on ESP32
  delay(1000);
  Serial.println();
  Serial.println("=== Sensor Calibration Utility ===");
  Serial.println("Readings refresh every 2 seconds. Follow the steps in the");
  Serial.println("header comment of this file (dry air / water / pH buffers).");
  Serial.println();
}

void loop() {
  int moistRaw = averagedRead(MOISTURE_PIN);
  int phRaw = averagedRead(PH_PIN);

  Serial.print("Moisture RAW: ");
  Serial.print(moistRaw);
  Serial.print("   |   pH RAW: ");
  Serial.println(phRaw);

  delay(2000);
}

/*
  Smart Irrigation System — Tinkercad Simulation Version
  ------------------------------------------------------
  Substitutions made for Tinkercad's supported component set:
    - Soil moisture sensor  -> Potentiometer #1 (A0)
    - Soil pH sensor        -> Potentiometer #2 (A1)
    - DHT11/DHT22           -> DHT22 (native Tinkercad support, pin 2)
    - Relay + water pump    -> Relay module (pin 7) driving an LED+resistor
                               as the "pump" load, OR a small DC motor if
                               you add one from Tinkercad's parts bin
    - Manual ON / OFF       -> Two pushbuttons (pins 5, 6)
    - Status                -> 16x2 LCD (I2C or parallel) + onboard LED

  WIRING LIST (Arduino Uno, since Tinkercad's ESP32 support is limited —
  the logic is identical to what would run on the real ESP32):

    Potentiometer 1 (Soil Moisture sim)
      - Left pin  -> GND
      - Right pin -> 5V
      - Wiper     -> A0

    Potentiometer 2 (Soil pH sim)
      - Left pin  -> GND
      - Right pin -> 5V
      - Wiper     -> A1

    DHT22
      - VCC  -> 5V
      - GND  -> GND
      - DATA -> Digital pin 2  (add a 10k pull-up resistor between DATA and 5V)

    Relay Module (pump driver)
      - VCC -> 5V
      - GND -> GND
      - IN  -> Digital pin 7
      - COM/NO on relay's high-voltage side -> LED (+ resistor) standing in
        for the pump, powered from a separate 5V rail

    Pushbutton 1 (Manual Pump ON)
      - One leg -> Digital pin 5
      - Other leg -> GND
      - (use INPUT_PULLUP, no external resistor needed)

    Pushbutton 2 (Manual Pump OFF / Mode toggle)
      - One leg -> Digital pin 6
      - Other leg -> GND
      - (use INPUT_PULLUP)

    16x2 LCD (parallel, standard Tinkercad wiring)
      - RS -> pin 12
      - E  -> pin 11
      - D4 -> pin 4
      - D5 -> pin 8      (adjust freely, just keep consistent with code below)
      - D6 -> pin 9
      - D7 -> pin 10
      - VSS -> GND, VDD -> 5V, V0 -> wiper of a 10k contrast potentiometer
      - RW -> GND, A -> 5V (backlight +), K -> GND (backlight -)

  LIBRARIES NEEDED IN TINKERCAD (Code > "+" > search, or add manually):
    - DHT sensor library (Adafruit)
    - LiquidCrystal (built-in, no need to add)

  HOW TO USE IN SIMULATION:
    - Turn Potentiometer 1 down (low) to simulate dry soil -> pump turns ON
    - Turn it back up past the target level -> pump turns OFF
    - Turn Potentiometer 2 outside the crop's pH range -> LCD shows "pH Warning"
    - Press button 5 / 6 to test manual override
    - Unplug/disconnect the DHT wire mid-run to simulate a sensor fault
      (or just watch the serial monitor for fault detection logic)
*/

#include <DHT.h>
#include <LiquidCrystal.h>

// ---------- Pin definitions ----------
#define MOISTURE_PIN   A0
#define PH_PIN         A1
#define DHTPIN         2
#define DHTTYPE        DHT22
#define RELAY_PIN      7
#define BTN_MANUAL_ON  5
#define BTN_MANUAL_OFF 6

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal lcd(12, 11, 4, 8, 9, 10);

// ---------- Crop configuration (Tomato example) ----------
struct CropConfig {
  const char* name;
  int moistureMin;
  int moistureTarget;
  float phMin;
  float phMax;
};

CropConfig crop = {"Tomato", 45, 65, 5.5, 7.0};

// ---------- System state ----------
bool pumpOn = false;
bool manualMode = false;
unsigned long pumpStartTime = 0;
const unsigned long MAX_RUNTIME_MS = 20000UL; // 20s cap for simulation
unsigned long lastSampleTime = 0;
const unsigned long SAMPLE_INTERVAL_MS = 1000UL;

void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BTN_MANUAL_ON, INPUT_PULLUP);
  pinMode(BTN_MANUAL_OFF, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);

  dht.begin();
  lcd.begin(16, 2);
  lcd.print("Irrigation Sys");
  delay(1500);
  lcd.clear();
}

void loop() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) return;
  lastSampleTime = millis();

  // ---- Read simulated sensors ----
  int moistureRaw = analogRead(MOISTURE_PIN);         // 0-1023
  int soilMoisture = map(moistureRaw, 0, 1023, 0, 100); // -> 0-100%

  int phRaw = analogRead(PH_PIN);
  float soilPH = map(phRaw, 0, 1023, 0, 140) / 10.0;   // -> 0.0-14.0

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  bool sensorFault = isnan(temperature) || isnan(humidity);

  // ---- Manual mode buttons ----
  if (digitalRead(BTN_MANUAL_ON) == LOW) manualMode = true, pumpOn = true;
  if (digitalRead(BTN_MANUAL_OFF) == LOW) manualMode = true, pumpOn = false;

  // ---- Automatic irrigation decision (hysteresis) ----
  if (!manualMode && !sensorFault) {
    if (soilMoisture < crop.moistureMin) {
      pumpOn = true;
    } else if (soilMoisture >= crop.moistureTarget) {
      pumpOn = false;
    }
    // else: hold current state (hysteresis band)
  }

  // ---- pH monitoring (alert only, no correction) ----
  bool phOK = (soilPH >= crop.phMin && soilPH <= crop.phMax);

  // ---- Safety Manager ----
  String safetyReason = "";
  if (sensorFault) { pumpOn = false; safetyReason = "Sensor fault"; }

  if (pumpOn) {
    if (pumpStartTime == 0) pumpStartTime = millis();
    if (millis() - pumpStartTime >= MAX_RUNTIME_MS) {
      pumpOn = false;
      safetyReason = "Max runtime";
    }
  } else {
    pumpStartTime = 0;
  }

  digitalWrite(RELAY_PIN, pumpOn ? HIGH : LOW);

  // ---- Display on LCD ----
  lcd.setCursor(0, 0);
  lcd.print("M:");
  lcd.print(soilMoisture);
  lcd.print("% P:");
  lcd.print(soilPH, 1);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  if (sensorFault) {
    lcd.print("SENSOR FAULT!   ");
  } else if (!phOK) {
    lcd.print("Pump:");
    lcd.print(pumpOn ? "ON " : "OFF");
    lcd.print(" pH WARN");
  } else {
    lcd.print("Pump:");
    lcd.print(pumpOn ? "ON " : "OFF");
    lcd.print(" pH OK   ");
  }

  // ---- Serial debug (watch this in Tinkercad's serial monitor) ----
  Serial.print("Moisture: "); Serial.print(soilMoisture); Serial.print("% | ");
  Serial.print("pH: "); Serial.print(soilPH); Serial.print(" ("); Serial.print(phOK ? "OK" : "WARNING"); Serial.print(") | ");
  Serial.print("Temp: "); Serial.print(temperature); Serial.print("C | ");
  Serial.print("Humidity: "); Serial.print(humidity); Serial.print("% | ");
  Serial.print("Pump: "); Serial.print(pumpOn ? "ON" : "OFF");
  if (safetyReason != "") { Serial.print(" [SAFETY: "); Serial.print(safetyReason); Serial.print("]"); }
  Serial.print(" | Mode: "); Serial.println(manualMode ? "MANUAL" : "AUTO");
}

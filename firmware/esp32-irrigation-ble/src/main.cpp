/*
  Smart Irrigation System — ESP32 BLE Firmware
  ---------------------------------------------
  Target board : ESP32 (any dev board with BLE)
  Libraries    : ESP32 BLE Arduino (built into the ESP32 board package)
                 ArduinoJson (install via Library Manager)
                 DHT sensor library (Adafruit) + Adafruit Unified Sensor

  This firmware implements the exact GATT contract expected by the
  "Irrigation Control" mobile web app (irrigation-mobile-app.html):

    Service:            6e400001-b5a3-f393-e0a9-e50e24dcca9e
    CropConfigChar  (W): 6e400002-b5a3-f393-e0a9-e50e24dcca9e  -> phone writes crop JSON
    SensorDataChar  (N): 6e400003-b5a3-f393-e0a9-e50e24dcca9e  -> ESP32 notifies status JSON
    ManualCtrlChar  (W): 6e400004-b5a3-f393-e0a9-e50e24dcca9e  -> phone writes mode/manual/estop JSON

  WIRING (real ESP32, not the Tinkercad/Uno substitute):
    Capacitive soil moisture sensor  -> AOUT to GPIO34 (ADC1_CH6)
    Soil pH sensor (analog board)    -> AOUT to GPIO35 (ADC1_CH7)
    DHT22 data pin                   -> GPIO4  (10k pull-up to 3V3)
    Relay module IN pin              -> GPIO26
    Water-level sensor (digital)     -> GPIO27 (optional; LOW = low water)
    All sensor/relay VCC             -> 3V3 or 5V per module spec, GND common with ESP32

  NOTE ON ADC CALIBRATION:
    Raw ADC readings from a capacitive moisture sensor / analog pH board
    must be calibrated against your specific sensor (dry-air reading,
    fully-wet reading, and known pH buffer solutions). The MOIST_DRY_RAW /
    MOIST_WET_RAW / PH_* constants below are placeholders — replace them
    with values you measure from your own sensors before trusting the
    output.
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ---------- BLE UUIDs (must match the mobile app) ----------
#define SERVICE_UUID       "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CROP_CONFIG_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define SENSOR_DATA_UUID   "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define MANUAL_CTRL_UUID   "6e400004-b5a3-f393-e0a9-e50e24dcca9e"

// ---------- Pin definitions ----------
#define MOISTURE_PIN   34
#define PH_PIN         35
#define DHTPIN         4
#define DHTTYPE        DHT22
#define RELAY_PIN      26
#define WATER_LEVEL_PIN 27   // optional; comment out usage below if not wired

// ---------- Calibration placeholders (REPLACE with measured values) ----------
const int MOIST_DRY_RAW = 3000;   // ADC reading in fully dry air
const int MOIST_WET_RAW = 1200;   // ADC reading fully submerged in water
const int PH_RAW_AT_PH4 = 2032;   // ADC reading in pH 4.0 buffer
const int PH_RAW_AT_PH7 = 1500;   // ADC reading in pH 7.0 buffer

DHT dht(DHTPIN, DHTTYPE);

BLEServer* pServer = nullptr;
BLECharacteristic* pCropConfigChar = nullptr;
BLECharacteristic* pSensorDataChar = nullptr;
BLECharacteristic* pManualCtrlChar = nullptr;
bool deviceConnected = false;

// ---------- Crop configuration (defaults; overwritten by app) ----------
struct CropConfig {
  String name = "Tomato";
  int moistureMin = 45;
  int moistureTarget = 65;
  float phMin = 5.5;
  float phMax = 7.0;
};
CropConfig crop;

// ---------- System state ----------
bool pumpOn = false;
bool manualMode = false;
bool manualCommandOn = false;
bool emergencyStop = false;
unsigned long pumpStartTime = 0;
const unsigned long MAX_RUNTIME_MS = 5UL * 60UL * 1000UL; // 5 min real-world cap
unsigned long lastSampleTime = 0;
const unsigned long SAMPLE_INTERVAL_MS = 2000UL;

// ================= BLE server callbacks =================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override { deviceConnected = true; }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    BLEDevice::startAdvertising(); // resume advertising after disconnect
  }
};

// Phone -> ESP32: crop config write
class CropConfigCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue().c_str();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) return;
    crop.name = doc["crop"] | crop.name;
    crop.moistureMin = doc["moistureMin"] | crop.moistureMin;
    crop.moistureTarget = doc["moistureTarget"] | crop.moistureTarget;
    crop.phMin = doc["phMin"] | crop.phMin;
    crop.phMax = doc["phMax"] | crop.phMax;
  }
};

// Phone -> ESP32: manual control / mode / emergency stop
class ManualCtrlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue().c_str();
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) return;

    if (doc.containsKey("mode")) {
      manualMode = (String((const char*)doc["mode"]) == "MANUAL");
    }
    if (doc.containsKey("pump")) {
      manualCommandOn = (String((const char*)doc["pump"]) == "ON");
    }
    if (doc.containsKey("command") && String((const char*)doc["command"]) == "ESTOP") {
      emergencyStop = true;
    }
  }
};

// ================= Sensor reading helpers =================
int readSoilMoisturePercent() {
  int raw = analogRead(MOISTURE_PIN);
  int pct = map(raw, MOIST_DRY_RAW, MOIST_WET_RAW, 0, 100);
  return constrain(pct, 0, 100);
}

float readSoilPH() {
  int raw = analogRead(PH_PIN);
  // linear interpolation between two calibration points
  float slope = (7.0 - 4.0) / float(PH_RAW_AT_PH7 - PH_RAW_AT_PH4);
  float ph = 4.0 + slope * (raw - PH_RAW_AT_PH4);
  return constrain(ph, 0.0, 14.0);
}

bool readWaterLevelOK() {
  // Adjust polarity to match your specific water-level sensor module.
  return digitalRead(WATER_LEVEL_PIN) == HIGH;
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  dht.begin();
  analogReadResolution(12); // 0-4095 on ESP32

  BLEDevice::init("ESP32-Irrigation");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCropConfigChar = pService->createCharacteristic(
      CROP_CONFIG_UUID, BLECharacteristic::PROPERTY_WRITE);
  pCropConfigChar->setCallbacks(new CropConfigCallbacks());

  pSensorDataChar = pService->createCharacteristic(
      SENSOR_DATA_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  pSensorDataChar->addDescriptor(new BLE2902());

  pManualCtrlChar = pService->createCharacteristic(
      MANUAL_CTRL_UUID, BLECharacteristic::PROPERTY_WRITE);
  pManualCtrlChar->setCallbacks(new ManualCtrlCallbacks());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("ESP32 Irrigation BLE server started, advertising...");
}

// ================= Main loop =================
void loop() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) return;
  lastSampleTime = millis();

  int soilMoisture = readSoilMoisturePercent();
  float soilPH = readSoilPH();
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  bool sensorFault = isnan(temperature) || isnan(humidity);
  bool waterOK = readWaterLevelOK();

  // ---- Irrigation decision (hysteresis) ----
  bool desiredPump;
  if (manualMode) {
    desiredPump = manualCommandOn;
  } else if (!sensorFault) {
    if (soilMoisture < crop.moistureMin) desiredPump = true;
    else if (soilMoisture >= crop.moistureTarget) desiredPump = false;
    else desiredPump = pumpOn; // hold state within hysteresis band
  } else {
    desiredPump = false;
  }

  // ---- Safety Manager (overrides everything) ----
  String safetyReason = "";
  bool forceOff = false;
  if (sensorFault) { forceOff = true; safetyReason = "Sensor fault"; }
  if (!waterOK)     { forceOff = true; safetyReason = "Low water"; }
  if (emergencyStop) { forceOff = true; safetyReason = "Emergency stop"; }

  bool wasOn = pumpOn;
  pumpOn = forceOff ? false : desiredPump;

  if (pumpOn && !wasOn) pumpStartTime = millis();
  if (pumpOn && (millis() - pumpStartTime >= MAX_RUNTIME_MS)) {
    pumpOn = false;
    safetyReason = "Max runtime exceeded";
  }
  if (!pumpOn) pumpStartTime = 0;

  digitalWrite(RELAY_PIN, pumpOn ? HIGH : LOW);

  // clear one-shot emergency stop after it has taken effect for a cycle
  if (emergencyStop) emergencyStop = false;

  // ---- pH status ----
  bool phOK = (soilPH >= crop.phMin && soilPH <= crop.phMax);

  // ---- Build and notify sensor data JSON ----
  StaticJsonDocument<256> doc;
  doc["soilMoisture"] = soilMoisture;
  doc["soilPH"] = soilPH;
  doc["phStatus"] = phOK ? "pH Suitable" : "pH Warning";
  doc["temperature"] = sensorFault ? 0 : temperature;
  doc["humidity"] = sensorFault ? 0 : humidity;
  doc["pumpStatus"] = pumpOn ? "ON" : "OFF";
  doc["crop"] = crop.name;
  doc["mode"] = manualMode ? "MANUAL" : "AUTO";
  if (safetyReason != "") doc["fault"] = safetyReason;

  String payload;
  serializeJson(doc, payload);

  if (deviceConnected) {
    pSensorDataChar->setValue(payload.c_str());
    pSensorDataChar->notify();
  }

  Serial.println(payload);
}

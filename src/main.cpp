#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <atomic>
#include <esp_crt_bundle.h>
#include <time.h>

#include <HaBridge.h>
#include <MQTTRemote.h>
#include <ModbusMaster.h>
#include <entities/HaEntityButton.h>
#include <entities/HaEntityCurrent.h>
#include <entities/HaEntityEnergy.h>
#include <entities/HaEntityPower.h>
#include <entities/HaEntitySensor.h>
#include <entities/HaEntityString.h>
#include <entities/HaEntityTemperature.h>
#include <entities/HaEntityVoltage.h>

#include "config.h"
#include "secrets.h"
#include "settings.h"

// ---------- Serial ports ----------
HardwareSerial &DdsuSerial = Serial1;     // RS485 bus of the DDSU666
HardwareSerial &InverterSerial = Serial2; // RS485 bus of the inverter
ModbusMaster node;

// ---------- MQTT + Home Assistant ----------
// Everything that depends on the broker settings. It is created in setup(), once the settings are loaded.
IJsonDocument device_doc;
std::string ca_cert; // Must outlive the MQTT client

static MQTTRemote::Configuration MqttConfiguration()
{
  MQTTRemote::Configuration configuration;
  configuration.rx_buffer_size = 1024;
  configuration.tx_buffer_size = 2048; // Discovery messages are bigger than the default
  configuration.keep_alive_s = 15;
  configuration.transport = settings.tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
  if (!settings.ca_cert.empty())
  {
    ca_cert = settings.ca_cert;
    configuration.verification.certificate = ca_cert.c_str();
  }
  else
  {
    configuration.verification.crt_bundle_attach = esp_crt_bundle_attach; // Public CAs
  }
  return configuration;
}

using PowerUnit = HaEntityPower::Unit;
homeassistantentities::Sensor::Frequency frequency_class;

struct HomeAssistant
{
  MQTTRemote mqtt;
  HaBridge bridge;

  // Grid meter
  HaEntityPower grid_power{bridge, "Potencia red", "grid", {.unit = PowerUnit::kW, .suggested_display_precision = 2}};
  HaEntityPower house_power{bridge, "Consumo casa", "house", {.unit = PowerUnit::kW, .suggested_display_precision = 2}};
  HaEntityEnergy imported{bridge, "Energía importada hoy", "imported_today"};
  HaEntityEnergy exported{bridge, "Energía exportada hoy", "exported_today"};

  // Inverter
  HaEntityString inverter_state{bridge, "Estado inversor", "inverter_state"};
  HaEntityPower ac_power{bridge, "Potencia AC", "ac", {.unit = PowerUnit::kW, .suggested_display_precision = 2}};
  HaEntityVoltage ac_voltage{bridge, "Tensión AC", "ac"};
  HaEntityCurrent ac_current{bridge, "Corriente AC", "ac"};
  HaEntitySensor ac_frequency{bridge, "Frecuencia AC", "ac_frequency",
                              {.device_class = frequency_class,
                               .unit_of_measurement = homeassistantentities::Sensor::Frequency::Unit::Hz}};
  HaEntityPower pv1_power{bridge, "Potencia PV1", "pv1", {.unit = PowerUnit::kW, .suggested_display_precision = 2}};
  HaEntityVoltage pv1_voltage{bridge, "Tensión PV1", "pv1"};
  HaEntityCurrent pv1_current{bridge, "Corriente PV1", "pv1"};
  HaEntityTemperature temp_ambient{bridge, "Temperatura ambiente", "ambient"};
  HaEntityTemperature temp_radiator{bridge, "Temperatura radiador", "radiator"};
  HaEntityEnergy generation_today{bridge, "Producción hoy", "generation_today"};
  HaEntityEnergy generation_total{bridge, "Producción total", "generation_total"};
  HaEntityString faults{bridge, "Errores", "faults", {.icon = "mdi:alert-circle-outline"}};

  // Controls
  HaEntityButton restart{bridge, "Reiniciar", "restart",
                         {.device_class = "restart", .icon = "", .entity_category = "config"}};
  HaEntityButton reset_counters{bridge, "Reiniciar contadores", "reset_counters",
                                {.device_class = "", .icon = "mdi:counter", .entity_category = "config"}};
  HaEntityButton config_portal{bridge, "Abrir portal de configuración", "config_portal",
                               {.device_class = "", .icon = "mdi:access-point", .entity_category = "config"}};

  HomeAssistant()
      : mqtt(settings.id, settings.host, settings.port, settings.user, settings.password, MqttConfiguration()),
        bridge(mqtt, settings.id, device_doc)
  {
  }

  void publishDiscovery()
  {
    HaEntity *const entities[] = {
        &grid_power, &house_power, &imported, &exported, &inverter_state, &ac_power, &ac_voltage,
        &ac_current, &ac_frequency, &pv1_power, &pv1_voltage, &pv1_current, &temp_ambient, &temp_radiator,
        &generation_today, &generation_total, &faults, &restart, &reset_counters, &config_portal};
    for (HaEntity *entity : entities)
    {
      entity->publishConfiguration();
    }
  }
};
HomeAssistant *ha = nullptr; // Created in setup()

// Set from the MQTT tasks, handled in loop()
std::atomic<bool> discovery_pending{false};
std::atomic<bool> restart_requested{false};
std::atomic<bool> reset_requested{false};
std::atomic<bool> portal_requested{false};

// ---------- State ----------
// Daily counters. They live in RTC memory, so they survive a restart (not a power cycle).
#define PERSIST_MAGIC 0x50FA7002
struct Persist
{
  uint32_t magic;
  double import_today; // kWh
  double export_today; // kWh
  int32_t day;         // Day of the year of the counters, -1 = unknown
};
RTC_NOINIT_ATTR Persist persist;

float inverter_power = 0;           // Last AC power read from the inverter (kW)
bool inverter_online = false;       // The inverter is answering
unsigned long inverter_last_ok = 0; // millis() of its last answer

// ---------- Helpers ----------
// True once every `period` ms
static bool every(unsigned long &last, unsigned long period)
{
  unsigned long now = millis();
  if (now - last < period)
  {
    return false;
  }
  last = now;
  return true;
}

uint16_t ModbusCrc(const uint8_t *data, int len)
{
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
    {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

// ---------- Grid meter (DDSU666) ----------
void RequestGridPower()
{
  static const uint8_t request[8] = {0x01, 0x03, 0x20, 0x04, 0x00, 0x02, 0x8E, 0x0A};
  if (DDSU_DE_PIN >= 0)
  {
    digitalWrite(DDSU_DE_PIN, HIGH);
  }
  DdsuSerial.write(request, sizeof(request));
  DdsuSerial.flush(); // Wait until the last byte is on the wire
  if (DDSU_DE_PIN >= 0)
  {
    digitalWrite(DDSU_DE_PIN, LOW);
  }
}

static bool headerMismatch(const uint8_t *frame, int pos)
{
  return frame[0] != 0x01 || (pos > 1 && frame[1] != 0x03) || (pos > 2 && frame[2] != 0x04);
}

// Non-blocking reader for the DDSU666 answer (01 03 04 + float + CRC = 9 bytes).
// Bytes are accumulated across loops; returns true only when a full frame with a valid CRC is in.
bool ReadDDSU666(float &power)
{
  static uint8_t frame[DDSU_FRAME_LEN];
  static int pos = 0;
  static unsigned long lastByte = 0;

  if (pos > 0 && millis() - lastByte > 100) // Incomplete frame left behind: drop it
  {
    pos = 0;
  }

  while (DdsuSerial.available() > 0)
  {
    frame[pos++] = DdsuSerial.read();
    lastByte = millis();

    if (pos == DDSU_FRAME_LEN)
    {
      uint16_t crc = frame[7] | (frame[8] << 8);
      if (!headerMismatch(frame, pos) && ModbusCrc(frame, 7) == crc)
      {
        union
        {
          uint8_t asBytes[4];
          float asFloat;
        } data;
        data.asBytes[3] = frame[3]; // Big endian float
        data.asBytes[2] = frame[4];
        data.asBytes[1] = frame[5];
        data.asBytes[0] = frame[6];
        power = data.asFloat;
        pos = 0;
        return true;
      }
      // Bad CRC: discard the first byte and look for the next header
      pos--;
      memmove(frame, frame + 1, pos);
    }

    // Keep the buffer aligned with the expected header
    while (pos > 0 && headerMismatch(frame, pos))
    {
      pos--;
      memmove(frame, frame + 1, pos);
    }
  }
  return false;
}

void HandleGrid()
{
  static unsigned long lastPoll = 0;
  static unsigned long lastPublish = 0;
  static unsigned long lastEnergyPublish = 0;
  static unsigned long lastSample = 0;
  static float lastPower = 0;
  static bool haveSample = false;

  float power; // kW, positive = exporting
  if (ReadDDSU666(power) && !isnan(power) && power < MAX_EXPORT && power > -MAX_IMPORT)
  {
    // Energy: trapezoid between this sample and the previous one
    unsigned long now = millis();
    if (haveSample)
    {
      float mean = (lastPower + power) / 2;
      double kwh = fabs(mean) * (now - lastSample) / 3600000.0;
      if (mean < 0)
      {
        persist.import_today += kwh;
      }
      else
      {
        persist.export_today += kwh;
      }
    }
    lastPower = power;
    lastSample = now;
    haveSample = true;

    if (every(lastPublish, gridPublishPeriod))
    {
      ha->grid_power.publish(power);

      // House consumption = inverter output - power exported to the grid.
      // With the inverter off (night) its output is 0.
      float house = (inverter_online ? inverter_power : 0) - power;
      ha->house_power.publish(house < 0 ? 0 : house);
    }
    if (every(lastEnergyPublish, energyPublishPeriod))
    {
      ha->imported.publish(persist.import_today);
      ha->exported.publish(persist.export_today);
    }
  }

  if (every(lastPoll, gridPollPeriod))
  {
    RequestGridPower();
  }
}

// ---------- Inverter ----------
// Reads a block of holding registers and checks that the answer has the length we asked for,
// so a late/foreign frame is never decoded as if it was this block.
bool ReadInverterBlock(uint16_t address, uint8_t count)
{
  node.clearResponseBuffer();
  if (node.readHoldingRegisters(address, count) != node.ku8MBSuccess)
  {
    return false;
  }
  // ModbusMaster 2.0.1 reports (registers received - 1) in available()
  return node.available() == count - 1;
}

// The total generation can only grow, and slowly. Anything else (old value, 0, garbage)
// is dropped unless it repeats long enough to be the real value.
bool FilterGenerationTotal(float value)
{
  static float accepted = -1;
  static float candidate = -1;
  static int count = 0;

  if (accepted >= 0 && value >= accepted && value - accepted <= MAX_TOTAL_STEP)
  {
    accepted = value;
    count = 0;
    return true;
  }

  if (candidate >= 0 && fabs(value - candidate) <= MAX_TOTAL_STEP)
  {
    count++;
  }
  else
  {
    count = 1;
  }
  candidate = value;

  if (count >= (accepted < 0 ? TOTAL_CONFIRM_BOOT : TOTAL_CONFIRM_JUMP))
  {
    accepted = value;
    count = 0;
    return true;
  }
  Serial.printf("Total generation rejected: %.1f\n", value);
  return false;
}

// Publishes the fault registers of the block 0x0400 that has just been read as a text.
// Each register is a bit field: bit n of register r is the fault ID (r * 16 + n + 1).
void PublishInverterFaults()
{
  char summary[200] = "";
  int active = 0;
  int listed = 0;

  for (int i = 0; i < FAULT_REGISTERS; i++)
  {
    uint16_t value = node.getResponseBuffer(0x0005 + i);
    for (int bit = 0; bit < 16; bit++)
    {
      if (value & (1 << bit))
      {
        active++;
        if (listed < MAX_FAULTS_LISTED)
        {
          size_t used = strlen(summary);
          snprintf(summary + used, sizeof(summary) - used, "%sID%03d", listed ? ", " : "", i * 16 + bit + 1);
          listed++;
        }
      }
    }
  }
  if (active > listed)
  {
    size_t used = strlen(summary);
    snprintf(summary + used, sizeof(summary) - used, " (+%d)", active - listed);
  }
  ha->faults.publish(active ? summary : "Sin errores");
}

void ReadInverterLive() // 0x0480: AC output
{
  if (!ReadInverterBlock(0x0480, 0x000F))
  {
    return;
  }
  float freq = node.getResponseBuffer(0x0004) * 0.01;
  float powerac = node.getResponseBuffer(0x0005) * 0.01;
  float voltageac = node.getResponseBuffer(0x000D) * 0.1;
  float currentac = node.getResponseBuffer(0x000E) * 0.01;

  inverter_power = powerac;
  inverter_online = true;
  inverter_last_ok = millis();

  ha->ac_power.publish(powerac);
  ha->ac_current.publish(currentac);
  if (freq > 0)
  {
    ha->ac_frequency.publish(freq);
  }
  if (voltageac > 0)
  {
    ha->ac_voltage.publish(voltageac);
  }
}

void ReadInverterStatus() // 0x0400: state, faults and temperatures
{
  if (!ReadInverterBlock(0x0400, 0x001B))
  {
    return;
  }
  static const char *const states[] = {"Inactivo", "Verificando red", "Normal", "Suministro de emergencia",
                                       "Error", "Fallo permanente en el sistema", "Actualizando sistema", "Autocarga"};
  uint16_t state = node.getResponseBuffer(0x0004);
  if (state < sizeof(states) / sizeof(states[0]))
  {
    ha->inverter_state.publish(states[state]);
  }

  PublishInverterFaults();

  int tempamb = node.getResponseBuffer(0x0018);
  int temprad = node.getResponseBuffer(0x001A);
  if (tempamb > 0)
  {
    ha->temp_ambient.publish(tempamb);
  }
  if (temprad > 0)
  {
    ha->temp_radiator.publish(temprad);
  }
}

void ReadInverterPv() // 0x0580: PV1 input
{
  if (!ReadInverterBlock(0x0580, 0x0007))
  {
    return;
  }
  float voltagepv1 = node.getResponseBuffer(0x0004) * 0.1;
  float currentpv1 = node.getResponseBuffer(0x0005) * 0.01;
  float powerpv1 = node.getResponseBuffer(0x0006) * 0.01;

  ha->pv1_power.publish(powerpv1);
  ha->pv1_voltage.publish(voltagepv1);
  if (powerpv1 > 0)
  {
    ha->pv1_current.publish(currentpv1);
  }
}

void ReadInverterGeneration() // 0x0680: energy counters
{
  if (!ReadInverterBlock(0x0680, 0x0008))
  {
    return;
  }
  float generationtoday = node.getResponseBuffer(0x0005) * 0.01;
  float generationtotal = node.getResponseBuffer(0x0007) * 0.1;
  // If the total is not trustworthy the whole frame is suspect: publish nothing
  if (FilterGenerationTotal(generationtotal))
  {
    ha->generation_today.publish(generationtoday);
    ha->generation_total.publish(generationtotal);
  }
}

// Every cycle reads the AC output and, a moment later, one of the other three blocks in turn.
void HandleInverter()
{
  static unsigned long lastCycle = 0;
  static unsigned long cycleStart = 0;
  static bool secondPending = false;
  static int block = 0;

  if (every(lastCycle, inverter_online ? inverterPollPeriod : inverterOfflinePollPeriod))
  {
    ReadInverterLive();
    cycleStart = millis();
    secondPending = inverter_online; // No point in a second timeout while it does not answer
  }

  if (secondPending && millis() - cycleStart >= inverterBlockDelay)
  {
    secondPending = false;
    switch (block)
    {
    case 0:
      ReadInverterStatus();
      break;
    case 1:
      ReadInverterPv();
      break;
    default:
      ReadInverterGeneration();
      break;
    }
    block = (block + 1) % 3;
  }

  // The inverter switches off at night: show it instead of keeping the last values
  if (inverter_online && millis() - inverter_last_ok > inverterTimeout)
  {
    inverter_online = false;
    inverter_power = 0;
    ha->ac_power.publish(0);
    ha->pv1_power.publish(0);
    ha->inverter_state.publish("Sin respuesta");
  }
}

// ---------- Network ----------
// Holding the BOOT button opens the configuration portal
void HandleConfigButton()
{
  static unsigned long pressedSince = 0;
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW)
  {
    if (pressedSince == 0)
    {
      pressedSince = millis() | 1;
    }
    else if (millis() - pressedSince > CONFIG_BUTTON_HOLD_MS)
    {
      portal_requested = true;
      pressedSince = 0;
      while (digitalRead(CONFIG_BUTTON_PIN) == LOW) // Wait for the release
      {
        delay(10);
      }
    }
  }
  else
  {
    pressedSince = 0;
  }
}

void HandleNetwork()
{
  static bool otaStarted = false;
  static bool mqttStarted = false;
  static unsigned long lastWifiRetry = 0;
  static unsigned long lastMqttOk = 0;
  unsigned long now = millis();

  HandleConfigButton();
  if (portal_requested.exchange(false))
  {
    StartConfigPortal(true);
  }
  if (HandleConfigPortal())
  {
    Serial.println("New settings saved, restarting");
    delay(2000); // Let the portal answer the browser
    ESP.restart();
  }

  if (WiFi.isConnected())
  {
    if (!otaStarted)
    {
      Serial.print("WiFi connected, IP: ");
      Serial.println(WiFi.localIP());
      ArduinoOTA.setHostname(OTA_HOSTNAME);
#ifdef OTA_PASSWORD
      ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
      ArduinoOTA.begin();
      otaStarted = true;
    }
    ArduinoOTA.handle();

    if (!mqttStarted && settings.mqttConfigured())
    {
      // The callback runs in an MQTT task: it only raises a flag for loop()
      ha->mqtt.start([](bool connected) {
        if (connected)
        {
          discovery_pending = true;
        }
      });
      mqttStarted = true;
    }
    lastWifiRetry = now;
  }
  else if (WifiConfigured() && !ConfigPortalActive() && every(lastWifiRetry, wifiRetryPeriod))
  {
    Serial.println("WiFi down, reconnecting");
    WiFi.reconnect();
  }

  // Last resort if the connection does not come back: restart (the daily counters are kept).
  // Not while the portal is open or before the broker is configured.
  if (!mqttStarted || ha->mqtt.connected() || ConfigPortalActive())
  {
    lastMqttOk = now;
  }
  else if (now - lastMqttOk > restartAfter)
  {
    Serial.println("Watchdog: no MQTT connection, restarting");
    ESP.restart();
  }

  if (mqttStarted && ha->mqtt.connected() && discovery_pending.exchange(false))
  {
    ha->publishDiscovery();
  }
}

// Resets the daily counters at local midnight
void HandleDay()
{
  static unsigned long lastCheck = 0;
  if (!every(lastCheck, 1000))
  {
    return;
  }
  struct tm now;
  if (!getLocalTime(&now, 0))
  {
    return; // Time not synchronised yet
  }
  if (persist.day == -1)
  {
    persist.day = now.tm_yday;
  }
  else if (persist.day != now.tm_yday)
  {
    persist.day = now.tm_yday;
    persist.import_today = 0;
    persist.export_today = 0;
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println();

  if (persist.magic != PERSIST_MAGIC) // Power cycle: RTC memory holds garbage
  {
    persist.magic = PERSIST_MAGIC;
    persist.import_today = 0;
    persist.export_today = 0;
    persist.day = -1;
  }

  if (DDSU_DE_PIN >= 0)
  {
    pinMode(DDSU_DE_PIN, OUTPUT);
    digitalWrite(DDSU_DE_PIN, LOW);
  }
  DdsuSerial.begin(9600, SERIAL_8N1, DDSU_RX_PIN, DDSU_TX_PIN);

  InverterSerial.begin(9600, SERIAL_8N1, INVERTER_RX_PIN, INVERTER_TX_PIN);
  node.begin(INVERTER_MODBUS_ID, InverterSerial);
  if (INVERTER_DE_PIN >= 0)
  {
    pinMode(INVERTER_DE_PIN, OUTPUT);
    digitalWrite(INVERTER_DE_PIN, LOW);
    node.preTransmission([]() { digitalWrite(INVERTER_DE_PIN, HIGH); });
    node.postTransmission([]() { digitalWrite(INVERTER_DE_PIN, LOW); });
  }

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  LoadSettings();

  // Device shown in Home Assistant, shared by every entity
  HaBridge::DeviceInfo device;
  device.identifiers = {settings.id};
  device.name = DEVICE_NAME;
  device.manufacturer = "CanoHard";
  device.model = "Sofar RS485 logger";
  device.sw_version = FIRMWARE_VERSION;
  HaBridge::populateDeviceJson(device_doc, device);

  // The callbacks run in the MQTT tasks: they only raise flags for loop()
  ha = new HomeAssistant();
  ha->restart.onCommand([]() { restart_requested = true; });
  ha->reset_counters.onCommand([]() { reset_requested = true; });
  ha->config_portal.onCommand([]() { portal_requested = true; });
  // Send the discovery again when Home Assistant restarts (and on every MQTT connection)
  ha->bridge.onHomeAssistantBirth([]() { discovery_pending = true; });

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  configTzTime(TIMEZONE, NTP_SERVER);

  if (WifiConfigured())
  {
    WiFi.begin(); // Saved credentials. Not waited for: WiFi and MQTT keep retrying on their own
  }
  if (!WifiConfigured() || !settings.mqttConfigured())
  {
    // First start (or incomplete settings): the portal stays open until it is configured
    StartConfigPortal(false);
  }
}

void loop()
{
  HandleNetwork();

  if (reset_requested.exchange(false))
  {
    persist.import_today = 0;
    persist.export_today = 0;
    ha->imported.publish(0);
    ha->exported.publish(0);
  }
  if (restart_requested.exchange(false))
  {
    delay(200); // Let the pending MQTT traffic go out
    ESP.restart();
  }

  HandleGrid();
  HandleInverter();
  HandleDay();
}

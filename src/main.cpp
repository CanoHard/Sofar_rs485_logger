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
#include <entities/HaEntitySignalStrength.h>
#include <entities/HaEntitySensor.h>
#include <entities/HaEntityString.h>
#include <entities/HaEntityTemperature.h>
#include <entities/HaEntityVoltage.h>

#include "config.h"
#include "faults.h"
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
homeassistantentities::Sensor::Duration duration_class;
homeassistantentities::Sensor::PowerFactor power_factor_class;
homeassistantentities::Sensor::ReactivePower reactive_power_class;
homeassistantentities::Sensor::Current current_class;
homeassistantentities::Sensor::Voltage voltage_class;
homeassistantentities::Sensor::Undefined::Empty no_class;
using DurationUnit = homeassistantentities::Sensor::Duration::Unit;
using CurrentUnit = homeassistantentities::Sensor::Current::Unit;
using VoltageUnit = homeassistantentities::Sensor::Voltage::Unit;

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
  HaEntitySensor reactive_power{bridge, "Potencia reactiva", "reactive",
                                {.device_class = reactive_power_class,
                                 .unit_of_measurement = homeassistantentities::Sensor::ReactivePower::Unit::kvar,
                                 .suggested_display_precision = 2}};
  HaEntitySensor power_factor{bridge, "Factor de potencia", "power_factor",
                              {.device_class = power_factor_class,
                               .unit_of_measurement = homeassistantentities::Sensor::PowerFactor::Unit::None,
                               .suggested_display_precision = 3}};
  HaEntityTemperature temp_module{bridge, "Temperatura módulo", "module"};
  HaEntitySensor generation_time_today{bridge, "Tiempo de producción hoy", "generation_time_today",
                                       {.device_class = duration_class,
                                        .unit_of_measurement = DurationUnit::min,
                                        .state_class = "total_increasing"}};
  HaEntitySensor generation_time_total{bridge, "Tiempo de producción total", "generation_time_total",
                                       {.device_class = duration_class,
                                        .unit_of_measurement = DurationUnit::h,
                                        .state_class = "total_increasing",
                                        .entity_category = "diagnostic",
                                        .suggested_display_precision = 0}};
  HaEntitySensor service_time_total{bridge, "Tiempo de funcionamiento total", "service_time_total",
                                    {.device_class = duration_class,
                                     .unit_of_measurement = DurationUnit::h,
                                     .state_class = "total_increasing",
                                     .entity_category = "diagnostic",
                                     .suggested_display_precision = 0}};
  HaEntitySensor insulation{bridge, "Resistencia de aislamiento", "insulation",
                            {.device_class = no_class,
                             .icon = "mdi:omega",
                             .custom_unit_of_measurement = "kΩ",
                             .entity_category = "diagnostic"}};
  HaEntitySensor leakage_current{bridge, "Corriente de fuga", "leakage",
                                 {.device_class = current_class,
                                  .unit_of_measurement = CurrentUnit::mA,
                                  .entity_category = "diagnostic"}};
  HaEntitySensor bus_voltage{bridge, "Tensión bus DC", "bus",
                             {.device_class = voltage_class,
                              .unit_of_measurement = VoltageUnit::V,
                              .entity_category = "diagnostic"}};

  // Inverter information (read once and every hour)
  HaEntityString inverter_serial{bridge, "Número de serie inversor", "inverter_serial",
                                 {.icon = "mdi:barcode", .entity_category = "diagnostic"}};
  HaEntityString inverter_hardware{bridge, "Versión hardware inversor", "inverter_hardware",
                                   {.icon = "mdi:chip", .entity_category = "diagnostic"}};
  HaEntityString inverter_fw_com{bridge, "Firmware comunicaciones", "inverter_fw_com",
                                 {.icon = "mdi:memory", .entity_category = "diagnostic"}};
  HaEntityString inverter_fw_master{bridge, "Firmware control principal", "inverter_fw_master",
                                    {.icon = "mdi:memory", .entity_category = "diagnostic"}};
  HaEntityString inverter_fw_slave{bridge, "Firmware control secundario", "inverter_fw_slave",
                                   {.icon = "mdi:memory", .entity_category = "diagnostic"}};
  HaEntityString inverter_safety{bridge, "Versión normativa de red", "inverter_safety",
                                 {.icon = "mdi:shield-check-outline", .entity_category = "diagnostic"}};

  // Logger
  HaEntitySignalStrength wifi_signal{bridge, "Señal WiFi", "wifi", {.entity_category = "diagnostic"}};
  HaEntitySensor uptime{bridge, "Tiempo encendido", "uptime",
                        {.device_class = duration_class,
                         .unit_of_measurement = DurationUnit::h,
                         .state_class = std::nullopt,
                         .entity_category = "diagnostic",
                         .suggested_display_precision = 1}};
  HaEntityString ip_address{bridge, "Dirección IP", "ip", {.icon = "mdi:ip-network", .entity_category = "diagnostic"}};

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
        &generation_today, &generation_total, &faults, &reactive_power, &power_factor, &temp_module,
        &generation_time_today, &generation_time_total, &service_time_total, &insulation, &leakage_current,
        &bus_voltage, &inverter_serial, &inverter_hardware, &inverter_fw_com, &inverter_fw_master,
        &inverter_fw_slave, &inverter_safety, &wifi_signal, &uptime, &ip_address, &restart, &reset_counters,
        &config_portal};
    for (HaEntity *entity : entities)
    {
      entity->publishConfiguration();
    }
    // Send the last known values again, so Home Assistant does not wait for the next reading
    // (the inverter information is only read once an hour)
    for (HaEntity *entity : entities)
    {
      entity->republishState();
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

// Signed 16 bit register (I16 in the protocol)
static int16_t RegI16(uint8_t index)
{
  return (int16_t)node.getResponseBuffer(index);
}

// Unsigned 32 bit value in two registers, high word first (U32 in the protocol)
static uint32_t RegU32(uint8_t index)
{
  return ((uint32_t)node.getResponseBuffer(index) << 16) | node.getResponseBuffer(index + 1);
}

// Publishes the fault registers of the block 0x0400 that has just been read as a text.
// Each register is a bit field: bit n of register r is the fault ID (r * 16 + n + 1),
// described in the sheet "Fault Addresses" of the protocol (faults.h).
void PublishInverterFaults()
{
  static const size_t maxLength = 240; // Home Assistant drops states longer than 255 characters
  std::string summary;
  int active = 0;
  int listed = 0;

  for (int i = 0; i < FAULT_REGISTERS; i++)
  {
    uint16_t value = node.getResponseBuffer(0x0005 + i);
    for (int bit = 0; bit < 16; bit++)
    {
      if (!(value & (1 << bit)))
      {
        continue;
      }
      active++;
      int id = i * 16 + bit + 1;
      char item[80];
      const char *name = FAULT_NAMES[id - 1];
      snprintf(item, sizeof(item), "%sID%03d%s%s", listed ? ", " : "", id, name ? " " : "", name ? name : "");
      if (summary.size() + strlen(item) <= maxLength - 8) // Room left for " (+NN)"
      {
        summary += item;
        listed++;
      }
    }
  }
  if (active > listed)
  {
    summary += " (+" + std::to_string(active - listed) + ")";
  }
  ha->faults.publish(active ? summary : "Sin errores");
}

void ReadInverterLive() // 0x0480: AC output
{
  if (!ReadInverterBlock(0x0480, 0x0012)) // 0x0480..0x0491
  {
    return;
  }
  float freq = node.getResponseBuffer(0x0004) * 0.01;
  float powerac = RegI16(0x0005) * 0.01; // I16: slightly negative while the inverter is idle
  float voltageac = node.getResponseBuffer(0x000D) * 0.1;
  float currentac = node.getResponseBuffer(0x000E) * 0.01;

  inverter_power = powerac;
  inverter_online = true;
  inverter_last_ok = millis();

  ha->ac_power.publish(powerac);
  ha->ac_current.publish(currentac);
  ha->reactive_power.publish(RegI16(0x0006) * 0.01); // 0x0486
  if (powerac > 0)
  {
    ha->power_factor.publish(RegI16(0x0011) * 0.001); // 0x0491, meaningless without output
  }
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
  if (!ReadInverterBlock(0x0400, 0x002C)) // 0x0400..0x042B
  {
    return;
  }
  static const char *const states[] = {"Esperando",
                                       "Comprobando",
                                       "Conectado a red",
                                       "Suministro de emergencia",
                                       "Fallo recuperable",
                                       "Fallo permanente",
                                       "Actualizando",
                                       "Autocarga",
                                       "Modo SVG",
                                       "Modo PID"};
  uint16_t state = node.getResponseBuffer(0x0004);
  if (state < sizeof(states) / sizeof(states[0]))
  {
    ha->inverter_state.publish(states[state]);
  }
  else
  {
    ha->inverter_state.publish("Desconocido (" + std::to_string(state) + ")");
  }

  PublishInverterFaults();

  // I16: they can be below zero. 0 is what the inverter reports while the sensor is not ready
  int16_t tempamb = RegI16(0x0018);
  int16_t temprad = RegI16(0x001A);
  if (tempamb != 0)
  {
    ha->temp_ambient.publish(tempamb);
  }
  if (temprad != 0)
  {
    ha->temp_radiator.publish(temprad);
  }
  int16_t tempmodule = RegI16(0x0020);
  if (tempmodule != 0)
  {
    ha->temp_module.publish(tempmodule);
  }

  ha->generation_time_today.publish(node.getResponseBuffer(0x0026));  // Minutes
  ha->generation_time_total.publish(RegU32(0x0027) / 60.0);            // Minutes -> hours
  ha->service_time_total.publish(RegU32(0x0029) / 60.0);               // Minutes -> hours
  uint16_t insulation = node.getResponseBuffer(0x002B);
  if (insulation > 0)
  {
    ha->insulation.publish(insulation);
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
  // Both are U32 (0x0684-0x0685 and 0x0686-0x0687). Reading only the low word made the total
  // wrap around at 6553.5 kWh
  float generationtoday = RegU32(0x0004) * 0.01;
  float generationtotal = RegU32(0x0006) * 0.1;
  // If the total is not trustworthy the whole frame is suspect: publish nothing
  if (FilterGenerationTotal(generationtotal))
  {
    ha->generation_today.publish(generationtoday);
    ha->generation_total.publish(generationtotal);
  }
}

void ReadInverterDiagnostics() // 0x06C4..0x06CC: leakage current and DC bus (installer level registers)
{
  if (!ReadInverterBlock(0x06C4, 0x0009))
  {
    return;
  }
  ha->leakage_current.publish(node.getResponseBuffer(0x0000));    // 0x06C4, mA
  ha->bus_voltage.publish(node.getResponseBuffer(0x0008) * 0.1);  // 0x06CC
}

// Text stored as two ASCII characters per register, high byte first
static std::string RegAscii(uint8_t index, uint8_t count)
{
  std::string out;
  for (uint8_t i = 0; i < count; i++)
  {
    uint16_t value = node.getResponseBuffer(index + i);
    for (char c : {(char)(value >> 8), (char)(value & 0xFF)})
    {
      if (c >= 0x20 && c < 0x7F)
      {
        out += c;
      }
    }
  }
  // Trim the padding
  size_t end = out.find_last_not_of(' ');
  return end == std::string::npos ? "" : out.substr(0, end + 1);
}

// Software version: stage letter (low byte of the first register) + major + custom + minor (two digits each),
// e.g. "V050001"
static std::string RegVersion(uint8_t index)
{
  char stage = node.getResponseBuffer(index) & 0xFF;
  std::string version = (stage >= 0x20 && stage < 0x7F) ? std::string(1, stage) : "";
  return version + RegAscii(index + 1, 3);
}

// 0x0440..0x0471: serial number and versions. They do not change, so it is read rarely.
bool ReadInverterInfo()
{
  if (!ReadInverterBlock(0x0440, 0x0032))
  {
    return false;
  }
  std::string serial = RegAscii(0x0005, 8) + RegAscii(0x0030, 2); // 0x0445..0x044C + 0x0470..0x0471
  ha->inverter_serial.publish(serial);
  ha->inverter_hardware.publish(RegAscii(0x000D, 2));  // 0x044D..0x044E
  ha->inverter_fw_com.publish(RegVersion(0x000F));     // 0x044F..0x0452
  ha->inverter_fw_master.publish(RegVersion(0x0013));  // 0x0453..0x0456
  ha->inverter_fw_slave.publish(RegVersion(0x0017));   // 0x0457..0x045A
  char safety[16];
  snprintf(safety, sizeof(safety), "%u.%u", node.getResponseBuffer(0x001B), node.getResponseBuffer(0x001C));
  ha->inverter_safety.publish(safety); // 0x045B..0x045C
  return true;
}

// Every cycle reads the AC output and, a moment later, one of the other blocks in turn.
void HandleInverter()
{
  static unsigned long lastCycle = 0;
  static unsigned long cycleStart = 0;
  static bool secondPending = false;
  static int block = 0;
  static unsigned long lastInfo = 0;
  static bool haveInfo = false;

  if (every(lastCycle, inverter_online ? inverterPollPeriod : inverterOfflinePollPeriod))
  {
    ReadInverterLive();
    cycleStart = millis();
    secondPending = inverter_online; // No point in a second timeout while it does not answer
  }

  if (secondPending && millis() - cycleStart >= inverterBlockDelay)
  {
    secondPending = false;
    // On failure it is retried after a few minutes, so the other blocks keep being read
    static bool triedInfo = false;
    if (!triedInfo || millis() - lastInfo > (haveInfo ? inverterInfoPeriod : 300000UL))
    {
      triedInfo = true;
      // Takes the place of the regular block in this cycle
      haveInfo = ReadInverterInfo();
      lastInfo = millis();
      return;
    }
    switch (block)
    {
    case 0:
      ReadInverterStatus();
      break;
    case 1:
      ReadInverterPv();
      break;
    case 2:
      ReadInverterGeneration();
      break;
    default:
      ReadInverterDiagnostics();
      break;
    }
    block = (block + 1) % 4;
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

  static unsigned long lastDiagnostics = 0;
  if (WiFi.isConnected() && every(lastDiagnostics, diagnosticsPeriod))
  {
    ha->wifi_signal.publish(WiFi.RSSI());
    ha->uptime.publish(millis() / 3600000.0);
    ha->ip_address.publish(WiFi.localIP().toString().c_str());
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

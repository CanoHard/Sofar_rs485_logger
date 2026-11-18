#pragma once
// ---------- CONFIG ----------
// The WiFi and the MQTT broker are set in the configuration portal (see README), not here.
// The passwords of the portal and of the OTA updates are in secrets.h, which is not committed.

// ---------- Identity ----------
#define DEVICE_NAME "Sofar"
#define OTA_HOSTNAME "SOFAR-LOGGER"
#define FIRMWARE_VERSION "2.1.0"

// ---------- Configuration portal ----------
#define PORTAL_SSID "SOFAR-LOGGER"  // Access point opened to configure the device
#define PORTAL_TIMEOUT_S 600        // An on-demand portal closes itself after this long
#define CONFIG_BUTTON_PIN 0         // BOOT button: hold it to open the portal
#define CONFIG_BUTTON_HOLD_MS 3000

// ---------- RS485 ports (Waveshare ESP32-S3-Relay-1CH) ----------
// Onboard isolated RS485 (terminal block): UART on GPIO17/18, transmit enable on GPIO21
#define ONBOARD_RS485_TX 17
#define ONBOARD_RS485_RX 18
#define ONBOARD_RS485_DE 21
// External RS485 converter on connector J5 (IO1, IO2, 3V3, GND).
// There is no direction pin on J5: the converter must switch direction automatically.
#define EXTERNAL_RS485_TX 1 // To the DI/TXD input of the converter
#define EXTERNAL_RS485_RX 2 // To the RO/RXD output of the converter
#define EXTERNAL_RS485_DE -1

// 1: inverter on the onboard port and DDSU666 on the external one. 0: the other way round.
#define INVERTER_ON_ONBOARD_PORT 1

#if INVERTER_ON_ONBOARD_PORT
#define INVERTER_TX_PIN ONBOARD_RS485_TX
#define INVERTER_RX_PIN ONBOARD_RS485_RX
#define INVERTER_DE_PIN ONBOARD_RS485_DE
#define DDSU_TX_PIN EXTERNAL_RS485_TX
#define DDSU_RX_PIN EXTERNAL_RS485_RX
#define DDSU_DE_PIN EXTERNAL_RS485_DE
#else
#define INVERTER_TX_PIN EXTERNAL_RS485_TX
#define INVERTER_RX_PIN EXTERNAL_RS485_RX
#define INVERTER_DE_PIN EXTERNAL_RS485_DE
#define DDSU_TX_PIN ONBOARD_RS485_TX
#define DDSU_RX_PIN ONBOARD_RS485_RX
#define DDSU_DE_PIN ONBOARD_RS485_DE
#endif

#define INVERTER_MODBUS_ID 0x01

// ---------- Grid meter (DDSU666) ----------
#define MAX_EXPORT 6     // kW, readings above this are discarded (inverter is 5 kW)
#define MAX_IMPORT 10    // kW, readings above this are discarded
#define DDSU_FRAME_LEN 9 // 01 03 04 + 4 bytes float + 2 bytes CRC

// ---------- Periods (ms) ----------
const unsigned long gridPollPeriod = 200;              // Request the power to the DDSU666
const unsigned long gridPublishPeriod = 5000;          // Publish grid power and house consumption
const unsigned long energyPublishPeriod = 15000;       // Publish imported/exported energy
const unsigned long inverterPollPeriod = 5000;         // Read the inverter while it answers
const unsigned long inverterOfflinePollPeriod = 30000; // Read the inverter while it does not answer (night)
const unsigned long inverterBlockDelay = 500;          // Wait between the two reads of each inverter cycle
const unsigned long inverterTimeout = 30000;           // Without answer for this long the inverter is taken as off
const unsigned long inverterInfoPeriod = 3600000;       // Read the serial number and firmware versions
const unsigned long diagnosticsPeriod = 60000;          // Publish WiFi signal, uptime and IP

// ---------- Connection watchdog ----------
const unsigned long wifiRetryPeriod = 30000; // Ask the WiFi to reconnect this often while it is down
const unsigned long restartAfter = 300000;   // Without MQTT for this long the ESP is restarted

// ---------- Total generation filter ----------
#define MAX_TOTAL_STEP 2.0    // kWh, max plausible increase between two consecutive reads
#define TOTAL_CONFIRM_BOOT 3  // Consistent reads needed to accept the first value after boot
#define TOTAL_CONFIRM_JUMP 20 // Consistent reads needed to accept a decrease / big jump (~5 min)

// ---------- Inverter faults ----------
#define FAULT_REGISTERS 18   // Fault registers 0x0405..0x0416

// ---------- Time ----------
// Used to reset the daily counters at local midnight (POSIX TZ string, this one is Spain mainland)
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER "europe.pool.ntp.org"

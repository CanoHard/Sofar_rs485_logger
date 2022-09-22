#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <CanoMqtt.h>
#include <Chrono.h> 
#include <EEPROM.h>
#include <NTPClient.h>
// ---------- CONFIG ----------

// Set WiFi credentials
#define WIFI_SSID "NAME"
#define WIFI_PASS "PASSWORD"

#define NAME "SOFAR-LOGGER"

// Buffer
#define LEN 8           // Length of buffer for receiving 9
#define LENR 8          // Length of buffer for outgoing packets

int PINMAX = 14; // PIN for DE and RE

#define MAX_EXPORT 5
#define MAX_IMPORT 10

#define MQTT_PORT 1883
const char *broker_ip = "mosquitto.broker.ip";
const char *mqtt_user = "admin";
const char *mqtt_password = "1234";

const char *topicddsu666 = "Sofar/ddsu666/RED_Potencia";

const char *topic_grid_imported = "Sofar/ddsu666/imported";
const char *topic_grid_exported = "Sofar/ddsu666/exported";

// Topics send inverter state
const char *topic_inverter_state = "Sofar/Logger/Estado_Inversor";
const char *topic_pv1_voltage = "Sofar/Logger/PV1_Voltaje";
const char *topic_pv1_current = "Sofar/Logger/PV1_Intensidad";
const char *topic_pv1_power = "Sofar/Logger/PV1_Potencia";
const char *topic_ac_voltage = "Sofar/Logger/AC_Voltaje";
const char *topic_ac_current = "Sofar/Logger/AC_Intensidad";
const char *topic_ac_power = "Sofar/Logger/AC_Potencia";
const char *topic_ac_frequency = "Sofar/Logger/AC_Frecuencia";
const char *topic_temp_ambient = "Sofar/Logger/Temp_ambiente";
const char *topic_temp_radiator = "Sofar/Logger/Temp_radiador";
const char *topic_fault_1 = "Sofar/Logger/topic_fault_1";
const char *topic_fault_2 = "Sofar/Logger/topic_fault_2";
const char *topic_fault_3 = "Sofar/Logger/topic_fault_3";
const char *topic_fault_4 = "Sofar/Logger/topic_fault_4";
const char *topic_fault_5 = "Sofar/Logger/topic_fault_5";
const char *topic_fault_6 = "Sofar/Logger/topic_fault_6";
const char *topic_fault_7 = "Sofar/Logger/topic_fault_7";
const char *topic_fault_8 = "Sofar/Logger/topic_fault_8";
const char *topic_fault_9 = "Sofar/Logger/topic_fault_9";
const char *topic_fault_10 = "Sofar/Logger/topic_fault_10";
const char *topic_fault_11 = "Sofar/Logger/topic_fault_11";
const char *topic_fault_12 = "Sofar/Logger/topic_fault_12";
const char *topic_fault_13 = "Sofar/Logger/topic_fault_13";
const char *topic_fault_14 = "Sofar/Logger/topic_fault_14";
const char *topic_fault_15 = "Sofar/Logger/topic_fault_15";
const char *topic_fault_16 = "Sofar/Logger/topic_fault_16";
const char *topic_fault_17 = "Sofar/Logger/topic_fault_17";
const char *topic_fault_18 = "Sofar/Logger/topic_fault_18";
const char *topic_generation_today = "Sofar/Logger/Prod_Hoy";
const char *topic_generation_total = "Sofar/Logger/Prod_Total";
const char *topic_logger_control = "Sofar/Logger/Control";

SoftwareSerial ss(12, 13); // Softwareserial object to be used for communicating with RS485 MAX MODULE (RX,TX)
SoftwareSerial inverterdata(4, 5); // Softwareserial object to be used for communicating with RS485 MAX MODULE (RX,TX)


const int period = 5000; // Period in milliseconds for sending data
const int DataInverterPeriod = 5000; // Period in milliseconds for sending inverter data, 3*period = interval other data
const int pullPeriod = 200; // Period in milliseconds for pulling data from ddsu666
const int sendDataEnergyPeriod = 15000; // Period in milliseconds for sending data to ddsu666
const int interval_between_pulls = 500; // Period in milliseconds for pulling data from different registers

CanoMqtt mqttClient(WIFI_SSID, WIFI_PASS, broker_ip, mqtt_user, mqtt_password, NAME);
// Instanciate a Chrono object.
Chrono sendDataDDSU;
Chrono sendDataEnergy;
Chrono pullDataDDSU;
Chrono sendDataInverter;
Chrono betweenpulls;

ModbusMaster node;                  //Instantiate modbus object

int registerindex = 0; 
// Energy

unsigned long import_today = 0; // Import today
unsigned long export_today = 0; // Export today

unsigned long last_time = 0; // Last time data was sent
float last_power = 0; // Last power

//Ntp time
WiFiUDP ntpUDP;

// You can specify the time server pool and the offset, (in seconds)
// additionally you can specify the update interval (in milliseconds).
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

int last_day = 0; // Last day
bool time_setchanged = false; // Time set changed
// Prototype functions

void Send_Udp_Packet(IPAddress ip);
void OnWifiConnect();
void OnWifiDisconect();
void SendInverterData();
void Transmit_Rs485_Packet(char *packet, int len);
void OnMqttConnect();
void OnMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);

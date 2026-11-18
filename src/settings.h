#pragma once
#include <cstdint>
#include <string>

// Network settings entered in the configuration portal and stored in flash (NVS).
// The WiFi credentials are not here: the WiFi driver stores them itself.
struct Settings
{
  std::string id = "sofar"; // MQTT client id and root of the topics ([a-z0-9_])
  std::string host;         // MQTT broker, without scheme
  uint16_t port = 8883;
  std::string user;
  std::string password;
  bool tls = true;
  std::string ca_cert; // PEM. Empty = check against the public CAs (Let's Encrypt, etc.)

  bool mqttConfigured() const { return !host.empty(); }
};

extern Settings settings;

// Loads the settings from flash
void LoadSettings();

// Configuration portal: access point + web page to set the WiFi and the MQTT broker.
// It does not block: the device keeps reading the meters while it is open.
void StartConfigPortal(bool closeAfterTimeout);
bool ConfigPortalActive();
// To call from loop(). Returns true when new settings were saved and the device must restart.
bool HandleConfigPortal();
// There are saved WiFi credentials
bool WifiConfigured();

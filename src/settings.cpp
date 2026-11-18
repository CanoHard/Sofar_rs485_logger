#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFiManager.h>

#include "config.h"
#include "secrets.h"

Settings settings;

static WiFiManager wm;
static bool saved = false;

// ---------- Storage ----------
void LoadSettings()
{
  Preferences prefs;
  prefs.begin("mqtt", true);
  settings.id = prefs.getString("id", settings.id.c_str()).c_str();
  settings.host = prefs.getString("host", "").c_str();
  settings.port = prefs.getUShort("port", settings.port);
  settings.user = prefs.getString("user", "").c_str();
  settings.password = prefs.getString("pass", "").c_str();
  settings.tls = prefs.getBool("tls", settings.tls);
  settings.ca_cert = prefs.getString("ca", "").c_str();
  prefs.end();
}

static void SaveSettings()
{
  Preferences prefs;
  prefs.begin("mqtt", false);
  prefs.putString("id", settings.id.c_str());
  prefs.putString("host", settings.host.c_str());
  prefs.putUShort("port", settings.port);
  prefs.putString("user", settings.user.c_str());
  prefs.putString("pass", settings.password.c_str());
  prefs.putBool("tls", settings.tls);
  prefs.putString("ca", settings.ca_cert.c_str());
  prefs.end();
}

// ---------- Input clean-up ----------
static std::string Trim(const std::string &s)
{
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
  {
    return "";
  }
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Only [a-z0-9_]: valid as MQTT client id and as topic root
static std::string CleanId(const std::string &s)
{
  std::string out;
  for (char c : s)
  {
    if (isalnum((unsigned char)c))
    {
      out += (char)tolower((unsigned char)c);
    }
    else if (c == '_' || c == '-' || c == ' ')
    {
      out += '_';
    }
  }
  return out.empty() ? "sofar" : out;
}

// "mqtts://host:port" or "host" -> host, scheme and port go to their own settings
static std::string CleanHost(std::string s, bool &tls, uint16_t &port)
{
  s = Trim(s);
  if (s.rfind("mqtts://", 0) == 0)
  {
    tls = true;
    s = s.substr(8);
  }
  else if (s.rfind("mqtt://", 0) == 0)
  {
    tls = false;
    s = s.substr(7);
  }
  size_t colon = s.rfind(':');
  if (colon != std::string::npos && s.find(':') == colon) // host:port (not an IPv6 address)
  {
    int p = atoi(s.c_str() + colon + 1);
    if (p > 0 && p < 65536)
    {
      port = p;
    }
    s = s.substr(0, colon);
  }
  while (!s.empty() && s.back() == '/')
  {
    s.pop_back();
  }
  return s;
}

// A text field of a web form drops the line breaks of a pasted PEM, and mbedTLS needs them:
// rebuild every certificate of the chain with its header, body in 64 char lines and footer.
static std::string CleanPem(const std::string &in)
{
  static const std::string begin = "-----BEGIN CERTIFICATE-----";
  static const std::string end = "-----END CERTIFICATE-----";
  std::string out;
  size_t pos = 0;
  while (true)
  {
    size_t b = in.find(begin, pos);
    if (b == std::string::npos)
    {
      break;
    }
    size_t e = in.find(end, b);
    if (e == std::string::npos)
    {
      break;
    }
    std::string body;
    for (size_t i = b + begin.size(); i < e; i++)
    {
      char c = in[i];
      if (isalnum((unsigned char)c) || c == '+' || c == '/' || c == '=')
      {
        body += c;
      }
    }
    out += begin + "\n";
    for (size_t i = 0; i < body.size(); i += 64)
    {
      out += body.substr(i, 64) + "\n";
    }
    out += end + "\n";
    pos = e + end.size();
  }
  return out;
}

// ---------- Portal ----------
// The fields must outlive the portal, they are created once
static WiFiManagerParameter *p_id;
static WiFiManagerParameter *p_host;
static WiFiManagerParameter *p_port;
static WiFiManagerParameter *p_user;
static WiFiManagerParameter *p_pass;
static WiFiManagerParameter *p_tls;
static WiFiManagerParameter *p_ca;
static WiFiManagerParameter *p_ca_clear;
static char portBuffer[8];

static void OnParamsSaved()
{
  settings.id = CleanId(Trim(p_id->getValue()));
  uint16_t port = atoi(p_port->getValue());
  settings.port = port ? port : settings.port;
  settings.tls = strlen(p_tls->getValue()) > 0; // An unticked checkbox is not sent
  settings.host = CleanHost(p_host->getValue(), settings.tls, settings.port);
  settings.user = Trim(p_user->getValue());
  // Secrets are not shown in the form: an empty field keeps the stored value
  if (strlen(p_pass->getValue()) > 0)
  {
    settings.password = p_pass->getValue();
  }
  if (strlen(p_ca_clear->getValue()) > 0)
  {
    settings.ca_cert.clear();
  }
  else if (strlen(p_ca->getValue()) > 0)
  {
    settings.ca_cert = CleanPem(p_ca->getValue());
  }
  SaveSettings();
  Serial.printf("Settings saved: %s:%u tls=%d id=%s\n", settings.host.c_str(), settings.port, settings.tls,
                settings.id.c_str());
  saved = true;
}

static void SetupPortal()
{
  static bool done = false;
  if (done)
  {
    return;
  }
  done = true;

  snprintf(portBuffer, sizeof(portBuffer), "%u", settings.port);
  p_id = new WiFiManagerParameter("id", "Nombre del dispositivo (raíz de los topics)", settings.id.c_str(), 32);
  p_host = new WiFiManagerParameter("host", "Servidor MQTT", settings.host.c_str(), 100);
  p_port = new WiFiManagerParameter("port", "Puerto MQTT (1883 sin TLS, 8883 con TLS)", portBuffer, 6,
                                    "type=\"number\" min=\"1\" max=\"65535\"");
  p_user = new WiFiManagerParameter("user", "Usuario MQTT", settings.user.c_str(), 64);
  p_pass = new WiFiManagerParameter("pass",
                                    settings.password.empty() ? "Contraseña MQTT"
                                                              : "Contraseña MQTT (vacío = mantener la actual)",
                                    "", 64, "type=\"password\"");
  p_tls = new WiFiManagerParameter("tls", "Conexión cifrada (TLS)", "1", 2,
                                   settings.tls ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
                                   WFM_LABEL_AFTER);
  p_ca = new WiFiManagerParameter("ca",
                                  settings.ca_cert.empty()
                                      ? "Certificado CA en PEM, solo si el del broker es autofirmado"
                                      : "Certificado CA en PEM (vacío = mantener el actual)",
                                  "", 4000);
  p_ca_clear = new WiFiManagerParameter("caclear", "Borrar el certificado guardado", "1", 2, "type=\"checkbox\"",
                                        WFM_LABEL_AFTER);

  wm.addParameter(p_id);
  wm.addParameter(p_host);
  wm.addParameter(p_port);
  wm.addParameter(p_user);
  wm.addParameter(p_pass);
  wm.addParameter(p_tls);
  wm.addParameter(p_ca);
  wm.addParameter(p_ca_clear);

  static const char *menu[] = {"wifi", "param", "info", "restart"};
  wm.setMenu(menu, 4);
  wm.setParamsPage(true); // MQTT on its own page, so it can be changed without touching the WiFi
  wm.setTitle(DEVICE_NAME);
  wm.setShowInfoUpdate(false);
  wm.setShowInfoErase(false);
  wm.setConfigPortalBlocking(false);
  wm.setBreakAfterConfig(true);
  wm.setSaveParamsCallback(OnParamsSaved);
  wm.setSaveConfigCallback([]() { saved = true; }); // New WiFi
}

void StartConfigPortal(bool closeAfterTimeout)
{
  if (wm.getConfigPortalActive())
  {
    return;
  }
  SetupPortal();
  wm.setConfigPortalTimeout(closeAfterTimeout ? PORTAL_TIMEOUT_S : 0);
  Serial.printf("Configuration portal open: WiFi \"%s\", http://192.168.4.1\n", PORTAL_SSID);
  wm.startConfigPortal(PORTAL_SSID, PORTAL_PASSWORD);
}

bool ConfigPortalActive()
{
  return wm.getConfigPortalActive();
}

bool HandleConfigPortal()
{
  if (wm.getConfigPortalActive())
  {
    wm.process();
  }
  return saved;
}

bool WifiConfigured()
{
  return wm.getWiFiIsSaved();
}

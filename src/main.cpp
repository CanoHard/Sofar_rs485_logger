#include "config.h"

void setup()
{

  pinMode(PINMAX, OUTPUT);
  digitalWrite(PINMAX, LOW);
  ss.begin(9600);
  Serial.begin(115200);
  Serial.println();
  mqttClient.SetDebug(false);
  mqttClient.SetOnWiFiConnect(OnWifiConnect);
  mqttClient.SetOnWiFiDisconnect(OnWifiDisconect);
  mqttClient.SetOnMqttConnect(OnMqttConnect);
  mqttClient.SetOnMqttMessage(OnMqttMessage);
  mqttClient.Init();
  inverterdata.begin(9600);
  node.begin(0x01, inverterdata);
  betweenpulls.restart();
  betweenpulls.stop();
}

void loop()
{

  if (ss.available() > 0)
  {                             // If there is data in the RS485 MAX MODULE
    char buffer[LENR];          // Buffer for outgoing packets
    ss.readBytes(buffer, LENR); // Read the data into the buffer

    Serial.println("Local-----------");
    for (int i = 0; i < LENR; i++)
    {
      Serial.print(buffer[i], HEX);
      Serial.print(" ");
    }

    if (buffer[2] == 0x04 && buffer[1] == 0x03 && buffer[0] == 0x01) // If power reading
    {
      union
      {
        byte asBytes[4];
        float asFloat;
      } data;
      data.asBytes[3] = buffer[3];
      data.asBytes[2] = buffer[4];
      data.asBytes[0] = buffer[5];
      data.asBytes[1] = buffer[6];

      float powergrid = data.asFloat;
      Serial.println(powergrid);

      // Calculate energy
      if (powergrid < MAX_EXPORT && powergrid > (MAX_IMPORT * -1)) // If power is a valid value
      {
        float med = (last_power + powergrid) / 2;
        unsigned long now = millis();
        unsigned long interval = now - last_time;
        if (med < 0)
        { // If the power is negative
          import_today += ((med * -1) * interval);
        }
        else
        { // If the power is positive
          export_today += (med * interval);
        }
        last_power = powergrid;
        last_time = now;

        if (sendDataDDSU.hasPassed(period))
        {
          char xc[6];
          dtostrf(powergrid, 5, 2, xc);
          mqttClient.Publish(topicddsu666, 0, false, xc);
          Serial.println("Publicando");
          Serial.println(xc);
          sendDataDDSU.restart();
        }
        if (sendDataEnergy.hasPassed(sendDataEnergyPeriod))
        {
          float parsed_import = import_today / 3600000.0;
          float parsed_export = export_today / 3600000.0;

          char imported[9];
          dtostrf(parsed_import, 6, 3, imported);
          char exported[9];
          dtostrf(parsed_export, 6, 3, exported);
          // Crash?
          mqttClient.Publish(topic_grid_imported, 0, false, imported);
          if (parsed_export > 0)
          {
            mqttClient.Publish(topic_grid_exported, 0, false, exported);
          }
          else
          {
            mqttClient.Publish(topic_grid_exported, 0, false, "0.000");
          }

          sendDataEnergy.restart();
        }
      }
    }
  }

  if (pullDataDDSU.hasPassed(pullPeriod))
  {
    char buffer[8];
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x20;
    buffer[3] = 0x04;
    buffer[4] = 0x00;
    buffer[5] = 0x02;
    buffer[6] = 0x8E;
    buffer[7] = 0x0A;
    Transmit_Rs485_Packet(buffer, 8);
    Serial.println("Pulling data from DDSU666");
    pullDataDDSU.restart();
  }

  SendInverterData();
  mqttClient.NetworkLoop();
  timeClient.update();
  if (time_setchanged != timeClient.isTimeSet())
  {
    time_setchanged = timeClient.isTimeSet();
    last_day = timeClient.getDay();
  }
  else if (timeClient.isTimeSet() && last_day != timeClient.getDay())
  {
    last_day = timeClient.getDay();
    import_today = 0;
    export_today = 0;
  }
}

void OnWifiConnect()
{
  // Connected to WiFi
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  timeClient.begin();
}

void OnWifiDisconect()
{
}
void OnMqttConnect()
{

  mqttClient.Subscribe(topic_logger_control, 0);
}
void OnMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  if (payload[0] == '1')
  {
    import_today = 0;
    export_today = 0;
  }
  if (payload[0] == '2')
  {
    ESP.restart();
  }
  if (payload[0] == '3')
  {
    import_today = 0;
    export_today = 0;
    ESP.restart();
  }
  if (payload[0] == '4')
  {
    char buffera[8];
    buffera[0] = 0x01;
    buffera[1] = 0x03;
    buffera[2] = 0x20;
    buffera[3] = 0x04;
    buffera[4] = 0x00;
    buffera[5] = 0x02;
    buffera[6] = 0x8E;
    buffera[7] = 0x0A;
    Transmit_Rs485_Packet(buffera, 8);
  }
}

void Transmit_Rs485_Packet(char packet[LEN], int len)
{
  digitalWrite(PINMAX, HIGH); // Turn on the DE and RE pins
  delay(10);
  for (int i = 0; i < len; i++)
  {
    ss.print(packet[i]); // Send the packet to the RS485 MAX MODULE
  }
  delay(10);
  digitalWrite(PINMAX, LOW); // Turn on the DE and RE pins
}

void SendInverterData()
{
  if (sendDataInverter.hasPassed(DataInverterPeriod))
  {

    if (node.readHoldingRegisters(0x0480, 0x000F) == node.ku8MBSuccess)
    {
      // checkifinverteralive.restart();
      Serial.println("Reading Inverter Data");
      float powerac = node.getResponseBuffer(0x0005) * 0.01;
      float freq = node.getResponseBuffer(0x0004) * 0.01;
      float voltageac = node.getResponseBuffer(0x000D) * 0.1;
      float currentac = node.getResponseBuffer(0x000E) * 0.01;
      node.clearResponseBuffer();
      char poweracB[8];
      dtostrf(powerac, 5, 2, poweracB);
      char currentacB[8];
      dtostrf(currentac, 5, 2, currentacB);
      mqttClient.Publish(topic_ac_power, 0, false, poweracB);
      if (freq > 0)
      {
        char freqB[8];
        dtostrf(freq, 5, 2, freqB);
        mqttClient.Publish(topic_ac_frequency, 0, false, freqB);
      }
      if (voltageac > 0)
      {
        char voltageacB[8];
        dtostrf(voltageac, 5, 2, voltageacB);
        mqttClient.Publish(topic_ac_voltage, 0, false, voltageacB);
      }
      mqttClient.Publish(topic_ac_current, 0, false, currentacB);
    }

    sendDataInverter.restart();
    betweenpulls.restart();
    betweenpulls.start();
  }

  if (betweenpulls.hasPassed(interval_between_pulls))
  {

    switch (registerindex)
    {
    case 0:
    {
      if (node.readHoldingRegisters(0x0400, 0x001B) == node.ku8MBSuccess)
      {
        int stateinverter = node.getResponseBuffer(0x0004);
        switch (stateinverter)
        {
        case 0:
          mqttClient.Publish(topic_inverter_state, 0, false, "Inactivo");
          break;
        case 1:
          mqttClient.Publish(topic_inverter_state, 0, false, "Verificando Red");
          break;
        case 2:
          mqttClient.Publish(topic_inverter_state, 0, false, "Normal");
          break;
        case 3:
          mqttClient.Publish(topic_inverter_state, 0, false, "Sumistro de emergencia");
          break;
        case 4:
          mqttClient.Publish(topic_inverter_state, 0, false, "Error ");
          break;
        case 5:
          mqttClient.Publish(topic_inverter_state, 0, false, "Fallo permanente en el sistema");
          break;
        case 6:
          mqttClient.Publish(topic_inverter_state, 0, false, "Actualizando sistema");
          break;
        case 7:
          mqttClient.Publish(topic_inverter_state, 0, false, "Autocarga");
          break;

        default:
          break;
        }
        // Fault Code
        int fault1code = node.getResponseBuffer(0x0005);
        char fault1r[8];
        dtostrf(fault1code, 6, 0, fault1r);
        mqttClient.Publish(topic_fault_1, 0, false, fault1r);
        int fault2code = node.getResponseBuffer(0x0006);
        char fault2r[8];
        dtostrf(fault2code, 6, 0, fault2r);
        mqttClient.Publish(topic_fault_2, 0, false, fault2r);
        int fault3code = node.getResponseBuffer(0x0007);
        char fault3r[8];
        dtostrf(fault3code, 6, 0, fault3r);
        mqttClient.Publish(topic_fault_3, 0, false, fault3r);
        int fault4code = node.getResponseBuffer(0x0008);
        char fault4r[8];
        dtostrf(fault4code, 6, 0, fault4r);
        mqttClient.Publish(topic_fault_4, 0, false, fault4r);
        int fault5code = node.getResponseBuffer(0x0009);
        char fault5r[8];
        dtostrf(fault5code, 6, 0, fault5r);
        mqttClient.Publish(topic_fault_5, 0, false, fault5r);
        int fault6code = node.getResponseBuffer(0x000A);
        char fault6r[8];
        dtostrf(fault6code, 6, 0, fault6r);
        mqttClient.Publish(topic_fault_6, 0, false, fault6r);
        int fault7code = node.getResponseBuffer(0x000B);
        char fault7r[8];
        dtostrf(fault7code, 6, 0, fault7r);
        mqttClient.Publish(topic_fault_7, 0, false, fault7r);
        int fault8code = node.getResponseBuffer(0x000C);
        char fault8r[8];
        dtostrf(fault8code, 6, 0, fault8r);
        mqttClient.Publish(topic_fault_8, 0, false, fault8r);
        int fault9code = node.getResponseBuffer(0x000D);
        char fault9r[8];
        dtostrf(fault9code, 6, 0, fault9r);
        mqttClient.Publish(topic_fault_9, 0, false, fault9r);
        int fault10code = node.getResponseBuffer(0x000E);
        char fault10r[8];
        dtostrf(fault10code, 6, 0, fault10r);
        mqttClient.Publish(topic_fault_10, 0, false, fault10r);
        int fault11code = node.getResponseBuffer(0x000F);
        char fault11r[8];
        dtostrf(fault11code, 6, 0, fault11r);
        mqttClient.Publish(topic_fault_11, 0, false, fault11r);
        int fault12code = node.getResponseBuffer(0x0010);
        char fault12r[8];
        dtostrf(fault12code, 6, 0, fault12r);
        mqttClient.Publish(topic_fault_12, 0, false, fault12r);
        int fault13code = node.getResponseBuffer(0x0011);
        char fault13r[8];
        dtostrf(fault13code, 6, 0, fault13r);
        mqttClient.Publish(topic_fault_13, 0, false, fault13r);
        int fault14code = node.getResponseBuffer(0x0012);
        char fault14r[8];
        dtostrf(fault14code, 6, 0, fault14r);
        mqttClient.Publish(topic_fault_14, 0, false, fault14r);
        int fault15code = node.getResponseBuffer(0x0013);
        char fault15r[8];
        dtostrf(fault15code, 6, 0, fault15r);
        mqttClient.Publish(topic_fault_15, 0, false, fault15r);
        int fault16code = node.getResponseBuffer(0x0014);
        char fault16r[8];
        dtostrf(fault16code, 6, 0, fault16r);
        mqttClient.Publish(topic_fault_16, 0, false, fault16r);
        int fault17code = node.getResponseBuffer(0x0015);
        char fault17r[8];
        dtostrf(fault17code, 6, 0, fault17r);
        mqttClient.Publish(topic_fault_17, 0, false, fault17r);
        int fault18code = node.getResponseBuffer(0x0016);
        char fault18r[8];
        dtostrf(fault18code, 6, 0, fault18r);
        mqttClient.Publish(topic_fault_18, 0, false, fault18r);

        int tempamb = node.getResponseBuffer(0x0018);

        int temprad = node.getResponseBuffer(0x001A);
        char tempradB[8];
        dtostrf(temprad, 5, 0, tempradB);
        if (tempamb > 0)
        {
          char tempambB[8];
          dtostrf(tempamb, 5, 0, tempambB);
          mqttClient.Publish(topic_temp_ambient, 0, false, tempambB);
        }
        if (temprad > 0)
        {
          char tempradB[8];
          dtostrf(temprad, 5, 0, tempradB);
          mqttClient.Publish(topic_temp_radiator, 0, false, tempradB);
        }
        node.clearResponseBuffer();
      }
      registerindex = 1;
      break;
    }
    case 1:
    {

      if (node.readHoldingRegisters(0x0580, 0x0007) == node.ku8MBSuccess)
      {
        float voltagepv1 = node.getResponseBuffer(0x0004) * 0.1;
        float currentpv1 = node.getResponseBuffer(0x0005) * 0.01;
        float powerpv1 = node.getResponseBuffer(0x0006) * 0.01;
        char powerpv1B[8];
        dtostrf(powerpv1, 5, 2, powerpv1B);
        char voltagepv1B[8];
        dtostrf(voltagepv1, 5, 2, voltagepv1B);
        char currentpv1B[8];
        dtostrf(currentpv1, 5, 2, currentpv1B);
        mqttClient.Publish(topic_pv1_power, 0, false, powerpv1B);
        mqttClient.Publish(topic_pv1_voltage, 0, false, voltagepv1B);
        if (powerpv1 > 0)
        {
          char powerpv1B[8];
          dtostrf(powerpv1, 5, 2, powerpv1B);
          mqttClient.Publish(topic_pv1_current, 0, false, currentpv1B);
        }

        node.clearResponseBuffer();
      }
      registerindex = 2;
      break;
    }
    case 2:
    {
      if (node.readHoldingRegisters(0x0680, 0x0008) == node.ku8MBSuccess)
      {
        float generationtoday = node.getResponseBuffer(0x0005) * 0.01;
        // unsigned long totalgen32b = word(node.getResponseBuffer(0x0007), node.getResponseBuffer(0x0006));
        float generationtotal = node.getResponseBuffer(0x0007) * 0.1;
        char generationtodayB[11];
        dtostrf(generationtoday, 10, 2, generationtodayB);
        char generationtotalB[11];
        dtostrf(generationtotal, 10, 1, generationtotalB);
        mqttClient.Publish(topic_generation_today, 0, false, generationtodayB);
        mqttClient.Publish(topic_generation_total, 0, false, generationtotalB);

        node.clearResponseBuffer();
      }
      registerindex = 0;
      break;
    }

    default:
      registerindex = 0;
      break;
    }
    betweenpulls.restart();
    betweenpulls.stop();
  }
}

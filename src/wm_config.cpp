#include "wm_config.h"

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>


bool shouldSaveConfig = false;      //flag for saving data

char devaddr[20];
char devname[40];
IPAddress ip;
String devname_lc;
std::function<void(const char*)> statusCallback;
Settings sett;

//uint16_t zweiterstart = 0;

void dumpEEPROMBuffer()
{
  char buf[80];
  for (unsigned int pos = 0; pos < sizeof(Settings); pos++) {
    if (pos % 16 == 0)  {
      sprintf(buf, "\n%04x: ", pos);
      Serial.print(buf);
    }
    uint8_t val = EEPROM.read(pos);
    sprintf(buf, "%c ", char(val));
    Serial.print(val ? String(buf) : String("• "));
  }
  Serial.println();
}


// callback notifying us that AP was opened
void startAPCallback(WiFiManager* wm)
{
  char buf[30];
  snprintf(buf, sizeof(buf), "AP: %s", devaddr);
  if (statusCallback) statusCallback(buf);
}


// callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void initiateFactoryReset()
{
  strncpy(sett.settings_identifier, "RESET", IDENT_LENGTH);

  EEPROM.put(SETTINGS_POS, sett);
  if (EEPROM.commit()) {
    Serial.println("identifier temporarily cleared for factory reset");
    ESP.restart();
  } else {
    Serial.println("EEPROM error");
  }
}

void setup_wifi(std::function<void(const char*)> func /* = 0 */) {

  statusCallback = func;

  if (statusCallback) {
    Serial.println("Show 'init' status");
    statusCallback("init");
  } else {
    Serial.println("no status info callback set");
  }

  bool needsSetup = false;

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  EEPROM.begin(512);
  dumpEEPROMBuffer();

  EEPROM.get(SETTINGS_POS, sett);
  Serial.println("Settings loaded");

  Serial.println((String)"stored ident = " + sett.settings_identifier);
  if (strncmp(sett.settings_identifier, IDENT, IDENT_LENGTH) != 0) {

    if (statusCallback) statusCallback("f-reset");

    Serial.println("incorrect identifier, initiating factory reset!");
    // maybe first start at all, or maybe
    // reset was issued while identifier was cleared
    needsSetup = true;

    Serial.println((String)"Identifier not ok: " + sett.settings_identifier + " != " + IDENT);
    strcpy(sett.mqtt_broker, "");
    strcpy(sett.mqtt_user, "");
    strcpy(sett.mqtt_password, "");
    Serial.println("Settings initialized");

  } else {

    // identifier ok, option to reset to factory defaults
    Serial.println("Press reset for factory defaults or wait 3 seconds to continue");

    char  savedIdent[IDENT_LENGTH];
    strncpy(savedIdent, sett.settings_identifier, IDENT_LENGTH);

    strncpy(sett.settings_identifier, "RESET", IDENT_LENGTH);

    EEPROM.put(SETTINGS_POS, sett);
    if (EEPROM.commit()) {
      Serial.println("identifier temporarily cleared for factory reset");
    } else {
      Serial.println("EEPROM error");
    }
    char buf[10];
    for (int sec = 1; sec > 0; sec--) {
      Serial.print((String)"[" + sec + "] ");
      //sprintf(buf, "res? %d", sec);
      sprintf(buf, "reset?");
      if (statusCallback) statusCallback(buf);
      delay(1000);
    }
    Serial.println(" --  done.");
    if (statusCallback) statusCallback("continue");
    strncpy(sett.settings_identifier, savedIdent, IDENT_LENGTH);
    EEPROM.put(SETTINGS_POS, sett);
    if (EEPROM.commit()) {
      Serial.println("identifier restored to former value");
    } else {
      Serial.println("EEPROM error");
    }
  }

  // prepare WiFi + WiFiManager

  // Use [APPNAME]-[MAC] as Name for (a) WifiManager AP, (b) OTA hostname, (c) MQTT client name
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(devaddr, sizeof(devaddr), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  snprintf(devname, sizeof(devname), APPNAME "-%02X%02X%02X", mac[3], mac[4], mac[5]);
  if (statusCallback) statusCallback(devaddr);

  WiFiManagerParameter param_mqtt_broker( "host", "MQTT broker hostname / IP",  sett.mqtt_broker, STR_LENGTH);
  WiFiManagerParameter param_mqtt_user( "user", "MQTT user name",  sett.mqtt_user, STR_LENGTH);
  WiFiManagerParameter param_mqtt_password( "pswd", "MQTT password",  sett.mqtt_password, STR_LENGTH);

  WiFiManager wm;
  wm.setAPCallback(startAPCallback);
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter( &param_mqtt_broker );
  wm.addParameter( &param_mqtt_user );
  wm.addParameter( &param_mqtt_password );

  if (needsSetup) {
    Serial.println("FIRST SETUP");
    if (statusCallback) statusCallback("use AP");
    wm.setConfigPortalTimeout(120);
    wm.startConfigPortal(devname, "12345678");
    if (statusCallback) statusCallback("WiFi ok");
  } else {
    Serial.println("REGULAR SETUP");
    if (statusCallback) statusCallback("WiFiManager");
    wm.autoConnect(devname);          // just try. will automatically fall back to AP mode if necessary
    if (statusCallback) statusCallback("WiFi ok");
  }


#if LOG_TELNET
  TelnetStream.begin();
  Serial.println((String)"All further logging is routed to telnet. Just connect to " + devname + " TCP port 22.");
#endif

  LogTarget.println((String)"This device is called '" + devname + "'.");
  devname_lc = String(devname);
  devname_lc.toLowerCase(); // used for topic comparisons

  strncpy(sett.settings_identifier, IDENT, IDENT_LENGTH);   // mark settings as valid
  strncpy(sett.mqtt_broker, param_mqtt_broker.getValue(), STR_LENGTH);
  sett.mqtt_broker[STR_LENGTH - 1] = '\0';
  strncpy(sett.mqtt_user, param_mqtt_user.getValue(), STR_LENGTH);
  sett.mqtt_user[STR_LENGTH - 1] = '\0';
  strncpy(sett.mqtt_password, param_mqtt_password.getValue(), STR_LENGTH);
  sett.mqtt_password[STR_LENGTH - 1] = '\0';

  LogTarget.println((String)"MQTT broker hostname: " + sett.mqtt_broker);
  LogTarget.println((String)"MQTT user name:       " + sett.mqtt_user);
  LogTarget.println((String)"MQTT password:        " + sett.mqtt_password);
  
  if (shouldSaveConfig) {
    LogTarget.println("Settings changed, need to save them to flash");

    EEPROM.put(SETTINGS_POS, sett);
    if (EEPROM.commit()) {
      if (statusCallback) statusCallback("cfg saved");
      LogTarget.println("Settings saved");
      dumpEEPROMBuffer();
    } else {
      LogTarget.println("EEPROM error");
      if (statusCallback) statusCallback("cfg error!");
    }
  }

  LogTarget.println("Done with WiFi Setup! Results:");
  LogTarget.println((String)"MQTT broker hostname: " + sett.mqtt_broker);
  LogTarget.println((String)"MQTT user name:       " + sett.mqtt_user);
  LogTarget.println((String)"MQTT password:        " + sett.mqtt_password);
  

  //// OTA SETUP + FUNCTIONS
  //
  //
  ArduinoOTA.setHostname(devname);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    LogTarget.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    LogTarget.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    LogTarget.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    LogTarget.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      LogTarget.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      LogTarget.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      LogTarget.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      LogTarget.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      LogTarget.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  //
  //
  //// END OF OTA FUNCTIONS

}

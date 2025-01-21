/*
    DosingFeeder     Controller for peristaltic pump

    2025 kater

*/

// version number + history see globals.h

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ezTime.h>

#include "local_config.h"
#include "globals.h"

#include "wm_config.h"       // WiFiManager support
#include "pumps.h"

// constants
int MAX_TEXT_LENGTH = 1000;

// variables

// global (from local_config.h / globals.h)

WiFiClient espClient;
PubSubClient client(espClient);


// local

uint64_t marqueeCycleTimestamp = 0;
bool first_connect = true;

Timezone myTZ;
int hh = 0, mm = 0, ss = 0;
int ss0 = -1;

// function prototypes

void reconnect();
void mqttReceiveCallback(char *topic, byte *payload, unsigned int length);
void processDuration(int pumpidx, char *payload, int length);
void processInterval(int pumpidx, char *payload, int length);
void processStarttime(int pumpidx, char *payload, int length);
void showStatus(const char* text);


#ifdef USE_INTERRUPT
// forward declaration
void IRAM_ATTR TimerHandler();
#endif


// functions

void setup() {

  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("##########################");
  Serial.println("DosingFeeder Version " VERSION);
  Serial.println("##########################");
  Serial.println();

  setup_pumps();    // ensure all pumps are off

  EEPROM.begin(512);
  //checkzweiterstart();
  dumpEEPROMBuffer();

  setup_wifi(showStatus);
  //ledmatrix_width = String(sett.num_segments).toInt() * 8;
  
  //private_only = String(sett.privateonly_flag).toInt() != 0;
  client.setServer(sett.mqtt_broker, 1883);
  client.setCallback(mqttReceiveCallback);
  client.setBufferSize(MAX_TEXT_LENGTH);

#ifdef INTERRUPT_MARQUEE
  // timer interrupt setup and activation
  if (ITimer.attachInterruptInterval(TIMER_INTERVAL_MS * 1000, TimerHandler))
  {
    Serial.print("ITimer started.");
  }
  else {
    Serial.println("Can't set ITimer correctly. Select another freq. or interval");
  }
#endif

  // obtain time via NTP
  waitForSync();

  LogTarget.println();
  LogTarget.println("UTC:             " + UTC.dateTime());


  // See if local time can be obtained (does not work in countries that span multiple timezones)
  LogTarget.print(F("Local (GeoIP):   "));
  if (myTZ.setLocation()) {    // TODO: this seems to fail, maybe bug in ezTime?
    LogTarget.println((String)"→ location set via GeoIP: " + myTZ.dateTime());
  } else {
    LogTarget.println((String)"→ error: [" + errorString() + "]");
    delay(5000);
    LogTarget.print((String)"Local (" + TIMELOC + "):      ");
    if (myTZ.setLocation(TIMELOC)) {
      LogTarget.println(myTZ.dateTime());
    } else {
      LogTarget.println((String)"→ error: [" + errorString() + "]");
      LogTarget.println((String)"using UTC: " + myTZ.dateTime());
    }
  }
}


void loop()
{
  ArduinoOTA.handle();

  ss = myTZ.second();
  mm = myTZ.minute();
  hh = myTZ.hour();

  if (ss % 10 == 0) { // 10 s cycle
    if (ss0 != ss) {
      ss0 = ss; // only once (this will not work if the cycle is set to n*60! you will need to check the minutes as well.)
      // debug: publish current time
      char timestr[20];
      sprintf(timestr, "%02d:%02d:%02d", hh, mm, ss);
      LogTarget.println(timestr);
      client.publish((((String)TOPICROOT "/" + devname + "/status/time").c_str()), timestr);
    }
  }

  if (!client.connected()) {
    reconnect();
    showStatus("ready");
    if (do_publishes)
      if (first_connect) {
        ip = WiFi.localIP();
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "startup");
        client.publish((((String)TOPICROOT "/" + devname + "/status/version").c_str()), ((String)"version " + VERSION).c_str());
        client.publish((((String)TOPICROOT "/" + devname + "/status/ip").c_str()), ip.toString());
        String confstr = get_pump_setup();
        client.publish((((String)TOPICROOT "/" + devname + "/status/pump_pins").c_str()), confstr.c_str());

        first_connect = false;
      }
    //client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "online");
  }
  client.loop();    // for MQTT / WiFi
  events();         // for ezTime
  loop_pumps();     // for pumps

  delay(100);
}


/* MQTT command syntax:
    topic:    TOPICROOT / DEV / COMMAND / PUMPIDX
    or:       TOPICROOT / DEV / config / PARAMNAME
    message:  PARAMVALUE
    with
      TOPICROOT:  as defined, default is "DosingFeeder"
      DEV:        either "all"
                  or "dosingfeeder-HEXADR" for individual addressing, HEXADR being the last 3 bytes of the MAC address
      PUMPIDX:    index of pump, starting with 0
      COMMAND:    command name, one of:
                  interval
                  size
                  shot
                  reset
    or
      PARAMNAME:  parameter name, one of:
                  pump_pins -> list of portpin numbers of all pumps, separated with " "
      This command may only be given after reset (either power-on reset or reset triggered by the reset command).
      This command should be deployed as retained in order to configure the device.
*/

void mqttReceiveCallback(char* topic, byte* payload, unsigned int length)
{
  const bool pr = DEBUGPRINT;                   // set to 1 for debug prints

  if (String(topic).indexOf("/status") != -1) return; // don't process stuff we just published

  LogTarget.println("MQTT in");
  
  String command = topic + String(topic).lastIndexOf(TOPICROOT "/") + strlen(TOPICROOT) + 1;
  command.toLowerCase();

  if (pr) LogTarget.println((String)"Check command " + command + " for match with devname_lc " + devname_lc);


  if (command.startsWith("dosingfeeder-")) { // device-specific topic was used
    if (pr) LogTarget.println((String)"    ok, has required prefix");
    if (command.startsWith(devname_lc)) {   // this device was addressed
      if (pr) LogTarget.println((String)"    ok, matches our name");
      command.remove(0, strlen(devname) + 1);   // strip device name
    } else {                                // other device => ignore
      if (pr) LogTarget.print((String)"    no, name does not match - ignore command");
      return;
    }
  } else if (command.startsWith("all")) {   // all devices, but only if we are listening to these topic
    if (pr) LogTarget.println((String)"    ok, matches 'all'");
    command.remove(0, strlen("all") + 1);   // strip device name
  } else {
    if (pr) LogTarget.println((String)"    incorrect/obsolete addressing scheme, ignore command");
    return;
  }

  LogTarget.print((String)"got topic: " + topic + "\t = [");
  for (int i = 0; i < min((int)length, 80); i++)  LogTarget.print((char)(payload[i]));
  if (length > 80) LogTarget.print("...");
  LogTarget.println("]");
  LogTarget.println((String)"-> command = [" + command + "]");

  if (command.equals("config/pump_pins")) {
        if (pump_setup_is_valid()) {
          // ignore config if already configured
          return;
        }
        payload[length] = 0;  // set string end
        bool ok = config_pumps((char*)payload);
        String confstr = get_pump_setup();
        client.publish((((String)TOPICROOT "/" + devname + "/status/pump_pins").c_str()), confstr.c_str());

        if (ok) {
          // now subscribe to all relevant topics
          String topic = (String)TOPICROOT + "/#";
          LogTarget.println((String)"subscribe to all topics: " + topic);
          client.subscribe(topic.c_str());
          client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "online, active", true);
        } else {
          LogTarget.println("error: invalid pump setup");
        }
  }

  // all following commands have syntax "cmd/pumpidx", cmd being the command name and pumpidx the pump index

  int pumpidx = atoi(command.c_str() + command.lastIndexOf("/") + 1);
  LogTarget.println((String)"pumpidx = " + pumpidx);

  if (command.startsWith("starttime/")) {
    processStarttime(pumpidx, (char*)payload, length);
    return;
  }

  if (command.startsWith("interval/")) {
    processInterval(pumpidx, (char*)payload, length);
    return;
  }

  if (command.startsWith("duration/")) {
    return processDuration(pumpidx, (char*)payload, length);
  }

  if (command.startsWith("params/")) {
    // need to copy into internal buffer because MQTT buffer is overwritten on publishes in-between
    const int PARAMSIZE = 20;
    char params[PARAMSIZE];
    strncpy(params, (char*)payload, PARAMSIZE);   
    int newlen = min((int)length, PARAMSIZE-1);
    LogTarget.println((String)"processing params command, payload length = " + length + " -> " + newlen);
    params[newlen] = 0;
    LogTarget.println((String)"processing params command with payload [" + (char*)params + "]");
    char* context;
    char* token = strtok_r((char*)params, ";", &context);
    if (token == NULL) {
      LogTarget.println((String)"error in params command: payload [" +  (char*)params + "]");
      return;
    }
    LogTarget.println((String)" -> starttime " + (char*)params);
    // first field is starttime, will be set after other parameters
    token = strtok_r(NULL, ";", &context);
    while (token != NULL) {
      LogTarget.println((String)" token: " + token);
      switch (token[0]) {
        case 'i': // interval
                  LogTarget.println((String)" -> interval " + (token+1));
                  processInterval(pumpidx, token+1, strlen(token+1));
                  break;
        case 'd': // duration
                  LogTarget.println((String)" -> duration " + (token+1));
                  processDuration(pumpidx, token+1, strlen(token+1));
                  break;
        /*
        case 's': // size
                  LogTarget.println((String)" -> size " + token+1);
                  processSize(pumpidx, token+1, strlen(token+1));
                  break;
        */
        default:  // unknown id char
                  LogTarget.println((String)"error in params command: unknown parameter id '" + token[0] + "'");
                  break;
      }
      token = strtok_r(NULL, ";", &context);
    }
    processStarttime(pumpidx, (char*)params, length);
    return;
  }

  if (command.startsWith("shot")) {
    char* errptr;
    float value = strtof((char*)payload, &errptr);
    if ((char*)payload == errptr) {
      LogTarget.println((String)"error parsing shot value for pump " + pumpidx + ": value = " + value);
      return;
    }
    value = max(0.0f, min(120.0f, value));
    // set shot;
    LogTarget.println((String)"pump " + pumpidx + ": perform shot = " + value);
    start_pump(pumpidx, value);
    return;
  }

  if (command.equals("reset")) {
        reset_pump_config();
        String confstr = get_pump_setup();
        client.publish((((String)TOPICROOT "/" + devname + "/status/pump_pins").c_str()), confstr.c_str());
        // unsubscribe from all topics
        String topic;
        topic = (String)TOPICROOT + "/#";
        LogTarget.println((String)"unsubscribe from all topics: " + topic);
        client.unsubscribe(topic.c_str());
        // resubscribe to config topics
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "online, wait for config", true);
        topic = (String)TOPICROOT + "/" + devname + "/config/pump_pins";
        LogTarget.println((String)"resubscribe to config topic: " + topic);
        client.subscribe(topic.c_str()); // for now, only subscribe to configuration topic
  }

  if (command.equals("restart")) {
        ESP.restart();
  }

}

void processDuration(int pumpidx, char* payload, int length)
{
  char* errptr;
  float value = strtof((char*)payload, &errptr);
  if ((char*)payload == errptr) {
    LogTarget.println((String)"error parsing duration value for pump " + pumpidx + ": value = " + value);
    return;
  }
  value = max(0.0f, min(120.0f, value));
  LogTarget.println((String) "pump " + pumpidx + ": set duration = " + value);
  if (!set_pump_duration(pumpidx, value)) {
    LogTarget.println((String) "Error");
  }
  return;
}

void processInterval(int pumpidx, char* payload, int length)
{
  char* errptr;
  float value = strtof((char*)payload, &errptr);
  if ((char*)payload == errptr) {
    LogTarget.println((String)"error parsing duration value for pump " + pumpidx + ": value = " + value);
    return;
  }
  value = max(0.0f, min(24.0f * 3600 - 1, value));
  LogTarget.println((String) "pump " + pumpidx + ": set interval = " + value);
  if (!set_pump_interval(pumpidx, value)) {
    LogTarget.println((String) "Error");
  }
}

void processStarttime(int pumpidx, char* payload, int length)
{
  if (strncasecmp("now", payload, 3) == 0) {
    LogTarget.println((String) "pump " + pumpidx + ": set starttime to now");
    if (!set_pump_starttime_now(pumpidx)) {
      LogTarget.println((String) "Error");
    }
  } else {
    int hh = max(0, min(23, (10 * (payload[0] - '0') + (payload[1] - '0'))));
    int mm = max(0, min(59, (10 * (payload[3] - '0') + (payload[4] - '0'))));
    int ss = max(0, min(59, (10 * (payload[6] - '0') + (payload[7] - '0'))));
    LogTarget.println((String) "pump " + pumpidx + ": set starttime = " + hh + ":" + mm + ":" + ss);
    if (!set_pump_starttime(pumpidx, hh, mm, ss)) {
      LogTarget.println((String) "Error");
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    LogTarget.print("Attempting MQTT connection...");
    String clientId = APPNAME "-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), sett.mqtt_user, sett.mqtt_password, (((String)TOPICROOT "/" + devname + "/status").c_str()), 1, true, "offline")) {
      LogTarget.println("connected");
      if (pump_setup_is_valid()) {
        String topic = (String)TOPICROOT + "/#";
        LogTarget.println((String)"subscribe to all topics: " + topic);
        client.subscribe(topic.c_str());
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "online, active", true);
      } else {
        client.publish((((String)TOPICROOT "/" + devname + "/status").c_str()), "online, wait for config", true);
        String topic = (String)TOPICROOT + "/" + devname + "/config/pump_pins";
        LogTarget.println((String)"subscribe to config topic: " + topic);
        client.subscribe(topic.c_str()); // for now, only subscribe to configuration topic
      }
      showStatus("MQTT ok");
    } else {
      LogTarget.print("failed, rc=");
      LogTarget.print(client.state());
      LogTarget.println(" try again in 5 seconds");
      showStatus("MQTT error");
      delay(5000);
    }
  }
}

void showStatus(const char* text)
{
  Serial.println((String)"showStatus: [" + text + "]");
}

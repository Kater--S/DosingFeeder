#pragma once

#include "local_config.h"


#define VERSION "1.0.0"           // complete version
#define VERSION_2LVL  "1.0"       // 2-level version number used for validating flash data

/*  Version history:
    1.0.0   first release version, MVP status
    0.0.1   initial version, copied from MarQueTTino sources
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ezTime.h>

// extern declarations

// in main.cpp
extern WiFiClient espClient;
extern PubSubClient client;
extern Timezone myTZ;

// in wm_config.cpp
extern char devaddr[20];
extern char devname[40];
extern String devname_lc;
extern IPAddress ip;


// default settings if not set by local_config.h: APPNAME, TOPICROOT, LOG_TELNET, DEBUGPRINT

#ifndef TIMELOC
#define TIMELOC     "DE"
#endif

#ifndef APPNAME
#define APPNAME     "DosingFeeder"
#endif

#ifndef TOPICROOT
#define TOPICROOT   APPNAME
#endif

#ifndef LOG_TELNET
#define LOG_TELNET  0
#endif

#if LOG_TELNET
#include <TelnetStream.h>
#define LogTarget   TelnetStream
#else
#define LogTarget   Serial
#endif

#ifndef DEBUGPRINT
#define DEBUGPRINT  0
#endif

// max GPIO pin number accepted; should be platform-dependant
#define MAX_PIN     16
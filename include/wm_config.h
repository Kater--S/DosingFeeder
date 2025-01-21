#pragma once

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Arduino.h>
#include <EEPROM.h>

#include "local_config.h"
#include "globals.h"

////////////////
// SETUP

// general
#define APPVERSION    VERSION
//      ^^^^^------ change APPVERSION (aka VERSION) in order to invalidate flash storage!
#define IDENT         (APPNAME "_" APPVERSION)
#define RESET_IDENT   "RESET"

#define IDENT_LENGTH  30
#define STR_LENGTH    80
#define NUM_LENGTH     5

#define SETTINGS_POS  0


// callback function for status information
extern std::function<void(const char*)> statusCallback;

//extern uint16_t zweiterstart;

extern struct Settings {
  char settings_identifier[IDENT_LENGTH];
  char mqtt_broker[STR_LENGTH];
  char mqtt_user[STR_LENGTH];
  char mqtt_password[STR_LENGTH];
} sett;



void dumpEEPROMBuffer();
//void startAPCallback(WiFiManager* wm);
//void saveConfigCallback ();
//void initiateFactoryReset();
void setup_wifi(std::function<void(const char*)> func = 0);

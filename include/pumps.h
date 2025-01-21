#pragma once

#include "local_config.h"
#include "globals.h"

// pump setup
const int MAX_PUMPS = 10;                         // max number of pumps

// job management (static ring buffer)
const int MAX_JOBS  = 20;

void setup_pumps();
bool config_pumps(char* configstr);
void reset_pump_config();
bool pump_setup_is_valid();
String get_pump_setup();

void loop_pumps();

bool set_pump_starttime(int pumpidx, int hh=0, int mm=0, int ss=0);
bool set_pump_starttime_now(int pumpidx);
bool set_pump_interval(int pumpidx, float intv=0);
bool set_pump_duration(int pumpidx, float dur=0);

bool start_pump(int pumpidx, float duration);

//bool enqueue_job(int pumpidx, long int duration);
//bool dequeue_job(int& index, long int& duration);
//int queuesize();
//bool is_empty();
//bool is_full();

//void set_pump(int index, int state);

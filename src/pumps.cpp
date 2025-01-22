
#include <Arduino.h>

#include "pumps.h"

#ifdef ARDUINO_ARCH_ESP8266
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
const int allowed_gpios[] = {16, 5,4, 0, 2, 14, 12, 13, 15};    // all GPIOs except RX, TX
#endif
#endif

const bool pr = DEBUGPRINT;

uint8_t num_pumps = 0;
uint8_t pump_pins[MAX_PUMPS];              // DOUT port numbers for pumps, D4 = builtin LED

// timer
int pump_starttime_hh[MAX_PUMPS];
int pump_starttime_mm[MAX_PUMPS];
int pump_starttime_ss[MAX_PUMPS];
long int pump_activation_millis[MAX_PUMPS];
float pump_duration[MAX_PUMPS];
float pump_interval[MAX_PUMPS];
bool pump_starttime_pending[MAX_PUMPS];

// queue
int curr = 0, next = 0;
int job_pump[MAX_JOBS];
long int job_duration[MAX_JOBS];
bool is_working = false;
long int last_start, next_end;
int pump;
long int duration;

// forward declarations
bool enqueue_job(int pumpidx, long int duration);
bool dequeue_job(int& pump, long int& duration);
int queuesize();
bool is_empty();
bool is_full();
void set_pump(int pump, int state);
void publish_pump_params(int pumpidx);
void publish_jobs_queue();
void init_data()
{
    // initialize bookkeeping
    for (int i=0; i<MAX_PUMPS; i++) {
        pump_starttime_hh[i] = 0;
        pump_starttime_mm[i] = 0;
        pump_starttime_ss[i] = 0;
        pump_activation_millis[i] = 0;
        pump_duration[i] = 0;
        pump_interval[i] = 0;
    }
}

void setup_pumps()
{
    // switch off all GPIOs
    for (unsigned int i=0; i < (sizeof(allowed_gpios)/sizeof(int)); i++) {
        Serial.println((String)"switch off GPIO " + allowed_gpios[i]);
        pinMode(allowed_gpios[i], OUTPUT);
        digitalWrite(allowed_gpios[i], 0);
    }
    init_data();
}

bool config_pumps(char* configstr)
{
    if (pr) LogTarget.println((String)"config_pumps(): parse configstr=" + configstr);

    reset_pump_config();

    num_pumps = 0;
    char* token = strtok(configstr, " ");
    while (token != NULL) {
        int pin = atoi(token);
        bool ok = false;
        for (unsigned int j=0; j < (sizeof(allowed_gpios)/sizeof(int)); j++) {
            if (allowed_gpios[j] == pin) ok = true;
        }
        if (!ok) {
            LogTarget.println((String)"illegal pin number in pump config: " + pin);
            return false;
        }
        if (pr) LogTarget.println((String)"  " + token + " -> " + atoi(token));
        pump_pins[num_pumps++] = atoi(token);
        token=strtok(NULL, " ");
    }
    LogTarget.print((String)"pump config: " + num_pumps + " pumps:");
    for (int i=0; i<num_pumps; i++)
        LogTarget.print((String)" " + pump_pins[i]);
    LogTarget.println();

    for (int i = 0; i < num_pumps; i++) {
        LogTarget.println((String)"initializing pump #" + i + " = Pin " + pump_pins[i]);
        pinMode(pump_pins[i], OUTPUT);
        set_pump(i, 0);
        publish_pump_params(i);
    }
    init_data();
    return true;
}

void reset_pump_config()
{
    LogTarget.println((String)"reset pump configuration");
    for (int i = 0; i < num_pumps; i++) {
        LogTarget.println((String)"switch off pump #" + i + " = Pin " + pump_pins[i]);
        set_pump(i, 0);
    }
    num_pumps = 0;
    curr = 0;
    next = 0;
    is_working = false;
    init_data();
    LogTarget.println((String)"- done.");
}

bool pump_setup_is_valid()
{
    return num_pumps > 0;
}

String get_pump_setup()
{
    String ret = (String)num_pumps + ":";
    for (int i = 0; i < num_pumps; i++) {
        ret = ret + " " + pump_pins[i];
    }
    return ret;
}

bool set_pump_starttime(int pumpidx, int hh, int mm, int ss)
{
    if (pr) LogTarget.println((String)"set pump starttime: pump " + pumpidx + ", starttime " + hh + ":" + mm + ":" + ss);
    if (pumpidx >= num_pumps) {
        if (pr) LogTarget.println((String)"error: pump index out of range");
        return false;
    }
    pump_starttime_hh[pumpidx] = hh;
    pump_starttime_mm[pumpidx] = mm;
    pump_starttime_ss[pumpidx] = ss;
    pump_starttime_pending[pumpidx] = true;
    pump_activation_millis[pumpidx] = 0;
    publish_pump_params(pumpidx);
    return true;
}

bool set_pump_starttime_now(int pumpidx)
{
    if (pr) LogTarget.println((String)"set pump starttime now: pump " + pumpidx);
    return set_pump_starttime(pumpidx, myTZ.hour(), myTZ.minute(), myTZ.second());
}

bool set_pump_interval(int pumpidx, float intv)
{
    if (pr) LogTarget.println((String)"set pump interval: pump " + pumpidx + ", interval " + intv);
    if (pumpidx >= num_pumps) {
        if (pr) LogTarget.println((String)"error: pump index out of range");
        return false;
    }
    pump_interval[pumpidx] = intv;
    publish_pump_params(pumpidx);
    return true;
}

bool set_pump_duration(int pumpidx, float dur)
{
    if (pr) LogTarget.println((String)"set pump duration: pump " + pumpidx + ", duration " + dur);
    if (pumpidx >= num_pumps) {
        if (pr) LogTarget.println((String)"error: pump index out of range");
        return false;
    }
    pump_duration[pumpidx] = dur;
    publish_pump_params(pumpidx);
    return true;
}

bool start_pump(int pumpidx, float duration)
{
    return enqueue_job(pumpidx, duration * 1000);  // convert float seconds to in milliseconds
}

bool stop_pump(int pumpidx)
{
    set_pump(pumpidx, 0);
    pump_activation_millis[pumpidx] = 0;
    pump_starttime_pending[pumpidx] = false;
    // check if this pump is currently running
    if (is_working && pump == pumpidx) {
        is_working = false;
        set_pump(pump, 0);
    }
    publish_pump_params(pumpidx);
    // remove all jobs for this pump
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_pump[i] == pumpidx) {
            job_pump[i] = -1;
            job_duration[i] = 0;
        }
    }
    return true;
}

void stop_all_pumps()
{
    for (int i = 0; i < num_pumps; i++) {
        stop_pump(i);
    }
}

long int lastcall_s = -1;
int last_hh = -1;

/// @brief this is called once in each loop() cycle in the main program
void loop_pumps()
{
    long int now = millis();
    //if (pr) LogTarget.println((String)"loop_pumps(): now=" + now);

    // skip if still in the same second interval designated by lastcall_s
    long int now_s = now / 1000;
    if (lastcall_s != now_s) {
        lastcall_s = now_s;
        LogTarget.println((String)"new second: " + now_s);

        // when new day has begun, reset all start times
        int hh = myTZ.hour();
        if (hh == 0 && last_hh > 0) {
            LogTarget.println((String)"daystart: reset all start times");
            for (int i=0; i < num_pumps; i++) {
                pump_starttime_pending[i] = true;
                publish_pump_params(i);
            }
        }

        // manage start times
        // TODO:
        // This could fail if the activation time has a high seconds value (e.g. 59) 
        // and the check happens in the next minute interval.
        // Calculation based on UNIX timestamps would be rather costly... could be done once at daystart.
        int mm = -1;
        int ss = -1;
        for (int i=0; i < num_pumps; i++) {
            if (!pump_starttime_pending[i]) {
                LogTarget.println((String)"pump " + i + " starttime not pending");
                continue;
            }
            LogTarget.println((String)"check pump " + i + ": pending starttime hour: should be " + pump_starttime_hh[i] + ", is " + hh);
            if (pump_starttime_hh[i] == hh) {
                if (mm == -1) mm = myTZ.minute();
                LogTarget.println((String)"match -- check minute: should be " + pump_starttime_mm[i] + ", is " + mm);
                if (pump_starttime_mm[i] == mm) {
                    if (ss == -1) ss = myTZ.second();
                    LogTarget.println((String)"match -- check second: should be " + pump_starttime_ss[i] + " or greater, is " + ss);
                    if (pump_starttime_ss[i] <= ss) {
                        if (pump_duration[i] > 0) {
                            // starttime matched
                            LogTarget.println((String)"start pump " + i + " now at " + hh + ":" + mm + ":" + ss 
                                                    + " -- should be on ::" + pump_starttime_ss[i]);
                            LogTarget.println((String)"      interval " + pump_interval[i] + " s, duration " + pump_duration[i] + " s");
                            pump_activation_millis[i] = now;
                            pump_starttime_pending[i] = false;
                        } else {
                            LogTarget.println((String)"pump " + i + " duration is " + pump_duration[i] + ", nop");
                        }
                    } else {
                        LogTarget.println((String)"no match in second value");
                    }
                }
            }
        }
    }

    // manage intervals
    for (int i=0; i < num_pumps; i++) {
        if ((pump_activation_millis[i] != 0) && (pump_activation_millis[i] <= now)) {
            // activation is due
            enqueue_job(i, pump_duration[i] * 1000);
            if (pump_interval[i] > 0) {
                pump_activation_millis[i] += pump_interval[i] * 1000;
                if (pump_activation_millis[i] <= now) {
                    // next activation is also due -- problem
                    // only warning, no action
                    LogTarget.println((String)"Cannot keep up with schedule for pump #" + i + ": already lagging " + (now-pump_activation_millis[i])/1000 + " s, with interval " + pump_interval[i] + " s and duration " + pump_duration[i] + " s");
                    // ??publish a warning via MQTT??
                }
            } else {
                pump_activation_millis[i] = 0;
            }
        }
    }

    // manage running + queued jobs
    if (is_working) {
        if (pr) LogTarget.println((String)"busy, job running: last_start=" + last_start + ", duration=" + duration + " => next_end=" + next_end);
        if (now > next_end) {  // done
            if (pr) LogTarget.println((String)"job done");
            set_pump(pump, 0); // turn off
            is_working = false;
        } else {

        }
    } else {
        //if (pr) LogTarget.println((String)"idle, no job running");
        // idle. check if jobs are waiting to be done
        if (!is_empty()) {
            if (pr) LogTarget.println((String)queuesize() + " job(s) in queue");
            if (dequeue_job(pump, duration)) {
                // start next jobs
                set_pump(pump, 1);
                last_start = now;
                next_end = now + duration;
                is_working = true;
            }
        }
    }
}


void printQueueStatus()
{
    LogTarget.println((String)"queue: curr=" + curr + ", next=" + next);
    LogTarget.print((String)"now " + queuesize() + " jobs in queue: [");
    for (int i = 0; i < MAX_JOBS; i++) {
        LogTarget.print((String)"(" + job_pump[i] + "," + job_duration[i] + ") ");
    }
    LogTarget.println("]");
    publish_jobs_queue();
}

bool enqueue_job(int pump, long int duration)
{
    if (pr) printQueueStatus();
    if (pr) LogTarget.println((String)"enqueue job: pump " + pump + ", duration " + duration);
    if (pump >= num_pumps) {
        if (pr) LogTarget.println((String)"error: pump index out of range");
        return false;
    }
    if (is_full()) {
        if (pr) LogTarget.println((String)"error: queue overflow");
        return false;
    }
    job_pump[next] = pump;
    job_duration[next] = duration;
    next = (next+1) % MAX_JOBS;
    if (pr) printQueueStatus();
    return true;
}

bool dequeue_job(int& pump, long int& duration)
{
    if (pr) printQueueStatus();
    if (pr) LogTarget.print((String)"dequeue job: ");
    if (is_empty()) {
        if (pr) LogTarget.println((String)"error: queue underflow");
        return false;
    }
    pump = job_pump[curr];
    duration = job_duration[curr];
    curr = (curr+1) % MAX_JOBS;
    if (pr) LogTarget.println((String)"pump " + pump + ", duration " + duration);
    if (pr) printQueueStatus();
    return true;
}

int queuesize()
{
    return (next + MAX_JOBS - curr) % MAX_JOBS;
}

bool is_empty()
{
    return curr == next;
}
bool is_full()
{
    return curr == ((next+1) % MAX_JOBS);
}

void set_pump(int pump, int state)
{
    if (pump >= num_pumps) return;
    LogTarget.println((String)"set pump #" + pump + " = Pin " + pump_pins[pump] + " to " + state);
    digitalWrite(pump_pins[pump], state);
    client.publish((((String)TOPICROOT "/" + devname + "/status/pump/" + pump + "/state").c_str()), state ? "1":"0");
}

void publish_pump_params(int pumpidx)
{
    char params[80];
    snprintf(params, 80, "%02d:%02d:%02d;i%.1f;d%.1f;%s", 
                pump_starttime_hh[pumpidx], pump_starttime_mm[pumpidx], pump_starttime_ss[pumpidx], 
                pump_interval[pumpidx], 
                pump_duration[pumpidx], 
                (pump_starttime_pending[pumpidx] ? "pending":"not pending"));
    client.publish((((String)TOPICROOT "/" + devname + "/status/pump/" + pump + "/params").c_str()), params);
}

void publish_jobs_queue()
{
    //"curr=" + curr + ", next=" + next + 
    String s = (String)"" + queuesize() + " jobs: [ ";
    for (int i = 0; i < queuesize(); i++) {
        s += (String)"(" + job_pump[(curr+i) % MAX_JOBS] + "," + job_duration[(curr+i) % MAX_JOBS] + ") ";
    }
    s += (String)"]";
    client.publish((((String)TOPICROOT "/" + devname + "/status/queue").c_str()), s.c_str());
}
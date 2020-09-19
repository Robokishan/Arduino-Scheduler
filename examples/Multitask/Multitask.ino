#include "Scheduler.h"


Scheduler scheduler;


void taskSystemLoad(timeUs_t currentTimeUs);
void taskMain(timeUs_t currentTimeUs);
void taskInfo(timeUs_t currentTimeUs);
void taskBlink(timeUs_t currentTimeUs);

task_t tasks[TASK_COUNT] = {
    [TASK_SYSTEM] = DEFINE_TASK("SYSTEM", NULL, taskSystemLoad, TASK_PERIOD_HZ(10), TASK_PRIORITY_MEDIUM_HIGH),
    [TASK_MAIN] = DEFINE_TASK("MAIN", NULL, taskMain, TASK_PERIOD_HZ(1000), TASK_PRIORITY_REALTIME),
    [TASK_INFO] = DEFINE_TASK("INFO", NULL, taskInfo, TASK_PERIOD_MS(2000), TASK_PRIORITY_LOW),
    [TASK_BLINK] = DEFINE_TASK("BLINK", NULL, taskBlink, TASK_PERIOD_HZ(1), TASK_PRIORITY_HIGH),
};


void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN,OUTPUT);
    scheduler.queueClear();
    scheduler.debug(true);//default is false
    scheduler.queueAdd(scheduler.getTask(TASK_SYSTEM));
    scheduler.setTaskEnabled(TASK_INFO, true);
    scheduler.setTaskEnabled(TASK_MAIN, true);
    scheduler.setTaskEnabled(TASK_BLINK, true);
}


void loop() {
    scheduler.run_scheduler();
}


void taskSystemLoad(timeUs_t currentTimeUs){
  scheduler.taskSystemLoad(currentTimeUs);
}

void taskInfo(timeUs_t currentTimeUs){
  scheduler.printTasks();
}

void taskBlink(timeUs_t currentTimeUs){
  digitalWrite(LED_BUILTIN,!digitalRead(LED_BUILTIN));
}

// you should keep realtime task as low as possible maximum 2 is preferable
void taskMain(timeUs_t currentTimeUs){
//   Serial.println("THIS IS REALTIME TASK"); //uncomment this line to see fast loop
}
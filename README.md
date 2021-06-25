= Scheduler library for Arduino =

This library allows an Arduino board to run tasks at specific frequency.

For more information about this library please visit us at
https://github.com/Robokishan/Arduino-Scheduler

Project is inspired from scheduler of [betaflight](https://github.com/betaflight/betaflight) 

## Declare

    Scheduler scheduler;

## Declare a task

Declare the function and add that call back function to the task

    void taskBlink(timeUs_t currentTimeUs){
        scheduler.Logln("Blink %d second",1);
        digitalWrite(LED_BUILTIN,!digitalRead(LED_BUILTIN));
    }
    
    task_t tasks[TASK_COUNT] = {
	    [TASK_BLINK] = DEFINE_TASK("BLINK", NULL, taskBlink, TASK_PERIOD_HZ(10), TASK_PRIORITY_MEDIUM_HIGH),
    }

## Declare task Another way

    Scheduler scheduler;
    
    // Function structure should be same void as return  type and argument as timeUs_t
    void taskBlink(timeUs_t currentTimeUs);
    
    task_t tasks[TASK_COUNT] = {
        [TASK_BLINK] = DEFINE_TASK("BLINK", NULL, taskBlink, TASK_PERIOD_HZ(1), TASK_PRIORITY_HIGH),
    };
    
    void setup() {
        Serial.begin(115200);
        pinMode(LED_BUILTIN,OUTPUT);
        scheduler.queueClear();
        scheduler.debug(true);//default is false
        scheduler.setTaskEnabled(TASK_BLINK, true);
    }
    
    void loop() {
	      scheduler.run_scheduler();
    }
    
    void taskBlink(timeUs_t currentTimeUs){
        scheduler.Logln("Blink %d second",1);
        digitalWrite(LED_BUILTIN,!digitalRead(LED_BUILTIN));
    }

## Enable Task

    scheduler.setTaskEnabled(TASK_BLINK, true);

## Disable Task

    scheduler.setTaskEnabled(TASK_BLINK, false);

## Run Scheduler

    void loop() {
	    scheduler.run_scheduler();
    }

## Don't Forget

### Inside Scheduler.h

inside scheduler.h you need to add your custom task ids 

    typedef enum {
        /* Actual tasks */
        TASK_SYSTEM = 0,
        TASK_BLINK, //your custom taskid 
        <ADD YOUR TASK>,
        /* Service task IDs */
        TASK_NONE = TASK_COUNT,
        TASK_SELF
    } taskId_e;

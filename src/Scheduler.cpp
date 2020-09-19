#include "Scheduler.h"
#include <stdio.h>
#include <string.h>


#define TASK_AVERAGE_EXECUTE_FALLBACK_US 30 // Default task average time if USE_TASK_STATISTICS is not defined
#define TASK_AVERAGE_EXECUTE_PADDING_US 5   // Add a little padding to the average execution time

#if defined(USE_TASK_STATISTICS)
timeUs_t checkFuncMaxExecutionTimeUs;
timeUs_t checkFuncTotalExecutionTimeUs;
timeUs_t checkFuncMovingSumExecutionTimeUs;
timeUs_t checkFuncMovingSumDeltaTimeUs;
#endif

task_t* taskQueueArray[TASK_COUNT + 1]; // extra item for NULL pointer at end of queue
static int taskQueuePos = 0;
int taskQueueSize = 0;
static task_t *currentTask = NULL;
static bool calculateTaskStatistics = true;
uint16_t averageSystemLoadPercent = 0;
static int periodCalculationBasisOffset = offsetof(task_t, lastExecutedAtUs);


static uint32_t totalWaitingTasks;
static uint32_t totalWaitingTasksSamples;


Scheduler::Scheduler() {

}

void Scheduler::queueClear(void)
{
    memset(taskQueueArray, 0, sizeof(taskQueueArray));
    taskQueuePos = 0;
    taskQueueSize = 0;
}

bool Scheduler::queueContains(task_t *task)
{
    for (int ii = 0; ii < taskQueueSize; ++ii) {
        if (taskQueueArray[ii] == task) {
            return true;
        }
    }
    return false;
}

bool Scheduler::queueAdd(task_t *task)
{
    if ((taskQueueSize >= TASK_COUNT) || queueContains(task)) {
        return false;
    }
    for (int ii = 0; ii <= taskQueueSize; ++ii) {
        if (taskQueueArray[ii] == NULL || taskQueueArray[ii]->staticPriority < task->staticPriority) {
            memmove(&taskQueueArray[ii+1], &taskQueueArray[ii], sizeof(task) * (taskQueueSize - ii));
            taskQueueArray[ii] = task;
            ++taskQueueSize;
            return true;
        }
    }
    return false;
}

bool Scheduler::queueRemove(task_t *task)
{
    for (int ii = 0; ii < taskQueueSize; ++ii) {
        if (taskQueueArray[ii] == task) {
            memmove(&taskQueueArray[ii], &taskQueueArray[ii+1], sizeof(task) * (taskQueueSize - ii));
            --taskQueueSize;
            return true;
        }
    }
    return false;
}

/*
 * Returns first item queue or NULL if queue empty
 */
task_t* Scheduler::queueFirst(void)
{
    taskQueuePos = 0;
    return taskQueueArray[0]; // guaranteed to be NULL if queue is empty
}

/*
 * Returns next item in queue or NULL if at end of queue
 */
task_t* Scheduler::queueNext(void)
{
    return taskQueueArray[++taskQueuePos]; // guaranteed to be NULL at end of queue
}

task_t* Scheduler::getTask(unsigned taskId)
{
    return &tasks[taskId];
}

timeUs_t Scheduler::schedulerExecuteTask(task_t *selectedTask, timeUs_t currentTimeUs)
{
    timeUs_t taskExecutionTimeUs = 0;

    if (selectedTask) {
        currentTask = selectedTask;
        selectedTask->taskLatestDeltaTimeUs = cmpTimeUs(currentTimeUs, selectedTask->lastExecutedAtUs);
#if defined(USE_TASK_STATISTICS)
        float period = currentTimeUs - selectedTask->lastExecutedAtUs;
#endif
        selectedTask->lastExecutedAtUs = currentTimeUs;
        selectedTask->lastDesiredAt += (cmpTimeUs(currentTimeUs, selectedTask->lastDesiredAt) / selectedTask->desiredPeriodUs) * selectedTask->desiredPeriodUs;
        selectedTask->dynamicPriority = 0;

        // Execute task
#if defined(USE_TASK_STATISTICS)
        if (calculateTaskStatistics) {
            const timeUs_t currentTimeBeforeTaskCallUs = micros();
            selectedTask->taskFunc(currentTimeBeforeTaskCallUs);
            taskExecutionTimeUs = micros() - currentTimeBeforeTaskCallUs;
            selectedTask->movingSumExecutionTimeUs += taskExecutionTimeUs - selectedTask->movingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
            selectedTask->movingSumDeltaTimeUs += selectedTask->taskLatestDeltaTimeUs - selectedTask->movingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
            selectedTask->totalExecutionTimeUs += taskExecutionTimeUs;   // time consumed by scheduler + task
            selectedTask->maxExecutionTimeUs = MAX(selectedTask->maxExecutionTimeUs, taskExecutionTimeUs);
            selectedTask->movingAverageCycleTimeUs += 0.05f * (period - selectedTask->movingAverageCycleTimeUs);
        } else
#endif
        {
            selectedTask->taskFunc(currentTimeUs);
        }
    }

    return taskExecutionTimeUs;
}

inline static timeUs_t getPeriodCalculationBasis(const task_t* task)
{
    if (task->staticPriority == TASK_PRIORITY_REALTIME) {
        return *(timeUs_t*)((uint8_t*)task + periodCalculationBasisOffset);
    } else {
        return task->lastExecutedAtUs;
    }
}

void Scheduler::taskSystemLoad(timeUs_t currentTimeUs)
{
    // Calculate system load
    if (totalWaitingTasksSamples > 0) {
        averageSystemLoadPercent = 100 * totalWaitingTasks / totalWaitingTasksSamples;
        totalWaitingTasksSamples = 0;
        totalWaitingTasks = 0;
    }
}

void Scheduler::rescheduleTask(taskId_e taskId, timeDelta_t newPeriodUs)
{
    if (taskId == TASK_SELF) {
        task_t *task = currentTask;
        task->desiredPeriodUs = MAX(SCHEDULER_DELAY_LIMIT, newPeriodUs);  // Limit delay to 100us (10 kHz) to prevent scheduler clogging
    } else if (taskId < TASK_COUNT) {
        task_t *task = getTask(taskId);
        task->desiredPeriodUs = MAX(SCHEDULER_DELAY_LIMIT, newPeriodUs);  // Limit delay to 100us (10 kHz) to prevent scheduler clogging
    }
}

void Scheduler::setTaskEnabled(taskId_e taskId, bool enabled)
{
    if (taskId == TASK_SELF || taskId < TASK_COUNT) {
        task_t *task = taskId == TASK_SELF ? currentTask : getTask(taskId);
        if (enabled && task->taskFunc) {
            queueAdd(task);
        } else {
            queueRemove(task);
        }
    }
}

void Scheduler::run_scheduler(void)
{
    // Cache currentTime
    const timeUs_t schedulerStartTimeUs = micros();
    timeUs_t currentTimeUs = schedulerStartTimeUs;
    timeUs_t taskExecutionTimeUs = 0;
    task_t *selectedTask = NULL;
    uint16_t selectedTaskDynamicPriority = 0;
    uint16_t waitingTasks = 0;
    bool realtimeTaskRan = false;
    timeDelta_t mainTaskDelayUs = 0;

    task_t *mainTask = getTask(TASK_MAIN);
    const timeUs_t mainExecuteTimeUs = getPeriodCalculationBasis(mainTask) + mainTask->desiredPeriodUs;
    mainTaskDelayUs = cmpTimeUs(mainExecuteTimeUs, currentTimeUs);
    if (cmpTimeUs(currentTimeUs, mainExecuteTimeUs) >= 0) {
        taskExecutionTimeUs = schedulerExecuteTask(mainTask, currentTimeUs);
        currentTimeUs = micros();
        realtimeTaskRan = true;
    }

    // if something goes wrong look this variable
    // SerialDebug.println(mainTaskDelayUs);

    if (realtimeTaskRan || (mainTaskDelayUs > GUARD_INTERVAL_US)) {
        // The task to be invoked

        // Update task dynamic priorities
        for (task_t *task = queueFirst(); task != NULL; task = queueNext()) {
            if (task->staticPriority != TASK_PRIORITY_REALTIME) {
                // Task has checkFunc - event driven
                if (task->checkFunc) {
#if defined(SCHEDULER_DEBUG)
                    const timeUs_t currentTimeBeforeCheckFuncCallUs = micros();
#else
                    const timeUs_t currentTimeBeforeCheckFuncCallUs = currentTimeUs;
#endif
                    // Increase priority for event driven tasks
                    if (task->dynamicPriority > 0) {
                        task->taskAgeCycles = 1 + ((currentTimeUs - task->lastSignaledAtUs) / task->desiredPeriodUs);
                        task->dynamicPriority = 1 + task->staticPriority * task->taskAgeCycles;
                        waitingTasks++;
                    } else if (task->checkFunc(currentTimeBeforeCheckFuncCallUs, cmpTimeUs(currentTimeBeforeCheckFuncCallUs, task->lastExecutedAtUs))) {

#if defined(USE_TASK_STATISTICS)
                        if (calculateTaskStatistics) {
                            const uint32_t checkFuncExecutionTimeUs = micros() - currentTimeBeforeCheckFuncCallUs;
                            checkFuncMovingSumExecutionTimeUs += checkFuncExecutionTimeUs - checkFuncMovingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
                            checkFuncMovingSumDeltaTimeUs += task->taskLatestDeltaTimeUs - checkFuncMovingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
                            checkFuncTotalExecutionTimeUs += checkFuncExecutionTimeUs;   // time consumed by scheduler + task
                            checkFuncMaxExecutionTimeUs = MAX(checkFuncMaxExecutionTimeUs, checkFuncExecutionTimeUs);
                        }
#endif
                        task->lastSignaledAtUs = currentTimeBeforeCheckFuncCallUs;
                        task->taskAgeCycles = 1;
                        task->dynamicPriority = 1 + task->staticPriority;
                        waitingTasks++;
                    } else {
                        task->taskAgeCycles = 0;
                    }
                } else {
                    // Task is time-driven, dynamicPriority is last execution age (measured in desiredPeriods)
                    // Task age is calculated from last execution
                    task->taskAgeCycles = ((currentTimeUs - getPeriodCalculationBasis(task)) / task->desiredPeriodUs);
                    if (task->taskAgeCycles > 0) {
                        task->dynamicPriority = 1 + task->staticPriority * task->taskAgeCycles;
                        waitingTasks++;
                    }
                }

                if (task->dynamicPriority > selectedTaskDynamicPriority) {
                    selectedTaskDynamicPriority = task->dynamicPriority;
                    selectedTask = task;
                }
            }
        }

        totalWaitingTasksSamples++;
        totalWaitingTasks += waitingTasks;

        if (selectedTask) {
            timeDelta_t taskRequiredTimeUs = TASK_AVERAGE_EXECUTE_FALLBACK_US;  // default average time if task statistics are not available
#if defined(USE_TASK_STATISTICS)
            if (calculateTaskStatistics) {
                taskRequiredTimeUs = selectedTask->movingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT + TASK_AVERAGE_EXECUTE_PADDING_US;
            }
#endif
            // Add in the time spent so far in check functions and the scheduler logic
            taskRequiredTimeUs += cmpTimeUs(micros(), currentTimeUs);
            if (realtimeTaskRan || (taskRequiredTimeUs < mainTaskDelayUs)) {
                taskExecutionTimeUs += schedulerExecuteTask(selectedTask, currentTimeUs);
            } else {
                selectedTask = NULL;
            }
        }
    }
}

#if defined(USE_TASK_STATISTICS)
void Scheduler::getCheckFuncInfo(cfCheckFuncInfo_t *checkFuncInfo)
{
    checkFuncInfo->maxExecutionTimeUs = checkFuncMaxExecutionTimeUs;
    checkFuncInfo->totalExecutionTimeUs = checkFuncTotalExecutionTimeUs;
    checkFuncInfo->averageExecutionTimeUs = checkFuncMovingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
    checkFuncInfo->averageDeltaTimeUs = checkFuncMovingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
}
#endif

void Scheduler::printTasks(void)
{
    int maxLoadSum = 0;
    int averageLoadSum = 0;
    Logln("Task list             rate/hz  max/us  avg/us maxload avgload  total/ms");
    for (int taskId = 0; taskId < TASK_COUNT; taskId++) {
        taskInfo_t taskInfo;
        getTaskInfo((taskId_e)taskId, &taskInfo);
        if (taskInfo.isEnabled) {
            int taskFrequency = taskInfo.averageDeltaTimeUs == 0 ? 0 : lrintf(1e6f / taskInfo.averageDeltaTimeUs);
            Log("%02d - (%15s) ", taskId, taskInfo.taskName);
            const int maxLoad = taskInfo.maxExecutionTimeUs == 0 ? 0 :(taskInfo.maxExecutionTimeUs * taskFrequency + 5000) / 1000;
            const int averageLoad = taskInfo.averageExecutionTimeUs == 0 ? 0 : (taskInfo.averageExecutionTimeUs * taskFrequency + 5000) / 1000;
            // if (taskId != TASK_SERIAL) {
                maxLoadSum += maxLoad;
                averageLoadSum += averageLoad;
            // }
            #ifdef USE_TASK_STATISTICS
                Logln("%6d %7d %7d %4d.%1d%% %4d.%1d%% %9d",
                        taskFrequency, taskInfo.maxExecutionTimeUs, taskInfo.averageExecutionTimeUs,
                        maxLoad/10, maxLoad%10, averageLoad/10, averageLoad%10, taskInfo.totalExecutionTimeUs / 1000);
            #else
                Logln("%6d", taskFrequency);
            #endif

            schedulerResetTaskMaxExecutionTime((taskId_e)taskId);
        }
    }
    #ifdef USE_TASK_STATISTICS
        cfCheckFuncInfo_t checkFuncInfo;
        getCheckFuncInfo(&checkFuncInfo);
        Logln("Check Function %19d %7d %25d", checkFuncInfo.maxExecutionTimeUs, checkFuncInfo.averageExecutionTimeUs, checkFuncInfo.totalExecutionTimeUs / 1000);
        Logln("Total%25d.%1d%% %4d.%1d%%", maxLoadSum/10, maxLoadSum%10, averageLoadSum/10, averageLoadSum%10);
        schedulerResetCheckFunctionMaxExecutionTime();
    #endif
}
void Scheduler::schedulerResetTaskMaxExecutionTime(taskId_e taskId)
{
#if defined(USE_TASK_STATISTICS)
    if (taskId == TASK_SELF) {
        currentTask->maxExecutionTimeUs = 0;
    } else if (taskId < TASK_COUNT) {
        getTask(taskId)->maxExecutionTimeUs = 0;
    }
#endif
}

void Scheduler::Logln(const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    vprintln(fmt, argp);
    va_end(argp);
}
void Scheduler::Log(const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    vprint(fmt, argp);
    va_end(argp);
}

void Scheduler::vprintln(const char *fmt, va_list argp)
{
    char string[200];
    if(0 < vsprintf(string,fmt,argp)) // build string
    {
#ifndef THREAD_UART
        if(debug_flag == true)
        SerialDebug.println(string); // send message via UART
#else
        HAL_UART_Transmit_DMA(debug_uart, (uint8_t*)string, strlen(string));
#endif
    }
}
void Scheduler::vprint(const char *fmt, va_list argp)
{
    char string[200];
    if(0 < vsprintf(string,fmt,argp)) // build string
    {
#ifndef THREAD_UART
        if(debug_flag == true)
        SerialDebug.print(string); // send message via UART
#else
        HAL_UART_Transmit_DMA(debug_uart, (uint8_t*)string, strlen(string));
#endif
    }
}


void Scheduler::getTaskInfo(taskId_e taskId, taskInfo_t * taskInfo)
{
    taskInfo->isEnabled = queueContains(getTask(taskId));
    taskInfo->desiredPeriodUs = getTask(taskId)->desiredPeriodUs;
    taskInfo->staticPriority = getTask(taskId)->staticPriority;
    taskInfo->taskName = getTask(taskId)->taskName;
#if defined(USE_TASK_STATISTICS)
    taskInfo->maxExecutionTimeUs = getTask(taskId)->maxExecutionTimeUs;
    taskInfo->totalExecutionTimeUs = getTask(taskId)->totalExecutionTimeUs;
    taskInfo->averageExecutionTimeUs = getTask(taskId)->movingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
    taskInfo->averageDeltaTimeUs = getTask(taskId)->movingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
    taskInfo->latestDeltaTimeUs = getTask(taskId)->taskLatestDeltaTimeUs;
    taskInfo->movingAverageCycleTimeUs = getTask(taskId)->movingAverageCycleTimeUs;
#endif
}

#if defined(USE_TASK_STATISTICS)
void Scheduler::schedulerResetCheckFunctionMaxExecutionTime(void)
{
    checkFuncMaxExecutionTimeUs = 0;
}
#endif

void Scheduler::debug(bool _dbg){
    debug_flag = _dbg;
}
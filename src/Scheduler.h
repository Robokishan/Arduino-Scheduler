#pragma once

#include "Arduino.h"
#include <inttypes.h>
#include <stdarg.h>     /* va_list, va_start, va_arg, va_end */
#define SerialDebug Serial
#define USE_TASK_STATISTICS
#if defined(USE_TASK_STATISTICS)
#define TASK_STATS_MOVING_SUM_COUNT 32
#endif
// time difference, 32 bits always sufficient
typedef int32_t timeDelta_t;
// millisecond time
typedef uint32_t timeMs_t ;
// microsecond time
#ifdef USE_64BIT_TIME
typedef uint64_t timeUs_t;
#define TIMEUS_MAX UINT64_MAX
#else
typedef uint32_t timeUs_t;
#define TIMEUS_MAX UINT32_MAX
#endif

#define TASK_PERIOD_HZ(hz) (timeDelta_t(1000000) / float(hz))
#define TASK_PERIOD_MS(ms) (timeDelta_t(ms) * timeDelta_t(1000))
#define TASK_PERIOD_US(us) (timeDelta_t(us))
#define GUARD_INTERVAL_US 5
#define SCHEDULER_DELAY_LIMIT           100
#define MAX(a,b) \
  __extension__ ({ __typeof__ (a) _a = (a); \
  __typeof__ (b) _b = (b); \
  _a > _b ? _a : _b; })
static inline timeDelta_t cmpTimeUs(timeUs_t a, timeUs_t b) { return (timeDelta_t)(a - b); }


#if defined(USE_TASK_STATISTICS)
#define DEFINE_TASK(taskNameParam, checkFuncParam, taskFuncParam, desiredPeriodParam, staticPriorityParam) {  \
    .taskName = taskNameParam, \
    .checkFunc = checkFuncParam, \
    .taskFunc = taskFuncParam, \
    .desiredPeriodUs = desiredPeriodParam, \
    .staticPriority = staticPriorityParam \
}
#else
#define DEFINE_TASK(taskNameParam, checkFuncParam, taskFuncParam, desiredPeriodParam, staticPriorityParam) {  \
    .taskName = taskNameParam, \
    .checkFunc = checkFuncParam, \
    .taskFunc = taskFuncParam, \
    .desiredPeriodUs = desiredPeriodParam, \
    .staticPriority = staticPriorityParam \
}
#endif

typedef enum {
    TASK_PRIORITY_REALTIME = -1, // Task will be run outside the scheduler logic
    TASK_PRIORITY_IDLE = 0,      // Disables dynamic scheduling, task is executed only if no other task is active this cycle
    TASK_PRIORITY_LOW = 1,
    TASK_PRIORITY_MEDIUM = 3,
    TASK_PRIORITY_MEDIUM_HIGH = 4,
    TASK_PRIORITY_HIGH = 5,
    TASK_PRIORITY_MAX = 255
} taskPriority_e;

typedef struct {
    // Configuration
    const char * taskName;
    bool (*checkFunc)(timeUs_t currentTimeUs, timeDelta_t currentDeltaTimeUs);
    void (*taskFunc)(timeUs_t currentTimeUs);
    timeDelta_t desiredPeriodUs;      // target period of execution
    const int8_t staticPriority;    // dynamicPriority grows in steps of this size

    // Scheduling
    uint16_t dynamicPriority;       // measurement of how old task was last executed, used to avoid task starvation
    uint16_t taskAgeCycles;
    timeDelta_t taskLatestDeltaTimeUs;
    timeUs_t lastExecutedAtUs;        // last time of invocation
    timeUs_t lastSignaledAtUs;        // time of invocation event for event-driven tasks
    timeUs_t lastDesiredAt;         // time of last desired execution

#if defined(USE_TASK_STATISTICS)
    // Statistics
    float    movingAverageCycleTimeUs;
    timeUs_t movingSumExecutionTimeUs;  // moving sum over 32 samples
    timeUs_t movingSumDeltaTimeUs;  // moving sum over 32 samples
    timeUs_t maxExecutionTimeUs;
    timeUs_t totalExecutionTimeUs;    // total time consumed by task since boot
#endif
} task_t;

typedef struct {
    const char * taskName;
    const char * subTaskName;
    bool         isEnabled;
    int8_t       staticPriority;
    timeDelta_t  desiredPeriodUs;
    timeDelta_t  latestDeltaTimeUs;
    timeUs_t     maxExecutionTimeUs;
    timeUs_t     totalExecutionTimeUs;
    timeUs_t     averageExecutionTimeUs;
    timeUs_t     averageDeltaTimeUs;
    float        movingAverageCycleTimeUs;
} taskInfo_t;

typedef struct {
    timeUs_t     maxExecutionTimeUs;
    timeUs_t     totalExecutionTimeUs;
    timeUs_t     averageExecutionTimeUs;
    timeUs_t     averageDeltaTimeUs;
} cfCheckFuncInfo_t;

// this should be modified and in order with the main file
typedef enum {
    /* Actual tasks */
    TASK_SYSTEM = 0,
    TASK_MAIN,
    TASK_INFO,
    TASK_BLINK,
// TASK COUNT
    TASK_COUNT,
    /* Service task IDs */
    TASK_NONE = TASK_COUNT,
    TASK_SELF
} taskId_e;


extern task_t tasks[TASK_COUNT];

class Scheduler
{
    public:
        Scheduler();
        void run_scheduler(void);
        task_t* getTask(unsigned taskId);
        timeUs_t schedulerExecuteTask(task_t *selectedTask, timeUs_t currentTimeUs);
        void taskSystemLoad(timeUs_t currentTimeUs);
        void setTaskEnabled(taskId_e taskId, bool enabled);
        void rescheduleTask(taskId_e taskId, timeDelta_t newPeriodUs);
        void getTaskInfo(taskId_e taskId, taskInfo_t * taskInfo);
        void schedulerResetTaskMaxExecutionTime(taskId_e taskId);
        void printTasks(void);
        void queueClear(void);
        bool queueContains(task_t *task);
        bool queueAdd(task_t *task);
        bool queueRemove(task_t *task);
        void vprint(const char *fmt, va_list argp);
        void vprintln(const char *fmt, va_list argp);
        void Log(const char *fmt, ...); 
        void Logln(const char *fmt, ...); 
        void debug(bool _dbg=false);
        task_t* queueFirst(void);
        task_t* queueNext(void);
        int taskQueueSize = 0;
        task_t* taskQueueArray[TASK_COUNT + 1]; // extra item for NULL pointer at end of queue
        #ifdef USE_TASK_STATISTICS
        void getCheckFuncInfo(cfCheckFuncInfo_t *checkFuncInfo);
        void schedulerResetCheckFunctionMaxExecutionTime(void);
        #endif
    private:
        bool debug_flag=false;
};
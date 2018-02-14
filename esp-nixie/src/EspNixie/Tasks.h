#ifndef TASKS_h
#define TASKS_h

#define TASK_MAX_COUNT 10

enum MODES {
  MODE_NONE,
  MODE_CLOCK,
  MODE_TERMOMETER, 
  MODE_HYGROMETR, 
  MODE_BAROMETER,
  MODE_TERMOMETER_INSIDE,
  MODE_ALARM,
  MODE_OTA
};

struct task {
  MODES mode;
  uint16_t duration; //ms
};


#endif

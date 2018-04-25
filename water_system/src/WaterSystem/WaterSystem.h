#ifndef WaterSystem_h
#define WaterSystem_h

#define SERVO_PIN 13
#define PUMP_PIN 5

#define POT_COUNT 4

static int pot_angle[POT_COUNT] = {0, 60, 120, 180};

#define POT_FILL_TIME 12000
#define POT_FILL_PERIOD 60000


//TASKS
#define TASK_SERVO 1
#define TASK_PUMP 2
#define TASK_DELAY 3

#define TASK_QUEUE_MAX_LENGTH 50

struct Task{
  unsigned char id;
  int param;
};

struct TaskEx: Task{
  long start_time;
};

struct Task* get_servo_task(int pot){
  struct Task task_servo_pos[2] = {
    { TASK_SERVO, pot_angle[pot]},
    { TASK_DELAY, 1000}
  };
  return task_servo_pos;
}



#endif

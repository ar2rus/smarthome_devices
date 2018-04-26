#ifndef WaterSystem_h
#define WaterSystem_h

#define SERVO_PIN 13
#define PUMP_PIN 5

#define POT_COUNT 4

static int pot_angle[POT_COUNT] = {0, 60, 120, 180};

#define POT_FILL_TIME 12000
#define POT_FILL_PERIOD 60000

long fill_time[POT_COUNT] = {0};

//TASKS
#define TASK_SERVO 1
#define TASK_PUMP 2
#define TASK_DELAY 3

#define TASK_QUEUE_MAX_LENGTH 50

typedef struct {
  unsigned char id;
  int param;
} Task;

typedef struct : Task{
  long start_time;
} TaskExt;

boolean has_pot(int pot){
  return pot >=0 && pot<POT_COUNT;
}

boolean get_servo_task(Task* task, int pot){
  if (has_pot(pot)){
    task->id = TASK_SERVO;
    task->param = pot_angle[pot];
    return true;
  }
  return false;
}

int get_delay_task(Task* task, int _delay){
  task->id = TASK_DELAY;
  task->param = _delay;
  return 1;
}

int get_pump_task(Task* task, int state){
  task->id = TASK_PUMP;
  task->param = state;
  return 1;
}

int get_servo_test_task(Task* task, int count, int _delay){
  int i;
  for (i=0; i<count*2; i+=2){
    get_servo_task(&task[i+0], random(POT_COUNT));
    get_delay_task(&task[i+1], _delay);
  }
  return i;
}

int get_water_task(Task* task, int pot){
  int i=0;
  if (get_servo_task(&task[i++], pot)){
    if ((fill_time[i] == 0) || (millis() - fill_time[i] > POT_FILL_PERIOD)){
      get_delay_task(&task[i++], 1000);
      get_pump_task(&task[i++], 1);
      get_delay_task(&task[i++], POT_FILL_TIME);
      get_pump_task(&task[i++], 0);
      return i;
    }else{
      return -1;  //forbidden
    }
  }
  return 0;
}

int get_water_all_task(Task* task){
  int n = 0;
  for (int i=0; i<POT_COUNT; i++){
    int r = get_water_task(&task[n], i);
    if (r >= 0){
      n+=r;
      get_delay_task(&task[n++], 1000);
    }
  }
  return n;
}


#endif

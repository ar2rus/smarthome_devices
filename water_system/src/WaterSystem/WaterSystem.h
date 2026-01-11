#ifndef WaterSystem_h
#define WaterSystem_h

#define CLUNET_DEVICE_ID 0x83
#define CLUNET_DEVICE_NAME "WaterSystem"

const char index_html[] PROGMEM = "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'><title>Поливалка</title><style>input { margin: 14px;font-size: 20pt;padding: 10px;}td{text-align: center;}</style></head><body><iframe width='0' height='0' border='0' name='dummyframe' id='dummyframe'></iframe><table align='center'><tr><td><form action='/water' method='get' target='dummyframe'><input type='submit' value='Полить всех' id='frm_submit' /><input type='hidden' name='all'/> </form></td><td><form action='/stop' method='get' target='dummyframe'><input type='submit' value='СТОП' id='frm_stop' /></form></td></tr><tr><td><form action='/water' method='get' target='dummyframe'><input type='submit' value='Кирилл' id='frm0_submit' /><input type='hidden' name='pot' value='0'/> </form></td><td><form action='/water' method='get' target='dummyframe'><input type='submit' value='Наташа' id='frm1_submit' /><input type='hidden' name='pot' value='1'/> </form></td></tr><tr><td><form action='/water' method='get' target='dummyframe'><input type='submit' value='Абремуток' id='frm2_submit' /><input type='hidden' name='pot' value='2'/> </form></td><td><form action='/water' method='get' target='dummyframe'><input type='submit' value='Мелкий абремуток' id='frm3_submit' /><input type='hidden' name='pot' value='3'/> </form></td></tr></table></body></html>";

//PINS
#define SERVO_PIN 13
#define PUMP_PIN 5
#define SENSOR_PIN 4

//POTS
#define POT_COUNT 4
static const int pot_angle[POT_COUNT] = {0, 52, 115, 180};

//DELAYS
#define SERVO_POS_TIME 1000
#define POT_FILL_TIME 10000
#define POT_FILL_PERIOD 60000

//TASKS
#define TASK_SERVO 1
#define TASK_PUMP 2
#define TASK_DELAY 3
#define TASK_SERVO_DETACH 5
#define TASK_WATER_TIME_CHECK 10
#define TASK_WATER_TIME_SAVE 11

//QUEUE
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

boolean can_water_pot(long* fill_time, int pot){
  return has_pot(pot) && ((fill_time[pot] == 0) || ((millis() - fill_time[pot]) > POT_FILL_PERIOD));
}

int get_servo_task(Task* task, int pot){
  if (pot >= 0){
    if (has_pot(pot)){
      task->id = TASK_SERVO;
      task->param = pot_angle[pot];
      return 1;
    }
  }else {
      task->id = TASK_SERVO_DETACH;
      return 1;
  }
  return 0;
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
  int pot, ppot = -1;
  int i;
  for (i=0; i<count*2; i+=2){
    do{                           //skip duplicates
      pot = random(POT_COUNT);
    }while (pot == ppot);         // POT_COUNT > 1
    ppot = pot;
    
    get_servo_task(&task[i+0], pot);
    get_delay_task(&task[i+1], _delay);
  }
  return i;
}

int get_water_time_check(Task* task, int pot){
  task->id = TASK_WATER_TIME_CHECK;
  task->param = pot;
  return 1;
}

int get_water_time_save(Task* task, int pot){
  task->id = TASK_WATER_TIME_SAVE;
  task->param = pot;
  return 1;
}

int get_water_task(Task* task, long* fill_time, int pot){
  int i=0;
  if (get_servo_task(&task[i++], pot)){           //поворачиваем или указан неправильный горшок 

    if (can_water_pot(fill_time, pot)){           //проверяем можно ли уже лить в этот горшок
      get_delay_task(&task[i++], SERVO_POS_TIME);
      get_servo_task(&task[i++], -1);   //detach servo
      get_water_time_check(&task[i++], pot);      //на всякий случай проверем еще раз
      get_water_time_save(&task[i++], pot);       //сохраняем время запуска помпы
      get_pump_task(&task[i++], 1);
      get_delay_task(&task[i++], POT_FILL_TIME);
      get_pump_task(&task[i++], 0);
      get_water_time_save(&task[i++], pot);     //сохраняем время остановки помпы (если доработали до конца)

      return i;
    }else{
     
      get_servo_task(&task[i++], -1);   //detach servo
    
      return -1;                              //forbidden
    }
  }
  return 0;
}

int get_water_all_task(Task* task, long* fill_time){
  int n = 0;
  for (int i=0; i<POT_COUNT; i++){
    int r = get_water_task(&task[n], fill_time, i);
    if (r > 0){
      n += r;
      get_delay_task(&task[n++], 1000);
    }else{
      return -1;  //1 of all is forbidden
    }
  }
  return n;
}


#endif

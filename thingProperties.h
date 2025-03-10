#ifndef THING_PROPERTIES_H
#define THING_PROPERTIES_H

#include <ArduinoIoTCloud.h>

#include "secret.h"

// Arduino IoT Cloud Properties
extern bool feedNowControl;
extern bool waterNowControl;
extern float foodLevelCloud;
extern float waterLevelCloud;
extern int portionSizeCloud;
extern int waterAmountCloud;
extern String feedingScheduleCloud;
extern String lastFeedingTime;
extern bool deviceBusy;
extern String deviceStatus;

// Arduino IoT Cloud Schedule Properties
extern Schedule morningSchedule;
extern Schedule afternoonSchedule;
extern Schedule eveningSchedule;
extern Schedule nightSchedule;
extern Schedule customSchedule;

void initProperties();

#endif


#ifndef HMP60_H_
#define HMP60_H_

#include "ModbusMaster.h"
#include "ModbusRegister.h"

class HMP60 {
private:
    ModbusMaster humTempNode;
    ModbusRegister HUM;
    ModbusRegister TEMP;
    ModbusRegister humTempStatus;

public:
    HMP60(uint8_t nodeAddress = 241);
    int getTemperature();
    int getHumidity();
    int getStatus();
};

#endif  // HMP60_H_

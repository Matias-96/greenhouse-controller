/*
 * GMP252.h
 *
 *  Created on: 9.10.2023
 *      Author: Santeri
 */

#ifndef GMP252_H_
#define GMP252_H_

#include "ModbusMaster.h"
#include "ModbusRegister.h"

class GMP252 {
private:
    ModbusMaster co2Node;
    ModbusRegister CO2;
    ModbusRegister co2Status;

public:
    GMP252 (uint8_t nodeAddress = 240);
    int get_co2();
    int getStatus();
};

#endif /* GMP252_H_ */

/*
 * GMP252.cpp
 *
 *  Created on: 9.10.2023
 *      Author: Santeri
 */

#include "GMP252.h"

GMP252::GMP252(uint8_t nodeAddress)
    : co2Node(nodeAddress),
	  CO2(&co2Node, 256, true),
      co2Status(&co2Node, 2048, true)
{
	co2Node.begin(9600);
}

int GMP252::get_co2() {
	if(getStatus() == 0)
	{
		vTaskDelay(5);
		return CO2.read();
	}
    return -1;
}
int GMP252::getStatus() {

    return co2Status.read();
}

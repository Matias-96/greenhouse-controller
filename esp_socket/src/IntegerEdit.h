/*
 * IntegerEdit.h
 *
 *  Created on: 2.2.2016
 *      Author: krl
 */

#ifndef INTEGEREDIT_H_
#define INTEGEREDIT_H_

#include "PropertyEdit.h"
#include "LiquidCrystal.h"
#include "eeprom.h"
#include <string>

class IntegerEdit: public PropertyEdit {
public:
	IntegerEdit(LiquidCrystal *lcd_, std::string editTitle, int minValue,
			int maxValue, int step, std::string unit = "");
	virtual ~IntegerEdit();
	void increment();
	void decrement();
	void accept();
	void cancel();
	void setFocus(bool focus);
	bool getFocus();
	void display();
	int getValue();
	void setValue(int value);

private:
	void save();
	void displayEditValue();
	void saveToEEPROM();
	void loadFromEEPROM();
	LiquidCrystal *lcd;
	std::string title;
	std::string unit;
	int value;
	int edit;
	int minValue;
	int maxValue;
	int step;
	bool focus;
};

#endif /* INTEGEREDIT_H_ */

/*
 * IntegerEdit.cpp
 *
 *  Created on: 2.2.2016
 *      Author: krl
 */

#include "IntegerEdit.h"
#include <cstdio>

IntegerEdit::IntegerEdit(LiquidCrystal *lcd_, std::string editTitle,
		int minValue, int maxValue, int step, std::string unit) :
		lcd(lcd_), title(editTitle), unit(unit), minValue(minValue), maxValue(
				maxValue), step(step) {
	value = minValue;
	edit = minValue;
	focus = false;

	loadFromEEPROM();
}

IntegerEdit::~IntegerEdit() {
}

void IntegerEdit::increment() {
	if (edit < maxValue) {
		edit += step;
	}
}

void IntegerEdit::decrement() {
	if (edit > minValue) {
		edit -= step;
	}
}

void IntegerEdit::accept() {
	save();
}

void IntegerEdit::cancel() {
	edit = value;
}

void IntegerEdit::setFocus(bool focus) {
	this->focus = focus;
}

bool IntegerEdit::getFocus() {
	return this->focus;
}

void IntegerEdit::display() {
	lcd->clear();
	lcd->setCursor(0, 0);
	lcd->print(title);
	lcd->setCursor(0, 1);
	char s[17];
	if (focus) {
		snprintf(s, 17, "     [%4d %s]     ", edit, unit.c_str());
	} else {
		snprintf(s, 17, "      %4d %s      ", edit, unit.c_str());
	}
	lcd->print(s);
}

void IntegerEdit::save() {
	value = edit;
	saveToEEPROM();
}

int IntegerEdit::getValue() {
	return value;
}

void IntegerEdit::setValue(int value) {
	if (value > maxValue) {
		value = maxValue;
	} else if (value < minValue) {
		value = minValue;
	}
	edit = value;
	save();
}

void IntegerEdit::saveToEEPROM() {
	Chip_EEPROM_Write(0x00000100, reinterpret_cast<uint8_t*>(&value),
			sizeof(value));
}

void IntegerEdit::loadFromEEPROM() {
	uint8_t eepromData[sizeof(value)];
	if (Chip_EEPROM_Read(0x00000100, eepromData,
			sizeof(value)) == IAP_CMD_SUCCESS) {
		value = *reinterpret_cast<int*>(eepromData);
	} else {
		value = minValue;
	}

	edit = value;
}


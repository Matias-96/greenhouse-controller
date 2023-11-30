/*
 * StringEdit.cpp
 *
 *  Created on: 7.10.2023
 *      Author: Matias
 */

// StringEdit.cpp
#include "StringEdit.h"
#include <cstdio>

StringEdit::StringEdit(LiquidCrystal *lcd_, std::string editTitle, std::string defaultCursor) :
    lcd(lcd_), title(editTitle), defaultCursor(defaultCursor), value(""), currentCharacterIndex(-1), focus(false) {

    characters = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        'a', 'A', 'b', 'B', 'c', 'C', 'd', 'D', 'e', 'E', 'f', 'F',
        'g', 'G', 'h', 'H', 'i', 'I', 'j', 'J', 'k', 'K', 'l', 'L',
        'm', 'M', 'n', 'N', 'o', 'O', 'p', 'P', 'q', 'Q', 'r', 'R',
        's', 'S', 't', 'T', 'u', 'U', 'v', 'V', 'w', 'W', 'x', 'X',
        'y', 'Y', 'z', 'Z', '_', '.', '-', '/'
    };

    // Set the cursor position at the first available empty space on the second row
    lcd->clear();
    lcd->setCursor(0, 1);
    lcd->cursor();
}

StringEdit::~StringEdit() {}

void StringEdit::increment() {
    currentCharacterIndex = (currentCharacterIndex + 1) % characters.size();
    displayEditValue();
}

void StringEdit::decrement() {
    if (currentCharacterIndex == 0) {
        currentCharacterIndex = characters.size() - 1;
    } else {
        currentCharacterIndex--;
    }
    displayEditValue();
}

void StringEdit::accept() {
    if (value.length() < 16 && currentCharacterIndex >= 0) {
        value += characters[currentCharacterIndex];
        currentCharacterIndex = -1; // Reset the index after adding a character
    }
    display();
}

void StringEdit::cancel() {
    if (!value.empty()) {
        value.pop_back();
        display();
    }
}

void StringEdit::setFocus(bool focus) {
    this->focus = focus;
}

bool StringEdit::getFocus() {
    return this->focus;
}

void StringEdit::display() {
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print(title);
    lcd->setCursor(0, 1);
    char s[17];
    if (focus) {
        snprintf(s, 17, "     [%-16s]", value.c_str());
    } else {
        snprintf(s, 17, "      %-16s", value.c_str());
    }
    lcd->print(s);
    lcd->setCursor(value.length(), 1);
}

void StringEdit::displayEditValue() {
    lcd->setCursor(0, 1);
    lcd->print("                "); // Clear the line before displaying value
    lcd->setCursor(0, 1);
    for (int i = currentCharacterIndex - 7; i <= currentCharacterIndex + 8; ++i) {
        int index = i;
        if (index < 0) {
            index += characters.size();
        }
        index %= characters.size();
        lcd->write(characters[index]);
    }
    lcd->setCursor(value.length(), 1);
}

void StringEdit::save() {
    // Implement the save functionality if needed
}

std::string StringEdit::getValue() {
    return value;
}

void StringEdit::setValue(std::string value) {
    if (value.length() <= 16) {
        this->value = value;
    } else {
        this->value = value.substr(0, 16);
    }
    save();
}


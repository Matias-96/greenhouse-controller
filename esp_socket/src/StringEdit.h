/*
 * StringEdit.h
 *
 *  Created on: 7.10.2023
 *      Author: Matias
 */

#ifndef STRINGEDIT_H_
#define STRINGEDIT_H_

#include "PropertyEdit.h"
#include "LiquidCrystal.h"
#include <string>
#include <set>
#include <vector>

class StringEdit : public PropertyEdit {
public:
    StringEdit(LiquidCrystal *lcd_, std::string editTitle, std::string defaultCursor);
    virtual ~StringEdit();
    void increment();
    void decrement();
    void accept();
    void cancel();
    void setFocus(bool focus);
    bool getFocus();
    void display();
    std::string getValue();
    void setValue(std::string value);

private:
    LiquidCrystal *lcd;
    std::string title;
    std::string defaultCursor;
    std::string value;
    std::vector<char> characters; // Vector containing characters for editing
    int currentCharacterIndex; // Changed to int to allow negative values
    bool focus;

    void save();
    void displayEditValue();
};

#endif /* STRINGEDIT_H_ */


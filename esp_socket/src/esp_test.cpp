#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "SimpleMenu.h"
#include "heap_lock_monitor.h"
#include "retarget_uart.h"
#include "ModbusRegister.h"
#include "DigitalIoPin.h"
#include "LiquidCrystal.h"
#include "PropertyEdit.h"
#include "MenuItem.h"
#include "IntegerEdit.h"
#include "StringEdit.h"

// Stuff for menu
SemaphoreHandle_t inputSemaphore;
SemaphoreHandle_t uiMutex;

DigitalIoPin *upButton = nullptr;
DigitalIoPin *backButton = nullptr;
DigitalIoPin *okButton = nullptr;
DigitalIoPin *downButton = nullptr;

SimpleMenu *ui_ptr = nullptr;
// Stuff for menu

/* The following is required if runtime statistics are to be collected
 * Copy the code to the source file where other you initialize hardware */
extern "C" {
void vConfigureTimerForRunTimeStats(void) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}
}
/* end runtime statictics collection */

static void idle_delay() {
	vTaskDelay(1);
}

extern "C" {
void vStartSimpleMQTTDemo(void); // ugly - should be in a header
}

// void greenhouseMonitor(void *params) {}

static void userInput(void *params) {
	while (true) {

		bool pressedUp = upButton->read();
		bool pressedBack = backButton->read();
		bool pressedOk = okButton->read();
		bool pressedDown = downButton->read();

		if (pressedUp) {
			xSemaphoreTake(uiMutex, portMAX_DELAY); // Take mutex
			ui_ptr->event(MenuItem::up); // Use resource
			xSemaphoreGive(uiMutex); // Give mutex
		} else if (pressedBack) {
			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui_ptr->event(MenuItem::back);
			xSemaphoreGive(uiMutex);
		} else if (pressedOk) {
			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui_ptr->event(MenuItem::ok);
			xSemaphoreGive(uiMutex);
		} else if (pressedDown) {
			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui_ptr->event(MenuItem::down);
			xSemaphoreGive(uiMutex);
		}

		vTaskDelay(configTICK_RATE_HZ / 10);
	}
}

static void prvSetupHardware(void) {
	SystemCoreClockUpdate();
	Board_Init();
	Board_LED_Set(0, false);
}

int main(void) {
	prvSetupHardware();
	heap_monitor_setup();

	inputSemaphore = xSemaphoreCreateBinary();
	uiMutex = xSemaphoreCreateMutex();

	ModbusMaster node3(241); // Create modbus object that connects to slave id 241 (HMP60)
	node3.begin(9600); // all nodes must operate at the same speed!
	node3.idle(idle_delay); // idle function is called while waiting for reply from slave
	ModbusRegister RH(&node3, 256, true);

	DigitalIoPin relay(0, 27, DigitalIoPin::output); // CO2 relay
	relay.write(0);

	DigitalIoPin sw_a2(1, 8, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a3(0, 5, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a4(0, 6, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a5(0, 7, DigitalIoPin::pullup, true);

	upButton = &sw_a5;
	backButton = &sw_a4;
	okButton = &sw_a3;
	downButton = &sw_a2;

	DigitalIoPin *rs = new DigitalIoPin(0, 29, DigitalIoPin::output);
	DigitalIoPin *en = new DigitalIoPin(0, 9, DigitalIoPin::output);
	DigitalIoPin *d4 = new DigitalIoPin(0, 10, DigitalIoPin::output);
	DigitalIoPin *d5 = new DigitalIoPin(0, 16, DigitalIoPin::output);
	DigitalIoPin *d6 = new DigitalIoPin(1, 3, DigitalIoPin::output);
	DigitalIoPin *d7 = new DigitalIoPin(0, 0, DigitalIoPin::output);

	LiquidCrystal *lcd = new LiquidCrystal(rs, en, d4, d5, d6, d7);
	lcd->begin(16, 2);
	lcd->setCursor(0, 0);

	// Implementing user interface
	// And menu items to set Co2, configure MQTT (Broker IP, Client ID and Topic) and monitor current values

	SimpleMenu ui;
	ui_ptr = &ui;

	IntegerEdit co2Setpoint(lcd, "Set Co2", 200, 35000, 10, "ppm");
	StringEdit brokerEdit(lcd, "MQTT Broker IP", "_");

	ui_ptr->addItem(new MenuItem(&co2Setpoint));
	ui_ptr->addItem(new MenuItem(&brokerEdit));

	ui_ptr->event(MenuItem::show);

	/*
	 xTaskCreate(greenhouseMonitor, "monitor",
	 configMINIMAL_STACK_SIZE * 4, NULL, (tskIDLE_PRIORITY + 1UL),
	 (TaskHandle_t*) NULL);
	 */

	xTaskCreate(userInput, "input", configMINIMAL_STACK_SIZE * 3, NULL,
	tskIDLE_PRIORITY + 1, NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}

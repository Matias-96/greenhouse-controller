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
#include "queue.h"
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
#include "GMP252.h"
#include "HMP60.h"

SemaphoreHandle_t uiMutex;
TickType_t lastButtonPressTime = 0;

QueueHandle_t temperatureQueue;
QueueHandle_t humidityQueue;
QueueHandle_t co2Queue;

SimpleMenu *ui_ptr = nullptr;

DigitalIoPin *upInput = nullptr;
DigitalIoPin *backInput = nullptr;
DigitalIoPin *okInput = nullptr;
DigitalIoPin *downInput = nullptr;

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

extern "C" {
void vStartSimpleMQTTDemo(void);
}

static void prvSetupHardware(void) {
	SystemCoreClockUpdate();
	Board_Init();

	// Needed for eeprom
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_EEPROM);
	Chip_SYSCTL_PeriphReset(RESET_EEPROM);

	Board_LED_Set(0, false);
}

static void userInput(void *params) {
	while (true) {

		// user is considered active for 15 seconds
		bool pressedUp = upInput->read();
		bool pressedBack = backInput->read();
		bool pressedOk = okInput->read();
		bool pressedDown = downInput->read();

		if (pressedUp) {

			lastButtonPressTime = xTaskGetTickCount();

			xSemaphoreTake(uiMutex, portMAX_DELAY); // Take mutex
			ui_ptr->event(MenuItem::up); // Use resource
			xSemaphoreGive(uiMutex); // Give mutex

		} else if (pressedBack) {

			lastButtonPressTime = xTaskGetTickCount();

			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui_ptr->event(MenuItem::back);
			xSemaphoreGive(uiMutex);

		} else if (pressedOk) {

			lastButtonPressTime = xTaskGetTickCount();

			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui_ptr->event(MenuItem::ok);
			xSemaphoreGive(uiMutex);

		} else if (pressedDown) {

			lastButtonPressTime = xTaskGetTickCount();

			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui_ptr->event(MenuItem::down);
			xSemaphoreGive(uiMutex);
		}

		vTaskDelay(configTICK_RATE_HZ / 10);
	}
}

void systemMonitor(void *pvParameters) {
	LiquidCrystal *lcd = reinterpret_cast<LiquidCrystal*>(pvParameters);

	char co2Str[10];
	char tempStr[10];
	char rhStr[10];

	int co2;
	int temp;
	int rh;

	while (true) {
		// we want this to be an idle screen used to monitor data
		// display should revert here once you haven't touched buttons for 15 secs
		TickType_t currentTime = xTaskGetTickCount();
		TickType_t timeSinceLastPress = currentTime - lastButtonPressTime;

		co2 = xQueueReceive(co2Queue, &co2, 0);
		temp = xQueueReceive(temperatureQueue, &temp, 0);
		rh = xQueueReceive(humidityQueue, &rh, 0);

		if (timeSinceLastPress >= pdMS_TO_TICKS(15000)) {

			snprintf(co2Str, 10, "%d ppm ", co2);
			snprintf(tempStr, 10, "%d C ", temp);
			snprintf(rhStr, 10, "%d %% ", rh);

			// Enough time has passed, acquire mutex and display info
			// mutex should be free
			xSemaphoreTake(uiMutex, portMAX_DELAY);

			lcd->clear();

			lcd->setCursor(0, 0);
			lcd->print(rhStr);
			lcd->print(tempStr);
			lcd->print("OFFL"); // placeholder, indicate network success here

			lcd->setCursor(0, 1);
			lcd->print(co2Str);

			xSemaphoreGive(uiMutex);
		}

		vTaskDelay(configTICK_RATE_HZ / 2);
	}
}

/* // HARD FAULT - why?
 void systemControl(void *pvParameters) {
 GMP252 co2Probe;
 HMP60 humTempProbe;

 int co2Read;
 int tempRead;
 int rhRead;

 TickType_t lastRead = xTaskGetTickCount();

 while (true) {

 co2Read = co2Probe.get_co2();
 tempRead = humTempProbe.getTemperature();
 rhRead = humTempProbe.getHumidity();

 // Send the measurements to their respective queues
 xQueueSend(co2Queue, &co2Read, portMAX_DELAY);
 xQueueSend(temperatureQueue, &tempRead, portMAX_DELAY);
 xQueueSend(humidityQueue, &rhRead, portMAX_DELAY);

 lastRead = xTaskGetTickCount();

 // CONTROL LOGIC BASED ON DIRECT MEASUREMENTS

 vTaskDelay(configTICK_RATE_HZ * 5);
 }
 }
 */

int main(void) {
	prvSetupHardware();
	heap_monitor_setup();

	DigitalIoPin sw_a2(1, 8, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a3(0, 5, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a4(0, 6, DigitalIoPin::pullup, true);
	DigitalIoPin sw_a5(0, 7, DigitalIoPin::pullup, true);

	backInput = &sw_a5;
	upInput = &sw_a4;
	downInput = &sw_a3;
	okInput = &sw_a2;

	DigitalIoPin *rs = new DigitalIoPin(0, 29, DigitalIoPin::output);
	DigitalIoPin *en = new DigitalIoPin(0, 9, DigitalIoPin::output);
	DigitalIoPin *d4 = new DigitalIoPin(0, 10, DigitalIoPin::output);
	DigitalIoPin *d5 = new DigitalIoPin(0, 16, DigitalIoPin::output);
	DigitalIoPin *d6 = new DigitalIoPin(1, 3, DigitalIoPin::output);
	DigitalIoPin *d7 = new DigitalIoPin(0, 0, DigitalIoPin::output);
	LiquidCrystal *lcd = new LiquidCrystal(rs, en, d4, d5, d6, d7);

	lcd->begin(16, 2);
	lcd->setCursor(0, 0);

	SimpleMenu ui;
	IntegerEdit co2Setpoint(lcd, "Set Co2", 200, 35000, 10, "ppm");

	ui_ptr = &ui;
	ui_ptr->addItem(new MenuItem(&co2Setpoint));
	ui_ptr->event(MenuItem::show);

	DigitalIoPin relay(0, 27, DigitalIoPin::output); // CO2 relay
	relay.write(0);

	temperatureQueue = xQueueCreate(5, sizeof(int));
	humidityQueue = xQueueCreate(5, sizeof(int));
	co2Queue = xQueueCreate(5, sizeof(int));

	uiMutex = xSemaphoreCreateMutex();

	xTaskCreate(userInput, "input", configMINIMAL_STACK_SIZE * 3, NULL,
	tskIDLE_PRIORITY + 1, NULL);

	xTaskCreate(systemMonitor, "monitor", configMINIMAL_STACK_SIZE * 4, lcd,
	tskIDLE_PRIORITY + 1, NULL);

	/*
	 xTaskCreate(systemControl, "control", configMINIMAL_STACK_SIZE * 4, NULL,
	 tskIDLE_PRIORITY + 2, NULL);
	 */

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}

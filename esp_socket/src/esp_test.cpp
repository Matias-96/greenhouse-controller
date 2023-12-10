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
#include "ModbusRegister.h"
#include "DigitalIoPin.h"
#include "LiquidCrystal.h"
#include "PropertyEdit.h"
#include "MenuItem.h"
#include "IntegerEdit.h"
#include "LpcUart.h"

SemaphoreHandle_t uiMutex;
QueueHandle_t dataQueue;
QueueHandle_t inputQueue;
QueueHandle_t controlQueue;
LpcUart *dbgu;

struct Measurement {
	int co2;
	int temperature;
	int humidity;
};

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

static void prvSetupHardware(void) {
	SystemCoreClockUpdate();
	Board_Init();
	Board_LED_Set(0, false);

	// Needed for eeprom
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_EEPROM);
	Chip_SYSCTL_PeriphReset(RESET_EEPROM);
}

#ifdef __cplusplus
extern "C" {
#endif

void MRT_IRQHandler(void) {
	uint32_t int_pend;

	DigitalIoPin backInput(0, 7, DigitalIoPin::pullup, true);
	DigitalIoPin upInput(0, 6, DigitalIoPin::pullup, true);
	DigitalIoPin downInput(0, 5, DigitalIoPin::pullup, true);
	DigitalIoPin okInput(1, 8, DigitalIoPin::pullup, true);

	/* Get and clear interrupt pending status for all timers */
	int_pend = Chip_MRT_GetIntPending();
	Chip_MRT_ClearIntPending(int_pend);

	int buttonCode = 0;

	if (backInput.read()) {
		buttonCode = 3;
		xQueueSend(inputQueue, &buttonCode, portMAX_DELAY);
	} else if (upInput.read()) {
		buttonCode = 0;
		xQueueSend(inputQueue, &buttonCode, portMAX_DELAY);
	} else if (downInput.read()) {
		buttonCode = 1;
		xQueueSend(inputQueue, &buttonCode, portMAX_DELAY);
	} else if (okInput.read()) {
		buttonCode = 2;
		xQueueSend(inputQueue, &buttonCode, portMAX_DELAY);
	}
}

#ifdef __cplusplus
}
#endif

void init_MRT_interupt() {
	/* MRT Initialization and disable all timers */
	Chip_MRT_Init();
	int mrtch;
	for (mrtch = 0; mrtch < MRT_CHANNELS_NUM; mrtch++) {
		Chip_MRT_SetDisabled(Chip_MRT_GetRegPtr(mrtch));
	}

	/* Enable the interrupt for the MRT */
	NVIC_EnableIRQ(MRT_IRQn);

	/* Get pointer to timer selected by ch */
	LPC_MRT_CH_T *pMRT = Chip_MRT_GetRegPtr(0);

	/* Setup timer with rate based on MRT clock */
	// 100ms interval
	uint32_t value = 100 * (SystemCoreClock / 1000);
	Chip_MRT_SetInterval(pMRT, value | MRT_INTVAL_LOAD);

	/* Timer mode */
	Chip_MRT_SetMode(pMRT, MRT_MODE_REPEAT);

	/* Clear pending interrupt and enable timer */
	Chip_MRT_IntClear(pMRT);
	Chip_MRT_SetEnabled(pMRT);
}

void displayHandler(void *pvParameters) {
	LiquidCrystal *lcd = static_cast<LiquidCrystal*>(pvParameters);

	char co2Str[10];
	char tempStr[10];
	char rhStr[10];

	Measurement receivedMeasurement;

	while (true) {

		if (xQueueReceive(dataQueue, &receivedMeasurement,
		portMAX_DELAY) == pdTRUE) {

			snprintf(co2Str, 10, "%d ppm ", receivedMeasurement.co2);
			snprintf(tempStr, 10, "%d C ", receivedMeasurement.temperature);
			snprintf(rhStr, 10, "%d %% ", receivedMeasurement.humidity);

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
	}
}

void inputHandler(void *pvParameters) {
	LiquidCrystal *lcd = static_cast<LiquidCrystal*>(pvParameters);

	SimpleMenu ui;
	IntegerEdit co2Setpoint(lcd, "Set Co2", 200, 35000, 10, "ppm");
	ui.addItem(new MenuItem(&co2Setpoint));
	ui.event(MenuItem::show);

	// send initial setpoint to queue

	int received_event;
	int co2Setting = 0;

	while (true) {

		if (co2Setting != co2Setpoint.getValue()) {
			co2Setting = co2Setpoint.getValue();
			xQueueOverwrite(controlQueue, &co2Setting);
		}

		if (xQueueReceive(inputQueue, &received_event, portMAX_DELAY) == pdTRUE) {
			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui.event(static_cast<MenuItem::menuEvent>(received_event));
			xSemaphoreGive(uiMutex);

			if (co2Setpoint.getFocus() == true) {
				xSemaphoreTake(uiMutex, portMAX_DELAY);
				while (co2Setpoint.getFocus() == true) {
					if (xQueueReceive(inputQueue, &received_event, portMAX_DELAY) == pdTRUE) {
						ui.event(static_cast<MenuItem::menuEvent>(received_event));
					}
				}
			}

			xSemaphoreGive(uiMutex);
		}
	}
}

void periphHandler(void *pvParameters) {

	// HMP60 - properly encapsulate both later
	ModbusMaster node1(241);
	node1.begin(9600);
	node1.idle(idle_delay);
	ModbusRegister RH(&node1, 256, true);
	ModbusRegister TEMP(&node1, 257, true);

	// GMP252
	ModbusMaster node2(240);
	node2.begin(9600);
	node2.idle(idle_delay);
	ModbusRegister CO2(&node2, 256, true);

	DigitalIoPin relay(0, 27, DigitalIoPin::output); // CO2 relay
	relay.write(0);

	int co2 = 0;
	int temperature = 0;
	int humidity = 0;

	int co2Target = 0;
	int co2Difference = 0;
	int threshold = 5;

	char buffer[150];

	float valveCounter = 0;
	float valveOverTime = 0;

	while (true) {
		co2 = CO2.read();
		temperature = TEMP.read() / 10;
		humidity = RH.read() / 10;

		Measurement currentMeasurement = { co2, temperature, humidity };
		xQueueSend(dataQueue, &currentMeasurement, portMAX_DELAY);

		if (true) {
			xQueueReceive(controlQueue, &co2Target, 0);
			co2Difference = co2 - co2Target;
			valveOverTime = (valveCounter / xTaskGetTickCount()) * 100;

			snprintf(buffer, sizeof(buffer),
					"\r\nCO2 level (ppm): %d \r\n"
					"Relative Humidity: %d %% \r\n"
					"Temperature: %d C \r\n"
					"Valve opening percentage: %.2f %% \r\n"
					"CO2 set point (ppm): %d\r\n",
					co2, humidity, temperature, valveOverTime, co2Target);

			dbgu->write(buffer);

			// CONTROL LOGIC BASED ON DIRECT MEASUREMENTS
			if (co2Difference < -threshold) {
				relay.write(1);
				valveCounter += 1000;
				vTaskDelay(1000);
				relay.write(0);
				vTaskDelay(configTICK_RATE_HZ * 14);
			} else if (co2Difference > threshold) {
				vTaskDelay(configTICK_RATE_HZ * 15);
			} else {
				vTaskDelay(configTICK_RATE_HZ * 15);
			}
		}
	}
}

extern "C" {
void vStartSimpleMQTTDemo(void); // ugly - should be in a header
}

int main(void) {

	prvSetupHardware();
	heap_monitor_setup();
	init_MRT_interupt();

	LpcPinMap none = { .port = -1, .pin = -1 }; // unused pin has negative values in it
	LpcPinMap txpin = { .port = 0, .pin = 18 }; // transmit pin that goes to debugger's UART->USB converter
	LpcPinMap rxpin = { .port = 0, .pin = 13 }; // receive pin that goes to debugger's UART->USB converter
	LpcUartConfig cfg = { .pUART = LPC_USART0, .speed = 115200, .data =
	UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1, .rs485 =
			false, .tx = txpin, .rx = rxpin, .rts = none, .cts = none };

	dbgu = new LpcUart(cfg);
	dbgu->write("Boot\r\n");

	DigitalIoPin *rs = new DigitalIoPin(0, 29, DigitalIoPin::output);
	DigitalIoPin *en = new DigitalIoPin(0, 9, DigitalIoPin::output);
	DigitalIoPin *d4 = new DigitalIoPin(0, 10, DigitalIoPin::output);
	DigitalIoPin *d5 = new DigitalIoPin(0, 16, DigitalIoPin::output);
	DigitalIoPin *d6 = new DigitalIoPin(1, 3, DigitalIoPin::output);
	DigitalIoPin *d7 = new DigitalIoPin(0, 0, DigitalIoPin::output);

	LiquidCrystal *lcd = new LiquidCrystal(rs, en, d4, d5, d6, d7);
	lcd->begin(16, 2);
	lcd->setCursor(0, 0);

	dataQueue = xQueueCreate(5, sizeof(Measurement));
	inputQueue = xQueueCreate(20, sizeof(int));
	controlQueue = xQueueCreate(5, sizeof(int));
	uiMutex = xSemaphoreCreateMutex();

	xTaskCreate(displayHandler, "display", configMINIMAL_STACK_SIZE * 3, lcd,
	tskIDLE_PRIORITY + 1, NULL);

	xTaskCreate(inputHandler, "input", configMINIMAL_STACK_SIZE * 3, lcd,
	tskIDLE_PRIORITY + 1, NULL);

	xTaskCreate(periphHandler, "peripherals", configMINIMAL_STACK_SIZE * 5,
	NULL,
	tskIDLE_PRIORITY + 1, NULL);

	vStartSimpleMQTTDemo();
	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}

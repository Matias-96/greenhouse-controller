#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif
#include <cr_section_macros.h>
#include "LpcUart.h"

// FreeRTOS API related
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "heap_lock_monitor.h"

// Setup MODBUS Sensors
#include "DigitalIoPin.h"
#include "ModbusRegister.h"
#include "GMP252.h"
#include "HMP60.h"

// Required by the user interface
#include "LiquidCrystal.h"
#include "PropertyEdit.h"
#include "MenuItem.h"
#include "SimpleMenu.h"
#include "IntegerEdit.h"

// created and explained in main()
SemaphoreHandle_t uiMutex;
QueueHandle_t dataQueue;
QueueHandle_t inputQueue;
QueueHandle_t controlQueue;

LpcUart *dbgu; // pointer to the debug uart

// sensor measurements are integer variables grouped into a structure
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

static void prvSetupHardware(void) {
	// Read clock settings and update SystemCoreClock variable
	SystemCoreClockUpdate();

	// Set up and initialize all required blocks and
	// functions related to the board hardware
	Board_Init();
	Board_LED_Set(0, false);

	// Initialize EEPROM
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_EEPROM);
	Chip_SYSCTL_PeriphReset(RESET_EEPROM);
}

#ifdef __cplusplus
extern "C" {
#endif

void MRT_IRQHandler(void) {
	uint32_t int_pend;

	DigitalIoPin backInput(0, 7, DigitalIoPin::pullup, true); // SW_A5
	DigitalIoPin upInput(0, 6, DigitalIoPin::pullup, true); // SW_A4
	DigitalIoPin downInput(0, 5, DigitalIoPin::pullup, true); // SW_A3
	DigitalIoPin okInput(1, 8, DigitalIoPin::pullup, true); // SW_A2

	/* Get and clear interrupt pending status for all timers */
	int_pend = Chip_MRT_GetIntPending();
	Chip_MRT_ClearIntPending(int_pend);

	int buttonCode = 0;

	// Interrupt handler will poll the buttons every 100 milliseconds
	// An input has a corresponding code that is then sent to queue
	// Receiving an integer from the queue unblocks a task
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

		// Wait for a measurement to be taken
		if (xQueueReceive(dataQueue, &receivedMeasurement,
		portMAX_DELAY) == pdTRUE) {

			// Format the strings shown on the display with proper units
			snprintf(co2Str, 10, "%d ppm ", receivedMeasurement.co2);
			snprintf(tempStr, 10, "%d C ", receivedMeasurement.temperature);
			snprintf(rhStr, 10, "%d %% ", receivedMeasurement.humidity);

			// Getting to use the LCD display requires this mutex to be free
			// Simply update the data seen on the display
			xSemaphoreTake(uiMutex, portMAX_DELAY);

			lcd->clear();
			lcd->setCursor(0, 0);
			lcd->print(rhStr);
			lcd->print(tempStr);
			lcd->setCursor(0, 1);
			lcd->print(co2Str);

			xSemaphoreGive(uiMutex);

		}
	}
}

void inputHandler(void *pvParameters) {
	LiquidCrystal *lcd = static_cast<LiquidCrystal*>(pvParameters);

	// IntegerEdit constructor takes eeprom address in the constructor (0x00000100)
	// The value is saved to and loaded from this address
	// If no value exists there the defined minimum is used (200 here)
	// The unit should also be configured as the last parameter
	SimpleMenu ui;
	IntegerEdit co2Setpoint(lcd, "Set Co2", 200, 35000, 10, 0x00000100, "ppm");
	ui.addItem(new MenuItem(&co2Setpoint));

	int received_event;
	int co2Setting = 0;

	while (true) {

		// The co2 set point must be usable by the task controlling the valve
		// If there's a change, send new setting to the queue
		if (co2Setting != co2Setpoint.getValue()) {
			co2Setting = co2Setpoint.getValue();
			xQueueOverwrite(controlQueue, &co2Setting);
		}

		// This event comes from the MRT_IRQHandler and through the inputQueue
		// The mutex is taken so that displayHandler doesn't get to the screen at the same time
		if (xQueueReceive(inputQueue, &received_event, portMAX_DELAY) == pdTRUE) {
			xSemaphoreTake(uiMutex, portMAX_DELAY);
			ui.event(static_cast<MenuItem::menuEvent>(received_event));
			xSemaphoreGive(uiMutex);

			/* If we know that the user is actively using the interface
			 * we retake and keep the mutex for the duration of the operation.
			 * This should only happen when the IntegerEdit is in "focus". */
			if (co2Setpoint.getFocus() == true) {
				xSemaphoreTake(uiMutex, portMAX_DELAY);
				while (co2Setpoint.getFocus() == true) {
					// Listen for additional input events
					if (xQueueReceive(inputQueue, &received_event,
					portMAX_DELAY) == pdTRUE) {
						ui.event(
								static_cast<MenuItem::menuEvent>(received_event));
					}
				}
			}

			xSemaphoreGive(uiMutex); // focus has been cancelled
		}
	}
}

void periphHandler(void *pvParameters) {

	// Modbus sensors are encapsulated and use proper delays for real hardware
	GMP252 co2Sensor;
	vTaskDelay(1);
	HMP60 humTempSensor;
	vTaskDelay(1);

	// The relay is used for binary control of the valve
	// Opened for 1 second at a time due to the small size of the test system
	DigitalIoPin relay(0, 27, DigitalIoPin::output); // CO2 relay
	relay.write(0);

	// measurement values
	int co2 = 0;
	int temperature = 0;
	int humidity = 0;

	// comprise the simple maths of the algorithm
	int co2Target = 0;
	int co2Difference = 0;
	int threshold = -30; // co2 should be more than 30 ppm under the target to warrant release

	char buffer[150]; // one large buffer to include debug print data

	float valveCounter = 0; // incremented by 1000 ticks/ms each time the valve is opened
	float valveOverTime = 0; // the percentage valveCounter is of the task total tick counts

	while (true) {
		// Read the sensors and group the data by using the Measurement structure
		// Measurement is used by the displayHandler task
		co2 = co2Sensor.get_co2();
		temperature = humTempSensor.getTemperature();
		humidity = humTempSensor.getHumidity();

		Measurement currentMeasurement = { co2, temperature, humidity };
		xQueueSend(dataQueue, &currentMeasurement, portMAX_DELAY);

		if (true) {

			/* Receive the most recent setpoint from the controlQueue
			 * and calculate the difference between it and the measured carbon dioxide */
			xQueueReceive(controlQueue, &co2Target, 0);
			co2Difference = co2 - co2Target;

			// Open the valve for 1 second if we are "undershooting" by at least 30 ppm
			// Print all relevant and updated data through debug uart
			if (co2Difference < threshold) {
				relay.write(1);
				valveCounter += 1000; // i.e. valve has been open for 1000 ticks/total
				vTaskDelay(1000);
				relay.write(0);

				valveOverTime = (valveCounter / xTaskGetTickCount()) * 100;

				snprintf(buffer, sizeof(buffer), "\r\nCO2 level (ppm): %d \r\n"
						"Relative Humidity: %d %% \r\n"
						"Temperature: %d C \r\n"
						"Valve opening percentage: %.2f %% \r\n"
						"CO2 set point (ppm): %d\r\n", co2, humidity,
						temperature, valveOverTime, co2Target);

				dbgu->write(buffer);

				vTaskDelay(configTICK_RATE_HZ * 29);
			} else {
				valveOverTime = (valveCounter / xTaskGetTickCount()) * 100;

				snprintf(buffer, sizeof(buffer), "\r\nCO2 level (ppm): %d \r\n"
						"Relative Humidity: %d %% \r\n"
						"Temperature: %d C \r\n"
						"Valve opening percentage: %.2f %% \r\n"
						"CO2 set point (ppm): %d\r\n", co2, humidity,
						temperature, valveOverTime, co2Target);

				dbgu->write(buffer);
				vTaskDelay(configTICK_RATE_HZ * 30);
			}

			// Wait 30 seconds to go again
		}
	}
}

int main(void) {

	prvSetupHardware();
	heap_monitor_setup();
	init_MRT_interupt();

	// Set up debug uart
	LpcPinMap none = { .port = -1, .pin = -1 }; // unused pin has negative values in it
	LpcPinMap txpin = { .port = 0, .pin = 18 }; // transmit pin that goes to debugger's UART->USB converter
	LpcPinMap rxpin = { .port = 0, .pin = 13 }; // receive pin that goes to debugger's UART->USB converter
	LpcUartConfig cfg = { .pUART = LPC_USART0, .speed = 115200, .data =
	UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1, .rs485 =
			false, .tx = txpin, .rx = rxpin, .rts = none, .cts = none };
	dbgu = new LpcUart(cfg);
	dbgu->write("Debug UART ok\r\n");

	// LCD initialized here; two tasks need to use it
	DigitalIoPin *rs = new DigitalIoPin(0, 29, DigitalIoPin::output);
	DigitalIoPin *en = new DigitalIoPin(0, 9, DigitalIoPin::output);
	DigitalIoPin *d4 = new DigitalIoPin(0, 10, DigitalIoPin::output);
	DigitalIoPin *d5 = new DigitalIoPin(0, 16, DigitalIoPin::output);
	DigitalIoPin *d6 = new DigitalIoPin(1, 3, DigitalIoPin::output);
	DigitalIoPin *d7 = new DigitalIoPin(0, 0, DigitalIoPin::output);
	LiquidCrystal *lcd = new LiquidCrystal(rs, en, d4, d5, d6, d7);
	lcd->begin(16, 2);
	lcd->setCursor(0, 0);

	dataQueue = xQueueCreate(5, sizeof(Measurement)); // sensor data in structs
	inputQueue = xQueueCreate(20, sizeof(int)); // send menu events
	controlQueue = xQueueCreate(5, sizeof(int)); // co2 setpoint
	uiMutex = xSemaphoreCreateMutex(); // controls print access to the lcd

	// Display sensor readings on the lcd
	xTaskCreate(displayHandler, "display", configMINIMAL_STACK_SIZE * 3, lcd,
	tskIDLE_PRIORITY + 1, NULL);

	// Implementation of the user interface, controls
	xTaskCreate(inputHandler, "input", configMINIMAL_STACK_SIZE * 3, lcd,
	tskIDLE_PRIORITY + 1, NULL);

	// Read the sensors and control the valve
	xTaskCreate(periphHandler, "peripherals", configMINIMAL_STACK_SIZE * 5,
	NULL,
	tskIDLE_PRIORITY + 1, NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}

/*
===============================================================================
 Name        : main.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif
#include "FreeRTOS.h"
#include "task.h"
#include <cr_section_macros.h>
#include <stdlib.h>
#include "DigitalIoPin.h"

void SCT_Init(void)
{
	LPC_SYSCTL->SYSAHBCLKCTRL[1] |= (1 << 2);

	LPC_SCT0->CONFIG |= (1 << 17);							// two 16-bit timers, auto limit
	LPC_SCT0->CTRL_L |= SCT_CTRL_PRE_L(72);	// set prescaler, SCTimer/PWM clock = 1 MHz

	LPC_SCT0->MATCHREL[0].L = 20000-1;						// match 0 @ 20000/1MHz = 20 msec (50 Hz PWM freq)
	LPC_SCT0->MATCHREL[1].L = 0;							// match 1 used for duty cycle (in 10 steps)

	LPC_SCT0->EVENT[0].STATE = 0xFFFFFFFF; 					// event 0 happens in all states
	LPC_SCT0->EVENT[0].CTRL  = (1 << 12);					// match 0 condition only

	LPC_SCT0->EVENT[1].STATE = 0xFFFFFFFF;					// event 1 happens in all states
	LPC_SCT0->EVENT[1].CTRL  = (1 << 0) | (1 << 12);		// match 1 condition only

	LPC_SCT0->OUT[0].SET	= (1 << 0);						// event 0 will set SCTx_OUT0
	LPC_SCT0->OUT[0].CLR    = (1 << 1);						// event 1 will clear SCTx_OUT0

	LPC_SCT0->CTRL_L	&= ~(1 << 2);						// unhalt it by clearing bit 2 of CTRL reg
}

/* Sets up system hardware */
static void prvSetupHardware(void)
{
#if defined (__USE_LPCOPEN)
    // Read clock settings and update SystemCoreClock variable
    SystemCoreClockUpdate();
#if !defined(NO_BOARD_LIB)
    // Set up and initialize all required blocks and
    // functions related to the board hardware
    Board_Init();
    // Set the LED to the state of "On"
    Board_LED_Set(0, false);
    Board_LED_Set(1, false);
    Board_LED_Set(2, false);
#endif
#endif

}

void servoTask(void *pvParameters)
{
	char buffer[10];
	uint32_t dutyCycle;
	DigitalIoPin button1 = DigitalIoPin( 0, 17, DigitalIoPin::pullup, false);
	DigitalIoPin button2 = DigitalIoPin( 1, 11, DigitalIoPin::input, false);
	DigitalIoPin button3 = DigitalIoPin( 1, 9, DigitalIoPin::pullup, false);
	int step = LPC_SCT0->MATCHREL[1].L;

	while (1)
	{
//		if (button1.read() && button2.read() && button3.read()) // no button pressed
//			LPC_SCT0->MATCHREL[1].L = 0;
		if (!button1.read())
		{
			if(LPC_SCT0->MATCHREL[1].L <= 2000)
				LPC_SCT0->MATCHREL[1].L++;
		}
		else if (!button2.read())
			LPC_SCT0->MATCHREL[1].L = 1500;
		else if (!button3.read())
		{
			if (LPC_SCT0->MATCHREL[1].L >= 1000)
			LPC_SCT0->MATCHREL[1].L--;
		}
		vTaskDelay(10);
	}
}

int main(void) {

	prvSetupHardware();

	SCT_Init();

	Chip_SWM_MovablePortPinAssign(SWM_SCT0_OUT0_O, 0, 8);

	/* Queue control */
	xTaskCreate(servoTask, "servoTask", configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

    volatile static int i = 0 ;
    // Force the counter to be placed into memory
    // Enter an infinite loop, just incrementing a counter
    while(1) {
        i++ ;
    }
    return 0 ;
}

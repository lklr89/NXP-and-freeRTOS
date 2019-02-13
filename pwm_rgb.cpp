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
#include "app_usbd_cfg.h"
#include "board.h"
#endif
#endif
#include "FreeRTOS.h"
#include "task.h"
#include <cr_section_macros.h>

#include <string.h>
#include <stdlib.h>
#include "DigitalIoPin.h"
#include "user_vcom.h"
#include "ITM_write.h"

void SCT_Init(void)
{

	/* Enable SCTimers 0 and 1 */
	LPC_SYSCTL->SYSAHBCLKCTRL[1] |= (1 << 2) | (1 << 3);

	/* Configure SCTimer 0 */
	LPC_SCT0->CONFIG |= SCT_CONFIG_16BIT_COUNTER | 			// two 16-bit counters,
						SCT_CONFIG_AUTOLIMIT_L	 |
						SCT_CONFIG_AUTOLIMIT_H;

	LPC_SCT0->CTRL_U |= SCT_CTRL_PRE_L(72);					// set prescaler, SCTimer/PWM clock = 1 MHz
	LPC_SCT0->CTRL_U |= SCT_CTRL_PRE_H(72);

	LPC_SCT0->CTRL_U |= SCT_CTRL_CLRCTR_L;
	LPC_SCT0->CTRL_U |= SCT_CTRL_CLRCTR_H;

	LPC_SCT0->MATCHREL[0].L = 255;							// match 0 @ 255/1MHz
	LPC_SCT0->MATCHREL[1].L = 0;							// match 1 used for duty cycle
	LPC_SCT0->MATCHREL[0].H = 255;							// match 0 @ 255/1MHz =
	LPC_SCT0->MATCHREL[1].H = 0;							// match 1 used for duty cycle

	LPC_SCT0->EVENT[0].STATE = 0xFFFFFFFF; 					// event 0 happens in all states
	LPC_SCT0->EVENT[0].CTRL  = (1 << 12) |					// Uses the specified match only.
							   (0 << 4);					// Selects the L state and the L match register selected by MATCHSEL.

	LPC_SCT0->EVENT[1].STATE = 0xFFFFFFFF;					// event 1 happens in all states
	LPC_SCT0->EVENT[1].CTRL  = (1 << 12) |					// Uses the specified match only.
							   (1 << 0)  |					// match 0 L condition only
							   (0 << 4);					// Selects the L state and the L match register selected by MATCHSEL.

	LPC_SCT0->EVENT[2].STATE = 0xFFFFFFFF; 					// event 2 happens in all states
	LPC_SCT0->EVENT[2].CTRL  = (1 << 12) |					// Uses the specified match only.
							   (1 << 4);					// Selects the H state and the H match register selected by MATCHSEL.

	LPC_SCT0->EVENT[3].STATE = 0xFFFFFFFF;					// event 3 happens in all states
	LPC_SCT0->EVENT[3].CTRL  = (1 << 12) |					// Uses the specified match only.
							   (1 << 0)  |					// match 0 H rgbcondition
							   (1 << 4);					// Selects the H state and the H match register selected by MATCHSEL.

	LPC_SCT0->OUT[0].SET	= (1 << 0);						// event 0 will set SCTx_OUT0
	LPC_SCT0->OUT[0].CLR    = (1 << 1);						// event 1 will clear SCTx_OUT0

	LPC_SCT0->OUT[1].SET	= (1 << 2);						// event 2 will set SCTx_OUT1
	LPC_SCT0->OUT[1].CLR    = (1 << 3);						// event 3 will clear SCTx_OUT1

	LPC_SCT0->CTRL_L	&= ~(1 << 2);						// unhalt it by clearing bit 2 of CTRL reg
	LPC_SCT0->CTRL_H	&= ~(1 << 2);						// unhalt it by clearing bit 2 of CTRL reg



	/* Configure SCTimer 1 */
	LPC_SCT1->CONFIG |= (1 << 17);							// two 16-bit timers, auto limit
	LPC_SCT1->CTRL_L |= SCT_CTRL_PRE_L(72);					// set prescaler, SCTimer/PWM clock = 1 MHz

	LPC_SCT1->MATCHREL[0].L = 255;							// match 0 @ 1000/1MHz = 1 msec (1 kHz PWM freq)
	LPC_SCT1->MATCHREL[1].L = 0;							// match 1 used for duty cycle (in 10 steps)

	LPC_SCT1->EVENT[0].STATE = 0xFFFFFFFF; 					// event 0 happens in all states
	LPC_SCT1->EVENT[0].CTRL  = (1 << 12);					// Uses the specified match only.

	LPC_SCT1->EVENT[1].STATE = 0xFFFFFFFF;					// event 1 happens in all states
	LPC_SCT1->EVENT[1].CTRL  = (1 << 0) | (1 << 12);		// match 1 condition

	LPC_SCT1->OUT[0].SET	= (1 << 0);						// event 0 will set SCTx_OUT0
	LPC_SCT1->OUT[0].CLR    = (1 << 1);						// event 1 will clear SCTx_OUT0

	LPC_SCT1->CTRL_L	&= ~(1 << 2);						// unhalt it by clearing bit 2 of CTRL reg

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
    // Set the LED to the state of "Off"
    Board_LED_Set(0, false);
    Board_LED_Set(1, false);
    Board_LED_Set(2, false);
#endif
#endif

}

/* Receive and compute commands from USB */
static void usbReceiveTask(void *pvParameters)
{

	vTaskDelay(100);
	char *wordPtr;
	unsigned long rVal, gVal, bVal, hexNum;

	while (1)
	{
		char str[RCV_BUFSIZE], buf[RCV_BUFSIZE];
		uint32_t len;

		len = USB_receive((uint8_t *)str, RCV_BUFSIZE);
		str[len] = 0; /* make sure we have a zero at the end so that we can print the data */

		wordPtr = strtok (str,"#");
		if ( strncmp(str,"rgb",3) == 0 )
		{
			wordPtr = strtok (NULL,"#");
			hexNum = strtoul(wordPtr, NULL, 16);
			rVal = 255 - ((hexNum >> 16) & 0xff);
			gVal = 255 - ((hexNum >> 8)  & 0xff);
			bVal = 255 - ((hexNum >> 0)  & 0xff);
			len = sprintf(buf,"Change RGB to %d %d %d \n", 255-rVal, 255-gVal, 255-bVal);
			LPC_SCT0->MATCHREL[1].L = rVal;
			LPC_SCT0->MATCHREL[1].H = gVal;
			LPC_SCT1->MATCHREL[1].L = bVal;
			USB_send((uint8_t *)buf,len);
		}
		else
			USB_send((uint8_t *)"\rUnknown command..\n ",21);

		vTaskDelay(10);
	}
}

int main(void) {

	prvSetupHardware();

	SCT_Init();

	ITM_init();

	Chip_SWM_MovablePortPinAssign(SWM_SCT0_OUT0_O, 0, 25); // red
	Chip_SWM_MovablePortPinAssign(SWM_SCT0_OUT1_O, 0, 3);  // green
	Chip_SWM_MovablePortPinAssign(SWM_SCT1_OUT0_O, 1, 1);  // blue

	/* RX */
	xTaskCreate(usbReceiveTask, "Rx", configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* CDC */
	xTaskCreate(cdc_task, "CDC", configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);


	/* Start the scheduler */
	vTaskStartScheduler();

    // Force the counter to be placed into memory
    volatile static int i = 0 ;
    // Enter an infinite loop, just incrementing a counter
    while(1) {
        i++ ;
    }
    return 0 ;
}

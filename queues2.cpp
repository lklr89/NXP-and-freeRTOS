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

#include <cr_section_macros.h>
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "DigitalIoPin.h"

// TODO: insert other include files here

// TODO: insert other definitions and declarations here

/* Declare Semaphore */
SemaphoreHandle_t xSemaphore;

/* Declare Queue */
QueueHandle_t xQueue1;

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
    Board_LED_Set(0, true);
#endif
#endif
}

/* Mutex Controled Debug Output */
void mDebugOut(char *str, void *varArg)
{
	if(xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
	{
		DEBUGOUT(str, (int)varArg);
	}
	xSemaphoreGive(xSemaphore);
}

/* Mutex Controled Debug Intput */
int mDebugIn(void)
{
	volatile int j = 0;
	if(xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
	{
		j = Board_UARTGetChar();
	}
	xSemaphoreGive(xSemaphore);
	return j;
}

void randomNumbersToQueue(void *pvParameters)
{
	int i = 0, j = 0;

	while (1)
	{
		i = (rand() % 500) + 100;
		j = (rand() % 20);
#if defined (DEBUG)
			mDebugOut("Sending %d to queue", (void*)j);
			mDebugOut("  with %d delay.\n", (void*)i);
#endif
		xQueueSend(xQueue1, &j, 500);
		vTaskDelay(i);
	}
}

/* Button Monitor thread */
static void vButtonMonitor(void *pvParameters)
{

	DigitalIoPin button1 = DigitalIoPin( 0, 17, DigitalIoPin::pullup, false);
	int j = 112;

	while (1)
	{
		if (button1.read() == false)
		{
			/* Send -1 to queue. */
#if defined (DEBUG)
			mDebugOut("Sending -1 to queue.\n", NULL);
#endif
			xQueueSendToFront(xQueue1, &j, 500);

			/* About a 1s delay here */
			vTaskDelay(100);
		}

	}
}

/* Queue Control thread */
static void vQueueControl(void *pvParameters)
{

	int k = 0, sum = 0;
	while (1)
	{
		if (xQueue1 != 0)
		{
			if (xQueueReceive(xQueue1, &k, (TickType_t)100))
			{
#if defined (DEBUG)
				mDebugOut("Receiving Queue element : ", NULL);
#endif
				mDebugOut("%d \n", (void *)k);
				if (k  == 112)
				{
					mDebugOut("Help me.\n", (void *)sum);
					sum = 0; k = 0;
					vTaskDelay(300);
				}
				else
					sum += k;
			}
		}
		vTaskDelay(50);
	}
}

int main(void) {

	prvSetupHardware();

	/* Define Semaphores */
	xSemaphore = xSemaphoreCreateMutex();

	if (xSemaphore == NULL)
	{
		DEBUGOUT("Error! There was insufficient FreeRTOS heap available for the semaphore to be created successfully.\n");
		return 1;
	}

	/* Create a queue capable of containing 5 integer values. */
	xQueue1 = xQueueCreate( 19, sizeof( int ) );

    /* We want this queue to be viewable in a RTOS kernel aware debugger, so register it. */
    vQueueAddToRegistry( xQueue1, "CharacterCountQueue" );

	/* Read Serial Port thread */
	xTaskCreate(randomNumbersToQueue, "randomNumbersToQueue", configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* Button thread */
	xTaskCreate(vButtonMonitor, "vButtonMonitor", configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* Queue control */
	xTaskCreate(vQueueControl, "vQueueControl", configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

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

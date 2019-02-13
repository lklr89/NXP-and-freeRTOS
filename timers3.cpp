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

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"
#include <stdlib.h>

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

 /* An array to hold handles to the created timers. */
 TimerHandle_t xTimers[ 2 ];

 /* Create Semaphore */
 SemaphoreHandle_t xSemaphoreOUT;
 SemaphoreHandle_t xSemaphoreIN;
 SemaphoreHandle_t xSemaphoreReset;

 /*****************************************************************************
 * Private functions
 ****************************************************************************/

/* Sets up system hardware */
static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LED0 state is off */
	Board_LED_Set(0, false);
	Board_LED_Set(1, false);
	Board_LED_Set(2, false);
}

/* Mutex Controled Debug Output */
void mDebugOut(char *str, void *varArg)
{
	if(xSemaphoreTake(xSemaphoreOUT, portMAX_DELAY) == pdTRUE)
	{
		DEBUGOUT(str, (int)varArg);
	}
	xSemaphoreGive(xSemaphoreOUT);
}

/* Mutex Controled Debug Input */
int mDebugIn(void)
{
	int j = 0;
	if(xSemaphoreTake(xSemaphoreIN, portMAX_DELAY) == pdTRUE)
	{
		j = Board_UARTGetChar();
	}
	xSemaphoreGive(xSemaphoreIN);
	return j;
}

void vTimerCallback( TimerHandle_t xTimer )
{

	uint32_t ulCount;

	/* The number of times this timer has expired is saved as the
   timer's ID.  Obtain the count. */
   ulCount = ( uint32_t ) pvTimerGetTimerID( xTimer );

   if (ulCount == 0)
   {
	   /* enter inactive state */
	   mDebugOut("\r[inactive]\n ", NULL);
	   xSemaphoreGive(xSemaphoreReset);
   }
   else if (ulCount == 1)
   {
	   /* toggle green LED */
	   Board_LED_Toggle(1);
   }

}


void setupTimers(void)
{
	xTimers[0] = xTimerCreate
	                   ( "Watchdog",				// Just a text name, not used by the RTOS kernel.
	                	 pdMS_TO_TICKS( 10000 ),	// The timer period in ticks, must be greater than 0.
	                     pdFALSE, 					// The timers will auto-reload themselves when they expire.
	                     ( void * ) 0, 				// The ID is used to store a count of the number of
	                                        		// times the timer has expired, which is initialised to 0.
	                     vTimerCallback				// Each timer calls the same callback when it expires.
	                   );
	xTimers[1] = xTimerCreate
	                   ( "LedTimer",				// Just a text name, not used by the RTOS kernel.
	                	 pdMS_TO_TICKS( 5000 ),		// The timer period in ticks, must be greater than 0.
	                     pdTRUE, 					// The timers will auto-reload themselves when they expire.
	                     ( void * ) 1, 				// The ID is used to store a count of the number of
	                                        		// times the timer has expired, which is initialised to 0.
	                     vTimerCallback				// Each timer calls the same callback when it expires.
	                   );

	 /* Start the timer.  No block time is specified, and even if one was it would be ignored because the RTOS
	 scheduler has not yet been started. */
	 xTimerStart( xTimers[0], 0 );
	 xTimerStart( xTimers[1], 0 );

}

static void vGetCommandTask(void* pvParameters)
{
	int i = 0;
	char buf[63] = {0};
	char *wordPtr;

	TickType_t interval = 5000, xRemainingTime = 0;
	while (1)
	{
		if (xSemaphoreTake(xSemaphoreReset, (TickType_t) 10) == pdTRUE)
		{
			memset(buf,0,64);
			i = 0;
		}
		buf[i] = mDebugIn();
		if ( buf[i] == 255 )  // 255 and not EOF, because it is saved to a char
			vTaskDelay( 10 ); // !! terminal : force off "local line-editing" !!
		else
		{
			if ( buf[i] == '\n' || buf[i] == '\r'  || buf[i] == '\t' )
			{
				i = 0;
				wordPtr = strtok (buf," ,.-");
				if ( strncmp(buf,"help",4) == 0 )
				{
					mDebugOut("Instructions:\n", NULL);
					mDebugOut("help - this help instructions\n", NULL);
					mDebugOut("interval <number> - set the LED toggle interval\n", NULL);
					mDebugOut("time - prints the time since last LED toggle\n", NULL);
					mDebugOut("Program will be in inactive state after 30s without any command\n\n", NULL);
				}
				else if ( strncmp(buf,"interval",8) == 0 )
				{
					wordPtr = strtok (NULL," ,.-");
					interval = atoi(wordPtr);
					xTimerChangePeriod( xTimers[1], interval / portTICK_PERIOD_MS, 100 );
				}
				else if ( strncmp(buf,"time",4) == 0 )
				{
					xRemainingTime = xTimerGetExpiryTime( xTimers[1] ) - xTaskGetTickCount();
					mDebugOut("Time left: %d\n", (void*)xRemainingTime);
				}
				else
				{
					mDebugOut("Error! Unknown command..\n", NULL);
				}
				wordPtr = strtok (NULL," ,.-");
			}
			else if ( i == 64 )
			{
				i = 0;
				mDebugOut("Buffer overflow..\n", NULL);
			}
			else
			{
				xTimerReset(xTimers[0], 0);
				i++;
			}
		}
	}
}

/****************************************************************************/

int main(void)
{

	prvSetupHardware();

	setupTimers();

	xSemaphoreOUT = xSemaphoreCreateMutex();
	xSemaphoreIN = xSemaphoreCreateMutex();
	xSemaphoreReset = xSemaphoreCreateBinary();

	/* print thread */
	xTaskCreate(vGetCommandTask, "Get Commands", configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}

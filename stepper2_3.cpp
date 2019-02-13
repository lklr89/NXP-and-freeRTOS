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
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "DigitalIoPin.h"


/* Create Semaphore */
SemaphoreHandle_t xSemaphoreOUT;
SemaphoreHandle_t xGoSemaphore;
SemaphoreHandle_t xRedLedSemaphore;
SemaphoreHandle_t xGreenLedSemaphore;
SemaphoreHandle_t sbRIT;

/* Global Pointers for IO - defined in main() */
/* If just defined in global scope they would be overwritten by board internal functions */
DigitalIoPin *LimitSW2;
DigitalIoPin *LimitSW1;
DigitalIoPin *dir;
DigitalIoPin *step;


/* Global values */
int pps = 1000;
int stepAvg = 0;

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
    // initialize RIT (= enable clocking etc.)
    Chip_RIT_Init(LPC_RITIMER);
    // set the priority level of the interrupt
    // The level must be equal or lower than the maximum priority specified in FreeRTOS config
    // Note that in a Cortex-M3 a higher number indicates lower interrupt priority
    NVIC_SetPriority( RITIMER_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1 );
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


bool Led1State = false, Led2State = false;

int xCalibrate(void)
{
	bool ndir = false;
	int stepCount = 0, runCount = 0, stepSum = 0;

	while (1)
	{
		dir->write(ndir);
		if ( (Led1State = LimitSW2->read()) && (Led2State = LimitSW1->read()) ) // NO Limitswitch pressed ?
		{
			stepCount++;
			step->write(true);
			vTaskDelay(1);
			step->write(false);
			vTaskDelay(1);
		}
		else
		{
			/* Limits arrived */
			if (!Led1State)	xSemaphoreGive(xRedLedSemaphore);
			if (!Led2State)	xSemaphoreGive(xGreenLedSemaphore);
			mDebugOut("Limits arrived. StepCount : %d\n", (void*)stepCount);
			if (runCount > 0)	// skip the first unwhole step
				stepSum += stepCount;
			mDebugOut("StepSum : %d\n", (void*)stepSum);
			/* Change direction */
			ndir = (bool)!ndir;
			dir->write(ndir);

			/* Calculate stepcount average when there is enough data */
			if (++runCount == 3)
			{
				stepAvg = (1.0/2.0) * stepSum; // Average for 3 whole stepcounts
				mDebugOut("Step Average: %d\n", (void*)stepAvg);
				return 0; // success
			}
			else // if still calibrating
			{
				stepCount = 0;
				for(int i=0; i<15; i++)
				{
					stepCount++;
					step->write(true);
					vTaskDelay(1);
					step->write(false);
					vTaskDelay(1);
				}
			}
		}
	}
	return 1; // error
}

/* Repititive Interrupt Timer */
volatile uint32_t RIT_count;
volatile bool on = false;
extern "C"
{
	void RIT_IRQHandler(void)
	{
		// This used to check if a context switch is required
		portBASE_TYPE xHigherPriorityWoken = pdFALSE;

		// Tell timer that we have processed the interrupt.
		// Timer then removes the IRQ until next match occurs
		Chip_RIT_ClearIntStatus(LPC_RITIMER); // clear IRQ flag

		if(RIT_count > 0)
		{
			RIT_count--;
			if ( Led1State = !LimitSW2->read() )
			{
				xSemaphoreGiveFromISR(xRedLedSemaphore, &xHigherPriorityWoken);
				dir->write(1);
			}

			if ( Led2State = !LimitSW1->read() )
			{
				xSemaphoreGiveFromISR(xGreenLedSemaphore, &xHigherPriorityWoken);
				dir->write(0);
			}

				on = !on;
				step->write(on);
		}
		else
		{
			Chip_RIT_Disable(LPC_RITIMER); // disable timer
			// Give semaphore and set context switch flag if a higher priority task was woken up
			xSemaphoreGiveFromISR(sbRIT, &xHigherPriorityWoken);
		}

		// End the ISR and (possibly) do a context switch
		portEND_SWITCHING_ISR(xHigherPriorityWoken);
	}
}

void RIT_start(int count, int us)
{
	uint64_t cmp_value;

	// Determine approximate compare value based on clock rate and passed interval
	cmp_value = (uint64_t) Chip_Clock_GetSystemClockRate() * (uint64_t) us / 1000000;

	// disable timer during configuration
	Chip_RIT_Disable(LPC_RITIMER);
	RIT_count = count;

	// enable automatic clear on when compare value==timer value
	// this makes interrupts trigger periodically
	Chip_RIT_EnableCompClear(LPC_RITIMER);

	// reset the counter
	Chip_RIT_SetCounter(LPC_RITIMER, 0);
	Chip_RIT_SetCompareValue(LPC_RITIMER, cmp_value);

	// start counting
	Chip_RIT_Enable(LPC_RITIMER);

	// Enable the interrupt signal in NVIC (the interrupt controller)
	NVIC_EnableIRQ(RITIMER_IRQn);

	// wait for ISR to tell that we're done
	if(xSemaphoreTake(sbRIT, portMAX_DELAY) == pdTRUE)
	{
		// Disable the interrupt signal in NVIC (the interrupt controller)
		NVIC_DisableIRQ(RITIMER_IRQn);
	}
	else
	{
		// unexpected error
	}
}


/*-----------------------------------------------------------------------------------------
 *	||||||  ||||   ||̅ ̅ ̅ ̅  ||//
 * 	  ||   ||__||  ̅	̅̅ ̅ ̅ || |[_			TASKS
 *    ||  ||    || |||||| ||\\
 *-----------------------------------------------------------------------------------------*/

void vMotorTask(void *pvParameters)
{
	typedef enum { init, ready} state;
	state fsm = init;
	int diff = 200, missingStepsCount;


	while (1)
	{
		switch (fsm)
		{
			case init:
				if (xCalibrate() == 0)
					fsm = ready;
				break;

			case ready:
				missingStepsCount = 0;
				RIT_start(stepAvg*2, pps); // pps initial 1000

				while( LimitSW2->read() && LimitSW1->read() )
				{
					missingStepsCount++;
					RIT_start(1,pps);
				}

				if (missingStepsCount >= 10)
				{
					mDebugOut("\n\nMaximum pulses per second: %d\n",(void *)pps);
					while (1)
						RIT_start(stepAvg*2,pps+diff);
				}
				else if (missingStepsCount >= 2)
					diff = 100 / missingStepsCount;

				mDebugOut("Steps missed: %d\n",(void *)missingStepsCount);
				mDebugOut("Diff will now be: %d\n",(void *)diff);


				pps = pps - diff;
				mDebugOut("Time between steps: %d ms\n\n",(void *)pps);
		}
	}
}

void vLEDTask1(void *pvParameters)
{
	bool LedState = false;

	while (1)
	{
		if (xSemaphoreTake(xRedLedSemaphore, portMAX_DELAY) == pdTRUE) // binary -automatic return-
		{
			Board_LED_Set(0, (bool) true);
			vTaskDelay(1000);
			Board_LED_Set(0, (bool) false);
		}
		vTaskDelay(10);
	}
}

void vLEDTask2(void *pvParameters)
{
	bool LedState = false;

	while (1)
	{
		if (xSemaphoreTake(xGreenLedSemaphore, portMAX_DELAY) == pdTRUE) // binary -automatic return-
		{
			Board_LED_Set(1, (bool) true);
			vTaskDelay(1000);
			Board_LED_Set(1, (bool) false);
		}
		vTaskDelay(10);
	}
}


int main(void) {

	prvSetupHardware();

	/* Create Semaphores */
	xSemaphoreOUT = xSemaphoreCreateMutex();

	xGoSemaphore = xSemaphoreCreateBinary();
	xRedLedSemaphore = xSemaphoreCreateBinary();
	xGreenLedSemaphore = xSemaphoreCreateBinary();
	sbRIT = xSemaphoreCreateBinary();

	/* IO */
	LimitSW2 = new DigitalIoPin(0, 28, DigitalIoPin::pullup, false);
	LimitSW1 = new DigitalIoPin(0, 27, DigitalIoPin::pullup, false);
	dir = new DigitalIoPin(1, 0, DigitalIoPin::output, false);
	step = new DigitalIoPin(0, 24, DigitalIoPin::output, false);

	/* Create Task: Limit switches */
	xTaskCreate(vMotorTask, "Motor task", configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* LED1 toggle thread */
	xTaskCreate(vLEDTask1, "LED 1",	configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

	/* LED2 toggle thread */
	xTaskCreate(vLEDTask2, "LED 2",	configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL), (TaskHandle_t *) NULL);

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

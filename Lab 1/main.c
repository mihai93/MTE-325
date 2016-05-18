/*
 * main.c
 *
 *  Created on: 2015-06-07
 *      Author: mrlistov
 */

#include "alt_types.h"
#include <stdio.h>
#include <unistd.h>
#include "system.h"
#include "sys/alt_irq.h"
#include <io.h>
#include "altera_avalon_pio_regs.h"
#include "board_diag.h"
#include "ece224_egm.h"

#define MAX_SAMPLES 100 //max sample size, measured per pulse
#define DOUBLE_MAX 200  //max sample size, measured per edge

// variables to keep track of the samples responded to, and the current state of the response output
unsigned SAMPLES = 0;
unsigned OUT = 0;
alt_u32 timerPeriod2 = TIMER_0_FREQ;

#ifndef PHASE1
//check to see if EGM pulse has changed state, if it did, change the output correspondingly 
static void PHASE_2_TIMER_0_ISR(void *context, alt_u32 id)
{
	int a;
	// acknowledge the interrupt by clearing the TO bit in the status register
	IOWR(TIMER_0_BASE, 0, 0x0);

	if( IORD(PIO_PULSE_BASE, 0) && !OUT )
	{
		IOWR(PIO_RESPONSE_BASE,0,1);
		OUT = 1;
		SAMPLES++;
	}
	else if( !IORD(PIO_PULSE_BASE, 0) && OUT)
	{
		IOWR(PIO_RESPONSE_BASE,0,0);
		OUT = 0;
		SAMPLES++;
	}
}

//setup the timer, takes in a period value which specifies a timer period in microseconds
static void phase2_timer_init( alt_u32 period )
{
	timerPeriod2 = period * 50; //calculate the amount of ticks needed for a period in microseconds, 50 ticks is 1 microsecond

	// Set timer period
	IOWR(TIMER_0_BASE, 2, (alt_u16)timerPeriod2);
	IOWR(TIMER_0_BASE, 3, (alt_u16)(timerPeriod2 >> 16));

	alt_irq_register(TIMER_0_IRQ, (void*)0, PHASE_2_TIMER_0_ISR);

	// Start timer, run continuously, enable interrupts
	IOWR(TIMER_0_BASE, 0, 0x0);
	IOWR(TIMER_0_BASE, 1, 0x0);
}

//Checks to see if rising edge or falling edge, change output to reflect the EGM pulse edge
void EGM_ISR(void *context, alt_u32 id)
{
  /* Reset the Button's edge capture register. */
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_PULSE_BASE, 0);
	if( OUT ){
		IOWR(PIO_RESPONSE_BASE,0,0);
		OUT = 0;
		SAMPLES++;
	}
	else{
		IOWR(PIO_RESPONSE_BASE,0,1);
		OUT = 1;
	}
}

//
void enable_EGM_IRQ()
{
	//enable interrupt
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_PULSE_BASE, 0xf);
	/* Reset the edge capture register. */
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PIO_PULSE_BASE, 0x0);
}

void disable_EGM_IRQ()
{
	/* Disable interrupts from the PIO component. */
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PIO_PULSE_BASE, 0x0);
}

//initialize the EGM pulse interrupt
void  init_EGM_IRQ()
{
	disable_EGM_IRQ();
#ifdef ALT_ENHANCED_INTERRUPT_API_PRESENT
	alt_ic_isr_register(PIO_PULSE_IRQ_INTERRUPT_CONTROLLER_ID, PIO_PULSE_IRQ, EGM_ISR, (void*)0, 0x0);
#else
	alt_irq_register(PIO_PULSE_IRQ, (void*)0, EGM_ISR);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void phase2()
{
	int i,j,k,z;
	SAMPLES = 0;
	OUT = 0;

	printf("STARTING TESTS...\n");

	//TIMER PERIOD LOOP
	for( z=20; z<=80; z+=15 )
	{
		phase2_timer_init(z);
		//GRANULARITY LOOP
		for( k=5; k<=45; k+=10 )
		{
			//DUTY CYCLE LOOP
			for( j=1; j<=14; j+=2 )
			{
				//PERIOD LOOP
				for( i=1; i<=14; i+=2 )
				{
					printf("%d, %d, ", z, k); //timer interrupt period, granularity value

					//start timer interrupt and test periodic polling
					start_timer_0();
					init(i, j);

					while( SAMPLES <= DOUBLE_MAX )
					{
						background(k);
					}

					finalize();
					stop_timer_0();

					SAMPLES = 0;
					OUT = 0;
				}
			}
		}
	}


	init_EGM_IRQ();
	//PERIOD LOOP
	for( i=1; i<=14; i+=2 )
	{
		//DUTY CYCLE LOOP
		for( j=1; j<=14; j+=2 )
		{
			//GRANULARITY LOOP
			for( k=5; k<=45; k+=10 )
			{
				printf("0, %d, ", k); //granularity value

				//activate EMG pulse interrupt and run interrupt test
				enable_EGM_IRQ();
				init(i, j);

				while( SAMPLES <= MAX_SAMPLES )
				{
					background(k);
				}

				finalize();
				disable_EGM_IRQ();
				SAMPLES = 0;
				OUT = 0;
			}
		}
	}

	printf("FINISHED TESTS...\n");
}
#endif

int main(void)
{
#ifdef PHASE1
	printf("Using phase 1...\n");
	phase1();
#else
	printf("Using phase 2...\n");
	phase2();
#endif

	while(1){};
	return 0;
}

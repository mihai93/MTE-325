 #include "board_diag.h"
 
#define NUM_OF_SWITCH 8 //number of switches
alt_u32 timerPeriod = TIMER_0_FREQ; //50M, number of ticks needed for 1 second

//variables needed to display led
char ledState = 0x0;
char ledRun = 0;
char ledReset = 0;
volatile int ledCounter = 0;
volatile int lastLed = -1;

//variables needed to display 7 seg
char sevSegState = 0x0;
char sevSegRun = 0;
char sevSegReset = 0;
volatile int sevSegCounter = 0;
volatile int lastSevSeg = -1;


static void timer_init()
{
#ifdef ALT_ENHANCED_INTERRUPT_API_PRESENT
	alt_ic_isr_register(TIMER_0_IRQ_INTERRUPT_CONTROLLER_ID, TIMER_0_IRQ, TIMER_0_ISR, (void*)0, 0x0);
	alt_ic_isr_register(TIMER_1_IRQ_INTERRUPT_CONTROLLER_ID, TIMER_1_IRQ, TIMER_1_ISR, (void*)0, 0x0);
#else
	alt_irq_register(TIMER_0_IRQ, (void*)0, TIMER_0_ISR);
	alt_irq_register(TIMER_1_IRQ, (void*)0, TIMER_1_ISR);
#endif

	// Set timer period
	IOWR(TIMER_0_BASE, 2, (alt_u16)timerPeriod);
	IOWR(TIMER_0_BASE, 3, (alt_u16)(timerPeriod >> 16));

	IOWR(TIMER_1_BASE, 2, (alt_u16)timerPeriod);
	IOWR(TIMER_1_BASE, 3, (alt_u16)(timerPeriod >> 16));

	// Start timer, run continuously, enable interrupts
	IOWR(TIMER_0_BASE, 0, 0x0);
	IOWR(TIMER_0_BASE, 1, 0x3);

	IOWR(TIMER_1_BASE, 0, 0x0);
	IOWR(TIMER_1_BASE, 1, 0x3);
}

void stop_timer_0()
{
	IOWR(TIMER_0_BASE, 1, 0x0);
}

void start_timer_0()
{
	IOWR(TIMER_0_BASE, 1, 0x7);
}

static void stop_timer_1()
{
	IOWR(TIMER_1_BASE, 1, 0x0);
}

static void start_timer_1()
{
	IOWR(TIMER_1_BASE, 1, 0x7);
}

static void TIMER_0_ISR(void *context, alt_u32 id)
{
	// acknowledge the interrupt by clearing the TO bit in the status register
	IOWR(TIMER_0_BASE, 0, 0x0);
	//increment loop counter needed to change led state
	ledCounter++;
}

static void TIMER_1_ISR(void *context, alt_u32 id)
{
	// acknowledge the interrupt by clearing the TO bit in the status register
	IOWR(TIMER_1_BASE, 0, 0x0);
	//increment loop counter needed to change sev seg value
	sevSegCounter++;
}

//turn led on
void led_0_on()
{
	IOWR(LED_PIO_BASE, 0, 0x1);
}

//turn led off
void led_0_off()
{
	IOWR(LED_PIO_BASE, 0, 0x0);
}

#ifdef BUTTON_PIO_NAME

#ifdef ALT_ENHANCED_INTERRUPT_API_PRESENT
static void handle_button_interrupts(void* context)
#else
static void handle_button_interrupts(void* context, alt_u32 id)
#endif
{
  //check to see if button 1 or 2 is pressed, if the corresponding display method is already displaying something, reset it
  int reg = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);

  //if button 1 pressed, record switch states in ledState
  if( reg & 1 ){
	  ledState = IORD_ALTERA_AVALON_PIO_DATA(SWITCH_PIO_BASE);
	  if( ledRun )
		  ledReset = 1;
	  else
		  ledRun = 1;
  }

  //if button 2 pressed, record switch states in sevSegState
  if( reg & 2 ){
	  sevSegState = IORD_ALTERA_AVALON_PIO_DATA(SWITCH_PIO_BASE);
	  if( sevSegRun )
		  sevSegReset = 1;
	  else
		  sevSegRun = 1;
  }

  /* Reset the Button's edge capture register. */
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0);
  
  IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);
}

/* Initialize the button_pio. */

static void init_button_pio()
{
  /* Recast the edge_capture pointer to match the alt_irq_register() function
  * prototype. */
  void* edge_capture_ptr = (void*) &edge_capture;
  /* Enable all 4 button interrupts. */
  IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0xf);
  /* Reset the edge capture register. */
  IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x0);
  
   //Register the interrupt handler. 
#ifdef ALT_ENHANCED_INTERRUPT_API_PRESENT
  alt_ic_isr_register(BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID, BUTTON_PIO_IRQ,
    handle_button_interrupts, edge_capture_ptr, 0x0);
#else
  alt_irq_register( BUTTON_PIO_IRQ, edge_capture_ptr,
    handle_button_interrupts);
#endif
}

/* Tear down the button_pio. */

static void disable_button_pio()
{
  /* Disable interrupts from the button_pio PIO component. */
  IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0x0);
  /* Un-register the IRQ handler by passing a null handler. */
#ifdef ALT_ENHANCED_INTERRUPT_API_PRESENT
  alt_ic_isr_register(BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID, BUTTON_PIO_IRQ, 
    NULL, NULL, NULL);
#else
  alt_irq_register( BUTTON_PIO_IRQ, NULL, NULL );
#endif
}
#endif

#ifdef SEVEN_SEG_PIO_NAME

/*********************************************
 * Seven Segment Functions
 *********************************************/

/*********************************************
 * static void sevenseg_set_hex(alt_u8 hex)
 * 
 * Function which encodes the value passed in by
 * the variable 'hex' into what is displayed on
 * the Seven Segment Display.
 *********************************************/
 
static void sevenseg_set_hex(alt_u8 hex)
{
  static alt_u8 segments[16] = {
    0x81, 0xCF, 0x92, 0x86, 0xCC, 0xA4, 0xA0, 0x8F, 0x80, 0x84, /* 0-9 */
    0x88, 0xE0, 0xF2, 0xC2, 0xB0, 0xB8 };                       /* a-f */

  alt_u32 data = segments[hex & 15] | (segments[(hex >> 4) & 15] << 8);

  IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEG_PIO_BASE, data);
}

#endif

void phase1()
{
	timer_init();
	init_button_pio();

	led_0_off();
	sevenseg_set_hex(0);

	ledRun = 0;
	ledReset = 0;
	ledCounter = 0;
	lastLed = -1;

	sevSegRun = 0;
	sevSegReset = 0;
	sevSegCounter = 0;
	lastSevSeg = -1;

	while(1)
	{
		//if ledRun is true, it means that the led need to be flashing
		if( ledRun ){
			//if reset on, restart timer and led counter
			if( ledReset ){
				stop_timer_0();
				start_timer_0();
				ledCounter = 0;
				ledReset = 0;
			}

			//if finished displaying sequence, then turn ledRun off and reset variables
			if( ledCounter >= NUM_OF_SWITCH ){
				stop_timer_0();
				led_0_off();
				ledCounter = 0;
				lastLed = -1;
				ledRun = 0;
			}

			//if the counter value changed, change the led state as well, corresponding to the switch states
			else{
				if( lastLed != ledCounter ){
					if ( !ledCounter )
						start_timer_0();

					if( ledState & (1 << ledCounter))
						led_0_on();
					else
						led_0_off();
					lastLed = ledCounter;
				}
			}
		}

		//if sevSegRun is true, it means that the sev seg needs to be displaying a value
		if( sevSegRun ){
			//if reset on, restart timer and sev seg counter
			if( sevSegReset ){
				stop_timer_1();
				start_timer_1();
				sevSegCounter = 0;
				sevSegReset = 0;
			}

			//if finished displaying sequence, then turn sevSegRun off and reset variables
			if( sevSegCounter >= NUM_OF_SWITCH ){
				stop_timer_1();
				sevenseg_set_hex(0);
				sevSegCounter = 0;
				lastSevSeg = -1;
				sevSegRun = 0;
			}

			//if the counter value changed, change the sev seg display value as well, corresponding to the switch states
			else{
				if( lastSevSeg != sevSegCounter ){
					if ( !sevSegCounter )
						start_timer_1();

					if( sevSegState & (1 << sevSegCounter))
						sevenseg_set_hex(1);
					else
						sevenseg_set_hex(0);
					lastSevSeg = sevSegCounter;
				}
			}
		}
	}
}

/* Includes */

#include "alt_types.h"
#include <stdio.h>
#include <unistd.h>
#include "system.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"

//comment out below line if running phase 2
//#define PHASE1

#ifdef PHASE1
static void timer_init();
static void stop_timer_1();
static void start_timer_1();
static void TIMER_0_ISR(void *context, alt_u32 id);
static void TIMER_1_ISR(void *context, alt_u32 id);
void phase1();
#endif

//these functions need to be used by phase 2 as well
void stop_timer_0();
void start_timer_0();
void led_0_on();
void led_0_off();

/* Seven Segment Related Prototypes */
#ifdef SEVEN_SEG_PIO_NAME
static void sevenseg_set_hex(alt_u8 hex);
#endif

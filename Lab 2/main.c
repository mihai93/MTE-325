/*
 * main.c
 *
 *  Created on: 2015-06-24
 *      Author: mrlistov
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sys/alt_irq.h"
#include <io.h>
#include "altera_avalon_pio_regs.h"
#include "basic_io.h"
#include "LCD.h"
#include "SD_Card.h"
#include "fat.h"
#include "wm8731.h"

#define FILEEXTENSION "WAV"
#define ESC 27
#define CLEAR_LCD_STRING "[2J"
#define SECTORS_PER_SECOND 173

//state variables used to keep track of playing state
volatile int reverseFlag;
volatile int forwardFlag;
volatile int playFlag;
volatile int pressed = 0;
volatile char switchState = 0x0;

//variables used to help keep track of the list of playable files
int numOfFiles = 0;
int fileIndex = 0;

//playback options enum
typedef enum PLAYMODE {
	NORMAL = 2,
	DOUBLE = 4,
	HALF,
	DELAY,
	REVERSE
} PLAYBACK;

PLAYBACK playMode = NORMAL;		//holds the current playback mode
BYTE BUFFER[512] = {0};			//buffer to store sector data
data_file DATA;
int * CHAIN_CLUSTER;			//global pointer for chain cluster
BYTE DELAY_BUFFER[173][512];	//buffer used to store 1 second worth of bytes for delay playback
int modeS[]={0,0,5,0,1,2,3,4}; 	//int values for different playback options used in LCD functions

static void handle_button_interrupts(void* context, alt_u32 id)
{
	int reg = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);

	//keeps track of current button state, so button only triggers on release
	if( !pressed )
		pressed = 1;
	else
	{
		//check to see what button was pressed, if forward and back buttons, set appropriate flags
		if( reg & 8 ){
			reverseFlag = 1;
		}
		else if( reg & 4 ){
			forwardFlag = 1;
		}
		//if play button pressed, then check the current state of the switches and set up the appropriate playback mode
		else if( reg & 2 ){
			if( !playFlag )
			{
				switchState = IORD_ALTERA_AVALON_PIO_DATA(SWITCH_PIO_BASE);
				if( switchState & 8 )
					playMode = REVERSE;
				else if( switchState & 4 )
					playMode = DELAY;
				else if( switchState & 2 )
					playMode = DOUBLE;
				else if( switchState & 1 )
					playMode = HALF;
				else
					playMode = NORMAL;

				playFlag = 1;
			}
		}
		//if currently playing, and stop button pressed, stop the playing
		else if( reg & 1 ){
			if(playFlag){
				playFlag = 0;
				fileIndex--;
			}
		}
		pressed = 0;
	}

  /* Reset the Button's edge capture register. */
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0);

  /*
   * Read the PIO to delay ISR exit. This is done to prevent a spurious
   * interrupt in systems with high processor -> pio latency and fast
   * interrupts.
   */
  IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);
}

/* Initialize the button_pio. */

static void init_button_pio()
{
	/* Enable all 4 button interrupts. */
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0xf);
	/* Reset the edge capture register. */
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x0);

	alt_irq_register( BUTTON_PIO_IRQ, NULL,
	handle_button_interrupts);
}

void init_stuff(){
	//initialize the SD card, the fat file system, and the LCD
	SD_card_init();
	init_mbr();
	init_bs();
	LCD_Init();
	init_audio_codec();
	init_button_pio();

	//display information about the boot sector of the SD card
	info_bs();
}

//find out the total number of wav files stored in the SD card
int getNumFiles(){
	int counter = 0;
	char firstName[11];
	if(!search_for_filetype(FILEEXTENSION, &DATA, 0, 1))
	{
		counter++;
		strcpy(firstName, &DATA.Name);
		while(1)
		{
			search_for_filetype(FILEEXTENSION, &DATA, 0, 1);
			counter++;
			if(strcmp(&DATA.Name, firstName) == 0)
			{
				file_number = 0;
				return counter-1;
			}
		}
	}
	return 0;
}

int main()
{
	int i,j,k,num_sec,counter=0, lead_index, lag_index;
	UINT16 tmp;	//Create a 16-bit variable to pass to the FIFO

	//initialize all the required systems

	init_stuff();

	numOfFiles = getNumFiles();

	playFlag = 1;

	while(1){
		if(playFlag)
		{
			file_number = fileIndex;

			LCD_Display("BUFFERING", modeS[playMode]);

			//if files found, then proceed to play the song
			if( !search_for_filetype(FILEEXTENSION, &DATA, 0, 1))
			{
				printf("NAME: %s ", DATA.Name);

				//build the cluster chain
				UINT32 length = 1 + ceil(DATA.FileSize/(BPB_BytsPerSec * BPB_SecPerClus));
				CHAIN_CLUSTER = (int *) malloc(length*sizeof(int));
				LCD_File_Buffering(DATA.Name);
				build_cluster_chain(CHAIN_CLUSTER, length, &DATA);
				LCD_Display(DATA.Name, modeS[playMode]);

				num_sec = ceil(DATA.FileSize/BPB_BytsPerSec);

				//if playMode value smaller than half, then use playMode enum value as play speed
				if( playMode < HALF )
				{
					for( i=0; i<num_sec && playFlag; i++)
					{
						get_rel_sector(&DATA, BUFFER, CHAIN_CLUSTER, i);

						for( j=0; j<BPB_BytsPerSec && playFlag; j+=playMode ){
							while(IORD( AUD_FULL_BASE, 0 ) ) {} //wait until the FIFO is not full
							tmp = ( BUFFER[ j + 1 ] << 8 ) | ( BUFFER[ j ] );
							IOWR( AUDIO_0_BASE, 0, tmp ); //Write the 16-bit variable tmp to the FIFO where it
							//will be processed by the audio CODEC
						}
					}
				}
				//play at half speed
				else if( playMode == HALF )
				{
					for( i=0; i<num_sec && playFlag; i++)
					{
						get_rel_sector(&DATA, BUFFER, CHAIN_CLUSTER, i);

						for( j=0; j<BPB_BytsPerSec && playFlag; j+=NORMAL ){
							for( k=0; k<2 && playFlag; k++ )
							{
								while(IORD( AUD_FULL_BASE, 0 ) ) {} //wait until the FIFO is not full
								tmp = ( BUFFER[ j + 1 ] << 8 ) | ( BUFFER[ j ] );
								IOWR( AUDIO_0_BASE, 0, tmp ); //Write the 16-bit variable tmp to the FIFO where it
								//will be processed by the audio CODEC
							}
						}
					}
				}
				//play with 1 second delay on one channel
				else if( playMode == DELAY )
				{
					for( i=0; i<(num_sec + SECTORS_PER_SECOND) && playFlag; i++)
					{
						lead_index = i%SECTORS_PER_SECOND;
						lag_index = (i + 1 - SECTORS_PER_SECOND)%SECTORS_PER_SECOND;
						get_rel_sector(&DATA, DELAY_BUFFER[lead_index], CHAIN_CLUSTER, i);

						for( j=0; j<BPB_BytsPerSec && playFlag; j+=NORMAL ){
							//checks to see which channel to play, the regular channel, or the delay channel
							if( !(j%4) )
							{
								//play the delay channel, start once 1 second has passed by
								if( i >= SECTORS_PER_SECOND - 1)
									tmp = ( DELAY_BUFFER[ lag_index ][ j + 1 ] << 8 ) | ( DELAY_BUFFER[ lag_index ][ j ] );
								else
									tmp = 0x0;
							}
							else
							{
								//play the regular channel, stops once whole song has been played on this channel
								if( i < num_sec )
									tmp = ( DELAY_BUFFER[ lead_index ][ j + 1 ] << 8 ) | ( DELAY_BUFFER[ lead_index ][ j ] );
								else
									tmp = 0x0;
							}

							while(IORD( AUD_FULL_BASE, 0 ) ) {} //wait until the FIFO is not full
							IOWR( AUDIO_0_BASE, 0, tmp ); //Write the 16-bit variable tmp to the FIFO where it
							//will be processed by the audio CODEC

						}
					}
				}
				//play in reverse by fetching the sectors in reverse order
				else if( playMode == REVERSE )
				{
					for( i=num_sec-1; i>=0 && playFlag; i--)
					{
						get_rel_sector(&DATA, BUFFER, CHAIN_CLUSTER, i);

						for( j=BPB_BytsPerSec-2; j>=0 && playFlag; j-=NORMAL ){
							while(IORD( AUD_FULL_BASE, 0 ) ) {} //wait until the FIFO is not full
							tmp = ( BUFFER[ j + 1 ] << 8 ) | ( BUFFER[ j ] );
							IOWR( AUDIO_0_BASE, 0, tmp ); //Write the 16-bit variable tmp to the FIFO where it
							//will be processed by the audio CODEC
						}
					}
				}

				fileIndex++;
				free(CHAIN_CLUSTER);
			}
		}
		else
		{
			//currently not playing, check to see if need to move forward by one song because forward button was pressed
			if(forwardFlag)
			{
				fileIndex++;

				if(fileIndex >= numOfFiles)
					fileIndex = 0;

				file_number = fileIndex;

				search_for_filetype(FILEEXTENSION, &DATA, 0, 1);
				LCD_Display(DATA.Name, modeS[playMode]);

				forwardFlag = 0;
			}
			//currently not playing, check to see if need to move backward by one song because backward button was pressed
			else if(reverseFlag)
			{
				fileIndex--;

				if (fileIndex<0)
				{
					fileIndex = 0;
				}
				file_number = fileIndex;

				search_for_filetype(FILEEXTENSION, &DATA, 0, 1);
				LCD_Display(DATA.Name, modeS[playMode]);

				reverseFlag = 0;
			}
			else
			{
				//checks to see if the switches have changed, in which case, reflect the change on the LCD
				if( switchState != IORD_ALTERA_AVALON_PIO_DATA(SWITCH_PIO_BASE))
				{
					switchState = IORD_ALTERA_AVALON_PIO_DATA(SWITCH_PIO_BASE);
					if( switchState & 8 )
						playMode = REVERSE;
					else if( switchState & 4 )
						playMode = DELAY;
					else if( switchState & 2 )
						playMode = DOUBLE;
					else if( switchState & 1 )
						playMode = HALF;
					else
						playMode = NORMAL;

					LCD_Display(DATA.Name, modeS[playMode]);
				}
			}
		}
	}
}

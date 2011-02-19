//*****************************************************************************
// File Name	: a2dtest.c
// 
// Title		: example usage of some avr library functions
// Revision		: 1.0
// Notes		:	
// Target MCU	: Atmel AVR series
// Editor Tabs	: 4
// 
// Revision History:
// When			Who			Description of change
// -----------	-----------	-----------------------
// 20-Oct-2002	pstang		Created the program
//*****************************************************************************
 
//----- Include Files ---------------------------------------------------------
#include <avr/io.h>		// include I/O definitions (port names, pin names, etc)
#include <avr/interrupt.h>	// include interrupt support
#include <string.h>

#include "global.h"		// include our global settings
#include "uart.h"		// include uart function library
#include "rprintf.h"	// include printf function library
#include "timer.h"		// include timer function library (timing, PWM, etc)
#include "a2d.h"		// include A/D converter function library
#include "vt100.h"		// include VT100 terminal support

//----- Begin Code ------------------------------------------------------------
int main(void)
{
	u16 a=0;
	u08 i=0;

	// initialize our libraries
	// initialize the UART (serial port)
	uartInit();
	// make all rprintf statements use uart for output
	rprintfInit(uartSendByte);
	// initialize the timer system
	timerInit();
	// turn on and initialize A/D converter
	a2dInit();

	// print a little intro message so we know things are working
/*	vt100ClearScreen();
	vt100SetCursorPos(1,1);
	rprintf("Welcome to the a2d test!\r\n");*/
	
	// configure a2d port (PORTA) as input
	// so we can receive analog signals
	DDRC = 0x00;
	// make sure pull-up resistors are turned off
	PORTC = 0x00;

	// set the a2d prescaler (clock division ratio)
	// - a lower prescale setting will make the a2d converter go faster
	// - a higher setting will make it go slower but the measurements
	//   will be more accurate
	// - other allowed prescale values can be found in a2d.h
	a2dSetPrescaler(ADC_PRESCALE_DIV32);

	// set the a2d reference
	// - the reference is the voltage against which a2d measurements are made
	// - other allowed reference values can be found in a2d.h
	a2dSetReference(ADC_REFERENCE_AVCC);

	// use a2dConvert8bit(channel#) to get an 8bit a2d reading
	// use a2dConvert10bit(channel#) to get a 10bit a2d reading

	while(1)
	{
		u08 c=0;
		u08 n,i;
char radiace[10];
		
		char radka[201];

		for(n=0;n<=100;n++) radka[n]=0;  // vynuluj bufferovaci pole
		for(n=0;n<10;n++) radiace[n]=0;  // vynuluj bufferovaci pole

		n=0;
		while(1)		// pockej na $ kterym zacina NMEA radka
		{
		  uartReceiveByte(&c);
		  if(c == '$') break;
		}

		for(i=0;i<100;i++)		// nacti maximalne 100 znaku do bufferu
		  {
		    radka[n]=c;
		    if(c == '\n') break;		// kdyz narazis na konec radku zastav nacitani
		    uartReceiveByte(&c);
		    n++;
		  }

		radka[n]=0;	// naztav na konec retezce pro zpracovani pomoci strcat


		itoa(a2dConvert10bit(2),&radiace,10); //a2dConvert8bit(1)
		if(n != 0)
		{
		  strcat(radka, radiace);
		  strcat(radka,"\r");		
		}
		n=0;
		uartFlushReceiveBuffer();

		while (0!=radka[n])
		{
		  uartSendByte(radka[n]);
		  n++;
		  timerPause(35);
		}

	}
	
	return 0;
}

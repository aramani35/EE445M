// *************Interpreter.c**************
// EE445M/EE380L.6 Lab 4 solution
// high level OS functions
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 3/9/17, valvano@mail.utexas.edu
#include "ST7735.h"
#include "os.h"
#include "ADC.h"
#include "UART.h"
#include "efile.h"
#include "eDisk.h"
#include <string.h> 
#include <stdio.h>
#include "cmdline.h"

void Interpreter(void) {  
	char stringBuf[20];
	while(1) {
		OutCRLF();
		UART_OutString("Please enter a command: ");
		UART_InString(stringBuf, 19);
		CmdLineProcess(stringBuf);
	}
}


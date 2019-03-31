//*****************************************************************************
//
// cmdline.c - Functions to help with processing command lines.
//
// Copyright (c) 2007-2014 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 2.1.0.12573 of the Tiva Utility Library.
//
//*****************************************************************************

//*****************************************************************************
//
//! \addtogroup cmdline_api
//! @{
//
//*****************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "cmdline.h"
#include "ADC.h"
#include "ST7735.h"
#include "OS.h"
#include "UART.h"

//*****************************************************************************
//
// Defines the maximum number of arguments that can be parsed.
//
//*****************************************************************************
#ifndef CMDLINE_MAX_ARGS
#define CMDLINE_MAX_ARGS        8
#endif

//*****************************************************************************
//
// An array to hold the pointers to the command line arguments.
//
//*****************************************************************************
static char *g_ppcArgv[CMDLINE_MAX_ARGS + 1];

//*****************************************************************************
//
//! Process a command line string into arguments and execute the command.
//!
//! \param pcCmdLine points to a string that contains a command line that was
//! obtained by an application by some means.
//!
//! This function will take the supplied command line string and break it up
//! into individual arguments.  The first argument is treated as a command and
//! is searched for in the command table.  If the command is found, then the
//! command function is called and all of the command line arguments are passed
//! in the normal argc, argv form.
//!
//! The command table is contained in an array named <tt>g_psCmdTable</tt>
//! containing <tt>tCmdLineEntry</tt> structures which must be provided by the
//! application.  The array must be terminated with an entry whose \b pcCmd
//! field contains a NULL pointer.
//!
//! \return Returns \b CMDLINE_BAD_CMD if the command is not found,
//! \b CMDLINE_TOO_MANY_ARGS if there are more arguments than can be parsed.
//! Otherwise it returns the code that was returned by the command function.
//
//*****************************************************************************
int
CmdLineProcess(char *pcCmdLine)
{
    char *pcChar;
    uint_fast8_t ui8Argc;
    bool bFindArg = true;
    tCmdLineEntry *psCmdEntry;

    //
    // Initialize the argument counter, and point to the beginning of the
    // command line string.
    //
    ui8Argc = 0;
    pcChar = pcCmdLine;

    //
    // Advance through the command line until a zero character is found.
    //
    while(*pcChar)
    {
        //
        // If there is a space, then replace it with a zero, and set the flag
        // to search for the next argument.
        //
        if(*pcChar == ' ')
        {
            *pcChar = 0;
            bFindArg = true;
        }

        //
        // Otherwise it is not a space, so it must be a character that is part
        // of an argument.
        //
        else
        {
            //
            // If bFindArg is set, then that means we are looking for the start
            // of the next argument.
            //
            if(bFindArg)
            {
                //
                // As long as the maximum number of arguments has not been
                // reached, then save the pointer to the start of this new arg
                // in the argv array, and increment the count of args, argc.
                //
                if(ui8Argc < CMDLINE_MAX_ARGS)
                {
                    g_ppcArgv[ui8Argc] = pcChar;
                    ui8Argc++;
                    bFindArg = false;
                }

                //
                // The maximum number of arguments has been reached so return
                // the error.
                //
                else
                {
										UART_OutString("Too many arguments\n");
                    return(CMDLINE_TOO_MANY_ARGS);
                }
            }
        }

        //
        // Advance to the next character in the command line.
        //
        pcChar++;
    }

    //
    // If one or more arguments was found, then process the command.
    //
    if(ui8Argc)
    {
        //
        // Start at the beginning of the command table, to look for a matching
        // command.
        //
        psCmdEntry = &g_psCmdTable[0];

        //
        // Search through the command table until a null command string is
        // found, which marks the end of the table.
        //
        while(psCmdEntry->pcCmd)
        {
            //
            // If this command entry command string matches argv[0], then call
            // the function for this command, passing the command line
            // arguments.
            //
            if(!strcmp(g_ppcArgv[0], psCmdEntry->pcCmd))
            {
                return(psCmdEntry->pfnCmd(ui8Argc, g_ppcArgv));
            }

            //
            // Not found, so advance to the next entry.
            //
            psCmdEntry++;
        }

    }

    //
    // Fall through to here means that no matching command was found, so return
    // an error.
    //
    return(CMDLINE_BAD_CMD);
}

/* ----------START OF MODIFED CODE---------- */

// ----------parseNumber----------
// Parse 1-2 character string and converts to 
// two digit number
uint8_t parseNumber(char* num) {
	uint8_t length = strlen(num);
	if (length == 1)
		return (num[0] - 48);
	else if (length == 2)
		return((num[0] - 48)*10 + (num[1] - 48));
	else {
		OutCRLF();
		UART_OutString("Invalid Number. Defaulting to 1.");
	}
	
	return 1;
}

// ----------lcdCall----------
// Used for showing messages on lcd screen through
// commands from UART
// return -1 on failure, 1 on success
int lcdCall(int numArgs, char* args[]) {
	if (numArgs < 4) {
		OutCRLF();
		UART_OutString("Incorrect Format. Try 'lcd screenNum lineNum text to send'");
		return -1;
	}
	// first arg is which screen to show on
	uint8_t screenNum = parseNumber(args[1]);
	if (screenNum != 0 && screenNum != 1) {
		UART_OutString("Device num must be 0 or 1.");
		return -1;
	}
		
	// second arg is which line to start on
	uint8_t lineNum = parseNumber(args[2]);
	if (lineNum > 7) {
		UART_OutString("Device num must be between 0 and 7.");
		return -1;
	}
	
	// other args are message to send
	char message[100];
	int pos = 0;
	for (int i = 3; i < numArgs; i++) {
		int sizeOfWord = strlen(args[i]);
		for (int j = 0; j < sizeOfWord; j++) {
			message[pos] = args[i][j];
			pos++;
		}
		message[pos] = ' ';
		pos++;
	}
	message[pos] = '\0';
	
	ST7735_Message(screenNum, lineNum, message, 0);
	return 1;
}

// ----------adcCall----------
// Used for getting information related to the ADC channels and
// collecting data from them
// returns -1 on failure, return value of ADC function otherwise
int adcCall(int numArgs, char* args[]) {
	/* ADC commands: ADC_In(void) => numArgs = 2 
	 *               ADC_Open(uint32_t channelNum) => numArgs = 3
	 *							 ADC_Collect(uint32_t channelNum, uint32_t fs, int* data_buffer, uint32_t numberOfSamples)
	 *							  - data_buffer = buf in cmdline.c => numArgs = 5
	 *							 ADC_Status(void) => numArgs = 2	
	*/
	
	if (numArgs != 2 && numArgs != 3 && numArgs != 5) {
		UART_OutString("Incorrect number of arguments");
		return -1;
	}
	
	// Command must be either ADC_In or ADC_Status
	if (numArgs == 2) {
		if (!strcmp(args[1], "in")) {
			OutCRLF();
			UART_OutString("ADC Val: ");
			UART_OutUDec(ADC_In());
			return 2;
		}
		else if (!strcmp(args[1], "status")) {
			OutCRLF();
			UART_OutString("ADC Status: ");
			int status = ADC_Status();
			UART_OutUDec(status);
			return status;
		}
		else {
			UART_OutString("Invalid command");
			return -1;
		}
	}
	// Command must be: ADC_Open
	else if (numArgs == 3) {
		if (!strcmp(args[1], "open")) {
			uint8_t channelNum = parseNumber(args[2]);
			
			OutCRLF();
			if (channelNum > 12) {
				UART_OutString("Invalid Channel");
				return -1;
			}
			else {
				
				UART_OutString("Channel ");
				UART_OutUDec(channelNum);
				UART_OutString(" is being opened.");
				return ADC_Open(channelNum);
			}
		}

	}
	// Command must be: ADC_Collect
	else if (numArgs == 5) {
		OutCRLF();
		uint8_t channelNum = parseNumber(args[2]);
		// fs vals: 0 = 10Hz, 1 = 100Hz, 2 = 1000Hz, 3 = 10000Hz, 4 = 100000Hz
		// default fs = 1Hz
		uint8_t sampleMode = parseNumber(args[3]);
		uint32_t fs = 80000000;
		if (sampleMode < 5)
			fs = 8 * pow((double) 10, sampleMode + 1);
		else
			UART_OutString("Mode must be between 0 and 4. Defaulting to 1Hz sample rate.");
		// 2-digit val between 0 and 100
		uint8_t numSamples = parseNumber(args[4]);
		int buf[100];
		
		UART_OutString("Collecting ");
		UART_OutUDec(numSamples);
		UART_OutString(" samples on channel ");
		UART_OutUDec(channelNum);
		UART_OutString(" with a period of ");
		UART_OutUDec(fs);
		OutCRLF();
		
		ADC_Collect(channelNum, fs, buf, numSamples);
		
		UART_OutString("Printing every 5th sample ...");
		OutCRLF();
		
		int factor;
		if (numSamples > 20) factor = 5;
		else factor = 1;
		
		for (uint8_t i = 0; i < numSamples; i+=factor) {
			UART_OutString("Sample #");
			UART_OutUDec(i);
			UART_OutString(": ");
			UART_OutUDec(buf[i]);
			OutCRLF();
		}
		
		UART_OutString("... Done");
		OutCRLF();
		return 1;
	}
	
	return 1;
}

// ----------osCall----------
// Used for getting information about os tasks
// returns -1 on failure, 1 on success
int osCall(int numArgs, char* args[]) {
	OutCRLF();
	if (numArgs != 2) {
		UART_OutString("OS commands are either 'os clear' or 'os read'");
		return -1;
	}
	
	else {
		if (!strcmp(args[1], "clear")) {
			OS_ClearPeriodicTime();
			UART_OutString("OS counter has been cleared.");
		}
		else if (!strncmp(args[1], "read", 4)) {
			UART_OutString("OS count: ");
			UART_OutUDec(OS_ReadPeriodicTime());
		}
		else {
			UART_OutString("Invalid OS Call");
			return -1;
		}
	}
	return 1;
}

// Used for UART commands
char infoADC[] = "Send commands to the ADC";
char infoLCD[] = "Send commands to the LCD";
char infoOS[] = "Look up timer info";

tCmdLineEntry g_psCmdTable[] = {
    { "lcd", lcdCall, infoADC },
    { "adc", adcCall, infoLCD },
		{ "os", osCall, infoOS },
};

//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************

// *************Interpreter.c**************
// EE445M/EE380L.6 Lab 4 solution
// high level OS functions
// 
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 3/9/17, valvano@mail.utexas.edu
#include <stdio.h>
#include "string.h" 
#include "ST7735.h"
#include "os.h"
#include "UART2.h"
#include "ff.h"
#include "loader.h"
#include "heap.h"

#define MAX_IN 20
#define NUM_CMD 5
char input[MAX_IN];
int num_instances;

Sema4Type Debug; 

void printFile(TCHAR *path){
	FIL File_Handle;
	FRESULT status;
	char data;
	UINT bytesRead;

	f_open(&File_Handle, path, FA_READ);
	do{
		status = f_read(&File_Handle, &data, 1, &bytesRead);
		if(bytesRead){
			UART_OutChar(data);
		}
	}while(status == FR_OK && bytesRead);
	f_close(&File_Handle);
}

static const ELFSymbol_t symtab[] = {
	{ "ST7735_Message", ST7735_Message }
};

void LoadProgram() {
	ELFEnv_t env = { symtab, 1 };
	//OS_bWait (&Debug);
	if (!exec_elf("Proc.axf", &env)) {
		UART_OutString("Load Successful");
	}
	//OS_bSignal (&Debug);
}

void cmdLine_Start(void){
	int task;
	OutCRLF();
	//OS_InitSemaphore (&Debug, 1);
	UART_OutString ("Choose a task:"); OutCRLF();
	UART_OutString ("1.) Print a File"); OutCRLF();
	UART_OutString ("2.) Malloc");  OutCRLF();
	UART_OutString ("3.) Free"); OutCRLF(); 
	UART_OutString ("4.) Load ELF"); OutCRLF(); OutCRLF();
	UART_OutString ("Option #: ");
	task = UART_InUDec();
//	while ((task < 1) || (task > 4)){
//			OutCRLF(); OutCRLF();
//			UART_OutString ("Invalid Option. Try again!"); OutCRLF(); 
//			UART_OutString ("Option #: ");
//			task = UART_InUDec();
//	}
//	OutCRLF(); OutCRLF();
//	switch (task){
//		case 1: 
//			UART_OutString(">Enter File Name: ");
//			UART_InString(input, MAX_IN);
//			OutCRLF();
//			printFile(input);
//			break;
//		case 2: 
//			break;
//		case 3: 
//			break;
//		case 4:
//			LoadProgram();
//			break;					
//	}
    LoadProgram();
	OutCRLF();
}



void Interpreter(void) {
		while(1){
			cmdLine_Start();
		}
}

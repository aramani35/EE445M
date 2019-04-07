//*****************************************************************************
//
// Lab4.c - user programs, File system, stream data onto disk
//*****************************************************************************

// Jonathan W. Valvano 3/7/17, valvano@mail.utexas.edu
// EE445M/EE380L.6 
// You may use, edit, run or distribute this file 
// You are free to change the syntax/organization of this file to do Lab 4
// as long as the basic functionality is simular
// 1) runs on your Lab 2 or Lab 3
// 2) implements your own eFile.c system with no code pasted in from other sources
// 3) streams real-time data from robot onto disk
// 4) supports multiple file reads/writes
// 5) has an interpreter that demonstrates features
// 6) interactive with UART input, and switch input

// LED outputs to logic analyzer for OS profile 
// PF1 is preemptive thread switch
// PF2 is periodic task
// PF3 is SW1 task (touch PF4 button)

// **********ST7735 TFT and SDC*******************
// ST7735
// Backlight (pin 10) connected to +3.3 V
// MISO (pin 9) unconnected
// SCK (pin 8) connected to PA2 (SSI0Clk)
// MOSI (pin 7) connected to PA5 (SSI0Tx)
// TFT_CS (pin 6) connected to PA3 (SSI0Fss)
// CARD_CS (pin 5) connected to PB0
// Data/Command (pin 4) connected to PA6 (GPIO), high for data, low for command
// RESET (pin 3) connected to PA7 (GPIO)
// VCC (pin 2) connected to +3.3 V
// Gnd (pin 1) connected to ground

#include "OS.h"
#include "../inc/tm4c123gh6pm.h"
#include "ST7735.h"
#include "ADC.h"
#include <string.h> 
#include <stdio.h>
#include <stdint.h>
#include "PLL.h"
#include "UART.h"
#include "diskio.h"
#include "ff.h"
#include "heap.h"
  
unsigned long NumCreated;   // number of foreground threads created

#define TIMESLICE 2*TIME_1MS  // thread switch time in system time units
 
//******** IdleTask  *************** 
// foreground thread, runs when no other work needed
// never blocks, never sleeps, never dies
// inputs:  none
// outputs: none
unsigned long Idlecount=0;
void IdleTask(void){ 
  while(1) { 
    Idlecount++;        // debugging 
  }
}


//******** Interpreter **************
// your intepreter from Lab 4 
// foreground thread, accepts input from UART port, outputs to UART port
// inputs:  none
// outputs: none
extern void Interpreter(void); 
// add the following commands, remove commands that do not make sense anymore
// 1) format 
// 2) directory 
// 3) print file
// 4) delete file
// execute   eFile_Init();  after periodic interrupts have started

static FATFS g_sFatFs;
FIL Handle,Handle2;
FRESULT MountFresult;
FRESULT Fresult;

void pdummy(void) { while(1); }
	
void pdummy1(void) { while(1); }

void pdummy2(void) { while(1); }	
	
int AddProcessTest(void) {
    OS_Init();
    EnableInterrupts();
    unsigned char tbuffer[100];
    for (int i = 0; i < 100; i++){
        tbuffer[i] = i;
    }
    
    OS_AddProcess(IdleTask, &tbuffer, &tbuffer,128, 4);
    OS_AddThread(pdummy,128,4);
    OS_AddThread(pdummy1,128,4);
    
    OS_AddProcess(IdleTask, &tbuffer, &tbuffer,128, 4);
    OS_AddThread(pdummy2,128,4);
    OS_Launch(TIME_2MS);
    
	return 0;
}

//*******************lab 5 main **********
int main(void){        // lab 5 realmain
  OS_Init();           // initialize, disable interrupts
  ST7735_FillScreen(0);                 // set screen to black

//********initialize communication channels
  OS_Fifo_Init(256);    
	
  
  Heap_Init();
	
  MountFresult = f_mount(&g_sFatFs, "", 0);
  if(MountFresult){
	ST7735_OutString(0, 0, "f_mount error", ST7735_Color565(0,0,255));
    while(1) {}
  }
  
  else {
	  ST7735_OutString(0, 0, "f_mount success", ST7735_Color565(0,0,255));
  }
	
  NumCreated = 0 ;
// create initial foreground threads
  NumCreated += OS_AddThread(&Interpreter,128,2); 
  NumCreated += OS_AddThread(&IdleTask,128,7);  // runs when nothing useful to do
 
	
  Heap_Init();
  
  OS_Launch(TIMESLICE); // doesn't return, interrupts enabled in here
  return 0;             // this never executes
}

void EnableInterrupts(void);

unsigned char buffer[512];
#define MAXBLOCKS 100
// Describe the error with text on the LCD and then stall.  If
// the error was caused by a mistake in configuring SSI0, then
// the LCD will probably not work.
void diskError(char *errtype, int32_t code, int32_t block){
//  ST7735_OutString(0, 0, "Error:", ST7735_Color565(255, 0, 0));
//  ST7735_OutString(7, 0, errtype, ST7735_Color565(255, 0, 0));
//  ST7735_OutString(0, 1, "Code:", ST7735_Color565(255, 0, 0));
//  ST7735_SetCursor(6, 1);
//  // ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
//  ST7735_OutUDec(code);
//  ST7735_OutString(0, 2, "Block:", ST7735_Color565(255, 0, 0));
//  ST7735_SetCursor(7, 2);
//  ST7735_OutUDec(block);
  while(1){};
}
// The simple unformatted test will destroy the formatting and
// erase everything on the SD card.
void SimpleUnformattedTest(void){ DSTATUS result; uint16_t block; int i; uint32_t n; uint32_t errors = 0;
  // simple test of eDisk
  result = disk_initialize(0);  // initialize disk
  if(result) diskError("disk_initialize", result, 0);
  n = 1;    // seed
  for(block = 0; block < MAXBLOCKS; block++){
    for(i=0; i<512; i++){
      n = (16807*n)%2147483647; // pseudo random sequence
      buffer[i] = 0xFF&n;
    }
    result = disk_write (0,buffer, block, 1);
    if(result) diskError("disk_write", result, block); // save to disk
  }
  n = 1;  // reseed, start over to get the same sequence
  for(block = 0; block < MAXBLOCKS; block++){
    result = disk_read (0,buffer, block,1);
    if(result) diskError("disk_read ", result, block); // read from disk
    for(i=0; i<512; i++){
      n = (16807*n)%2147483647; // pseudo random sequence
      if(buffer[i] != (0xFF&n)){
        errors = errors + 1;
      }
    }
  }
//  ST7735_OutString(0, 0, "Test done", ST7735_Color565(0, 255, 0));
//  ST7735_OutString(0, 1, "Mismatches:", ST7735_Color565(0, 255, 0));
//  ST7735_SetCursor(12, 1);
//  // ST7735_SetTextColor(ST7735_Color565(0, 255, 0));
//  ST7735_OutUDec(errors);
}
#define FILETESTSIZE 10000
void FileSystemTest(void){
  UINT successfulreads, successfulwrites;
  char c, d;
  int16_t x, y; int i; uint32_t n;
  c = 0;
  x = 0;
  y = 10;
  n = 1;    // seed
  Fresult = f_open(&Handle2, "testFile.txt", FA_CREATE_ALWAYS|FA_WRITE);
  if(Fresult){
    ST7735_OutString(0, 0, "testFile error", ST7735_Color565(0, 0, 255));
	ST7735_OutUDec(Fresult);
    while(1){};
  } else{
    for(i=0; i<FILETESTSIZE; i++){
      n = (16807*n)%2147483647; // pseudo random sequence
      c = ((n>>24)%10)+'0'; // random digit 0 to 9
      Fresult = f_write(&Handle2, &c, 1, &successfulwrites);
      if((successfulwrites != 1) || (Fresult != FR_OK)){
        ST7735_OutString(0, 0, "f_write error", ST7735_Color565(0, 0, 255));
        while(1){};
      }
    }
    Fresult = f_close(&Handle2);
    if(Fresult){
      ST7735_OutString(0, 0, "file2 f_close error", ST7735_Color565(0, 0, 255));
      while(1){};
    }
  }
  n = 1;  // reseed, start over to get the same sequence
  Fresult = f_open(&Handle, "testFile.txt", FA_READ);
  if(Fresult == FR_OK){
    ST7735_OutString(0, 0, "Opened testFile.txt", ST7735_Color565(0, 0, 255));
    for(i=0; i<FILETESTSIZE; i++){
      n = (16807*n)%2147483647; // pseudo random sequence
      d = ((n>>24)%10)+'0'; // expected random digit 0 to 9
      Fresult = f_read(&Handle, &c, 1, &successfulreads);
      if((successfulreads == 1) && (Fresult == FR_OK) && (c == d)){
        ST7735_DrawChar(x, y, c, ST7735_Color565(255, 255, 0), 0, 1);
        x = x + 6;
        if(x > 122){
          x = 0;                          // start over on the next line
          y = y + 10;
        }
        if(y > 150){
          y = 10;                         // the screen is full
        }
      } else{
        ST7735_OutString(0, 0, "f_read error", ST7735_Color565(0, 0, 255));
        while(1){};
      }

    }
  } else{
    ST7735_OutString(0, 0, "Error testFile.txt (  )", ST7735_Color565(255, 0, 0));
    ST7735_SetCursor(20, 0);
    // ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
    ST7735_OutUDec((uint32_t)Fresult);
    while(1){};
  }
  ST7735_OutString(0, 0, "file test passed    ", ST7735_Color565(255, 255, 255));
  Fresult = f_close(&Handle);
/*  Fresult = f_open(&Handle,"out.txt", FA_CREATE_ALWAYS|FA_WRITE);
  if(Fresult == FR_OK){
    ST7735_OutString(0, 0, "Opened out.txt", ST7735_Color565(0, 0, 255));
    c = 'A';
    x = 0;
    y = 10;
    Fresult = f_write(&Handle, &c, 1, &successfulreads);
    ST7735_DrawChar(x, y, c, ST7735_Color565(255, 255, 0), 0, 1);
    while((c <= 'Z') && (Fresult == FR_OK)){
      x = x + 6;
      c = c + 1;
      if(x > 122){
        x = 0;                          // start over on the next line
        y = y + 10;
      }
      if(y > 150){
        break;                          // the screen is full
      }
      Fresult = f_write(&Handle, &c, 1, &successfulreads);
      ST7735_DrawChar(x, y, c, ST7735_Color565(255, 255, 0), 0, 1);
    }
  } else{
    ST7735_OutString(0, 0, "Error out.txt (  )", ST7735_Color565(255, 0, 0));
    ST7735_SetCursor(15, 0);
    // ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
    ST7735_OutUDec((uint32_t)Fresult);
  }*/
}

const char inFilename[] = "test.txt";   // 8 characters or fewer
const char outFilename[] = "out.txt";   // 8 characters or fewer

int sdctestmain (void){
  UINT successfulreads, successfulwrites;
  uint8_t c, x, y;
  PLL_Init(Bus80MHz);    // 80 MHz
  ST7735_InitR(INITR_REDTAB);
  ST7735_FillScreen(0);                 // set screen to black
  EnableInterrupts();
//  SimpleUnformattedTest();              // comment this out if continuing to the advanced file system tests
  MountFresult = f_mount(&g_sFatFs, "", 0);
  if(MountFresult){
    ST7735_OutString(0, 0, "f_mount error", ST7735_Color565(0, 0, 255));
    while(1){};
  }
  FileSystemTest();                     // comment this out if file system works
  // open the file to be read
//  Fresult = f_open(&Handle, "Proc.axf", FA_READ);
//  if(Fresult != FR_OK){
//    ST7735_OutString(0, 0, "Proc.axf f_open error", ST7735_Color565(0, 0, 255));
//    while(1){
//    }
//  }
  Fresult = f_open(&Handle, inFilename, FA_READ);
  if(Fresult == FR_OK){
    ST7735_OutString(0, 0, "Opened ", ST7735_Color565(0, 255, 0));
    ST7735_OutString(7, 0, (char *)inFilename, ST7735_Color565(0, 255, 0));
    // get a character in 'c' and the number of successful reads in 'successfulreads'
    Fresult = f_read(&Handle, &c, 1, &successfulreads);
    x = 0;                              // start in the first column
    y = 10;                             // start in the second row
    while((Fresult == FR_OK) && (successfulreads == 1) && (y <= 130)){
      if(c == '\n'){
        x = 0;                          // go to the first column (this seems implied)
        y = y + 10;                     // go to the next row
      } else if(c == '\r'){
        x = 0;                          // go to the first column
      } else{                           // the character is printable, so print it
        ST7735_DrawChar(x, y, c, ST7735_Color565(255, 255, 255), 0, 1);
        x = x + 6;                      // go to the next column
        if(x > 122){                    // reached the right edge of the screen
          x = 0;                        // go to the first column
          y = y + 10;                   // go to the next row
        }
      }
      // get the next character in 'c'
      Fresult = f_read(&Handle, &c, 1, &successfulreads);
    }
    // close the file
    Fresult = f_close(&Handle);
  } else{
    // print the error code
    ST7735_OutString(0, 0, "Error          (  )", ST7735_Color565(255, 0, 0));
    ST7735_OutString(6, 0, (char *)inFilename, ST7735_Color565(255, 0, 0));
    ST7735_SetCursor(16, 0);
    // ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
    ST7735_OutUDec((uint32_t)Fresult);
  }

  // open the file to be written
  // Options:
  // FA_CREATE_NEW    - Creates a new file, only if it does not already exist.  If file already exists, the function fails.
  // FA_CREATE_ALWAYS - Creates a new file, always.  If file already exists, it is over-written.
  // FA_OPEN_ALWAYS   - Opens a file, always.  If file does not exist, the function creates a file.
  // FA_OPEN_EXISTING - Opens a file, only if it exists.  If the file does not exist, the function fails.
  Fresult = f_open(&Handle, outFilename, FA_WRITE|FA_OPEN_ALWAYS);
  if(Fresult == FR_OK){
    ST7735_OutString(0, 14, "Opened ", ST7735_Color565(0, 255, 0));
    ST7735_OutString(7, 14, (char *)outFilename, ST7735_Color565(0, 255, 0));
    // jump to the end of the file
    Fresult = f_lseek(&Handle, Handle.fsize);
    // write a message and get the number of successful writes in 'successfulwrites'
    Fresult = f_write(&Handle, "Another successful write.\r\n", 27, &successfulwrites);
    if(Fresult == FR_OK){
      // print the number of successful writes
      // expect: third parameter of f_write()
      ST7735_OutString(0, 15, "Writes:    @", ST7735_Color565(0, 255, 0));
      ST7735_SetCursor(8, 15);
      // ST7735_SetTextColor(ST7735_Color565(255, 255, 255));
      ST7735_OutUDec((uint32_t)successfulwrites);
      ST7735_SetCursor(13, 15);
      // print the byte offset from the start of the file where the writes started
      // expect: (third parameter of f_write())*(number of times this program has been run before)
      ST7735_OutUDec((uint32_t)(Handle.fptr - successfulwrites));
    } else{
      // print the error code
      ST7735_OutString(0, 15, "f_write() error (  )", ST7735_Color565(255, 0, 0));
      ST7735_SetCursor(17, 15);
      // ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
      ST7735_OutUDec((uint32_t)Fresult);
    }
    // close the file
    Fresult = f_close(&Handle);
  } else{
    // print the error code
    ST7735_OutString(0, 14, "Error          (  )", ST7735_Color565(255, 0, 0));
    ST7735_OutString(6, 14, (char *)outFilename, ST7735_Color565(255, 0, 0));
    ST7735_SetCursor(16, 14);
    // ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
    ST7735_OutUDec((uint32_t)Fresult);
  }
  while(1){};
}

// global so easier to see with the debugger
// Proper style would be to make these variables local to main.
int16_t statush; 
int16_t* ptr;
int16_t* p1;
int16_t* p2;
int16_t* p3;
uint8_t* q1;
uint8_t* q2;
uint8_t* q3;
uint8_t* q4;
uint8_t* q5;
uint8_t* q6;
int16_t maxBlockSize;
uint8_t* bigBlock;
heap_stats_t stats;

int heaptestmain(void){
  int16_t i;

  statush = Heap_Init();

  ptr = Heap_Malloc(sizeof(int16_t));
  *ptr = 0x1111;
  statush = Heap_Test();

  statush = Heap_Free(ptr);
  statush = Heap_Test();

  ptr = Heap_Malloc(1);
  statush = Heap_Test();

  statush = Heap_Free(ptr);
  statush = Heap_Test();

  p1 = (int16_t*) Heap_Malloc(1 * sizeof(int16_t));
  p2 = (int16_t*) Heap_Malloc(2 * sizeof(int16_t));
  p3 = (int16_t*) Heap_Malloc(3 * sizeof(int16_t));
  p1[0] = 0xAAAA;
  p2[0] = 0xBBBB;
  p2[1] = 0xBBBB;
  p3[0] = 0xCCCC;
  p3[1] = 0xCCCC;
  p3[2] = 0xCCCC;
  statush = Heap_Test();
  stats = Heap_Stats();

  statush = Heap_Free(p1);
  statush = Heap_Free(p3);
  statush = Heap_Test();

  statush = Heap_Free(p2);
  statush = Heap_Test();
  stats = Heap_Stats();

  for(i = 0; i <= HEAP_SIZE_WORDS; i++){
    ptr = Heap_Malloc(sizeof(int16_t));
  }
  statush = Heap_Test();
  stats = Heap_Stats();

  statush = Heap_Init();
  q1 = Heap_Malloc(1);
  q2 = Heap_Malloc(2);
  q3 = Heap_Malloc(3);
  q4 = Heap_Malloc(4);
  q5 = Heap_Malloc(5);
  statush = Heap_Test();

  *q1 = 0xDD;
  q6 = Heap_Realloc(q1, 6);
  statush = Heap_Test();
  stats = Heap_Stats();

  //q1 should point to freed space
  statush = Heap_Free(q1);
  statush = Heap_Test();

  for(i = 0; i < 6; i++){
    q6[i] = 0xEE;
  }
  q1 = Heap_Realloc(q6, 2);
  statush = Heap_Test();

  statush = Heap_Init();
  maxBlockSize = HEAP_SIZE_BYTES - 2 * sizeof(int16_t);
  bigBlock = Heap_Malloc(maxBlockSize);
  for(i = 0; i < maxBlockSize; i++){
    bigBlock[i] = 0xFF;
  }
  statush = Heap_Test();
  stats = Heap_Stats();
  statush = Heap_Free(bigBlock);
  bigBlock = Heap_Calloc(maxBlockSize);
  statush = Heap_Test();
  stats = Heap_Stats();
  for(;;){
  } 
}


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

// Button inputs
// PF0 is SW2 task 
// PF4 is SW1 button input

// Analog inputs
// PE3 sequencer 3, channel 3, J8/PE0, sampling in DAS(), software start
// PE0 timer-triggered sampling, channel 0, J5/PE3, 50 Hz, processed by Producer
//******Sensor Board I/O*******************
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

// HC-SR04 Ultrasonic Range Finder 
// J9X  Trigger0 to PB7 output (10us pulse)
// J9X  Echo0    to PB6 T0CCP0
// J10X Trigger1 to PB5 output (10us pulse)
// J10X Echo1    to PB4 T1CCP0
// J11X Trigger2 to PB3 output (10us pulse)
// J11X Echo2    to PB2 T3CCP0
// J12X Trigger3 to PC5 output (10us pulse)
// J12X Echo3    to PF4 T2CCP0

// Ping))) Ultrasonic Range Finder 
// J9Y  Trigger/Echo0 to PB6 T0CCP0
// J10Y Trigger/Echo1 to PB4 T1CCP0
// J11Y Trigger/Echo2 to PB2 T3CCP0
// J12Y Trigger/Echo3 to PF4 T2CCP0

// IR distance sensors
// J5/A0/PE3
// J6/A1/PE2
// J7/A2/PE1
// J8/A3/PE0  

// ESP8266
// PB1 Reset
// PD6 Uart Rx <- Tx ESP8266
// PD7 Uart Tx -> Rx ESP8266

// Free pins (debugging)
// PF3, PF2, PF1 (color LED)
// PD3, PD2, PD1, PD0, PC4

#include "OS.h"
#include "../inc/tm4c123gh6pm.h"
#include "ST7735.h"
#include "ADC.h"
#include "eDisk.h"
#include "eFile.h"
#include <string.h> 
#include <stdio.h>
#include <stdint.h>
#include "PLL.h"
#include "UART.h"

#define UARTPRINT 0

//*********Prototype for FFT in cr4_fft_64_stm32.s, STMicroelectronics
void cr4_fft_64_stm32(void *pssOUT, void *pssIN, unsigned short Nbin);

#define PF0  (*((volatile unsigned long *)0x40025004))
#define PF1  (*((volatile unsigned long *)0x40025008))
#define PF2  (*((volatile unsigned long *)0x40025010))
#define PF3  (*((volatile unsigned long *)0x40025020))
#define PF4  (*((volatile unsigned long *)0x40025040))
  
#define PD0  (*((volatile unsigned long *)0x40007004))
#define PD1  (*((volatile unsigned long *)0x40007008))
#define PD2  (*((volatile unsigned long *)0x40007010))
#define PD3  (*((volatile unsigned long *)0x40007020))

void PortD_Init(void){ 
  SYSCTL_RCGCGPIO_R |= 0x08;       // activate port D
  while((SYSCTL_PRGPIO_R&0x08)==0){};      
  GPIO_PORTD_DIR_R |= 0x0F;    // make PE3-0 output heartbeats
  GPIO_PORTD_AFSEL_R &= ~0x0F;   // disable alt funct on PD3-0
  GPIO_PORTD_DEN_R |= 0x0F;     // enable digital I/O on PD3-0
  GPIO_PORTD_PCTL_R = ~0x0000FFFF;
  GPIO_PORTD_AMSEL_R &= ~0x0F;;      // disable analog functionality on PD
}  
unsigned long NumCreated;   // number of foreground threads created
unsigned long NumSamples;   // incremented every sample
unsigned long DataLost;     // data sent by Producer, but not received by Consumer
unsigned long PIDWork;      // current number of PID calculations finished
unsigned long FilterWork;   // number of digital filter calculations finished

int Running;                // true while robot is running

#define TIMESLICE 2*TIME_1MS  // thread switch time in system time units
long x[64],y[64];           // input and output arrays for FFT
Sema4Type doFFT;            // set every 64 samples by DAS

long median(long u1,long u2,long u3){ 
long result;
  if(u1>u2)
    if(u2>u3)   result=u2;     // u1>u2,u2>u3       u1>u2>u3
      else
        if(u1>u3) result=u3;   // u1>u2,u3>u2,u1>u3 u1>u3>u2
        else      result=u1;   // u1>u2,u3>u2,u3>u1 u3>u1>u2
  else 
    if(u3>u2)   result=u2;     // u2>u1,u3>u2       u3>u2>u1
      else
        if(u1>u3) result=u1;   // u2>u1,u2>u3,u1>u3 u2>u1>u3
        else      result=u3;   // u2>u1,u2>u3,u3>u1 u2>u3>u1
  return(result);
}
//------------ADC2millimeter------------
// convert 12-bit ADC to distance in 1mm
// it is known the expected range is 100 to 800 mm
// Input:  adcSample 0 to 4095 
// Output: distance in 1mm
long ADC2millimeter(long adcSample){
  if(adcSample<494) return 799; // maximum distance 80cm
  return (268130/(adcSample-159));  
}
long Distance3;     // distance in mm on IR3
uint32_t Index3;    // counts to 64 samples
long x1,x2,x3;
void DAS(void){ 
long output;  
  PD0 ^= 0x01;
  x3=x2; x2= x1;  // MACQ
  x1 = ADC_In();  // channel set when calling ADC_Init
  PD0 ^= 0x01;
  if(Index3<64){
    output = median(x1,x2,x3); // 3-wide median filter
    Distance3 = ADC2millimeter(output);
    FilterWork++;        // calculation finished
    x[Index3] = Distance3;
    Index3++;
    if(Index3==64){
      OS_Signal(&doFFT);
    }
  }
  PD0 ^= 0x01;
}
void DSP(void){ 
unsigned long DCcomponent;   // 12-bit raw ADC sample, 0 to 4095
  OS_InitSemaphore(&doFFT,0); 
  while(1) { 
    OS_Wait(&doFFT); // wait for 64 samples
    PD2 = 0x04;
    cr4_fft_64_stm32(y,x,64);  // complex FFT of last 64 ADC values
    PD2 = 0x00;
    Index3 = 0; // take another buffer
    DCcomponent = y[0]&0xFFFF; // Real part at frequency 0, imaginary part should be zero
    ST7735_Message(1,0,"IR3 (mm) =",DCcomponent);    
  }
}
char Name[8]="robot0";
//******** Robot *************** 
// foreground thread, accepts data from producer
// inputs:  none
// outputs: none
void Robot(void){   
unsigned long data;      // ADC sample, 0 to 1023
unsigned long voltage;   // in mV,      0 to 3300
unsigned long distance;  // in mm,      100 to 800
unsigned long time;      // in 10msec,  0 to 1000 
  OS_ClearMsTime();    
  DataLost = 0;          // new run with no lost data 
  OS_Fifo_Init(256);
  ST7735_FillScreen(0);
  #if UARTPRINT
  UART_OutString("Robot running...");
  #else
  printf("Robot running...");
  #endif
  eFile_Init();
  eFile_RedirectToFile(Name); // robot0, robot1,...,robot7
  #if UARTPRINT
  UART_OutString("time(sec)\tdata(volts)\tdistance(mm)");
  OutCRLF();
  #else
  printf("time(sec)\t\tdata(volts)\t\tdistance(mm)\n\r");
  #endif
	do{
    PIDWork++;    // performance measurement
    time=OS_MsTime();            // 10ms resolution in this OS
    data = OS_Fifo_Get();        // 1000 Hz sampling get from producer
    voltage = (300*data)/1024;   // in mV
    distance = ADC2millimeter(data);
	#if UARTPRINT
	UART_OutUDec(time/100);
	UART_OutChar('.');
	UART_OutUDec(time%100);
	UART_OutChar('\t');
	UART_OutUDec(voltage/1000);
	UART_OutChar('.');
	UART_OutUDec(voltage%1000);
	UART_OutChar('\t');
	UART_OutUDec(distance);
	OutCRLF();
	#else
    printf("%0u.%02u\t%0u.%03u \t%5u\n\r",time/100,time%100,voltage/1000,voltage%1000,distance);
	#endif
  }
  while(time < 200);       // change this to mean 2 seconds
  eFile_EndRedirectToFile();
  ST7735_Message(0,1,"IR0 (mm) =",distance); 
  #if UARTPRINT
  UART_OutString("done.");
  OutCRLF();
  #else
  printf("done.\n\r");
  #endif
  Name[5] = (Name[5]+1)&0xF7; // 0 to 7
  Running = 0;             // robot no longer running
  OS_Kill();
}
  
//************SW1Push*************
// Called when SW1 Button pushed
// background threads execute once and return
void SW1Push(void){
  if(Running==0){
    Running = 1;  // prevents you from starting two robot threads
    NumCreated += OS_AddThread(&Robot,128,1);  // start a 2 second run
  }
}
//************SW2Push*************
// Called when SW2 Button pushed
// background threads execute once and return
void SW2Push(void){

}



//******** Producer *************** 
// The Producer in this lab will be called from your ADC ISR
// A timer runs at 1 kHz, started by your ADC_Collect
// The timer triggers the ADC, creating the 1 kHz sampling
// Your ADC ISR runs when ADC data is ready
// Your ADC ISR calls this function with a 10-bit sample 
// sends data to the Robot, runs periodically at 1 kHz
// inputs:  none
// outputs: none
void Producer(unsigned long data){  
  if(Running){
    if(OS_Fifo_Put(data)){     // send to Robot
      NumSamples++;
    } else{ 
      DataLost++;
    } 
  }
}
 
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

//*******************lab 4 main **********
int realmain(void){        // lab 4 realmain
  OS_Init();           // initialize, disable interrupts
  Running = 0;         // robot not running
  DataLost = 0;        // lost data between producer and consumer
  NumSamples = 0;
  PortD_Init();  // user debugging profile
  
//********initialize communication channels
  OS_Fifo_Init(256);    
  ADC_Collect(0, 50, &Producer); // start ADC sampling, channel 0, PE3, 50 Hz
  ADC_Open(3);  // sequencer 3, channel 3, PE0, sampling in DAS()
  OS_AddPeriodicThread(&DAS,10*TIME_1MS,1); // 100Hz real time sampling of PE0

//*******attach background tasks***********
  OS_AddSW1Task(&SW1Push,2);    // PF4, SW1
  OS_AddSW2Task(&SW2Push,3);   // PF0
  OS_AddPeriodicThread(disk_timerproc,10*TIME_1MS,5);

  NumCreated = 0 ;
// create initial foreground threads
  NumCreated += OS_AddThread(&Interpreter,128,2); 
  NumCreated += OS_AddThread(&DSP,128,1); 
  NumCreated += OS_AddThread(&IdleTask,128,7);  // runs when nothing useful to do
 
  OS_Launch(TIMESLICE); // doesn't return, interrupts enabled in here
  return 0;             // this never executes
}

//+++++++++++++++++++++++++DEBUGGING CODE++++++++++++++++++++++++
// ONCE YOUR RTOS WORKS YOU CAN COMMENT OUT THE REMAINING CODE
// 
//*****************test project 0*************************
// This is the simplest configuration, 
// Just see if you can import your OS
// no UART interrupts
// no SYSTICK interrupts
// no timer interrupts
// no switch interrupts
// no ADC serial port or LCD output
// no calls to semaphores
unsigned long Count1;   // number of times thread1 loops
unsigned long Count2;   // number of times thread2 loops
unsigned long Count3;   // number of times thread3 loops
unsigned long Count4;   // number of times thread4 loops
unsigned long Count5;   // number of times thread5 loops
void Thread1(void){
  Count1 = 0;          
  for(;;){
    PD0 ^= 0x01;       // heartbeat
    Count1++;
  }
}
void Thread2(void){
  Count2 = 0;          
  for(;;){
    PD1 ^= 0x02;       // heartbeat
    Count2++;
  }
}
void Thread3(void){
  Count3 = 0;          
  for(;;){
    PD2 ^= 0x04;       // heartbeat
    Count3++;
  }
}

int Testmain0(void){  // Testmain0
  OS_Init();          // initialize, disable interrupts
  PortD_Init();       // profile user threads
  NumCreated = 0 ;
  NumCreated += OS_AddThread(&Thread1,128,1); 
  NumCreated += OS_AddThread(&Thread2,128,2); 
  NumCreated += OS_AddThread(&Thread3,128,3); 
  // Count1 Count2 Count3 should be equal or off by one at all times
  OS_Launch(TIME_2MS); // doesn't return, interrupts enabled in here
  return 0;            // this never executes
}
//*****************test project 1*************************
unsigned char buffer[512];
#define MAXBLOCKS 4096
void diskError(char* errtype, unsigned long n){
  #if UARTPRINT
  UART_OutString(errtype);
  UART_OutString(" disk error ");
  UART_OutUDec(n);
  #else
  printf(errtype);
  printf(" disk error %u",n);
  #endif  
  OS_Kill();
}
void TestDisk(void){  DSTATUS result;  unsigned short block;  int i; unsigned long n;
  // simple test of eDisk
  ST7735_OutString(0, 1, "eDisk test      ", ST7735_WHITE);
  #if UARTPRINT
  UART_OutString("\n\rEE445M/EE380L, Lab 4 eDisk test");
  OutCRLF();
  #else
  printf("\n\rEE445M/EE380L, Lab 4 eDisk test\n\r");
  #endif
  result = eDisk_Init(0);  // initialize disk
  if(result) diskError("eDisk_Init",result);
  #if UARTPRINT
  UART_OutString("Writing blocks");
  OutCRLF();
  #else
  printf("Writing blocks\n\r");
  #endif
  n = 1;    // seed
  for(block = 0; block < MAXBLOCKS; block++){
    for(i=0;i<512;i++){
      n = (16807*n)%2147483647 + block; // pseudo random sequence
      buffer[i] = 0xFF&n;        
    }
    PD3 = 0x08;     // PD3 high for 100 block writes
    if(eDisk_WriteBlock(buffer,block))diskError("eDisk_WriteBlock",block); // save to disk
    PD3 = 0x00;     
  }  
  #if UARTPRINT
  UART_OutString("Reading blocks");
  OutCRLF();
  #else
  printf("Reading blocks\n\r");
  #endif
  n = 1;  // reseed, start over to get the same sequence
  for(block = 0; block < MAXBLOCKS; block++){
    PD2 = 0x04;     // PF2 high for one block read
    if(eDisk_ReadBlock(buffer,block))diskError("eDisk_ReadBlock",block); // read from disk
    PD2 = 0x00;
    for(i=0;i<512;i++){
      n = (16807*n)%2147483647 + block; // pseudo random sequence
      if(buffer[i] != (0xFF&n)){
		#if UARTPRINT
		UART_OutString("Read data not correct, block=");
		UART_OutUDec(block);
		UART_OutString(", i=");
		UART_OutUDec(i);
		UART_OutString(", expected ");
		UART_OutUDec((0xFF&n));
		UART_OutString(" read ");
		UART_OutUDec(buffer[i]);
		OutCRLF();
		#else
        printf("Read data not correct, block=%u, i=%u, expected %u, read %u\n\r",block,i,(0xFF&n),buffer[i]);
		#endif
        OS_Kill();
      }      
    }
  }  
  
  #if UARTPRINT
  UART_OutString("Successful test of ");
  UART_OutUDec(MAXBLOCKS);
  UART_OutString(" blocks");
  OutCRLF();
  #else
  printf("Successful test of %u blocks\n\r",MAXBLOCKS);
  #endif
  ST7735_OutString(0, 1, "eDisk successful", ST7735_YELLOW);
  Running=0; // launch again
  OS_Kill();
}
void RunTest(void){
  NumCreated += OS_AddThread(&TestDisk,128,1);  
}
//************SW1Push*************
// Called when SW1 Button pushed
// background threads execute once and return
void SW1Push1(void){
  if(Running==0){
    Running = 1;  // prevents you from starting two test threads
    NumCreated += OS_AddThread(&TestDisk,128,1);  // test eDisk
  }
}
//******************* test main1 **********
// SYSTICK interrupts, period established by OS_Launch
// Timer interrupts, period established by first call to OS_AddPeriodicThread
int testmain1(void){   // testmain1
  OS_Init();           // initialize, disable interrupts
  PortD_Init();
  ST7735_InitR(INITR_REDTAB);
  ST7735_FillScreen(0);                 // set screen to black
  #if UARTPRINT
  UART_Init();
  #endif
//*******attach background tasks***********
  OS_AddPeriodicThread(&disk_timerproc,10*TIME_1MS,0);   // time out routines for disk
  OS_AddSW1Task(&SW1Push1,2);
  
  NumCreated = 0 ;
  Running = 1; 
// create initial foreground threads
  NumCreated += OS_AddThread(&TestDisk,128,1);  
  NumCreated += OS_AddThread(&IdleTask,128,3); 
  OS_AddSW1Task(&SW1Push1,2);    // PF4, SW1
 
  OS_Launch(10*TIME_1MS); // doesn't return, interrupts enabled in here
  return 0;               // this never executes
}

//*****************test project 2*************************

void TestFile(void){   int i; char data; 
  ST7735_FillScreen(0);
  unsigned char testBuf[512];
  #if UARTPRINT
  UART_OutString("\n\rEE445M/EE380L, Lab 4 eFile test");
  OutCRLF();
  #else
  printf("\n\rEE445M/EE380L, Lab 4 eFile test\n\r");
  #endif
  ST7735_OutString(0, 1, "eFile test      ", ST7735_WHITE);
  // simple test of eFile
  if(eFile_Init())              diskError("eFile_Init",0); 
  if(eFile_Format())            diskError("eFile_Format",0); 
  eFile_Directory(&UART_OutChar);
  if(eFile_Create("file1"))     diskError("eFile_Create",0);
  if(eFile_WOpen("file1"))      diskError("eFile_WOpen",0);
  for(i=0;i<1000;i++){
//	if (i < 512) {
//		testBuf[i] = 'a'+i%26;
//	}
    if(eFile_Write('a'+i%26))   diskError("eFile_Write",i);
    if(i%52==51){
      if(eFile_Write('\n'))     diskError("eFile_Write",i);  
      if(eFile_Write('\r'))     diskError("eFile_Write",i);
    }
  }
  if(eFile_WClose())            diskError("eFile_WClose",0);
  eFile_Directory(&UART_OutChar);
  
//  eDisk_WriteBlock(testBuf, 2);
//  for (int i = 0; i < 512; i++) {
//	  testBuf[i] = 0;
//  }
  eDisk_ReadBlock(testBuf, 2);
  
  if(eFile_ROpen("file1"))      diskError("eFile_ROpen",0);
  for(i=0;i<1000;i++){
    if(eFile_ReadNext(&data))   diskError("eFile_ReadNext",i);
    UART_OutChar(data);
  }
  if(eFile_Delete("file1"))     diskError("eFile_Delete",0);
  eFile_Directory(&UART_OutChar);
  if(eFile_Close())             diskError("eFile_Close",0);
  
  #if UARTPRINT
  UART_OutString("Successful test of creating a file");
  OutCRLF();
  #else
  printf("Successful test of creating a file\n\r");
  #endif
  
  ST7735_OutString(0, 1, "eFile successful", ST7735_YELLOW);
  Running=0; // launch again
  OS_Kill();
}
//************SW1Push2*************
// Called when SW1 Button pushed
// background threads execute once and return
void SW1Push2(void){
  if(Running==0){
    Running = 1;  // prevents you from starting two test threads
    NumCreated += OS_AddThread(&TestFile,128,1);  // test eFile
  }
}
//******************* test main2 **********
// SYSTICK interrupts, period established by OS_Launch
// Timer interrupts, period established by first call to OS_AddPeriodicThread
int testmain2(void){ 
  OS_Init();           // initialize, disable interrupts
  ST7735_InitR(INITR_REDTAB);
  PortD_Init();
  Running = 1; 

//*******attach background tasks***********
  OS_AddPeriodicThread(&disk_timerproc,10*TIME_1MS,0);   // time out routines for disk
  OS_AddSW1Task(&SW1Push1,2);    // PF4, SW1
  OS_AddSW2Task(&SW1Push2,2);    // PF0, SW2
  NumCreated = 0 ;
// create initial foreground threads
  NumCreated += OS_AddThread(&TestFile,128,1);  
  NumCreated += OS_AddThread(&IdleTask,128,3); 
 
  OS_Launch(10*TIME_1MS); // doesn't return, interrupts enabled in here
  return 0;               // this never executes
}

void diskError1(char *errtype, int32_t code, int32_t block){
  ST7735_DrawString(0, 0, "Error:", ST7735_Color565(255, 0, 0));
  ST7735_DrawString(7, 0, errtype, ST7735_Color565(255, 0, 0));
  ST7735_DrawString(0, 1, "Code:", ST7735_Color565(255, 0, 0));
//  ST7735_SetCursor(6, 1);
//  ST7735_SetTextColor(ST7735_Color565(255, 0, 0));
//  ST7735_OutUDec(code);
  ST7735_DrawString(0, 2, "Block:", ST7735_Color565(255, 0, 0));
//  ST7735_SetCursor(7, 2);
//  ST7735_OutUDec(block);
	
  while(1){};
}


// The simple unformatted test will destroy the formatting and
// erase everything on the SD card.
void SimpleUnformattedTest(void){ DSTATUS result; uint16_t block; int i; uint32_t n; uint32_t errors = 0;
  // simple test of eDisk
  
  result = eFile_Init();//eDisk_init(0);  // initialize disk
  if(result) diskError1("disk_initialize", result, 0);
  n = 1;    // seed
  for(block = 0; block < MAXBLOCKS; block++){
    for(i=0; i<512; i++){
      n = (16807*n)%2147483647; // pseudo random sequence
      buffer[i] = 0xFF&n;
    }
    result = eDisk_Write (0,buffer, block, 1);
    if(result) diskError1("disk_write", result, block); // save to disk
  }
  n = 1;  // reseed, start over to get the same sequence
  for(block = 0; block < MAXBLOCKS; block++){
    result = eDisk_Read(0,buffer, block,1);
    if(result) diskError1("disk_read ", result, block); // read from disk
    for(i=0; i<512; i++){
      n = (16807*n)%2147483647; // pseudo random sequence
      if(buffer[i] != (0xFF&n)){
        errors = errors + 1;
      }
    }
  }
  ST7735_DrawString(0, 0, "Test done", ST7735_Color565(0, 255, 0));
  ST7735_DrawString(0, 1, "Mismatches:", ST7735_Color565(0, 255, 0));
//  ST7735_SetCursor(12, 1);
//  ST7735_SetTextColor(ST7735_Color565(0, 255, 0));
//  ST7735_OutUDec(errors);
}


void formatTest(void){ DSTATUS result; uint16_t block; int i; uint32_t n; uint32_t errors = 0;
    // simple test of eDisk
  
    result = eFile_Init();//eDisk_init(0);  // initialize disk
    if(result) diskError1("disk_initialize", result, 0);
    result = eFile_Format();
    if(result) diskError1("disk_initialize", result, 0);
    n = 1;    // seed
//  for(block = 0; block < MAXBLOCKS; block++){
    for(i=0; i<512; i++){
      buffer[i] = i;
    }
//    result = eDisk_Write (0,buffer, block, 1);
//    if(result) diskError1("disk_write", result, block); // save to disk
//  }
//  n = 1;  // reseed, start over to get the same sequence
    for(block = 0; block < MAXBLOCKS; block++){
        result = eDisk_Read(0,buffer, block,1);
        if(result) diskError1("disk_read ", result, block); // read from disk
        for(i=0; i<512; i++){
            n = buffer[i];
            }
        }
  }

void quickTest(void){ DSTATUS result; uint16_t block; int i; uint32_t n; uint32_t errors = 0;
  // simple test of eDisk
  while(1) {
    result = eFile_Init();//eDisk_init(0);  // initialize disk
    if(result) diskError1("disk_initialize", result, 0);
    result = eFile_Format();
    if(result) diskError1("disk_initialize", result, 0);
    n = 1;    // seed
    
    for(i=0; i<512; i++) buffer[i] = i;
    result = eDisk_Write(0,buffer, 1, 1);
    if(result) diskError1("disk_write", result, block); // save to disk

    for(i=0; i<512; i++) buffer[i] = i*2;
    result = eDisk_Write(0,buffer, 2, 1);
    if(result) diskError1("disk_write", result, block); // save to disk

    
    result = eDisk_Read(0,buffer, 1,1);
    if(result) diskError1("disk_read ", result, block); // read from disk
    for(i=0; i<512; i++){
        n = buffer[i];
    }
    
    result = eDisk_Read(0,buffer, 2,1);
    if(result) diskError1("disk_read ", result, block); // read from disk
    for(i=0; i<512; i++){
        n = buffer[i];
    }
  }
}

void badFileTest(void){
    int result;
    result = eFile_Init();//eDisk_init(0);  // initialize disk
    if(result) diskError1("disk_initialize", result, 0);
    result = eFile_Format();
    if(result) diskError1("disk_initialize", result, 0);
    
    result = eFile_WOpen("test.txt"); // file not made
	 if(result) diskError1("WOpen 1", result, 0);
    result = eFile_WOpen("test.txt"); // file not made
     if(result) diskError1("WOpen 2", result, 0);
	
    result = eFile_ROpen("test.txt"); // file not made
	 if(result) diskError1("ROpen nonexistent", result, 0);
	
    result = eFile_ROpen("test.txt"); // file not made
     if(result) diskError1("ROpen nonexistent", result, 0);
	
	
    result = eFile_Delete("test.txt"); // delete none existed file
 if(result) diskError1("delete nonexistent", result, 0);
    
    result = eFile_Create("test.txt"); // Make file
	 if(result) diskError1("error making file", result, 0);
    eFile_Create("test.txt"); // Make file twice
	 if(result) diskError1("error making file twice", result, 0);
    
    result = eFile_WOpen("tes.txt"); // Open wrong file
	 if(result) diskError1("error opening wrong file", result, 0);
    result = eFile_ROpen("tes.txt"); // Open wrong file
	 if(result) diskError1("error opening wrong file", result, 0);
}
  
  
void writeTest(void){
	PortD_Init();
	unsigned long n;
	while(1){
        PD3 ^= 0x08;     // PD3 high for 100 block writes
        for(uint8_t block = 0; block < MAXBLOCKS; block++){
            for(int i=0; i<512; i++){
                n = (16807*n)%2147483647; // pseudo random sequence
                buffer[i] = 0xFF&n;        
            }
            if(eDisk_WriteBlock(buffer,block))
                diskError("eDisk_WriteBlock",block); // save to disk   
        }
		PD3 ^= 0x08;  
    }
}

void readTest(void){
	PortD_Init();
	unsigned long n;
	while(1){
        PD3 ^= 0x08;     // PD3 high for 100 block reads
        for(uint8_t block = 0; block < MAXBLOCKS; block++){
            if(eDisk_ReadBlock(buffer,block))
                diskError("eDisk_WriteBlock",block);  
        }
		PD3 ^= 0x08;  
    }
}
  
// bandwidth is the maximum data flow in bytes/second that can be processed by the system
// 8   for 10,000,000 bps fast mode, used during disk I/O
int testermain(void){     // testermain
    UINT successfulreads, successfulwrites;
    uint8_t c, x, y;
    PLL_Init();    // 80 MHz
    ST7735_InitR(INITR_REDTAB);
    ST7735_FillScreen(0);                 // set screen to black
	UART_Init();
    EnableInterrupts();
//    quickTest();
//    formatTest();
	badFileTest();
    //SimpleUnformattedTest();
    while(1){}
}

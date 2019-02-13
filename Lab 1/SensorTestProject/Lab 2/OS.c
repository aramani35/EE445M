// OS.c
// TI TM4C123 microcontroller
// Tariq Muhanna and Abhishek Ramani
// Executes a software task at a periodic rate


#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "OS.h"

int32_t StartCritical(void);
void EndCritical(int32_t primask);

#define PF1         (*((volatile uint32_t *)0x40025008))
#define NUMTHREADS 30
#define STACKSIZE   100      // number of 32-bit words in stack
static uint32_t num_threads = 0;


struct TCB {
	int32_t *savedSP;
	unsigned used;
	struct TCB *next;
  struct TCB *prev;
	int16_t id;
	uint16_t sleep;
	uint8_t priority;
	uint8_t blocked;
};
typedef struct TCB TCBtype;

TCBtype *runPT = 0;
TCBtype TCBs[NUMTHREADS];
unsigned int numTasks = 0;
int16_t CurrentID;
int32_t Stacks[NUMTHREADS][STACKSIZE];


void (*PeriodicTask)(void);   // user function
uint32_t OS_counter = 0;

// ***************** Timer5_Init ****************
// Activate Timer5 interrupts to run user task periodically
// Inputs:  task is a pointer to a user function
//          period in units (1/clockfreq)
// Outputs: none
int OS_AddPeriodicThread(void(*task)(void), unsigned long period, unsigned long priority){
  SYSCTL_RCGCTIMER_R |= 0x20;      // 0) activate timer5                    // wait for completion
	PeriodicTask = task;             // user function
  TIMER5_CTL_R &= ~0x00000001;     // 1) disable timer5A during setup
  TIMER5_CFG_R = 0x00000004;       // 2) configure for 16-bit timer mode
  TIMER5_TAMR_R = 0x00000002;      // 3) configure for periodic mode, default down-count settings
  TIMER5_TAILR_R = ((80000000/period)-1);       // 4) reload value
  TIMER5_TAPR_R = 49;              // 5) 1us timer5A
  TIMER5_ICR_R = 0x00000001;       // 6) clear timer5A timeout flag
  TIMER5_IMR_R |= 0x00000001;      // 7) arm timeout interrupt
  NVIC_PRI23_R = (NVIC_PRI23_R&0xFFFFFF00)|(0x20 << priority); // 8) priority 2
  NVIC_EN2_R |= 0x10000000;        // 9) enable interrupt 19 in NVIC
  // vector number 108, interrupt number 92
  TIMER5_CTL_R |= 0x00000001;      // 10) enable timer5A
// interrupts enabled in the main program after all devices initialized
	return 0;
}

void Timer5A_Handler(void){
	PF1 = 0x2;                         // Signal for entering the interrupt handler
  TIMER5_ICR_R = TIMER_ICR_TATOCINT; // Acknowledge timer0A timeout
	OS_counter++;                      // increment global counter
  (*PeriodicTask)();                 // Execute periodic task
	PF1 = 0x0;                         // Signal for exiting the interrupt handler
}

//long* OS_initStack(long *sp, void(*task)(void)){
//	*(sp)   = (long)task;
//	*(--sp) = (long)0x12121212L;
//  *(--sp) = (long)0x11111111L;
//  *(--sp) = (long)0x10101010L;
//  *(--sp) = (long)0x09090909L;
//  *(--sp) = (long)0x08080808L;
//  *(--sp) = (long)0x07070707L;
//  *(--sp) = (long)0x06060606L;
//  *(--sp) = (long)0x05050505L;
//  *(--sp) = (long)0x04040404L;
//  *(--sp) = (long)0x03030303L;
//  *(--sp) = (long)0x02020202L;
//  *(--sp) = (long)0x01010101L;
//  *(--sp) = (long)0x00000000L;

//	return sp;
//}

void SetInitialStack(int i){
  TCBs[i].savedSP = &Stacks[i][STACKSIZE-16]; // thread stack pointer
  Stacks[i][STACKSIZE-1] = 0x01000000;   // thumb bit
  Stacks[i][STACKSIZE-3] = 0x14141414;   // R14
  Stacks[i][STACKSIZE-4] = 0x12121212;   // R12
  Stacks[i][STACKSIZE-5] = 0x03030303;   // R3
  Stacks[i][STACKSIZE-6] = 0x02020202;   // R2
  Stacks[i][STACKSIZE-7] = 0x01010101;   // R1
  Stacks[i][STACKSIZE-8] = 0x00000000;   // R0
  Stacks[i][STACKSIZE-9] = 0x11111111;   // R11
  Stacks[i][STACKSIZE-10] = 0x10101010;  // R10
  Stacks[i][STACKSIZE-11] = 0x09090909;  // R9
  Stacks[i][STACKSIZE-12] = 0x08080808;  // R8
  Stacks[i][STACKSIZE-13] = 0x07070707;  // R7
  Stacks[i][STACKSIZE-14] = 0x06060606;  // R6
  Stacks[i][STACKSIZE-15] = 0x05050505;  // R5
  Stacks[i][STACKSIZE-16] = 0x04040404;  // R4
}

// 1 = free 
// 0 = busy
int add_thread() {
	// loop through all threads to find one that is free
	for (int i=0; i < NUMTHREADS; i++) {
	  // if the thread is free, set it to work
		if (TCBs[i].used) {
				TCBs[i].used = 0; // set to busy
				return i;
		}
	}
	// if no threads are free, return -1
	return -1; 
}


int find_prev(int thread) {
  // loop through all threads in forward direction 
	// to find one that is busy/alive   
	for (int i = (thread+NUMTHREADS-1)%NUMTHREADS; i != thread; i = (i+NUMTHREADS-1)%NUMTHREADS ) {
		if (!TCBs[i].used) 
				return i;
	}
	// if error, return -1
	return -1;
}


int find_next(int thread) {
	// loop through all threads in forward direction 
	// to find one that is busy/alive
	for (int i = (thread+1)%NUMTHREADS; i != thread; i = (i+1)%NUMTHREADS ) {
		if (!TCBs[i].used)
				return i;
	}
	// if error, return -1
	return -1;
}

//******** OS_AddThread *************** 
// add a foregound thread to the scheduler
// Inputs: pointer to a void/void foreground task
//         number of bytes allocated for its stack
//         priority, 0 is highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// stack size must be divisable by 8 (aligned to double word boundary)
// In Lab 2, you can ignore both the stackSize and priority fields
// In Lab 3, you can ignore the stackSize fields
int OS_AddThread(void(*task)(void), 
   unsigned long stackSize, unsigned long priority){

//void OS_AddThread(long *sp, void (*task)(void)){
//	TCBtype *tcb, *last;
//	tcb = &TCBs[numTasks++];
//	tcb->savedSP = OS_initStack(sp, task);
//	tcb->used = 1;

//	if(runPT){
//		last = runPT;
//		while(last->next != runPT) last = last->next;
//		last->next = tcb;
//		tcb->next = runPT;
//	} else {
//		runPT = tcb;
//		tcb->next = tcb;
//	}
//	
	int32_t status, thread, prev;
	 
	status = StartCritical();
	if(num_threads == 0) {
		add_thread();
		TCBs[0].next = &TCBs[0]; // 0 points to 0
		runPT = &TCBs[0];     // thread 0 will run first
	}
	else {
		thread = add_thread();
		prev = find_prev(thread);
		TCBs[thread].next = TCBs[prev].next;
		TCBs[prev].next = &TCBs[thread];
	}
  TCBs[thread].used = 1;
	TCBs[thread].id = thread;

	SetInitialStack(thread); 
	Stacks[thread][STACKSIZE-2] = (int32_t)(task); // PC
	num_threads++;

	SetInitialStack(thread);		//initialize stack
	Stacks[num_threads][STACKSIZE-2] = (int32_t)(task); //  set PC for Task
	EndCritical(status);

	return 0;
}




void ContextSwitch(void){
	NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV;//NVIC_PENDSVSET; // lowest priority
}

void OS_Yield(void){
	ContextSwitch();
}

void Sustick_handler(void) {
	ContextSwitch();
}

void OS_launch(void){
//	StartOS;
}


// ***************** OS_ClearPeriodicTime ****************
// Resets global counter
// Inputs:  none
// Outputs: none
void OS_ClearPeriodicTime(void){
	OS_counter = 0;
}

// ***************** OS_ReadPeriodicTime ****************
// Reads global counter
// Inputs:  none
// Outputs: none
uint32_t OS_ReadPeriodicTime(void){
	return OS_counter;
}

void OS_Init(void){} 

// ******** OS_InitSemaphore ************
// initialize semaphore 
// input:  pointer to a semaphore
// output: none
void OS_InitSemaphore(Sema4Type *semaPt, long value){} 

// ******** OS_Wait ************
// decrement semaphore 
// Lab2 spinlock
// Lab3 block if less than zero
// input:  pointer to a counting semaphore
// output: none
void OS_Wait(Sema4Type *semaPt){}  

// ******** OS_Signal ************
// increment semaphore 
// Lab2 spinlock
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a counting semaphore
// output: none
void OS_Signal(Sema4Type *semaPt){} 

// ******** OS_bWait ************
// Lab2 spinlock, set to 0
// Lab3 block if less than zero
// input:  pointer to a binary semaphore
// output: none
void OS_bWait(Sema4Type *semaPt){}  

// ******** OS_bSignal ************
// Lab2 spinlock, set to 1
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a binary semaphore
// output: none
void OS_bSignal(Sema4Type *semaPt){} 
	


//******** OS_Id *************** 
// returns the thread ID for the currently running thread
// Inputs: none
// Outputs: Thread ID, number greater than zero 
unsigned long OS_Id(void){return 0;}


//******** OS_AddSW1Task *************** 
// add a background task to run whenever the SW1 (PF4) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal	 OS_AddThread
// This task does not have a Thread ID
// In labs 2 and 3, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW1Task(void(*task)(void), unsigned long priority){return 0;}

//******** OS_AddSW2Task *************** 
// add a background task to run whenever the SW2 (PF0) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is highest, 5 is lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed user task will run to completion and return
// This task can not spin block loop sleep or kill
// This task can call issue OS_Signal, it can call OS_AddThread
// This task does not have a Thread ID
// In lab 2, this function can be ignored
// In lab 3, this command will be called will be called 0 or 1 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW2Task(void(*task)(void), unsigned long priority){return 0;}

// ******** OS_Sleep ************
// place this thread into a dormant state
// input:  number of msec to sleep
// output: none
// You are free to select the time resolution for this function
// OS_Sleep(0) implements cooperative multitasking
void OS_Sleep(unsigned long sleepTime){} 

// ******** OS_Kill ************
// kill the currently running thread, release its TCB and stack
// input:  none
// output: none
void OS_Kill(void){} 

// ******** OS_Suspend ************
// suspend execution of currently running thread
// scheduler will choose another thread to execute
// Can be used to implement cooperative multitasking 
// Same function as OS_Sleep(0)
// input:  none
// output: none
void OS_Suspend(void){}
 
// ******** OS_Fifo_Init ************
// Initialize the Fifo to be empty
// Inputs: size
// Outputs: none 
// In Lab 2, you can ignore the size field
// In Lab 3, you should implement the user-defined fifo size
// In Lab 3, you can put whatever restrictions you want on size
//    e.g., 4 to 64 elements
//    e.g., must be a power of 2,4,8,16,32,64,128
void OS_Fifo_Init(unsigned long size){}

// ******** OS_Fifo_Put ************
// Enter one data sample into the Fifo
// Called from the background, so no waiting 
// Inputs:  data
// Outputs: true if data is properly saved,
//          false if data not saved, because it was full
// Since this is called by interrupt handlers 
//  this function can not disable or enable interrupts
int OS_Fifo_Put(unsigned long data){ return 0;} 

// ******** OS_Fifo_Get ************
// Remove one data sample from the Fifo
// Called in foreground, will spin/block if empty
// Inputs:  none
// Outputs: data 
unsigned long OS_Fifo_Get(void){ return 0;}

// ******** OS_Fifo_Size ************
// Check the status of the Fifo
// Inputs: none
// Outputs: returns the number of elements in the Fifo
//          greater than zero if a call to OS_Fifo_Get will return right away
//          zero or less than zero if the Fifo is empty 
//          zero or less than zero if a call to OS_Fifo_Get will spin or block
long OS_Fifo_Size(void){ return 0;}

// ******** OS_MailBox_Init ************
// Initialize communication channel
// Inputs:  none
// Outputs: none
void OS_MailBox_Init(void){}

// ******** OS_MailBox_Send ************
// enter mail into the MailBox
// Inputs:  data to be sent
// Outputs: none
// This function will be called from a foreground thread
// It will spin/block if the MailBox contains data not yet received 
void OS_MailBox_Send(unsigned long data){}

// ******** OS_MailBox_Recv ************
// remove mail from the MailBox
// Inputs:  none
// Outputs: data received
// This function will be called from a foreground thread
// It will spin/block if the MailBox is empty 
unsigned long OS_MailBox_Recv(void){ return 0;}

// ******** OS_Time ************
// return the system time 
// Inputs:  none
// Outputs: time in 12.5ns units, 0 to 4294967295
// The time resolution should be less than or equal to 1us, and the precision 32 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_TimeDifference have the same resolution and precision 
unsigned long OS_Time(void){ return 0;}

// ******** OS_TimeDifference ************
// Calculates difference between two times
// Inputs:  two times measured with OS_Time
// Outputs: time difference in 12.5ns units 
// The time resolution should be less than or equal to 1us, and the precision at least 12 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_Time have the same resolution and precision 
unsigned long OS_TimeDifference(unsigned long start, unsigned long stop){return 0;}

// ******** OS_ClearMsTime ************
// sets the system time to zero (from Lab 1)
// Inputs:  none
// Outputs: none
// You are free to change how this works
void OS_ClearMsTime(void){}

// ******** OS_MsTime ************
// reads the current time in msec (from Lab 1)
// Inputs:  none
// Outputs: time in ms units
// You are free to select the time resolution for this function
// It is ok to make the resolution to match the first call to OS_AddPeriodicThread
unsigned long OS_MsTime(void){return 0;}

//******** OS_Launch *************** 
// start the scheduler, enable interrupts
// Inputs: number of 12.5ns clock cycles for each time slice
//         you may select the units of this parameter
// Outputs: none (does not return)
// In Lab 2, you can ignore the theTimeSlice field
// In Lab 3, you should implement the user-defined TimeSlice field
// It is ok to limit the range of theTimeSlice to match the 24-bit SysTick
void OS_Launch(unsigned long theTimeSlice){}


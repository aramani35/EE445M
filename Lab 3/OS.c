// OS.c
// TI TM4C123 microcontroller
// Tariq Muhanna and Abhishek Ramani
// Executes a software task at a periodic rate


#undef _OS_ASSEMBLY_

#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "OS.h"
#include "board.h"
#include "EdgeTriggered.h"
#include "LinkedList.h"
#include "PLL.h"
#include "ST7735.h"
#include "UART.h"


#define NVIC_ST_CTRL_R          (*((volatile uint32_t *)0xE000E010))
#define NVIC_ST_CTRL_CLK_SRC    0x00000004  // Clock Source
#define NVIC_ST_CTRL_INTEN      0x00000002  // Interrupt enable
#define NVIC_ST_CTRL_ENABLE     0x00000001  // Counter mode
#define NVIC_ST_RELOAD_R        (*((volatile uint32_t *)0xE000E014))
#define NVIC_ST_CURRENT_R       (*((volatile uint32_t *)0xE000E018))
#define NVIC_INT_CTRL_R         (*((volatile uint32_t *)0xE000ED04))
#define NVIC_INT_CTRL_PENDSTSET 0x04000000  // Set pending SysTick interrupt
#define NVIC_SYS_PRI3_R         (*((volatile uint32_t *)0xE000ED20))  // Sys. Handlers 12 to 15 Priority

// function definitions in osasm.s
void OS_DisableInterrupts(void);// Disable interrupts
void OS_EnableInterrupts(void); // Enable interrupts
int32_t StartCritical(void);
void EndCritical(int32_t primask);
void StartOS(void);
void (*Periodic1Task)(void);    // Periodic task 1
void (*Periodic2Task)(void);    // Periodic task 2
void (*SWOneTask)(void);        // SW1 task to execute
void (*SWTwoTask)(void);        // SW2 task to execute

//#define TIMESLICE
#define OSFIFOSIZE  250
#define NUMTHREADS  10           // maximum number of threads
#define STACKSIZE   100         // number of 32-bit words in stack
#define PF1                     (*((volatile uint32_t *)0x40025008))
#define PF4                     (*((volatile uint32_t *)0x40025040))
#define PE1                     (*((volatile unsigned long *)0x40024008))


/* VARIABLES FOR MEASURING JITTER */
volatile static unsigned long Last; // previous
volatile static unsigned long Release; // previous
uint32_t Periodic1TaskPeriod;
uint32_t Periodic2TaskPeriod;
unsigned long MaxJitter;              // largest time jitter between interrupts in usec for task 1
unsigned long MaxJitter2;             // largest time jitter between interrupts in usec for task 2
#define JITTERSIZE 64
unsigned long const JitterSize=JITTERSIZE;
unsigned long JitterHistogram[JITTERSIZE]={0,};
unsigned long JitterHistogram2[JITTERSIZE]={0,};
int delay;

/* DEFINES FOR CHANGING IMPLEMENTATION */
// #define SPINLOCK 
// #define ROUNDROBIN

static uint32_t num_threads = 0;
uint8_t periodic_threads_count = 0;
void SW1Task(void);
void SW2Task(void);
Sema4Type SW1sem;
Sema4Type SW2sem;
TCBtype *runPT = 0;
TCBtype *nextPT = 0;
TCBtype TCBs[NUMTHREADS];
unsigned int numTasks = 0;
int16_t CurrentID;
uint32_t time_slice;
uint32_t OS_ms_count = 0;
int32_t Stacks[NUMTHREADS][STACKSIZE];

TCBtype *head_sleep;      // head of sleeping thread list
TCBtype *tail_sleep;      // tail of sleeping thread list
TCBtype *head_kill;       // head of kill thread list
TCBtype *tail_kill;       // tail of kill thread list
TCBtype *head_reAdd;
TCBtype *tail_reAdd;

/* TCBtype list that stores head of each priority linked list */
TCBtype *pri_lists[NUMPRIS] = { 0 };    // initialize all elements to 0
uint32_t pri_count[NUMPRIS] = { 0 };	// initialize all elements to 0

uint32_t OS_counter1 = 0;
uint32_t OS_counter2 = 0;

//Register 4: Interrupt 0-31 Set Enable (EN0), offset 0x100
//Register 5: Interrupt 32-63 Set Enable (EN1), offset 0x104
//Register 6: Interrupt 64-95 Set Enable (EN2), offset 0x108
//Register 7: Interrupt 96-127 Set Enable (EN3), offset 0x10C


void WTimer0A_Init(void){          // for sleep
    SYSCTL_RCGCWTIMER_R |= 0x01;   //  activate WTIMER0
    delay = 0; 
    WTIMER0_CTL_R = (WTIMER0_CTL_R&~0x0000001F);    // disable Wtimer0A during setup
    WTIMER0_CFG_R = 0x00000004;    // configure for 32-bit timer mode
    WTIMER0_TAMR_R = 0x00000002;   // configure for periodic mode, default down-count settings
    WTIMER0_TAPR_R = 0;            // prescale value for trigger
    WTIMER0_ICR_R = 0x00000001;    // 6) clear WTIMER0A timeout flag
    WTIMER0_TAILR_R = TIME_1MS-1;    // start value for trigger
    NVIC_PRI23_R = (NVIC_PRI23_R&0xFF00FFFF)| (0 << 21); //set priority 
    WTIMER0_IMR_R = (WTIMER0_IMR_R&~0x0000001F)|0x00000001;    // enable timeout interrupts
    WTIMER0_CTL_R |= 0x00000001;   // enable Wtimer0A 32-b, periodic
    NVIC_EN2_R = 0x40000000;              // enable interrupt 94 in NVIC
}

void WideTimer0A_Handler(){
	WTIMER0_ICR_R |= 0x01;
    // Decrementing sleep counter for all sleeping threads
    TCBtype *node = head_sleep;
    // Remove connection from sleeping list to main list 
	while(node){
        if((node->next != 0 && node->next->sleep_state == 0) || node->next == node) {
                node->next = 0;
            }

		node->sleepCT--;                    // Dec sleep count
        TCBtype *next_node = node->next;    // Load in next node

            
        if(node->sleepCT <= 0){             // Wake up thread once sleep counter = 0
            node->sleep_state = 0;          // Turn of sleep state
            removeFromList(node, &head_sleep, &tail_sleep); // Remove from sleep list
            num_threads++;                                  // Increment active threads
            OS_AddThreadPri(node, node->priority);          // Add thread back to priority list
        } 
        node = next_node;                   // Transistion to next node
    }
}

void WTimer5A_Init(void){
	SYSCTL_RCGCWTIMER_R |= 0x20;   //  activate WTIMER5
	delay = 0;
	WTIMER5_CTL_R = 0x00000000;    // disable Wtimer5A during setup
    WTIMER5_CFG_R = 0x00000004;    // configure for 32-bit timer mode
    WTIMER5_TAMR_R = 0x00000002;   // configure for periodic mode, default down-count settings
    WTIMER5_TAPR_R = 0;            // prescale value for trigger
    WTIMER5_ICR_R = 0x00000001;    // 6) clear WTIMER5A timeout flag
    WTIMER5_TAILR_R = 0xFFFFFFFF-1;  // start value for trigger
    NVIC_PRI26_R = (NVIC_PRI26_R&0xFFFFFF00)|0x00000000; // 8) priority 1
    NVIC_EN3_R = 0x00000100;       // 9) enable interrupt 104 in NVIC
//    WTIMER5_IMR_R = 0x00000001;    // enable timeout interrupts
    WTIMER5_CTL_R |= 0x00000001;   // enable Wtimer5A 64-b, periodic, no interrupts
}

void WTimer4A_Init(void){
	SYSCTL_RCGCWTIMER_R |= 0x10;   //  activate WTIMER4
	delay = 0;
	WTIMER4_CTL_R = 0x00000000;    // disable Wtimer4A during setup
    WTIMER4_CFG_R = 0x00000004;    // configure for 32-bit timer mode
    WTIMER4_TAMR_R = 0x00000002;   // configure for periodic mode, default down-count settings
    WTIMER4_TAPR_R = 0;            // prescale value for trigger
    WTIMER4_ICR_R = 0x00000001;    // 6) clear WTIMER4A timeout flag
    WTIMER4_TAILR_R = 0x80000-1;   // trigger every 1 ms
    NVIC_PRI25_R = (NVIC_PRI25_R&0xFF00FFFF)|0x00000000; // 8) priority 0
    NVIC_EN3_R = 1<<6;             // 9) enable interrupt 102 in NVIC
    WTIMER4_IMR_R = 0x00000001;  // enable timeout interrupts
    WTIMER4_CTL_R |= 0x00000001;   // enable Wtimer4A 64-b, periodic, no interrupts
}

void WideTimer4A_Handler(void){
    WTIMER4_ICR_R |= 0x01;
    OS_ms_count++;
}

void Timer4A_Init(void){          //Periodic Task 1
	SYSCTL_RCGCTIMER_R |= 0x10;   //  activate TIMER4
	delay = 0;
	TIMER4_CTL_R = 0x00000000;    // disable timer4A during setup
    TIMER4_CFG_R = 0x00000000;             // configure for 32-bit timer mode
    TIMER4_TAMR_R = 0x00000002;   // configure for periodic mode, default down-count settings
    TIMER4_TAPR_R = 0;            // prescale value for trigger
    TIMER4_ICR_R = 0x00000001;    // 6) clear TIMER4A timeout flag
    TIMER4_IMR_R = 0x00000001;    // enable timeout interrupts
}

void Timer5_Init(void){
    SYSCTL_RCGCTIMER_R |= 0x20;      // 0) activate timer5                    // wait for completion
    delay = 0;
    TIMER5_CTL_R &= ~0x00000001;     // 1) disable timer5A during setup
    TIMER5_CFG_R = 0x00000000;       // 2) configure for 32-bit timer mode
    TIMER5_TAMR_R = 0x00000002;      // 3) 1-SHOT mode
    TIMER5_TAPR_R = 0;               // 5) bus clock resolution
    TIMER5_ICR_R = 0x00000001;       // 6) clear timer5A timeout flag
    TIMER5_IMR_R = 0x00000001;    // enable timeout interrupts
}

void dummy1(void) {
    while(1){}
}

// ******** OS_Init ************
// initialize operating system, disable interrupts until OS_Launch
// initialize OS controlled I/O: systick, 50 MHz PLL
// input:  none
// output: none
void OS_Init(void){
	Last = 1;
    Release = 1;
    OS_DisableInterrupts();
    UART_Init();              // initialize UART
    MaxJitter = 0;
    MaxJitter2 = 0;
    PLL_Init(Bus80MHz);
    Output_Init();
    WTimer0A_Init(); // sleep
    OS_InitSemaphore(&SW1sem, 0);
    OS_InitSemaphore(&SW2sem, 0);
    EdgeCounter_PF4_Init();
    Timer4A_Init();
    Timer5_Init();
    WTimer4A_Init();
   	WTimer5A_Init();
    NVIC_ST_CTRL_R = 0;                     // disable SysTick during setup
    NVIC_ST_CURRENT_R = 0;                  // any write to current clears it
    NVIC_SYS_PRI3_R =(NVIC_SYS_PRI3_R&0x00FFFFFF)|0xE0000000; // SysTick priority 6
    NVIC_SYS_PRI3_R =(NVIC_SYS_PRI3_R&0xFF00FFFF)|0x00E00000; // PendSV priority 7

	for (int i = 0; i < NUMTHREADS; i++){   // initialize all the threads as free
		TCBs[i].used = 1;
	} 
//    OS_AddThread(&dummy1, 128, 7);
    OS_AddThread(&SW1Task,128, 3);
    OS_AddThread(&SW2Task,128, 3);

}


// ***************** Timer5_Init ****************
// Activate WTimer5 interrupts to run user task periodically
// Inputs:  task is a pointer to a user function
//          period in units (1/clockfreq)
// Outputs: none
int OS_AddPeriodicThread(void(*task)(void), unsigned long period, unsigned long priority){
    long status = StartCritical();
    if(periodic_threads_count >= 2){
		EndCritical(status);
		return 0;
	}
    else if(periodic_threads_count== 0){
        periodic_threads_count++;
        OS_counter1 = 0;
        Periodic1Task = task;          // user function
        TIMER4_TAILR_R = (period)-1;    // start value for trigger
        Periodic1TaskPeriod = period;
        NVIC_PRI17_R = (NVIC_PRI17_R&0xFF00FFFF)| (priority << 21); //set priority
        NVIC_EN2_R = 1<<6;              // enable interrupt 70 in NVIC
        TIMER4_CTL_R |= 0x00000001;   // enable timer2A 32-b, periodic, no interrupts
		EndCritical(status);
		return 1;
	}
    else{
        periodic_threads_count++;
        OS_counter2 = 0;
        Periodic2Task = task;          // user function
        TIMER5_TAILR_R = (period-1);       // 4) reload value
        Periodic2TaskPeriod = period;
        NVIC_PRI23_R = (NVIC_PRI23_R&0xFFFFFF00)|(priority << 5); // 8) priority 2
        NVIC_EN2_R |= 0x10000000;        // 9) enable interrupt 19 in NVIC
        // vector number 108, interrupt number 92
        TIMER5_CTL_R |= 0x00000001;      // 10) enable timer5A
        EndCritical(status);
        return 1;
    }
}


void Jitter(void){
    ST7735_Message(0,0,"Jitter 0.1us=",MaxJitter);
	ST7735_Message(0,1,"Jitter 0.1us=",MaxJitter2);
}


// Periodic task 1
    // increments OS_counter1
    // calculates jiiter
    // omplements periodic task 1
void Timer4A_Handler(){
	//PF1 ^= 0x02;
	//PF1 ^= 0x02;
	TIMER4_ICR_R |= 0x01;
    unsigned static long LastTime;      // time at previous ADC sample
    unsigned long thisTime = OS_Time(); // time at current ADC sample
    long jitter;                        // time between measured and expected, in us
	Periodic1Task();
    OS_counter1++;
	if(OS_counter1 > 1){
        unsigned long diff = OS_TimeDifference(LastTime,thisTime);
      if(diff>Periodic1TaskPeriod){
        jitter = (diff-Periodic1TaskPeriod+4)/8;     // in 0.1 usec
      }else{
        jitter = (Periodic1TaskPeriod-diff+4)/8;     // in 0.1 usec
      }
      if(jitter > MaxJitter){
        MaxJitter = jitter;             // in usec
      }                                 // jitter should be 0
      if(jitter >= JitterSize){
        jitter = JITTERSIZE-1;
      }
      JitterHistogram[jitter]++; 
    }
    LastTime = thisTime;
    
	//PF1 ^= 0x02;
}


// Periodic task 2
    // increments OS_counter2
    // calculates jiiter
    // omplements periodic task 2
void Timer5A_Handler(){
	//PF1 ^= 0x02;
	//PF1 ^= 0x02;
	TIMER5_ICR_R |= 0x01;
    unsigned static long LastTime;      // time at previous ADC sample
    unsigned long thisTime = OS_Time(); // time at current ADC sample
    long jitter;                        // time between measured and expected, in us
    
	Periodic2Task();
    OS_counter2++;
    if(OS_counter2 > 1){
        unsigned long diff = OS_TimeDifference(LastTime,thisTime);
      if(diff>Periodic1TaskPeriod){
        jitter = (diff-Periodic2TaskPeriod+4)/8;     // in 0.1 usec
      }else{
        jitter = (Periodic2TaskPeriod-diff+4)/8;     // in 0.1 usec
      }
      if(jitter > MaxJitter2){
        MaxJitter2 = jitter;             // in usec
      }                                 // jitter should be 0
      if(jitter >= JitterSize){
        jitter = JITTERSIZE-1;
      }
      JitterHistogram2[jitter]++; 
    }
    LastTime = thisTime;
	//PF1 ^= 0x02;
}



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


int removeThreadPri(uint32_t priority) {
  TCBtype *next, *prev;
	
	pri_count[priority]--;
	
	prev = runPT->prev;
	next = runPT->next;
	
	if(pri_count[priority] > 0) {
		prev->next = next;
		next->prev = prev;
	} 
	else {
		prev = 0;
		next = 0;
	}
//	runPT->prev = 0;
	pri_lists[priority] = prev;
	
	return 1;
}


int OS_AddThreadPri(TCBtype *threadPT, uint32_t priority) {
	TCBtype *next, *prev, *curr;
		
	if(pri_count[priority] == 0) {
		threadPT->prev = threadPT;
		threadPT->next = threadPT;
		pri_lists[priority] = threadPT;
	}		
	else {
        curr = pri_lists[priority];
        for(int i=0; i<pri_count[priority]-1; i++){
            curr = curr->next;
        }
        prev = curr;
//		prev = pri_lists[priority];
		next = prev->next;
		threadPT->prev = prev;
		threadPT->next = next;
		prev->next = threadPT;
		next->prev = threadPT;
	}	
	pri_count[priority]++;	
	
	return 1;
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

long status = StartCritical();
	TCBtype *thread;
	int numThread;
	for(numThread = 0; numThread < NUMTHREADS; numThread++){
		if(TCBs[numThread].used == 1){
			break;
		}
	}
	if(numThread == NUMTHREADS){
		EndCritical(status);
		return 0;
	}
	thread = &TCBs[numThread];
	thread->id = CurrentID;	//Current ID is incremented forever for different IDs
	thread->sleepCT = 0;
    thread->sleep_state = 0;
    thread->used = 0;
    thread->priority = priority;
    thread->blocked = 0;
    if(num_threads == 0) {
//        add_thread(); // idk if nesscesary
		TCBs[0].next = &TCBs[0];    // 0 points to 0
        TCBs[0].prev = &TCBs[0];
        pri_lists[priority] = &TCBs[0];
        pri_count[priority]++;
		runPT = &TCBs[0];
    }
    else {
        OS_AddThreadPri(thread, priority);
        //linkThread(runPT, thread, num_threads); // RR
    }
    num_threads++;
	SetInitialStack(numThread);		//initialize stack
	Stacks[numThread][STACKSIZE-2] = (int32_t)(task); //  set PC for Task
	CurrentID+=1;
	EndCritical(status);
	return 1;
}




void ContextSwitch(void){
	NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV;//NVIC_PENDSVSET; // lowest priority
}

void OS_Yield(void){
	ContextSwitch();
}




///******** OS_Launch ***************
// start the scheduler, enable interrupts
// Inputs: number of 20ns clock cycles for each time slice
//         (maximum of 24 bits)
// Outputs: none (does not return)
void OS_Launch(unsigned long theTimeSlice){
  time_slice = theTimeSlice;
  NVIC_ST_RELOAD_R = theTimeSlice - 1; // reload value
  NVIC_ST_CTRL_R = 0x00000007; // enable, core clock and interrupt arm
  OS_EnableInterrupts();
  StartOS();                   // start on the first task
}


// ***************** OS_ClearPeriodicTime ****************
// Resets global counter
// Inputs:  none
// Outputs: none
void OS_ClearPeriodicTime(void){
	OS_counter1 = 0;
}

// ***************** OS_ReadPeriodicTime ****************
// Reads global counter
// Inputs:  none
// Outputs: none
uint32_t OS_ReadPeriodicTime(void){
	return OS_counter1;
}



// ******** OS_InitSemaphore ************
// initialize semaphore 
// input:  pointer to a semaphore
// output: none
void OS_InitSemaphore(Sema4Type *semaPt, long value) {
	semaPt->Value = value;
	semaPt->head_blocked_list = 0;
	semaPt->tail_blocked_list = 0;
} 

// ******** OS_Wait ************
// decrement semaphore 
// Lab2 spinlock
// Lab3 block if less than zero
// input:  pointer to a counting semaphore
// output: none
void OS_Wait(Sema4Type *semaPt) {
	OS_DisableInterrupts();
	#ifdef SPINLOCK
	while (semaPt->Value <= 0) {
		OS_EnableInterrupts();
//        OS_Suspend();       // run thread switcher
		OS_DisableInterrupts();
	}
	
	semaPt->Value -= 1;
	
	
	#else
	semaPt->Value -= 1;
	if (semaPt->Value < 0) {
		TCBtype *blockedThread = runPT;
		blockedThread->blocked = 1;
		
		// remove from active list, add to semaphore list
		num_threads--;
        removeThreadPri(blockedThread->priority);
        addToList(blockedThread, &(semaPt->head_blocked_list), &(semaPt->tail_blocked_list));

        NVIC_ST_CURRENT_R = 0;
        NVIC_INT_CTRL_R = 0x04000000;
	}
	
	#endif
	OS_EnableInterrupts();
}

// ******** OS_Signal ************
// increment semaphore 
// Lab2 spinlock
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a counting semaphore
// output: none
void OS_Signal(Sema4Type *semaPt) {
	long status;
    status = StartCritical();
	
	#ifdef SPINLOCK
	semaPt->Value += 1;

	#else
    semaPt->Value += 1;

	if(semaPt->Value <= 0){
        //semaPt->Value += 1;
        TCBtype *unblockedThread = semaPt->head_blocked_list;
        removeFromList(unblockedThread, &(semaPt->head_blocked_list), &(semaPt->tail_blocked_list));
        num_threads++;
        OS_AddThreadPri(unblockedThread, unblockedThread->priority);
		
		if (unblockedThread->priority < runPT->priority) {
			EndCritical(status);
			OS_Suspend();
			status = StartCritical();
		}
	}
	#endif
	EndCritical(status);
} 

// ******** OS_bWait ************
// Lab2 spinlock, set to 0
// Lab3 block if less than zero
// input:  pointer to a binary semaphore
// output: none
void OS_bWait(Sema4Type *semaPt) {
	OS_DisableInterrupts();
	#ifdef SPINLOCK
	while (semaPt->Value <= 0) {
		OS_EnableInterrupts();
//        OS_Suspend();       // run thread switcher
		OS_DisableInterrupts();
	}
	
	semaPt->Value -= 1;
	
	
	#else
	semaPt->Value -= 1;
	if (semaPt->Value < 0) {
		TCBtype *blockedThread = runPT;
		blockedThread->blocked = 1;
		
		// remove from active list, add to semaphore list
		num_threads--;
        removeThreadPri(blockedThread->priority);
        addToList(blockedThread, &(semaPt->head_blocked_list), &(semaPt->tail_blocked_list));

        NVIC_ST_CURRENT_R = 0;
        NVIC_INT_CTRL_R = 0x04000000;
	}
	
	#endif
	OS_EnableInterrupts();
}  

// ******** OS_bSignal ************
// Lab2 spinlock, set to 1
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a binary semaphore
// output: none
void OS_bSignal(Sema4Type *semaPt) {
	long status;
    status = StartCritical();
	
	#ifdef SPINLOCK
	semaPt->Value += 1;

	#else
    semaPt->Value += 1;

	if(semaPt->Value <= 0){
        //semaPt->Value += 1;
        TCBtype *unblockedThread = semaPt->head_blocked_list;
        removeFromList(unblockedThread, &(semaPt->head_blocked_list), &(semaPt->tail_blocked_list));
        num_threads++;
        OS_AddThreadPri(unblockedThread, unblockedThread->priority);
		
		if (unblockedThread->priority < runPT->priority) {
			EndCritical(status);
			OS_Suspend();
			status = StartCritical();
		}
	}
	#endif
	EndCritical(status);
} 
	


//******** OS_Id *************** 
// returns the thread ID for the currently running thread
// Inputs: none
// Outputs: Thread ID, number greater than zero 
unsigned long OS_Id(void){
    return runPT->id;
}


void portFDisable(void){
    GPIO_PORTF_IM_R &= ~0x10;            // Disarm PF4 interrupt
    GPIO_PORTF_IM_R &= ~0x01;            // Disarm PF0 interrupt
}
void portFEnable(void){
    GPIO_PORTF_IM_R &= 0x10;            // Arm PF4 interrupt
    GPIO_PORTF_IM_R &= 0x01;            // Arm PF0 interrupt
}
//-------------------------try2-------------------------
volatile int sw1Last;
volatile int sw2Last;
//----------------------------------try3-----------

void SW1Task(void){
    sw1Last = GPIO_PORTF_DATA_R&0x10;
    while(1){
        OS_Wait(&SW1sem);
        if(sw1Last){
            (*SWOneTask)();
        }
        OS_Sleep(10);
        sw1Last = GPIO_PORTF_DATA_R&0x10;
        GPIO_PORTF_IM_R |= 0x10;            // rearm PF4 interrupt
        GPIO_PORTF_ICR_R = 0x10;            // acknowledge flag4
    }
}

void SW2Task(void){
    sw1Last = GPIO_PORTF_DATA_R&0x01;
    while(1){
        OS_Wait(&SW2sem);
        if(sw2Last){
            (*SWTwoTask)();
        }
        OS_Sleep(10);
        sw2Last = GPIO_PORTF_DATA_R&0x01;
        GPIO_PORTF_IM_R |= 0x01;            // rearm PF1 interrupt
        GPIO_PORTF_ICR_R = 0x01;            // acknowledge flag1
    }
}


void GPIOPortF_Handler(void){
    if(GPIO_PORTF_RIS_R&0x10){
        GPIO_PORTF_ICR_R = 0x10;            // acknowledge flag4
        OS_Signal(&SW1sem);
        GPIO_PORTF_IM_R &= ~0x10;            // Disarm PF4 interrupt
    }
    else if(GPIO_PORTF_RIS_R&0x01){
        GPIO_PORTF_ICR_R = 0x01;            // acknowledge flag4
        OS_Signal(&SW2sem);
        GPIO_PORTF_IM_R &= ~0x01;            // Disarm PF4 interrupt
    }
    
}



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
int OS_AddSW1Task(void(*task)(void), unsigned long priority){
    SWOneTask = task;
    NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|(priority << 21); // (g) priority 2
    GPIO_PORTF_IM_R |= 0x10;      // (f) arm interrupt on PF4
	return 1;
}

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
int OS_AddSW2Task(void(*task)(void), unsigned long priority){
    SWTwoTask = task;
    NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|(priority << 21); // (g) priority 2
    GPIO_PORTF_IM_R |= 0x01;      // (f) arm interrupt on PF0
	return 1;
}



void OS_pendSVTrigger(void){
//    NVIC_ST_CURRENT_R = 0;
//    NVIC_INT_CTRL_R = 0x04000000;
    NVIC_ST_RELOAD_R = time_slice - 1;           // reset timeslice after kill/slice
	NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV;    // 0x10000000 Trigger PendSV
}


// ******** OS_Sleep ************
// place this thread into a dormant state
// input:  number of msec to sleep
// output: none
// You are free to select the time resolution for this function
// OS_Sleep(0) implements cooperative multitasking

//void OS_Sleep(unsigned long sleepTime){
//    long status = StartCritical();
//	TCBtype *sleeping_thread = runPT;
////    runPT = runPT->prev;
//	sleeping_thread->sleepCT = sleepTime;
//    sleeping_thread->sleep_state = 1;
//    num_threads--;
//	unlinkThread(sleeping_thread, num_threads);
//	addToList(sleeping_thread, &head_sleep, &tail_sleep);
//	EndCritical(status);
////    OS_pendSVTrigger();                         // thread is asleep so switch threads 

//} 

void OS_Sleep(unsigned long sleepTime){

    long status = StartCritical();
	TCBtype *sleeping_thread = runPT;
//    runPT = runPT->prev;
	sleeping_thread->sleepCT = sleepTime+1;
    sleeping_thread->sleep_state = 1;
    num_threads--;
	#ifdef ROUNDROBIN
	unlinkThread(sleeping_thread, num_threads);
	addToList(sleeping_thread, &head_sleep, &tail_sleep);
	#else
	removeThreadPri(sleeping_thread->priority);
	addToList(sleeping_thread, &head_sleep, &tail_sleep);
	#endif
	OS_Suspend();
    EndCritical(status);
} 


// ******** OS_Kill ************
// kill the currently running thread, release its TCB and stack
// input:  none
// output: none
void OS_Kill(void){
    long status = StartCritical();
    runPT->used = 1;                  // set thread to free/not busy
    num_threads--;
	#ifdef ROUNDROBIN
	unlinkThread(runPT, num_threads);
    addToList(runPT, &head_kill, &tail_kill);
	#else
	removeThreadPri(runPT->priority);
	#endif
    addToList(runPT, &head_kill, &tail_kill);
    OS_Suspend();
	EndCritical(status);
}

// ******** OS_Suspend ************
// suspend execution of currently running thread
// scheduler will choose another thread to execute
// Can be used to implement cooperative multitasking 
// Same function as OS_Sleep(0)
// input:  none
// output: none
void OS_Suspend(void){
	#ifdef ROUNDROBIN
	NVIC_INT_CTRL_R = 0x10000000;
	#else
	NVIC_ST_CURRENT_R = 0;	// Clear SysTick counter
	NVIC_INT_CTRL_R = 0x04000000; // Trigger SysTick
	#endif
}
 
// Two-pointer implementation of the receive FIFO
// can hold 0 to RXFIFOSIZE-1 elements
uint32_t fifolimit = OSFIFOSIZE;
uint16_t static OS_Fifo [OSFIFOSIZE];
volatile uint16_t *OSPutPt, *OSGetPt;
Sema4Type FifoAvailable;

// ******** OS_Fifo_Init ************
// Initialize the Fifo to be empty
// Inputs: size
// Outputs: none 
// In Lab 2, you can ignore the size field
// In Lab 3, you should implement the user-defined fifo size
// In Lab 3, you can put whatever restrictions you want on size
//    e.g., 4 to 64 elements
//    e.g., must be a power of 2,4,8,16,32,64,128
void OS_Fifo_Init(unsigned long size) {
    long sr = StartCritical();          // make atomic
	fifolimit = size;
    OS_InitSemaphore(&FifoAvailable, 0);// Empty
    OSPutPt = OSGetPt = &OS_Fifo[0];    // Empty
    EndCritical(sr); 	
}

// ******** OS_Fifo_Put ************
// Enter one data sample into the Fifo
// Add element to end of pointer FIFO
// Return RXFIFOSUCCESS if successful
// Called from the background, so no waiting 
// Inputs:  data
// Outputs: true if data is properly saved,
//          false if data not saved, because it was full
// Since this is called by interrupt handlers 
//  this function can not disable or enable interrupts
int OS_Fifo_Put(unsigned long data) { 
    uint16_t volatile *nextOSPutPt; 

    nextOSPutPt = OSPutPt + 1;        
    if(nextOSPutPt == &OS_Fifo[fifolimit]){ 
        nextOSPutPt = &OS_Fifo[0];      // wrap
    }                                     
    if(nextOSPutPt == OSGetPt ){      
        return 0;                       // Failed, fifo full
    }                                     
    else{                                 
        *(OSPutPt) = data;              // Put
        OSPutPt = nextOSPutPt;          // Success, update
        OS_Signal(&FifoAvailable);
        return(1); 
    }		
}  

// ******** OS_Fifo_Get ************
// Remove one data sample from the Fifo
// Remove element from front of pointer FIFO
// Return RXFIFOSUCCESS if successful
// Called in foreground, will spin/block if empty
// Inputs:  none
// Outputs: data 
unsigned long OS_Fifo_Get(void) { 
	OS_Wait(&FifoAvailable);                // Wait for data
	if(OSPutPt == OSGetPt)                  // Empty if OSPutPt=OSGetPt
        return 0;                                                          
  
	uint16_t data = *(OSGetPt++);            
    if(OSGetPt == &OS_Fifo[fifolimit]){    // Reset pointer to the front 
        OSGetPt = &OS_Fifo[0];              //if end of list reached
    }                                     
	return(data); 
}

// ******** OS_Fifo_Size ************
// Check the status of the Fifo
// Number of elements in pointer FIFO
// 0 to OSFIFOSIZE-1
// Inputs: none
// Outputs: returns the number of elements in the Fifo
//          greater than zero if a call to OS_Fifo_Get will return right away
//          zero or less than zero if the Fifo is empty 
//          zero or less than zero if a call to OS_Fifo_Get will spin or block
long OS_Fifo_Size(void) { 
	if( OSPutPt < OSGetPt )
		return ((unsigned short)(OSPutPt-OSGetPt+(fifolimit*sizeof(uint16_t)))/sizeof(uint16_t));                                      
    return ((unsigned short)(OSPutPt-OSGetPt)/sizeof(uint16_t)); 
}

Sema4Type BoxFree;
Sema4Type DataValid;
uint32_t MailBox;
// ******** OS_MailBox_Init ************
// Initialize communication channel
// Inputs:  none
// Outputs: none
void OS_MailBox_Init(void){
    OS_InitSemaphore(&BoxFree, 1);
	OS_InitSemaphore(&DataValid, 0);
}

// ******** OS_MailBox_Send ************
// enter mail into the MailBox
// Inputs:  data to be sent
// Outputs: none
// This function will be called from a foreground thread
// It will spin/block if the MailBox contains data not yet received 
void OS_MailBox_Send(unsigned long data){
	OS_bWait(&BoxFree);
	MailBox = data;
	OS_bSignal(&DataValid);
}
// ******** OS_MailBox_Recv ************
// remove mail from the MailBox
// Inputs:  none
// Outputs: data received
// This function will be called from a foreground thread
// It will spin/block if the MailBox is empty 
unsigned long OS_MailBox_Recv(void){
	OS_bWait(&DataValid);
	uint32_t data = MailBox;
	OS_bSignal(&BoxFree);
	return data;
}
// ******** OS_Time ************
// return the system time 
// Inputs:  none
// Outputs: time in 12.5ns units, 0 to 4294967295
// The time resolution should be less than or equal to 1us, and the precision 32 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_TimeDifference have the same resolution and precision 
unsigned long OS_Time(void){ 
    return WTIMER5_TAR_R;
}

// ******** OS_TimeDifference ************
// Calculates difference between two times
// Inputs:  two times measured with OS_Time
// Outputs: time difference in 12.5ns units 
// The time resolution should be less than or equal to 1us, and the precision at least 12 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_Time have the same resolution and precision 
unsigned long OS_TimeDifference(unsigned long start, unsigned long stop){
    long diff = stop - start;
	if(diff < 0){
		diff += 0xFFFFFFFF;
	}
	return diff;
}

// ******** OS_ClearMsTime ************
// sets the system time to zero (from Lab 1)
// Inputs:  none
// Outputs: none
// You are free to change how this works
void OS_ClearMsTime(void){
    OS_ms_count = 0;
}

// ******** OS_MsTime ************
// reads the current time in msec (from Lab 1)
// Inputs:  none
// Outputs: time in ms units
// You are free to select the time resolution for this function
// It is ok to make the resolution to match the first call to OS_AddPeriodicThread
unsigned long OS_MsTime(void){
    return OS_ms_count;
}


void OS_SelectNextThread(void){
    int level = runPT->priority;            // Record current priority

    if(pri_count[level] > 0) {              // If curr priotity list isn't empty, move to next thread
		pri_lists[level] = pri_lists[level]->next;
	}
	
	int pri_index = 0;                      // Init to zero
    
    // Loops from highest to lowest priority to find next highest priority
	while((pri_index < NUMPRIS) && (pri_count[pri_index] == 0)) {
		pri_index++;
	}
	if(pri_index == NUMPRIS) {}             // Bad, means there are no threads
	level = pri_index;                      // Record current highest priority level
	
    // Loop through priority lists, to check if there are threads to run in current level
        // else, check next level
	while(level < NUMPRIS) {
		for(pri_index = 0; (pri_index < pri_count[level]) && (pri_lists[level]->sleepCT); pri_index++) {
			pri_lists[level] = pri_lists[level]->next;
		}
		if(pri_index == pri_count[level]) { // Move to next level if done... 
			level++;                            // checking threads in current level
		}
		else { break; }                     // If we still have threads to run in...
	}                                           // current levelm break loop
	if(level == NUMPRIS) {}                 // Bad, no threads to run

	nextPT = pri_lists[level];              // load nextPT with next best thread to run
}


void SysTick_Handler(void) {
    #ifdef ROUNDROBIN
    TCBtype *node = head_kill;
    while(head_kill){
        if(node->next->used == 1){
            head_kill = node->next;
            node->next = 0;
        }
        else{
            head_kill = 0;
            node->next = 0;
        }
    }
    
    node = head_sleep;
	while(node){
        if(node->next != 0 && node->next->sleep_state == 0) {
                node->next = 0;
            }

		node->sleepCT--;
        TCBtype *next_node = node->next;

            
        if(node->sleepCT <= 0){
            node->sleep_state = 0;
            removeFromList(node, &head_sleep, &tail_sleep);
            num_threads++;
            linkThread(runPT, node, num_threads);
        } 
        node = next_node;

	}
	
	#else
//    // Decrementing sleep counter for all sleeping threads
//    TCBtype *node = head_sleep;
//    // Remove connection from sleeping list to main list 
//	while(node){
//        if((node->next != 0 && node->next->sleep_state == 0) || node->next == node) {
//                node->next = 0;
//            }

//		node->sleepCT--;                    // Dec sleep count
//        TCBtype *next_node = node->next;    // Load in next node

//            
//        if(node->sleepCT <= 0){             // Wake up thread once sleep counter = 0
//            node->sleep_state = 0;          // Turn of sleep state
//            removeFromList(node, &head_sleep, &tail_sleep); // Remove from sleep list
//            num_threads++;                                  // Increment active threads
//            OS_AddThreadPri(node, node->priority);          // Add thread back to priority list
//        } 
//        node = next_node;                   // Transistion to next node
//    }

    OS_SelectNextThread();
		
	#endif
	NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV;  //0x10000000 Trigger PendSV
//	PE1 ^= 0x02;
}

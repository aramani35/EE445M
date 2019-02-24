#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "OS.h"


int32_t StartCritical(void);
void EndCritical(int32_t primask);


void linkThread(TCBtype *linkPT, TCBtype *threadPT, int threads) {
	long status = StartCritical();
	
	if(threads == 0) {                  // set new thread pointing to itself    
		threadPT->next = threadPT;      // update next thread
		threadPT->prev = threadPT;      // update prev thread
        linkPT = threadPT;              // update runPT
	}
    else{
        threadPT->next = linkPT->next;  // new thread will run after runPT
        threadPT->prev = linkPT;        // new thread's prev is runPT
        linkPT->next   = threadPT;      // runPT points to new thread
        threadPT->next->prev = threadPT;// thread infront of new thread will point back to new thread
    }

    threads++;                                  // decrement # of threads
	EndCritical(status);
}

void unlinkThread(TCBtype *threadPT, int threads) {
//	long status = StartCritical();
	
	threads--;                                  // decrement # of threads
	if(threads > 0) {
		threadPT->prev->next = threadPT->next;  // update prev thread
		threadPT->next->prev = threadPT->prev;  // update next thread
	}
	
	threadPT->next = 0;                         // unlink thread
	threadPT->prev = 0;
	OS_pendSVTrigger();                         // thread is asleep so switch threads 
//	EndCritical(status);
}

void addToList(TCBtype *threadPT, TCBtype **startPT, TCBtype **tailPT){
    if(*startPT){
        (*tailPT)->next = threadPT;             // set current tail to new thread
        threadPT->prev  = *tailPT;              // set new thread prev to current tail to 
        *tailPT         = threadPT;             // set new tail of sleeping list
    }
    else{
		*startPT = threadPT;
		*tailPT = threadPT;
	}
}

void removeFromList(TCBtype *threadPT, TCBtype **startPT, TCBtype **tailPT){
	if(threadPT->prev)                          // check if new thread is not head
		threadPT->prev->next = threadPT->next;
	else
		*startPT = threadPT->next;              // else update new head
   
	if(threadPT->next)                          // check if new thread is not tail
		threadPT->next->prev = threadPT->prev;
	else
		*tailPT = threadPT->prev;               // else update new tail
}

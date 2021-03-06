#include <stdint.h>
#include "inc/tm4c123gh6pm.h"
#include "Timer.h"
#include "ADC.h"
#include "ADCSWTrigger.h"
#include "Timer.h"
#include "stdBool.h"

int32_t channel = -1;	// stores which ADC channel is opened, -1 means no channel is opened
int32_t sampleCounter = 0;
int32_t sampleSize = 0;
int* DATA;	// bufer that stores data
bool status = false;	// false means ADC is not running / complete, true represents ADC is still running

// ----------ADC_In----------
// Takes data in from specifed adc channel
int ADC_In(void){  
	uint32_t result;
	
	if (channel == -1) return -1; // channel hasn't been opened
  ADC0_PSSI_R = 0x0008;            // 1) initiate SS3
  while((ADC0_RIS_R&0x08)==0){};   // 2) wait for conversion done
    // if you have an A0-A3 revision number, you need to add an 8 usec wait here
  result = ADC0_SSFIFO3_R&0xFFF;   // 3) read result
  ADC0_ISC_R = 0x0008;             // 4) acknowledge completion
	return result;
}

// ----------ADC_Open----------
// Opens ADC on specified channel
int ADC_Open(uint32_t channelNum) {
	ADC0_InitSWTriggerSeq3(channelNum);	// sets up ADC on specified channel
	channel = channelNum;
	
	return 0;
}

// ----------ADC_Collect----------
// inputs:	channelNum = ADC channel from which data is to be collected
//					fs = rate at which data is to be collected
//					data_buffer = buffer to store collected ADC values
//					numberofSamples = number of samples to be collected
int ADC_Collect(uint32_t channelNum, uint32_t fs,
	int* data_buffer, uint32_t numberOfSamples){
	status = true;	// data is being collected from ADC
	ADC_Open(channelNum);	// open ADC channel
	DisableInterrupts();
	Timer2_Init(fs);	// Initialize Timer 2 at specfied rate
	
	sampleCounter = 0;	// reset timer (just in case)
	sampleSize = numberOfSamples;
	DATA = data_buffer;
	EnableInterrupts();	
	
	return 0;
}
	
void Timer2A_Handler(void){
  TIMER2_ICR_R = TIMER_ICR_TATOCINT;// acknowledge TIMER2A timeout
	if (sampleCounter < sampleSize) {	// need to take more samples
		*(DATA + sampleCounter) = ADC_In();	// value at specified position in DATA array is taken from ADC_In
		sampleCounter++;
	}
	
	else {
		status = false;	// ADC_Collect has completed
		sampleCounter = 0;	// reset counter
		TIMER2_CTL_R = 0x00000000;	// disable timer2
	}
}

// ----------ADC_Status----------
// Returns true if data is being collected from the ADC, false otherwise
int ADC_Status(){
	return status;
}

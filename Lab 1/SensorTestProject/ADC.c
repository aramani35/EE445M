#include <stdint.h>
#include "inc/tm4c123gh6pm.h"
#include "Timer.h"
#include "ADC.h"
#include "ADCSWTrigger.h"
#include "Timer.h"

int32_t channel = -1;
int32_t timerChannel = -1;


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

int ADC_Open(uint32_t channelNum) {
	ADC0_InitSWTriggerSeq3(channelNum);
	Timer2_Init(1000);
	
	channel = channelNum;
	
	return 0;
}

int ADC_Collect(uint32_t channelNum, uint32_t fs,
	uint16_t buffer[],uint32_t numberOfSamples){
	
	ADC0_InitSWTriggerSeq3(channelNum);
	Timer2_Init(fs);
	
	int sampleCounter = 0;
	while(sampleCounter < numberOfSamples)
	{
		buffer[sampleCounter++] = ADC_In();
	}
return 0;
		
	return 0;
	}
	
void Timer2A_Handler(void){
  TIMER2_ICR_R = TIMER_ICR_TATOCINT;// acknowledge TIMER2A timeout
	int result = ADC_In();
}



#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "EdgeTriggered.h"


#define PF4                     (*((volatile uint32_t *)0x40025040))

//volatile static unsigned long Touch = 0;     // true on touch
//volatile static unsigned long Release = 0;   // true on release
//volatile static unsigned long Last;      // previous



//------------EdgeCounter_Init------------
// Initialize GPIO Port F for edge triggered interrupts on
// PF4 as the Launchpad is wired.
// Input: none
// Output: none
void EdgeCounter_PF4_Init(void){                          
  SYSCTL_RCGCGPIO_R |= 0x00000020; // (a) activate clock for port F
  int delay = 0;
  GPIO_PORTF_DIR_R &= ~0x10;    // (c) make PF4 in (built-in button)
  GPIO_PORTF_AFSEL_R &= ~0x10;  //     disable alt funct on PF4
  GPIO_PORTF_DEN_R |= 0x10;     //     enable digital I/O on PF4   
  GPIO_PORTF_PCTL_R &= ~0x000F0000; // configure PF4 as GPIO
  GPIO_PORTF_AMSEL_R = 0;       //     disable analog functionality on PF
  GPIO_PORTF_PUR_R |= 0x10;     //     enable weak pull-up on PF4
  GPIO_PORTF_IS_R &= ~0x10;     // (d) PF4 is edge-sensitive
  GPIO_PORTF_IBE_R &= ~0x10;    //     PF4 is not both edges
  GPIO_PORTF_IEV_R &= ~0x10;    //     PF4 falling edge event
  GPIO_PORTF_ICR_R = 0x10;      // (e) clear flag4
//  GPIO_PORTF_IM_R |= 0x10;      // (f) arm interrupt on PF4 *** No IME bit as mentioned in Book ***
  NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|0x00A00000; // (g) priority 5
  NVIC_EN0_R = 0x40000000;      // (h) enable interrupt 30 in NVIC
}

void Timer5Arm(void){
  SYSCTL_RCGCTIMER_R |= 0x20;      // 0) activate timer5                    // wait for completion
  int delay = 0;
  TIMER5_CTL_R &= ~0x00000001;     // 1) disable timer5A during setup
  TIMER5_CFG_R = 0x00000000;       // 2) configure for 32-bit timer mode
  TIMER5_TAMR_R = 0x00000001;      // 3) 1-SHOT mode
  TIMER5_TAILR_R = (160000-1);     // 4) 10ms reload value
  TIMER5_TAPR_R = 0;               // 5) bus clock resolution
  TIMER5_ICR_R = 0x00000001;       // 6) clear timer5A timeout flag
  TIMER5_IMR_R |= 0x00000001;      // 7) arm timeout interrupt
  NVIC_PRI23_R = (NVIC_PRI23_R&0xFFFFFF00)|(0x20 << 2); // 8) priority 2
  NVIC_EN2_R |= 0x10000000;        // 9) enable interrupt 19 in NVIC
  // vector number 108, interrupt number 92
  TIMER5_CTL_R |= 0x00000001;      // 10) enable timer5A
// interrupts enabled in the main program after all devices initialized
	
}

//void GPIOArm(void){
//  GPIO_PORTF_ICR_R = 0x10;      // (e) clear flag4
//  GPIO_PORTF_IM_R |= 0x10;      // (f) arm interrupt on PF4 *** No IME bit as mentioned in Book ***
//  NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|0x00A00000; // (g) priority 5
//  NVIC_EN0_R = 0x40000000;      // (h) enable interrupt 30 in NVIC  
//}

// Interrupt 10 ms after rising edge of PF4
//void Timer5A_Handler(void){
//  TIMER5_IMR_R = 0x00000000;    // disarm timeout interrupt
//  Last = PF4;  // switch state
//  GPIOArm();   // start GPIO
//}

//void GPIOPortF_Handler(void){
//  GPIO_PORTF_IM_R &= ~0x10;     // disarm interrupt on PF4 
//  if(Last){    // 0x10 means it was previously released
//    Touch = 1;       // touch occurred
//    (*TouchTask)();  // execute user task
//  }
//  else{
//    Release = 1;       // release occurred
//    (*ReleaseTask)();  // execute user task
//  }
//  Timer0Arm(); // start one shot
//}

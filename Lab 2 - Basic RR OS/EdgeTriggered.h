
#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"




//------------EdgeCounter_Init------------
// Initialize GPIO Port F for edge triggered interrupts on
// PF4 as the Launchpad is wired.
// Input: none
// Output: none
void EdgeCounter_PF4_Init(void);

void Timer5Arm(void);

void Timer5_Init(void);
//void GPIOArm(void);

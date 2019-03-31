// OS.c
// TI TM4C123 microcontroller
// Tariq Muhanna and Abhishek Ramani
// Executes a software task at a periodic rate


int OS_AddPeriodicThread(void(*task)(void), uint32_t period, uint32_t priority);

void OS_ClearPeriodicTime(void);

uint32_t OS_ReadPeriodicTime(void);

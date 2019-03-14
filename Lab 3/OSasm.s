;/*****************************************************************************/
; OSasm.s: low-level OS commands, written in assembly                       */
; Runs on LM4F120/TM4C123
; A very simple real time operating system with minimal features.
; Daniel Valvano
; January 29, 2015
;
; This example accompanies the book
;  "Embedded Systems: Real Time Interfacing to ARM Cortex M Microcontrollers",
;  ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2015
;
;  Programs 4.4 through 4.12, section 4.2
;
;Copyright 2015 by Jonathan W. Valvano, valvano@mail.utexas.edu
;    You may use, edit, run or distribute this file
;    as long as the above copyright notice remains
; THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
; OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
; MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
; VALVANO SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL,
; OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
; For more information about my classes, my research, and my books, see
; http://users.ece.utexas.edu/~valvano/
; */

        AREA |.text|, CODE, READONLY, ALIGN=2
        THUMB
        REQUIRE8
        PRESERVE8

        EXTERN  runPT            ; currently running thread
		EXTERN  nextPT
        EXPORT  OS_DisableInterrupts
        EXPORT  OS_EnableInterrupts
        EXPORT  StartOS
        ;EXPORT  SysTick_Handler
		EXPORT  PendSV_Handler
		EXTERN 	threadRecordProfiler
		EXTERN 	OS_Id
		EXTERN  OS_timeDisabled
		EXTERN	OS_timeEnabled


OS_DisableInterrupts
        CPSID   I
		PUSH 	{LR}
		BL 		OS_timeDisabled
		POP 	{LR}
        BX      LR


OS_EnableInterrupts
        CPSIE   I
		PUSH 	{LR}
		BL 		OS_timeEnabled
		POP 	{LR}
        BX      LR


;PendSV_Handler                ; 1) Saves R0-R3,R12,LR,PC,PSR
    ;CPSID   I                  ; 2) Prevent interrupt during switch
    ;PUSH    {R4-R11}           ; 3) Save remaining regs r4-11
    ;LDR     R0, =runPT         ; 4) R0=pointer to RunPt, old thread
    ;LDR     R1, [R0]           ;    R1 = RunPt
    ;STR     SP, [R1]           ; 5) Save SP into TCB
    ;LDR     R1, [R1,#4]        ; 6) R1 = RunPt->next
    ;STR     R1, [R0]           ;    RunPt = R1
    ;LDR     SP, [R1]           ; 7) new thread SP; SP = RunPt->sp;
    ;POP     {R4-R11}           ; 8) restore regs r4-11
    ;CPSIE   I                  ; 9) tasks run with interrupts enabled
    ;BX      LR                 ; 10) restore R0-R3,R12,LR,PC,PSR


;switch using chosen NextPt
PendSV_Handler
	CPSID I
	PUSH {LR}
;	BL OS_Id
;	BL threadRecordProfiler
	POP {LR}
	PUSH {R4 - R11}
	LDR R0, =runPT
	LDR R1, [R0]
	STR SP, [R1]
	LDR R1, =nextPT
	LDR R2, [R1]
	STR R2, [R0]
	LDR SP, [R2]
	POP {R4 - R11}
	CPSIE I
	BX LR


;SysTick_Handler
;    CPSID   I                  ; Prevent interruption during context switch
;    PUSH    {R4-R11}           ; Save remaining regs r4-11 
;    LDR     R0, =RunPt         ; R0=pointer to RunPt, old thread
;    LDR     R1, [R0]		   ; RunPt->stackPointer = SP;
;    STR     SP, [R1]           ; save SP of process being switched out
;    LDR     R1, =NodePt
;    LDR     R2, [R1]           ; NodePt
;    LDR     R2, [R2]           ; next to run
;    STR     R2, [R1]           ; NodePt= NodePt->Next;
;    LDR     R3, [R2,#4]        ; RunPt = &sys[NodePt->Thread];// which thread
;    STR     R3, [R0]
;   
;    LDR     R2, [R2,#8]		   ; NodePt->TimeSlice
;    SUB     R2, #50            ; subtract off time to run this ISR
;    LDR     R1, =NVIC_ST_RELOAD
;    STR     R2, [R1]           ; how long to run next thread
;    LDR     R1, =NVIC_ST_CURRENT
;    STR     R2, [R1]           ; write to current, clears it

;    LDR     SP, [R3]           ; new thread SP; SP = RunPt->stackPointer;
;    POP     {R4-R11}           ; restore regs r4-11 

;    CPSIE   I				   ; tasks run with I=0
;    BX      LR                 ; Exception return will restore remaining context   
;    









;SysTick_Handler                ; 1) Saves R0-R3,R12,LR,PC,PSR
    ;CPSID   I                  ; 2) Prevent interrupt during switch
    ;PUSH    {R4-R11}           ; 3) Save remaining regs r4-11
    ;LDR     R0, =runPT         ; 4) R0=pointer to RunPt, old thread
    ;LDR     R1, [R0]           ;    R1 = RunPt
    ;STR     SP, [R1]           ; 5) Save SP into TCB
    ;LDR     R1, [R1,#4]        ; 6) R1 = RunPt->next
    ;STR     R1, [R0]           ;    RunPt = R1
    ;LDR     SP, [R1]           ; 7) new thread SP; SP = RunPt->sp;
    ;POP     {R4-R11}           ; 8) restore regs r4-11
    ;CPSIE   I                  ; 9) tasks run with interrupts enabled
    ;BX      LR                 ; 10) restore R0-R3,R12,LR,PC,PSR

StartOS
    LDR     R0, =runPT         ; currently running thread
    LDR     R2, [R0]           ; R2 = value of RunPt
    LDR     SP, [R2]           ; new thread SP; SP = RunPt->stackPointer;
    POP     {R4-R11}           ; restore regs r4-11
    POP     {R0-R3}            ; restore regs r0-3
    POP     {R12}
    POP     {LR}               ; discard LR from initial stack
    POP     {LR}               ; start location
    POP     {R1}               ; discard PSR
    CPSIE   I                  ; Enable interrupts at processor level
    BX      LR                 ; start first thread

    ALIGN
    END

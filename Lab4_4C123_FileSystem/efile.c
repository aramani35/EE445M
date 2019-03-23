// filename ************** eFile.c *****************************
// High-level routines to implement a solid-state disk 
// Jonathan W. Valvano 3/9/17

#include <string.h>
#include "edisk.h"
#include "UART2.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "efile.h"

#define SUCCESS 0
#define FAIL 1
#define MAXBLOCKS 4000    // Each block is 512 bytes
#define BLOCKS 4096       // Each block is 512 bytes
#define BLOCKSIZE 512
#define NUMOFFILES 30 
#define EMPTYSTRING ""

struct dir{
	char file_name[8];      // Name is limited to 8 bytes
	uint16_t start_block;   // Start location of data
	uint16_t size;          // Lenght of data
};
typedef struct dir dirType;

dirType DIR[NUMOFFILES];    // Global files list
unsigned char fbuffer[512]; // Global buffer for reading/writing
uint8_t Memory[BLOCKS];
bool file_open = false;
bool reader = false;
int curr_block = 0;

char empty_string[8];

//---------- eFile_Init-----------------
// Activate the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure (already initialized)
int eFile_Init(void){ // initialize file system
    if (eDisk_Init(0))
        return FAIL;
    return SUCCESS;
}


int writeDirectory(){
    int offset = 12;
    for (int i=0; i<NUMOFFILES; i++){
        int count = 0;
        for (int j=0+offset*i; j<offset+offset*i; j++){
            if (count < 8)
                fbuffer[j] = DIR[i].file_name[count];   // copies name 
            else if (count == 8)
                fbuffer[j] = (DIR[i].start_block&0xFF00)>>8;// 1 byte is 8 bits only
            else if (count == 9)
                fbuffer[j] = (DIR[i].start_block&0x00FF);
            else if (count == 10)
                fbuffer[j] = (DIR[i].size&0xFF00)>>8;
            else if (count == 11)
                fbuffer[j] = (DIR[i].size&0x00FF);
            count++;
        }
    }
    if(eDisk_WriteBlock(fbuffer, 1)) return FAIL;

	return SUCCESS;
}
//---------- eFile_Format-----------------
// Erase all files, create blank directory, initialize free space manager
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Format(void){ // erase disk, add format
    for (int i=0; i<BLOCKSIZE; i++)
            fbuffer[i] = 0;

    for (int i=0; i<NUMOFFILES; i++) {
        DIR[i].file_name[0] = 0;
        DIR[i].start_block = 0;
        DIR[i].size = 0;
    }
    writeDirectory();
    
    for(int i = 0; i < BLOCKS; i++){
        Memory[i] = 0;
    }
    if(eDisk_Write(0, Memory, 1, BLOCKS/BLOCKSIZE)) return FAIL;
    return SUCCESS;   // OK
}



//---------- eFile_Create-----------------
// Create a new, empty file with one allocated block
// Input: file name is an ASCII string up to seven characters 
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Create( char name[]){  // create new file, make it empty 
    // Check if name already exists
    int index;
	for (index = 1; index < NUMOFFILES; index++){	//skip file 0 - free space manager
		if (strncmp(name, DIR[index].file_name, 8) == 0) return FAIL; // Stop if file exists
		if (DIR[index].file_name[0] == 0)           // Found unused file
			break;
	}
    strcpy(name, DIR[index].file_name); // Copy name
    DIR[index].size = 0;                // Init new file size to 0
    
    int i;
    for (i = 0; i < BLOCKS; i+=100){    // Loop till free space found
        if (Memory[i] != 0) break;
    }
    Memory[i] = index;                  // 1st block has index of file
    DIR[index].start_block = i;
    
    if (writeDirectory()) return FAIL;      //-------------
    if (eDisk_Write(0, Memory, 1, BLOCKS/BLOCKSIZE)) return FAIL;

    return SUCCESS;     
}

//---------- eFile_WOpen-----------------
// Open the file, read into RAM last block
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WOpen(char name[]){               // open a file for writing 
    if (file_open) return FAIL;             // Fail if file already open
    reader = false;                         // Dont allow reading 
    int index;
    for (index = 1; index < NUMOFFILES; index++){	//skip file 0 - free space manager
		if (strncmp(name, DIR[index].file_name, 8) == 0) break; // Stop if file exists
	}
    if (index >= NUMOFFILES) return FAIL;   // Fail if out of range
    file_open = index;                      // Set global
    int length = DIR[index].size;           // Length in bytes
    int start = DIR[index].start_block;     // start block
    int last = start;                       // last block
    while (1){                              // Loop till last block found
        if (length > BLOCKSIZE){
            length -= BLOCKSIZE;
            last++;
        }
        else
            break;
    }
    //!!!! may need end of block size
    curr_block = last;                      // Update Global
    if (eDisk_ReadBlock(fbuffer,last)) return FAIL; // Read into RAM
    return SUCCESS;   
}

//---------- eFile_Write-----------------
// save at end of the open file
// Input: data to be saved
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Write(char data){
    if (!file_open || reader) return FAIL;      // Make sure file is open & no one reading
    
    if (DIR[file_open].size%BLOCKSIZE == 0){    // Save at end of the open file
        if(eDisk_WriteBlock(fbuffer, curr_block)) return FAIL;
//        DIR[file_open].size++;                // Increase size
        curr_block++;                           // Move to next block
    }
    
    fbuffer[DIR[file_open].size%BLOCKSIZE] = data;  // Add to fbuffer
    DIR[file_open].size++;                      // Increase size
    
    return SUCCESS;  
}


//---------- eFile_Close-----------------
// Deactivate the file system
// Input: none
// Output: 0 if successful and 1 on failure (not currently open)
int eFile_Close(void){ 
//    if (writeDirectory()) return FAIL;  // Write data to SD
//    if (eDisk_Write(0, Memory, 1, BLOCKS/BLOCKSIZE)) return FAIL;
    eFile_WClose();                     // Close writing and reading
    eFile_RClose();
  return SUCCESS;     
}

//---------- eFile_WClose-----------------
// close the file, left disk in a state power can be removed
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WClose(void){ // close the file for writing
    if (!file_open || reader) return FAIL;      // Make sure file is open & no one reading
    file_open = 0;
    
    if(eDisk_WriteBlock(fbuffer, curr_block)) return FAIL;
    if (writeDirectory()) return FAIL;  // Write data to SD
    if (eDisk_Write(0, Memory, 1, BLOCKS/BLOCKSIZE)) return FAIL;
   
    return SUCCESS;     
}


//---------- eFile_ROpen-----------------
// Open the file, read first block into RAM 
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble read to flash)
int eFile_ROpen( char name[]){      // open a file for reading 

  return SUCCESS;     
}
 
//---------- eFile_ReadNext-----------------
// retreive data from open file
// Input: none
// Output: return by reference data
//         0 if successful and 1 on failure (e.g., end of file)
int eFile_ReadNext( char *pt){       // get next byte 

  return SUCCESS; 
}

    
//---------- eFile_RClose-----------------
// close the reading file
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_RClose(void){ // close the file for writing

  return SUCCESS;
}




//---------- eFile_Directory-----------------
// Display the directory with filenames and sizes
// Input: pointer to a function that outputs ASCII characters to display
// Output: none
//         0 if successful and 1 on failure (e.g., trouble reading from flash)
int eFile_Directory(void(*fp)(char)){   

  return SUCCESS;
}

//---------- eFile_Delete-----------------
// delete this file
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Delete( char name[]){  // remove this file 

  return SUCCESS;    // restore directory back to flash
}

int StreamToFile=0;                // 0=UART, 1=stream to file

int eFile_RedirectToFile(char *name){
  eFile_Create(name);              // ignore error if file already exists
  if(eFile_WOpen(name)) return 1;  // cannot open file
  StreamToFile = 1;
  return 0;
}

int eFile_EndRedirectToFile(void){
  StreamToFile = 0;
  if(eFile_WClose()) return 1;    // cannot close file
  return 0;
}

int fputc (int ch, FILE *f) { 
  if(StreamToFile){
    if(eFile_Write(ch)){          // close file on error
       eFile_EndRedirectToFile(); // cannot write to file
       return 1;                  // failure
    }
    return 0; // success writing
  }

   // regular UART output
  UART_OutChar(ch);
  return 0; 
}

int fgetc (FILE *f){
  char ch = UART_InChar();  // receive from keyboard
  UART_OutChar(ch);            // echo
  return ch;
}

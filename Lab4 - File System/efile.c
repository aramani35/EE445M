// filename ************** eFile.c *****************************
// High-level routines to implement a solid-state disk 
// Jonathan W. Valvano 3/9/17

#include <string.h>
#include <math.h>
#include "edisk.h"
#include "UART2.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "efile.h"
#include "OS.h"

#define SUCCESS 0
#define FAIL 1
#define MAXBLOCKS 4096    // Each block is 512 bytes
#define BLOCKS 4096        // Each block is 512 bytes
#define BLOCKSIZE 512
#define DIRBLOCK 0
#define MEMBLOCK 1
#define NUMOFFILES 30 
#define EMPTYSTRING ""

struct dir{
	char file_name[8];      // Name is limited to 8 bytes
	uint16_t start_block;   // Start location of data
	uint16_t size;          // number of bytes
	
};
typedef struct dir dirType;

dirType DIR[NUMOFFILES];    // Global files list
unsigned char fbuffer[512]; // Global buffer for reading/writing
uint8_t Memory[BLOCKS];     // Blocks array corresponding to SDC data
bool file_open = false;     // File Flag
bool reader = false;        // Reading flag
bool readFirstBlock = false;
bool writeFirstBlock = false;

int curr_block = 0;         // Saves current block when reading/writing
int read_index = 0;         // Saves current index when reading
int write_index = 0; 		// Saves current index when writing
int curr_block_counter = 0; // Saves current num of blocks read/written

char empty_string[8];

// Sema4Type fsysOpen;

//---------- eFile_Init-----------------
// Activate the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure (already initialized)
int eFile_Init(void){ // initialize file system
	// OS_InitSemaphore(&fsysOpen, 1);
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
    if(eDisk_WriteBlock(fbuffer, DIRBLOCK)) {
		// OS_Signal(&fsysOpen);
		return FAIL;
	}
	
	// OS_Signal(&fsysOpen);
	return SUCCESS;
}
//---------- eFile_Format-----------------
// Erase all files, create blank directory, initialize free space manager
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Format(void){ // erase disk, add format
	// OS_Wait(&fsysOpen);
    for (int i=0; i<BLOCKSIZE; i++)
            fbuffer[i] = 0;

    for (int i=0; i<NUMOFFILES; i++) {
		if (DIR[i].file_name[0] == 0) break;
		
        DIR[i].file_name[0] = 0;
        DIR[i].start_block = 0;
        DIR[i].size = 0;
    }
    writeDirectory();
    
    for(int i = 0; i < BLOCKS; i++){
        Memory[i] = 0;
    }
	
	// Clears directory
    if(eDisk_WriteBlock(fbuffer, 0)) {
		// OS_Signal(&fsysOpen);
		return FAIL;
	}
	
	
	
	// OS_Signal(&fsysOpen);
    return SUCCESS;   // OK
}



//---------- eFile_Create-----------------
// Create a new, empty file with one allocated block
// Input: file name is an ASCII string up to seven characters 
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Create( char name[]){  // create new file, make it empty 
    // Check if name already exists
	// OS_Wait(&fsysOpen);
    int index;
	for (index = 1; index < NUMOFFILES; index++){	//skip file 0 - free space manager
		if (strncmp(name, DIR[index].file_name, 8) == 0) {
			// OS_Signal(&fsysOpen);
			return FAIL; // Stop if file exists
		}
		if (DIR[index].file_name[0] == 0)           // Found unused file
			break;
	}
    strcpy(DIR[index].file_name, name); // Copy name
    DIR[index].size = 0;                // Init new file size to 0
    
    int i;
    for (i = 2; i < BLOCKS; i++){    // Loop till free space found; 0th block is DIRBLOCK, 1st block is MEMBLOCK
        if (Memory[i] == 0) { 
			int j;
			for (j = 1; j < 64; j++) {	// look for contiguous sequence of 10 blocks
				if (Memory[i+j] != 0) {
					i += j;
					break;
				}
			}
			
			if (j == 64) 
				break;	// found continuous sequence of 10 blocks
		}
    }
	
	for (int k = 0; k < 64; k++) {
		Memory[i+k] = index;	// Memory entries contain file index they are taken up by 
	}
    DIR[index].start_block = i;
    
    if (writeDirectory()) {
		// OS_Signal(&fsysOpen);
		return FAIL;      //-------------
	}
	
    if (eDisk_Write(0, Memory, MEMBLOCK, 1)) {
		// OS_Signal(&fsysOpen);
		return FAIL;
	}
	
	// OS_Signal(&fsysOpen);
    return SUCCESS;     
}

void clearBuffer(void) {
	for (int h = 0; h < 512; h++) {
		fbuffer[h] = 0;
	}
}

//---------- eFile_WOpen-----------------
// Open the file, read into RAM last block
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WOpen(char name[]){               // open a file for writing 
	// OS_Wait(&fsysOpen);
    if (file_open) {
		// OS_Signal(&fsysOpen);
		return FAIL;             // Fail if file already open
	}
    reader = false;                         // Dont allow reading 
    int index;
    for (index = 1; index < NUMOFFILES; index++){	//skip file 0 - free space manager
		if (strncmp(name, DIR[index].file_name, 8) == 0) break; // Stop if file exists
	}
    if (index >= NUMOFFILES) {
		// OS_Signal(&fsysOpen);
		return FAIL;   // Fail if out of range
	}
    file_open = index;                      // Set global
    int length = DIR[index].size;           // Length in bytes
    int start = DIR[index].start_block;     // start block
    int last = start;                       // last block
    while (1){                              // Loop till last block found
        if (length > BLOCKSIZE) {
            length -= BLOCKSIZE;
            last++;
        }
        else
            break;
    }
	
	write_index = length;	// write_index is index of last byte entry in block
    //!!!! may need end of block size
    curr_block = last;                      // Update Global
	
	if (curr_block == start) {
		writeFirstBlock = true; 
	}
	
	else {
		writeFirstBlock = false;
	}
	
    if (eDisk_ReadBlock(fbuffer, last)) {
		// OS_Signal(&fsysOpen);
		return FAIL; // Read into RAM
	}
	
	clearBuffer();
    return SUCCESS;   
}

//---------- eFile_Write-----------------
// save at end of the open file
// Input: data to be saved
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Write(char data){
    if (!file_open || reader) 
		return FAIL;      // Make sure file is open & no one reading
    
	if (writeFirstBlock && DIR[file_open].size == 0) {}
	
    else if (DIR[file_open].size%BLOCKSIZE == 0){    // Save at end of block //the open file
		writeFirstBlock = false;
        if(eDisk_WriteBlock(fbuffer, curr_block)) 
			return FAIL;
		
		clearBuffer();
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
int eFile_Close(void) { 
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
	
    if(eDisk_WriteBlock(fbuffer, curr_block)) 
		return FAIL;
    if (writeDirectory()) 
		return FAIL;  // Write data to SD
    if (eDisk_WriteBlock(Memory, MEMBLOCK)) 
		return FAIL;
   
    return SUCCESS;     
}


//---------- eFile_ROpen-----------------
// Open the file, read first block into RAM 
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble read to flash)
int eFile_ROpen( char name[]){              // open a file for reading 
    if (file_open) 
		return FAIL;             // Fail if file already open
    reader = true;                          // Signify reading 
    int index;
    for (index = 1; index < NUMOFFILES; index++){	//skip file 0 - free space manager
		if (strncmp(name, DIR[index].file_name, 8) == 0) break; // Stop if file exists
	}
    if (index >= NUMOFFILES) 
		return FAIL;   // Fail if out of range
    
    file_open = index;                      // Set global
    int length = DIR[index].size;           // Length in bytes
    int start = DIR[index].start_block;     // start block
    curr_block = start;                     // Update Global 
    read_index = 0;                         // Global reader index
    curr_block_counter = 0;
	readFirstBlock = true;
    
    if (eDisk_ReadBlock(fbuffer, curr_block)) 
		return FAIL; // Read into RAM
    return SUCCESS;   
}
 
 
//---------- eFile_ReadNext-----------------
// retreive data from open file
// Input: none
// Output: return by reference dat
//         0 if successful and 1 on failure (e.g., end of file)
int eFile_ReadNext( char *pt){       // get next byte 
    if (!file_open || !reader) 
		return FAIL;     // Make sure file is open & no one reading
	
	if (read_index >= DIR[file_open].size) {
		return FAIL;
	}
	
	if (read_index == 0) {}
	
    else if (read_index%BLOCKSIZE == 0){    	// Reached end of block 
        if (read_index >= DIR[file_open].size)  // Check if reached end of file;
            return FAIL;
        curr_block++;                           // Move to next block
        curr_block_counter++;
        if(eDisk_ReadBlock(fbuffer, curr_block)) 
			return FAIL;
    }
    *pt = fbuffer[read_index%BLOCKSIZE];  // Add to fbuffer
    read_index++;                               // Increase index
    
  return SUCCESS; 
}

    
//---------- eFile_RClose-----------------
// close the reading file
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_RClose(void){ // close the file for writing
    if (!file_open || (file_open && reader)) return FAIL;      // Make sure file is open & no one reading
    file_open = false;
    reader = false;
    return SUCCESS;
}

//---------- eFile_PrintFile-----------------
// Prints file to selected output
// Input: which file to print, output function
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_PrintFile(char *name, void(*printFunc)(char)) {
	if (eFile_ROpen(name) == FAIL) return FAIL;
	
	char data;
	int status;
	int count;
	
	for (count = 0; count < DIR[file_open].size; count++) {
		status = eFile_ReadNext(&data);
		printFunc(data);
		
		if (status == FAIL) return FAIL;
	}
	
	eFile_RClose();
	
	return SUCCESS;
}

//---------- eFile_Directory-----------------
// Display the directory with filenames and sizes
// Input: pointer to a function that outputs ASCII characters to display
// Output: none
//         0 if successful and 1 on failure (e.g., trouble reading from flash)
int eFile_Directory(void(*fp)(char)){
    int num_files = 0;
    for (int index = 0; index < NUMOFFILES; index++){
		if (DIR[index].file_name[0])
            num_files++;                // counts num of files
		else
			continue;
		
        int i = 0;
        while (DIR[index].file_name[i] && i < 8){
            fp(DIR[index].file_name[i]);// Prints file name
			i++;
        }
        for (int j = 0; j < 5; j++){	// Prints spaces to separate name and size
            fp(' ');
        }
		
		int curFileSize = DIR[index].size;
		int maxMag = 0;	// stores how magnitudes size is (by power of 10)
		while ((curFileSize/=10) != 0) {
			maxMag++;
		}
		
		curFileSize = DIR[index].size;
		
		for (int k = 0; k < maxMag+1; k++) {
			fp('0' + curFileSize/((int)pow(10, maxMag-k)));
			curFileSize %= (int)pow(10, maxMag-k);
		}	
		
		char bytes[6] = " bytes";
		
		for (int l = 0; l < 6; l++) {
			fp(bytes[l]);
		}
		
        fp('\n');                       // Prints new line
		fp('\r');

    }      
//        for (int index = 0; index < num_files; index++){	//skip file 0 - free space manager
//            int i = 0;
//            while (DIR[index].file_name[i] && i < 8){
//                fp(DIR[index].file_name[i]);// Preints file name
//            }
//            for (int i = 0; i < 5; i++){	// Prints spaces to separate name and size
//                fp(' ');
//            }
//            fp('\n');                       // Prints new line

//        }
   // }

  return SUCCESS;
}

//---------- eFile_Delete-----------------
// delete this file
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Delete( char name[]){  // remove this file 
    int index;
    for (index = 0; index < NUMOFFILES; index++){	
		if (strncmp(name, DIR[index].file_name, 8) == 0) break; // Stop if file exists
	}
    if (index >= NUMOFFILES) return FAIL;   // Fail if out of range
    
    
    clearBuffer();	// clears buffer
    
    int start = DIR[index].start_block;     // Empty memory blocks
    for (int i = DIR[index].size; i > 0; i -= 512){
        Memory[start] = 0;
        if(eDisk_WriteBlock(fbuffer, start)) return FAIL;
        start++;
    }
    
    DIR[index].file_name[0] = 0;            // Reninitalize to new 
    DIR[index].size = 0;
    DIR[index].start_block = 0;
    
    writeDirectory();                       // Update SDC data
    if(eDisk_Write(0, Memory, 1, BLOCKS/BLOCKSIZE)) return FAIL;

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


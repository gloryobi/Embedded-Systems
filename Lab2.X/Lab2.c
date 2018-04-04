#include <stdio.h>
#include <stdlib.h>
#include <p32xxxx.h>
//#include <explore.h>
//#include <SDMMC.h> 

#define AmISDHC 0
#define LED _RD0 
#define I_TIMEOUT 10000
#define RI_TIMEOUT 25000
#define W_TIMEOUT 250000
#define R_TIMEOUT 25000

// I/O definitions
#define SDWP _RF1 // Write Protect input
#define SDCD _RF0 // Card Detect input
#define SDCS _RB1 // Card Select output
#define SCK	 _RF6
#define SDI	 _RF7
#define SDO	 _RF8
/*
#define SDWP _RG1 // Write Protect input
#define SDCD _RG0 // Card Detect input
#define SDCS _RB9 // Card Select output
*/

#define readSPI() writeSPI(0xFF)
#define clockSPI() writeSPI(0xFF)
// SD card commands
#define RESET 0 // a.k.a. GO_IDLE (CMD0)
#define INIT 1 // a.k.a. SEND_OP_COND (CMD1)
#define READ_SINGLE 17
#define WRITE_SINGLE 24 
#define disableSD() SDCS = 1; clockSPI()
#define enableSD() SDCS = 0 
#define DATA_START 0xFE
#define DATA_ACCEPT 0x05

#define FAIL 0//I made this 0!!!!!!
// Init ERROR code definitions
#define E_COMMAND_ACK 0x80
#define E_INIT_TIMEOUT 0x81

#define B_SIZE 512 // data block size
char data[ B_SIZE];
char buffer[ B_SIZE]; 
#define START_ADDRESS 0//10000 // start block address
#define N_BLOCKS 10 // number of blocks 

//config for 80mhz
// configuration bit settings, Fcy = 80MHz, Fpb = 40MHz
#pragma config POSCMOD = XT, FNOSC = PRIPLL 
#pragma config FPLLIDIV = DIV_2, FPLLMUL = MUL_20, FPLLODIV = DIV_1
#pragma config FPBDIV = DIV_2, FWDTEN = OFF, CP = OFF, BWP = OFF

typedef unsigned LBA; // logic block address, 32 bit wide

//User made wait function
void Delayms(int msdelay) { //approx 1ms delay at 80mhz
    int mycountz1 = 0;
    int mycountz2 = 0;
    while(mycountz1 < msdelay) {
        while(mycountz2 < 80000) {
            mycountz2++;
        }
        mycountz1++;
    }
}

void initSD(void) {
    SDCS = 1; // initially keep the SD card disabled
    _TRISF0 = 0; // make Card select an output pin
    // init the SPI2 module for a slow (safe) clock speed first
    SPI1CON = 0x8120; // ON, CKE = 1; CKP = 0, sample middle
    SPI1BRG = 71; // clock = Fpb/144 = 250kHz
}

// send one byte of data and receive one back at the same time
unsigned char writeSPI(unsigned char b) {
    SPI1BUF = b; // write to buffer for TX
    while(!SPI1STATbits.SPIRBF); // wait transfer complete
    return SPI1BUF; // read the received value
}

int sendSDCmd(unsigned char c, unsigned a) {
    int i, r;
    // enable SD card
    enableSD(); 
    // send a comand packet (6 bytes)
    writeSPI(c | 0x40); // send command
    writeSPI(a >> 24); // msb of the address
    writeSPI(a >> 16);
    writeSPI(a >> 8);
    writeSPI(a); // lsb
    if(!AmISDHC) {
        writeSPI(0x95); // send CMD0 CRC 
    }
    else {
       writeSPI(0x87);//SDHC cards need this
    }
    // now wait for a response, allow for up to 8 bytes delay
    for(i = 0; i < 8; i++) {
        r = readSPI();
        if (r != 0xFF)
        break;
    }
    return (r);
    // NOTE CSCD is still low!
}

int initMedia(void) {
    // returns 0 if successful
    // E_COMMAND_ACK failed to acknowledge reset command
    // E_INIT_TIMEOUT failed to initialize
    int i, r;
    // 1. with the card NOT selected
    disableSD();
    // 2. send 80 clock cycles start up
    for (i = 0; i < 10; i++)
        clockSPI();
    // 3. now select the card
    enableSD();
    // 4. send a single RESET command
    r = sendSDCmd(RESET, 0); disableSD();
    if (r != 1) // must return Idle
        return E_COMMAND_ACK; // comand rejected
    // 5. send repeatedly INIT until Idle terminates
    for (i = 0; i < I_TIMEOUT; i++) {
        r = sendSDCmd(INIT, 0); disableSD();
        if (!r)
            break;
    }
    if (i == RI_TIMEOUT)
        return E_INIT_TIMEOUT; // init timed out 
    // 6. increase speed
    SPI1CON = 0; // disable the SPI2 module
    SPI1BRG = 1; // Fpb/(2*(0+1))= 36/2 = 18 MHz
    SPI1CON = 0x8120; // re-enable the SPI2 module
    return 0;
}

int readSECTOR(LBA a, char *p) {
// a LBA of sector requested
// p pointer to sector buffer
// returns TRUE if successful
    int r, i;
    // 1. send READ command
    r = sendSDCmd(READ_SINGLE, (a << 9));
    if (r == 0) { // check if command was accepted
        // 2. wait for a response
        for(i = 0; i < R_TIMEOUT; i++) {
            r = readSPI();
            if (r == DATA_START)
                break;
        }
        // 3. if it did not timeout, read 512 byte of data
        if (i != R_TIMEOUT) {
            i = 512;
            do{
                *p++ = readSPI();
            } while (--i>0);
            // 4. ignore CRC
            readSPI();
            readSPI();
        } // data arrived
    } // command accepted
    // 5. remember to disable the card
    disableSD();
    return (r == DATA_START); // return TRUE if successful
}

int writeSECTOR(LBA a, char *p) {
// a LBA of sector requested
// p pointer to sector buffer
// returns TRUE if successful 
    // 0. check Write Protect
    if (getWP())
        return FAIL; 
    unsigned r, i;
    // 1. send WRITE command
    r = sendSDCmd(WRITE_SINGLE, (a << 9));
    if (r == 0) // check if command was accepted
    {
        // 2. send data
        writeSPI(DATA_START);
        // send 512 bytes of data
        for(i = 0; i < 512; i++)
            writeSPI(*p++);
        // 3. send dummy CRC
        clockSPI();
        clockSPI();
        // 4. check if data accepted
        r = readSPI();
        if ((r & 0xf) == DATA_ACCEPT)
        {
            // 5. wait for write completion
            for(i = 0; i < W_TIMEOUT; i++)
            {
                r = readSPI();
                if (r != 0 )
                    break;
            }
        } // accepted
        else
            r = FAIL;
    } // command accepted
    // 6. remember to disable the card
    disableSD();
    return (r); // return TRUE if successful
}

// SD card connector presence detection switch
int getCD(void) {
// returns TRUE card present
// FALSE card not present
    return !SDCD;
} 

// card Write Protect tab detection switch
int getWP(void) {
// returns TRUE write protect tab on LOCK
// FALSE write protection tab OPEN
    return SDWP;
} 
 
void setup(void) {
    AD1PCFG = 0x9fff;
    _TRISB1 = 0;
    SDCS = 1;
    SPI1CONbits.DISSDO = 0;
    //_RD0 = 0;
    _TRISC4 = 1;
    SPI1CON = 0x8120;
    SPI1BRG = 72;
    _TRISD1 = 0;//LEDS
    _TRISD0 = 0;
    _TRISD2 = 0;
    _RD0 = 0;
    _RD1 = 0;
    _RD2 = 0;
}

void main() {
    LBA addr;
    setup();    //User made additional set up function
    int i, j;
    
    while (1) {
        LED = 0;
        _RD1 = 0;
        // 1. initializations
        initSD(); // init SD/MMC module

        // 2. fill the buffer with pattern
        for(i = 0; i < B_SIZE; i++)
            data[i] = i; 

        // 3. wait for the card to be inserted
        while(!getCD());// check CD switch
        //_RD2 = 1;
        Delayms(100); // wait contacts de-bounce

        if (initMedia()) {     // init card
            // if error code returned
            // _RD2 = 1;
            goto End;
        } 

        // 4. fill 16 groups of N_BLOCK sectors with data
        LED = 1; // SD card in use 
        addr = START_ADDRESS;
        for(j = 0; j < 16; j++) {
            for(i = 0; i < N_BLOCKS; i++) {
                if (!writeSECTOR(addr + i * j, data)) { // writing failed
                    goto End;
                }
            }
        }
        // 5. verify the contents of each sector written
        addr = START_ADDRESS;
        //_RD2=0;
        for(j = 0; j < 16; j++) { 
            for(i = 0; i < N_BLOCKS; i++) { // read back one block at a time
                if (!readSECTOR(addr + i * j, buffer)) { // reading failed
                    goto End;
                }
                // verify each block content
                if (memcmp(data, buffer, B_SIZE))
                { // mismatch
                    _RD2 = 1;//Turns LED on if a mismatch is detected and ends the checking
                    goto End;
                }
            }
        }
        // 7. indicate successful execution
        _RD1 = 1;//TURNS LED ON when it finishes properly
        End:
        LED = 0; // SD card not in use
        Delayms(1000);
    }
}
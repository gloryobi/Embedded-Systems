#include <p32xxxx.h>
#include "i2c_master_noint.h"
#define WRITE 0b10100000
#define READ 0b10100001

// constants, funcs for startup and UART
// I2C Master utilities, 100 kHz, using polling rather than interrupts
// The functions must be callled in the correct order as per the I2C protocol
// Master will use I2C1 SDA1 (D9) and SCL1 (D10)
// Connect these through resistors to Vcc (3.3 V). 2.4k resistors recommended,
// but something close will do.
// Connect SDA1 to the SDA pin on the slave and SCL1 to the SCL pin on a slavevoid 

void i2c_master_setup(void) {
    I2C1BRG = 390;          // I2CBRG = [1/(2*Fsck) - PGD]*Pblck - 2
                            // Fsck is the freq (100 kHz here), PGD = 104 ns
    I2C1CONbits.ON = 1;     // turn on the I2C1 module
    TRISD = 0x0000;
}

// Start a transmission on the I2C busvoid 
void i2c_master_start(void) {
    I2C1CONbits.SEN = 1;            // send the start bit
    while(I2C1CONbits.SEN) { ; }    // wait for the start bit to be sent
}

void i2c_master_restart(void) {
  I2C1CONbits.RSEN = 1;             // send a restart
  while(I2C1CONbits.RSEN) { ; }     // wait for the restart to clear
}

void i2c_master_send(unsigned char byte) {  // send a byte to slave
   I2C1TRN = byte;                  // if an address, bit 0 = 0 for write, 1 for read
   while(I2C1STATbits.TRSTAT) { ; }  // wait for the transmission to finish
   if(I2C1STATbits.ACKSTAT) {        // if this is high, slave has not acknowledged
   }
}

unsigned char i2c_master_recv(void) {   // receive a byte from the slave
   I2C1CONbits.RCEN = 1;                // start receiving data
   while(!I2C1STATbits.RBF) { ; }       // wait to receive the data
   return I2C1RCV;                      // read and return the data
}

void i2c_master_ack(int val) {        // sends ACK = 0 (slave should send another byte)
                                      // or NACK = 1 (no more bytes requested from slave)
    I2C1CONbits.ACKDT = val;          // store ACK/NACK in ACKDT
    I2C1CONbits.ACKEN = 1;            // send ACKDT
    while(I2C1CONbits.ACKEN) { ; }    // wait for ACK/NACK to be sent
}

void i2c_master_stop(void) {          // send a STOP:
    I2C1CONbits.PEN = 1;              // comm is complete and master relinquishes bus
    while(I2C1CONbits.PEN) { ; }      // wait for STOP to complete
}

void send_address(void){        // set address
    i2c_master_send(0);
    i2c_master_send(0);
}

int main(int argc, char** argv) {
    i2c_master_setup();
    
    int i;
    char transmit[14];
    char receive[14];           // variable to hold received bytes
    for(i = 0; i < 14; i++){
        transmit[i] = ' ';
        receive[i] = 0;
    }
    sprintf(transmit, "Hello world!\n");        // bytes to send

    i2c_master_start(); // Begin the start sequence
    i2c_master_send(WRITE);     // send WRITE signal
	send_address();     // set address
    for (i = 0; i < 14; i++) {
        i2c_master_send(transmit[i]); // send a byte to the slave
    }
    i2c_master_stop();      // delay

    int delay = 0;
    while(delay < 200)
        delay++;
	
    i2c_master_start(); // Begin the start sequence
    i2c_master_send(WRITE);     // send WRITE signal
	send_address();     // set address
    i2c_master_start();
    i2c_master_send(READ);  //send READ signal
    
    for(i = 0; i < 13; i++){
        receive[i] = i2c_master_recv(); // receive a byte from the bus
        i2c_master_ack(0);  // send ACK (0): master wants another byte
    }
    receive[i] = i2c_master_recv(); // receive last byte from the bus
    i2c_master_ack(1);  // send NACK (1): master needs no more bytes
    i2c_master_stop(); // send STOP: end transmission, give up bus
	
	_RD0 = 1;           // turn on LED
    _RD1 = 0;
    _RD2 = 0;
	return 0;
}
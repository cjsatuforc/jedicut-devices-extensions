/*  Copyright 2013 Martin

    This file is part of jedicutplugin.

    fcifmdlcnc is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    fcifmdlcnc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
	
	This version of the code is for the letmathe mdlcnc board. For 
	other boards this code must be adapted accordingly !!!
*/
/*
The jedicut communication configuration for Clock and Direction must be fixed set to the below values, 
only the wires to the specific board may be different -- wire to -->

Function               Number in jedicut configuration   Arduino Pin        		e.g. Lethmate MDLCNC board SubD25 pin
                       (fixed !!)
EngineX1 Clock         2                                 D8  (PB0)    -- wire to -->        2    
EngineX2 Clock         3                                 D9  (PB1)    -- wire to -->        6
EngineY1 Clock         4                                 D10 (PB2)    -- wire to -->        4
EngineY2 Clock         5                                 D11 (PB3)    -- wire to -->        8
EngineX1 Direction     6                                 D4  (PD4)    -- wire to -->        3
EngineX2 Direction     7                                 D5  (PD5)    -- wire to -->        7
EngineY1 Direction     8                                 D6  (PD6)    -- wire to -->        5
EngineY2 Direction     9                                 D7  (PD7)    -- wire to -->        9

All Engines On/Off     -                                 D12 (PB4)    -- wire to -->        1 
Heating On/off         -                                 D2  (PD2)    -- wire to -->        14
Heating PWM            -                                 D3  (PD3)    -- wire to -->        - 
*/ 


#include <avr/interrupt.h>
#include <avr/io.h>

// Ringbuffer for the commands
#define CMD_BUFFER_SIZE 600  // must be even !
volatile byte cmdArray[CMD_BUFFER_SIZE];
volatile int arrayIdxRead  = 0;
volatile int arrayIdxWrite = 0;
volatile int cmdCounter = 0;
volatile boolean ovf = false;

volatile bool isrActive = false;

/**********************************************************************************/
void setup() 
{

  Serial.begin(115200);    // opens serial port, sets data rate to 115200 bps
  
  pinMode(13,OUTPUT);      // Led Pin used for signaling an underflow condition
  
  pinMode(12,OUTPUT);      // All axis on/off
  
  pinMode(2,OUTPUT);       // PD2, Heat Relay on/off low active for letmathe mdlcnc
  digitalWrite(2, HIGH);   // Off
  
  pinMode(3,OUTPUT);       // PWM for wire heating
  analogWrite(3, 0);       // Off
  
  // Motor Pins  
  pinMode(8,OUTPUT);   // PB0, X1 Clock
  pinMode(9,OUTPUT);   // PB1, X2 Clock
  pinMode(10,OUTPUT);  // PB2, Y1 Clock
  pinMode(11,OUTPUT);  // PB3, Y2 Clock
  
  pinMode(4,OUTPUT);   // PD4, X1 Direction
  pinMode(5,OUTPUT);   // PD5, X2 Direction
  pinMode(6,OUTPUT);   // PD6, Y1 Direction
  pinMode(7,OUTPUT);   // PD7, Y2 Direction
  
  // using timer1 (because timer2 is used for pwm on pin3 and timer0 is used for delay functions)
  // using Fast PWM (Mode 15)
  TCCR1A = 0x03;          // wgm11=1 wgm0=10
  TCCR1B = 0x1C;          // wgm13=1 wgm12=1    cs12=1 cs11=0 cs10=0 /256 prescaler
  OCR1A  = 255;           // Timer compare register. 255 = 250Hz, 127 = 500Hz, 63 = 1 KHz
  TIMSK1 = (1 << OCIE1A); // Timer interrupt enable for match to OCR1A
  TIFR1  = 0;             // Clear flags.
  TCNT1  = 0;
  sei();
  
}

/**********************************************************************************/
void sendMotorCmd(byte cmd)
{
  PORTD = (PORTD & 0x0F) | (cmd & 0xf0); // Directions first!
  PORTB = (PORTB & 0xF0) | (cmd & 0x0f); // and step
   delayMicroseconds(25); // eventually wait a little bit
   // and falling edge of step pulse
  PORTB = (PORTB & 0xF0);
}



/**********************************************************************************/
void handleCommand()
{

 byte val = cmdArray[arrayIdxRead+1]; // The command parameter value
  switch(cmdArray[arrayIdxRead])
  {
    case 'A':   // All Motors on/off
      if(val == '1')  {digitalWrite(12, HIGH);}
      else          {digitalWrite(12, LOW);}
    break;
    case 'H':   // Wire Heat ON/OFF (may be programmed as PWM (analog out))
      if(val > 0)   {digitalWrite(2, LOW);}
      else          {digitalWrite(2, HIGH);}
      analogWrite(3,val*2.55); // PWM for wire heating (stretch 0-100% to a range of 0-255)
    break;
    case 'M':   // Motor step Command
      sendMotorCmd(val);
    break;
    case 'F':   // Change the timer frequency, the time between two steps
		// OCR1A values 255 = 250Hz 190 = 328Hz 127 = 500hz 63 = 1 Khz
       if(val>127) val =127;  // restrict from 0 to 127 corresponds to 1kHz to 0.5kHz
       OCR1A = 63 + val;  
    break;
  }

}

/**********************************************************************************/
ISR(TIMER1_COMPA_vect) {
  
  if (isrActive) return;
  isrActive = true;  
  sei(); // reenable interrupts, to not miss serial data
  
do {
      // check if the buffer is empty
      if((arrayIdxRead != arrayIdxWrite) || ovf)
      {
        
        handleCommand();
        arrayIdxRead += 2;
        if(arrayIdxRead==CMD_BUFFER_SIZE) arrayIdxRead=0;
		
        noInterrupts();
        cmdCounter--;
        interrupts();
		
        if (ovf && (cmdCounter<CMD_BUFFER_SIZE/2-25))
        {
          Serial.write('C');
          ovf = false;
        } 
        digitalWrite(13, LOW); // underflow led off
      }
      else
      {
        // underflow !!
        digitalWrite(13, HIGH); // underflow led on
        break;
      }
    } while(cmdArray[arrayIdxRead] != 'M'); // only motor commands will wait for next sync, all others can be handled immediately
    
  cli();
  
  isrActive = false;    
}

/**********************************************************************************/
/**** The main loop                                                           *****/
/**********************************************************************************/
void loop() 
{ 
  if (Serial.available() > 0)
  {    
    // Each command consists of 2 bytes
    Serial.readBytes((char*)&cmdArray[arrayIdxWrite],2);

    // korrekt the write index
    arrayIdxWrite+=2;
    if(arrayIdxWrite==CMD_BUFFER_SIZE) arrayIdxWrite=0;
	  
    noInterrupts();
    cmdCounter++;
    interrupts();

    
    // check for oncoming overflow    
    if(cmdCounter >= CMD_BUFFER_SIZE/2-20)
    {
      ovf = true;
      Serial.write('S'); // Stop transmission, Buffer full
    }
  
  }
}

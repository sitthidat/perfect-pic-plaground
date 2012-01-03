/************************************************************************
	main.c

    usbGenericHidCommunication reference firmware 1_0_0_0
    Copyright (C) 2010 Simon Inns

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

	Email: simon.inns@gmail.com

************************************************************************/

#ifndef MAIN_C
#define MAIN_C

// Global includes
#include <htc.h>

// Local includes
#include "usb.h"
#include "HardwareProfile.h"
#include "usb_function_hid.h"
#include "genericHID.h"

// PIC 18F4550 fuse configuration:
// Config word 1 (Oscillator configuration)
// 20Mhz crystal input scaled to 48Mhz and configured for USB operation
__CONFIG(1, USBPLL & IESODIS & FCMDIS & HSPLL & CPUDIV1 & PLLDIV5);
// Config word 2
__CONFIG(2, VREGEN & PWRTDIS & BOREN & BORV20 & WDTDIS & WDTPS32K);
// Config word 3
__CONFIG(3, PBDIGITAL & LPT1DIS & MCLREN);
// Config word 4
__CONFIG(4, XINSTDIS & STVREN & LVPDIS & ICPORTDIS & DEBUGDIS);
// Config word 5, 6 and 7 (protection configuration)
__CONFIG(5, UNPROTECT);
__CONFIG(6, UNPROTECT);
__CONFIG(7, UNPROTECT);

	//Globals
bit on;

// Success indication globals (used to keep the success LED on so you can see it)
long int successIndicatorCounter = 0;
unsigned char successIndicatorFlag = FALSE;

// Failure indication globals (used to keep the failure LED on so you can see it)
long int failureIndicatorCounter = 0;
unsigned char failureIndicatorFlag = FALSE;

// Variables required for keeping track of a bulk receive command
unsigned char bulkReceiveFlag = FALSE;
int bulkReceivePacketCounter = 0;
int bulkReceiveExpectedPackets = 0;

// Variables required for keeping track of a bulk send command
unsigned char bulkSendFlag = FALSE;
int bulkSendPacketCounter = 0;
int bulkSendExpectedPackets = 0;

unsigned long *sendLongPointer;
unsigned int *sendIntPointer;

// Initialise system
static void InitialiseSystem(void)
{
	// PIC port set up --------------------------------------------------------

	// Default all pins to digital
    ADCON1 = 0x0F;

	// Note: To complile this code for the PIC18F2550 remove the TRISD, TRISE,
	// PORTD and PORTE commands below.  Then edit HardwareProfile.h to match
	// your target device (only the LED ports are really important).

	// Configure ports as inputs (1) or outputs(0)
	TRISA = 0b00000010;
	TRISB = 0b00000000;
	TRISC = 0b00000000;
	TRISD = 0b00000000;
	TRISE = 0b00000000;

	// Clear all ports
	PORTA = 0b00000000;
	PORTB = 0b00000000;
	PORTC = 0b00000000;
	PORTD = 0b00000000;
	PORTE = 0b00000000;

	//INT0 External Interrupt Enable. 
//	INT0IE = 1;

	//PEIE = Peripheral Interrupt Enable. Enables all interrupts from pins?
	PEIE = 1;

	//Global Interrupt Enable. Enables or disables all interrupts
	GIE = 1;

	sendIntPointer = (unsigned int*)&ToSendDataBuffer[0];
	sendLongPointer = (unsigned long*)&ToSendDataBuffer[0];

	// USB set up -------------------------------------------------------------

	// USB stack and parameter initialisation
    #if defined(USE_SELF_POWER_SENSE_IO)
    tris_self_power = INPUT_PIN;
    #endif
    
    // Initialize the variable holding the handle for the last
    // transmission
    USBOutHandle = 0;
    USBInHandle = 0;
    
	// Call the stack initialisation functions
    USBDeviceInit();
}

// Process input and output
void ProcessIO(void)
{   
    // If we are not in the configured state just return
    if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1))
    {
	    // We are not configured, set LED1 to off to show status
	    LED1 = OFF;
	    return;
	}
	
	// Note: For all tests we expect to receive a 64 byte packet containing
	// the command in byte[0] and then the numbers 0-62 in bytes 1-63.

	unsigned char bufferPointer;
    unsigned char expectedData;
    unsigned char dataReceivedOk;
    unsigned char dataSentOk;

	// Check if data was received from the host.
    if(!HIDRxHandleBusy(USBOutHandle))
    {   
	    // Test to see if we are in bulk send/receieve mode, or if we are waiting for a command
	    if (bulkSendFlag == TRUE || bulkReceiveFlag == TRUE)
	    {
		    // We are either bulk sending or receieving
		    
		    // If we are bulk sending, check that we are not busy and send the next packet
		    if (bulkSendFlag == TRUE && !HIDTxHandleBusy(USBInHandle))
		    {
				// Send the next packet
				expectedData = bulkSendPacketCounter;
	            
	            for (bufferPointer = 0; bufferPointer < 64; bufferPointer++)
	            {
		            	ToSendDataBuffer[bufferPointer] = expectedData;
		        }
		        
		        // Transmit the response to the host
                USBInHandle = HIDTxPacket(HID_EP,(BYTE*)&ToSendDataBuffer[0],64);
                
                // Add this following delay in if you want to simulate a slow transfer
                // from a busy PIC when testing bulk sends:
                
                // for (long int counter = 0; counter < 1000; counter++) __delay_us(100); 
				
				bulkSendPacketCounter++; // Next packet
				
				// Are we done yet?
				if (bulkSendPacketCounter == bulkSendExpectedPackets)
				{
					// All done, indicate success and go back to command mode
					bulkSendFlag = FALSE;
					successIndicatorFlag = TRUE;
				}	
			}
			
			// If we are bulk receiving get the next packet
			if (bulkReceiveFlag == TRUE)
			{
				// The received data buffer is already filled by the USB stack
				// we just have to confirm the data integrity
				
				expectedData = bulkReceivePacketCounter;
				
				for (bufferPointer = 0; bufferPointer < 64; bufferPointer++)
				{
					// If the data isn't what we expected, turn on the failure light
					if (ReceivedDataBuffer[bufferPointer] != expectedData)
						failureIndicatorFlag = TRUE;
				}
				
				bulkReceivePacketCounter++;
				
				// Are we done yet?
				if (bulkReceivePacketCounter == bulkReceiveExpectedPackets)
				{
					// All done, indicate success and go back to command mode
					bulkReceiveFlag = FALSE;
					successIndicatorFlag = TRUE;
				}
			}
		}
		else
		{
			// Command mode    
	        switch(ReceivedDataBuffer[0])
			{
//	            case 0x80:  // Test 1 - Single packet write to device
//	            	// Test the receieved data
//	            	expectedData = 0;
//	            	dataReceivedOk = TRUE;
//	            	for (bufferPointer = 1; bufferPointer < 64; bufferPointer++)
//	            	{
//		            	if (ReceivedDataBuffer[bufferPointer] != expectedData)
//		            		dataReceivedOk = FALSE;
//		            	
//		            	expectedData++;
//		            }
//		            
//		            // Display the test result
//		            if (dataReceivedOk == TRUE) successIndicatorFlag = TRUE;
//		            	else failureIndicatorFlag = TRUE;
//	            	break;
//	            	
//	            case 0x81:	// Test 2 - Single packet read/write
//	            	// Test the receieved data
//	            	expectedData = 0;
//	            	dataReceivedOk = TRUE;
//	            	for (bufferPointer = 1; bufferPointer < 64; bufferPointer++)
//	            	{
//		            	if (ReceivedDataBuffer[bufferPointer] != expectedData)
//		            		dataReceivedOk = FALSE;
//		            	
//		            	expectedData++;
//		            }
//		            
//		            // If we got the data correctly, send the response packet
//		            if (dataReceivedOk == TRUE)
//		            {
//			            expectedData = 0;
//			            dataSentOk = TRUE;
//			            
//			            for (bufferPointer = 0; bufferPointer < 64; bufferPointer++)
//			            {
//				            ToSendDataBuffer[bufferPointer] = expectedData;
//				            expectedData++;
//				        }
//				        
//				        // Transmit the response to the host
//		                if(!HIDTxHandleBusy(USBInHandle))
//						{
//							USBInHandle = HIDTxPacket(HID_EP,(BYTE*)&ToSendDataBuffer[0],64);
//						}
//						
//						// Show our success
//						successIndicatorFlag = TRUE;
//			        }
//			        else failureIndicatorFlag = TRUE;
//	            	break;
//	            	
//	            case 0x82:	// Test 3 - Single packet write, 128 packets read
//	                // Test the receieved data
//	            	expectedData = 0;
//	            	dataReceivedOk = TRUE;
//	            	for (bufferPointer = 1; bufferPointer < 64; bufferPointer++)
//	            	{
//		            	if (ReceivedDataBuffer[bufferPointer] != expectedData)
//		            		dataReceivedOk = FALSE;
//		            	
//		            	expectedData++;
//		            }
//		            
//		            // If the receive was ok, go into bulk sending mode
//		            if (dataReceivedOk == TRUE)
//		            {
//			            // Go into bulk sending mode...
//			            bulkSendExpectedPackets = 128;
//			            bulkSendFlag = TRUE;
//			            bulkSendPacketCounter = 0;
//			        }
//		            else failureIndicatorFlag = TRUE;
//	            	break;
//	            	
//	            case 0x83:	// Test 4 - 128 packets write, single packet read
//	            	// Test the receieved data
//	            	expectedData = 0;
//	            	dataReceivedOk = TRUE;
//	            	for (bufferPointer = 1; bufferPointer < 64; bufferPointer++)
//	            	{
//		            	if (ReceivedDataBuffer[bufferPointer] != expectedData)
//		            		dataReceivedOk = FALSE;
//		            	
//		            	expectedData++;
//		            }
//		            
//		            // If the receive was ok, go into bulk receiving mode
//		            if (dataReceivedOk == TRUE)
//		            {
//			            // Go into bulk sending mode...
//			            bulkReceiveExpectedPackets = 127;
//			            bulkReceiveFlag = TRUE;
//			            bulkReceivePacketCounter = 0;
//			        }
//		            else failureIndicatorFlag = TRUE;
//	            	break;
//	            	
//	            case 0x84:	// Test 5 - Single Packet write, timeout on read
//	            	// Here we receive a command which expects a reply, but we
//	            	// deliberately don't send one to test the timeout mechanisms
//	            	// in the communication class
//	            	
//	            	// Test the receieved data
//	            	expectedData = 0;
//	            	dataReceivedOk = TRUE;
//	            	for (bufferPointer = 1; bufferPointer < 64; bufferPointer++)
//	            	{
//		            	if (ReceivedDataBuffer[bufferPointer] != expectedData)
//		            		dataReceivedOk = FALSE;
//		            	
//		            	expectedData++;
//		            }
//		            
//		            // Display the test result
//		            if (dataReceivedOk == TRUE) successIndicatorFlag = TRUE;
//		            	else failureIndicatorFlag = TRUE;
//		            	
//		            // Now we quit without replying
//	            	break;
				case 'T': //Toggle LED
					on = !on;
					LED1 = on;
					break;
				case 'B':	//Test PORTB Hypothesis
					RB0 = ON;
					break;
				case 0x02:
			        // Transmit the response to the host
					if (ReceivedDataBuffer[2] == 0x03)
						LED1 = ON;
					for (int i=0;i<10;i++) {
						ToSendDataBuffer[i] = ReceivedDataBuffer[i];
					}
	                if(!HIDTxHandleBusy(USBInHandle))
					{
						USBInHandle = HIDTxPacket(HID_EP,(BYTE*)&ToSendDataBuffer[0],64);
					}
	            default:	// Unknown command received
	           		break;
			}
		}
		
		// Only rearm the OUT endpoint if we are not bulk sending
		if (bulkSendFlag == FALSE)
		{      
	        // Re-arm the OUT endpoint for the next packet
	        USBOutHandle = HIDRxPacket(HID_EP,(BYTE*)&ReceivedDataBuffer,64);
  		}      
    }  
}

// Main function
void main(void)
{
    InitialiseSystem();

	// We are up and running, show 'ready' status in LED0
	LED0 = ON;
	on = OFF;

    while(1)
    {
		// Check bus status and service USB interrupts.
        USBDeviceTasks();

		// Application-specific tasks.
        ProcessIO();        
    }
}

#endif

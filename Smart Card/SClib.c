/********************************************************************
 FileName:		SClib.c
 Dependencies:	See INCLUDES section
 Processor:		PIC18,PIC24,PIC32 & dsPIC33F Microcontrollers
 Hardware:		This demo is natively intended to be used on Exp 16, LPC
 				& HPC Exp board. This demo can be modified for use on other hardware
 				platforms.
 Complier:  	Microchip C18 (for PIC18), C30 (for PIC24 & dsPIC) & C32 (for PIC32) 
 Company:		Microchip Technology, Inc.

 Software License Agreement:

 The software supplied herewith by Microchip Technology Incorporated
 (the �Company�) for its PIC� Microcontroller is intended and
 supplied to you, the Company�s customer, for use solely and
 exclusively on Microchip PIC Microcontroller products. The
 software is owned by the Company and/or its supplier, and is
 protected under applicable copyright laws. All rights are reserved.
 Any use in violation of the foregoing restrictions may subject the
 user to criminal sanctions under applicable laws, as well as to
 civil liability for the breach of the terms and conditions of this
 license.

 THIS SOFTWARE IS PROVIDED IN AN �AS IS� CONDITION. NO WARRANTIES,
 WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED
 TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE COMPANY SHALL NOT,
 IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
 CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.

********************************************************************
 File Description:

 Change History:
  Rev   Description
  ----  -----------------------------------------
  1.0   Initial release
  1.01  Cleaned up unnecessary variables,supported T=1 protocol
        and improvments in T=0 functions following the coding standards
  1.02  Disciplined the code. No functional changes made.
********************************************************************/

#include <string.h>
#include "./Smart Card/SClib.h"
#include "sc_config.h"

#if defined(__PIC24F__) || defined(__PIC24H__)
	#include "./Smart Card/SCpic24.h"
#elif defined(__PIC32MX__)
	#include "./Smart Card/SCpic32.h"
#elif defined(__dsPIC33F__)
	#include "./Smart Card/SCdspic33f.h"
#else
	#ifdef __18CXX
	#include "./Smart Card/SCpic18.h"
	#else
	#error "Only PIC18 and PIC24F currently supported by SmartCard Library"
	#endif
#endif

#define MAX_ATR_LEN			(BYTE)33

BYTE scCardATR[MAX_ATR_LEN];
BYTE scATRLength = 0;

BYTE scTA1, scTA2, scTA3;
BYTE scTB1, scTB2, scTB3;
BYTE scTC1, scTC2, scTC3;
BYTE scTD1, scTD2, scTD3;

BYTE* scATR_HistoryBuffer = NULL;
BYTE  scATR_HistoryLength = 0;

typedef enum
{
	UNKNOWN,
	ATR_ON
} SC_STATUS;

SC_STATUS gCardState = UNKNOWN;
SC_ERROR scLastError = SC_ERR_NONE;

// Work Wait time for T=0 Protocol in units of etu's
unsigned long cgt;
unsigned long t0WWTetu;
unsigned long t0WWT;	

static void SC_WaitTime(void);

#ifdef SC_PROTO_T1

	#define R_BLOCK_IDENTIFIER		(BYTE)0x80
	#define S_BLOCK_IDENTIFIER		(BYTE)0xC0
	#define M_BIT_SET				(BYTE)0x20
	#define M_BIT_CLR				(BYTE)0xDF
	#define S_BIT_SET				(BYTE)0x40
	#define S_BIT_CLR				(BYTE)0xBF
	#define S_BIT_POSITION			(BYTE)0x40

	unsigned long t1BWT;
	unsigned long t1CWT;
	unsigned long t1BGT;
	unsigned int t1BWTetu;
	unsigned int t1CWTetu;

	BYTE t1BGTetu = 22;

	BYTE edcType = SC_LRC_TYPE_EDC;
	BYTE maxSegmentLength = 0x20;
	BOOL txSbit = TRUE;

	static WORD SC_UpdateCRC(BYTE data,WORD crc);
	static void SC_UpdateEDC(BYTE data,WORD *edc);
	static void SC_SendT1Block(BYTE nad,BYTE pcb,WORD length,BYTE *buffer);
	static BOOL SC_ReceiveT1Block(BYTE *rxNAD,BYTE *rxPCB,BYTE *rxLength,BYTE *buffer,unsigned long blockWaitTime);

#endif

// Character Guard Time for T=0 & T=1 Protocol
BYTE cgtETU;

// CLA set to '00' = no command chaining, 
//                   no secure messaging, 
//					 basic logical channel.

/*******************************************************************************
  Function:
    void SC_Initialize(void)
	
  Description:
    This function initializes the smart card library

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None
	
  Remarks:
    None
  *****************************************************************************/
void SC_Initialize()
{
	//Initialize the low level driver
	SCdrv_InitUART();

	// Initialize smart card library variables
	txSbit = TRUE;
}

/*******************************************************************************
  Function:
	BOOL SC_CardPresent(void)	
  
  Description:
    This macro checks if card is inserted in the socket

  Precondition:
    SC_Initialize() is called

  Parameters:
    None

  Return Values:
    TRUE if Card is inserted, otherwise return FALSE
	
  Remarks:
    None
  *****************************************************************************/
BOOL SC_CardPresent()
{
	return SCdrv_CardPresent();
}	

/*******************************************************************************
  Function:
	BOOL SC_PowerOnATR(void)	
  
  Description:
    This function performs the power on sequence of the SmartCard and 
	interprets the Answer-to-Reset data received from the card.

  Precondition:
    SC_Initialize() is called, and card is present

  Parameters:
    None

  Return Values:
    TRUE if Answer to Reset (ATR) was successfuly received and processed
	
  Remarks:
    None
  *****************************************************************************/
BOOL SC_PowerOnATR()
{
	unsigned long atrDelayCnt;

	if( !SCdrv_CardPresent() )  //check card present
	{
		gCardState = UNKNOWN;
		return FALSE;
	}
		
	SCdrv_SetSwitchCardReset(0);  //make sure card in reset
	memset( scCardATR, 0xFF, sizeof scCardATR );
	WaitMilliSec(2);	

	#ifdef ENABLE_SC_POWER_THROUGH_PORT_PIN
		SCdrv_SetSwitchCardPower(1);	//Turn on power
	#endif

	scATR_HistoryLength = 0;
	scATR_HistoryBuffer = NULL;
	gCardState = UNKNOWN;
	scLastError = SC_ERR_NONE;
	scATRLength = 0;
//	t0WWT = (9600UL * (FCY/baudRate))/4;
	
	// Wait count for maximum of 40,000 to 45,000 smard card clock cycles 
	// to get an ATR from card
	atrDelayCnt = 40000UL * (FCY/scReferenceClock);
	
	WaitMilliSec(2);

	SCdrv_EnableUART();

	WaitMilliSec(2);

	//Start the clock
	SCdrv_EnableClock();	

	// Wait for atleast 400 Clock Cycles after applying reference clock to card.
	WaitMilliSec(2);

	SCdrv_SetSwitchCardReset(1);  //Release card reset line. set to high state

	while(1)  ///////////////// Read Answer to RESET
	{
		if( SCdrv_GetRxData( &scCardATR[scATRLength], atrDelayCnt ) ) //wait for data byte from CARD
		{
			scATRLength++;

			if( scATRLength == MAX_ATR_LEN )
				break;
//			else
//				atrDelayCnt = t0WWT;
		}
		else
			break;	//no data
	}
		
	//decode the ATR values
	if( scATRLength >= 3 ) //min TS, T0 and setup byte
	{
		BYTE T0 = scCardATR[1];	
		BYTE atrIdx = 2;
		
		//Extract Interface bytes TAx TBx TCx and TDx from ATR
		
		scTA1 = scTB1 = scTC1 = scTD1 = 0;
		scTA2 = scTB2 = scTC2 = scTD2 = 0;
		scTA3 = scTB3 = scTC3 = scTD3 = 0;
		
		// Read the global interface bytes
		
		if( T0 & 0x10 )
			scTA1 = scCardATR[atrIdx++];
			
		if( T0 & 0x20 )
			scTB1 = scCardATR[atrIdx++];
			
		if( T0 & 0x40 )
			scTC1 = scCardATR[atrIdx++];

		if( T0 & 0x80 )
			scTD1 = scCardATR[atrIdx++];
			
		//read the next set of interface bytes if present
		if( scTD1 & 0xF0 )
		{
			if( scTD1 & 0x10 )
				scTA2 = scCardATR[atrIdx++];
				
			if( scTD1 & 0x20 )
				scTB2 = scCardATR[atrIdx++];
				
			if( scTD1 & 0x40 )
				scTC2 = scCardATR[atrIdx++];
				
			if( scTD1 & 0x80 )
				scTD2 = scCardATR[atrIdx++];
				
			if( scTD2 & 0xF0 )
			{
				if( scTD2 & 0x10 )
				{
					scTA3 = scCardATR[atrIdx++];
					
					if ((scTA3 < 0x10) || (scTA3 == 0xFF))
					{
						SC_Shutdown();
						scLastError = SC_ERR_ATR_DATA;
						return FALSE;
					}

					#ifdef SC_PROTO_T1
						maxSegmentLength = scTA3;
					#endif
				}

				if( scTD2 & 0x20 )
					scTB3 = scCardATR[atrIdx++];
					
				if( scTD2 & 0x40 )
				{
					scTC3 = scCardATR[atrIdx++];

					#ifdef SC_PROTO_T1
						edcType = (scTC3 & 0x01) ? SC_CRC_TYPE_EDC : SC_LRC_TYPE_EDC;
					#endif
				}

				if( scTD2 & 0x80 )
					scTD3 = scCardATR[atrIdx++];				
			}
		}
		
		scATR_HistoryLength = T0 & 0x0F;
		scATR_HistoryBuffer = (scATR_HistoryLength)?(&scCardATR[atrIdx]):NULL;
		SC_WaitTime();
		gCardState = ATR_ON;
		return TRUE;
	}
	else
	{
		// Not a Valid ATR Reponse
		scLastError = SC_ERR_BAR_OR_NO_ATR_RESPONSE;
		gCardState = UNKNOWN;	
		SC_Shutdown();	
		return FALSE;
	}	
}


/*******************************************************************************
  Function:
	BOOL SC_DoPPS(void)	
  
  Description:
    This function does the PPS to the card & configures the baud rate of the PIC UART
	to match with Answer-to-Reset data from smartcard.

  Precondition:
    SC_PowerOnATR was success

  Parameters:
    None

  Return Values:
    TRUE if Baud rate is supported by the PIC
	
  Remarks:
    This function is called when SC_PowerOnATR() returns TRUE.  If the Baud 
	rate configration file inside the card is changed, these function should 
	be called again for the new baud to take effect.
  *****************************************************************************/
BOOL SC_DoPPS()
{
	if( !SCdrv_CardPresent() || gCardState != ATR_ON )
		return FALSE;
	
	if( scTA1 == 0x11 )	//card using 9600 baud.  no need to configure
		return TRUE;

	// If TA2 is absent & TD1 is present
	if(!(scTD1 & 0x10) && (scCardATR[1] & 0x80) && !(scTD1 & 0x0F))	
	{
		SCdrv_SendTxData( 0xFF ); // PPSS Byte = 0xFF always

		if(scCardATR[1] & 0x10)
		{	
			SCdrv_SendTxData( 0x10 );	// PPS0
			SCdrv_SendTxData( scTA1 );	// PPS1
			SCdrv_SendTxData( 0xFF ^ 0x10 ^ scTA1 );	// PCK

			SCdrv_SetBRG( scTA1 );	//tell the driver to configure baud rate
			SC_WaitTime();
		}
		else
		{
			SCdrv_SendTxData( 0x00 );	// PPS0
			SCdrv_SendTxData( 0xFF);	// PCK
		}		
	}

	return TRUE;
}

/*******************************************************************************
  Function:
    int SC_GetCardState(void)
	
  Description:
    This function returns the current state of SmartCard

  Precondition:
    SC_Initialize is called.

  Parameters:
    None

  Return Values:
    SC_STATE_CARD_NOT_PRESENT:		No Card Detected
    SC_STATE_CARD_ACTIVE:			Card is powered and ATR received
    SC_STATE_CARD_INACTIVE:			Card present but not powered
	
  Remarks:
    None
  *****************************************************************************/
int SC_GetCardState()
{
	if( !SCdrv_CardPresent() )
		return SC_STATE_CARD_NOT_PRESENT;
	else if( gCardState == ATR_ON )
		return SC_STATE_CARD_ACTIVE;
	else
		return SC_STATE_CARD_INACTIVE;
}		

/*******************************************************************************
  Function:
    void SC_Shutdown(void)
	
  Description:
    This function Performs the Power Down sequence of the SmartCard

  Precondition:
    SC_Initialize is called.

  Parameters:
    None

  Return Values:
    None
	
  Remarks:
    None
  *****************************************************************************/
void SC_Shutdown()
{
	SCdrv_SetSwitchCardReset(0);	//bring reset line low
	WaitMilliSec(1);
	SCdrv_CloseUART();			//shut down UART and remove any pullups
	#ifdef ENABLE_SC_POWER_THROUGH_PORT_PIN
		SCdrv_SetSwitchCardPower(0);   //Turn Off Card Power
	#endif
	gCardState = UNKNOWN;
}


/*******************************************************************************
  Function:
    void SC_WaitTime(void)
	
  Description:
    This function calculates the work wait time for T=0 Protocol

  Precondition:
    SC_PowerOnATR is called.

  Parameters:
    None

  Return Values:
    None
	
  Remarks:
    This function is planned to calculate CWT & BWT for T=1 protocol in future.
  *****************************************************************************/
static void SC_WaitTime(void)
{
	BYTE ta1Code,tb2Code,index;
	BYTE tempVariable1;
	unsigned int tempVariable2 = 1;
	
	ta1Code = scTA1 & 0x0F;

	factorDNumerator = 1;
	factorDdenominator = 1;

	// Calculate Factor 'D' from TA1 value
	switch(ta1Code)
	{
		case 0x00:
		case 0x07:
		case 0x01:
					break;

		case 0x02:
					factorDNumerator = 2;
					break;

		case 0x03:
					factorDNumerator = 4;
					break;

		case 0x04:
					factorDNumerator = 8;
					break;

		case 0x05:
					factorDNumerator = 16;
					break;

		case 0x06:
					factorDNumerator = 32;
					break;

		case 0x08:
					factorDNumerator = 12;
					break;

		case 0x09:
					factorDNumerator = 20;
					break;

		case 0x0A:
					factorDdenominator = 2;
					break;

		case 0x0B:
					factorDdenominator = 4;
					break;

		case 0x0C:
					factorDdenominator = 8;
					break;

		case 0x0D:
					factorDdenominator = 16;
					break;

		case 0x0E:
					factorDdenominator = 32;
					break;

		case 0x0F:
					factorDdenominator = 64;
					break;
	}

	ta1Code = (scTA1 & 0xF0) >> 4;
		
	// Calculate Factor 'F' from TA1 value
	switch(ta1Code)
	{
		case 0x00:
		case 0x07:
		case 0x08:
		case 0x0E:
		case 0x0F:
					break;
	
		case 0x01:
					factorF = 372;
					break;
	
		case 0x02:
					factorF = 558;
					break;

		case 0x03:
					factorF = 744;
					break;
	
		case 0x04:
					factorF = 1116;
					break;
	
		case 0x05:
					factorF = 1488;
					break;
	
		case 0x06:
					factorF = 1860;
					break;
	
		case 0x09:
					factorF = 512;
					break;
	
		case 0x0A:
					factorF = 768;
					break;
	
		case 0x0B:
					factorF = 1024;
					break;
	
		case 0x0C:
					factorF = 1536;
					break;
	
		case 0x0D:
					factorF = 2048;
					break;	
	}
	
	if(scTC1 != 0xFF)
	{
		cgtETU = 12 + (BYTE)((unsigned long)((unsigned long)(factorF * (unsigned long)factorDdenominator * scTC1)/factorDNumerator)/scReferenceClock);
	}

	// Check whether T=0 or T=1 protocol ?
	switch(scTD1 & 0x0F)
	{
		case 1 :
					// Calculate Character Guard Time in ETU's for T=1 Protocol
					if(scTC1 == 0xFF)
					{
						cgtETU = (BYTE)11;
					}
		
					#ifdef SC_PROTO_T1

						if(scTD1 & 0x20)
						{
							tb2Code = scTB2 & 0x0F;
						
							tempVariable1 = (scTB2 & 0xF0) >> 4;
						}
						else
						{
							tb2Code = SC_CWI;

							tempVariable1 = SC_BWI;

						}

						for(index = 0;index < tb2Code;index++)
							tempVariable2 = tempVariable2 * 2;

						// Calculate Character Wait Time in ETU's for T=1 Protocol as set in the card
						t1CWTetu = 11 + tempVariable2;
						
						tempVariable2 = 1;

						for(index = 0;index < tempVariable1;index++)
							tempVariable2 = tempVariable2 * 2;

						// Calculate Block Wait Time in ETU's for T=1 Protocol as set in the card						
						t1BWTetu = 11 + (unsigned int)((unsigned long)(tempVariable2 * 35712UL)/(scReferenceClock/10));
					
					#endif

					break;
		case 0 :
		default :

					// Calculate Character Guard Time in ETU's for T=1 Protocol		
					if(scTC1 == 0xFF)
					{
						cgtETU = (BYTE)12;
					}

					// If scTC2 is transmitted by the card then calculate work wait time
					// or else use default value
					if(scTD1 & 0x40)
					{
						tempVariable1 = scTC2;
					}
					else
					{
						tempVariable1 = SC_WI;
					}

					t0WWTetu = (unsigned long)(tempVariable1 * (unsigned long)factorDNumerator * 960)/factorDdenominator;

					break;
	}

	// Calculate Character Guard Time in number of Instruction Counts for T=0/T=1 Protocol		
	cgt = cgtETU * (FCY/baudRate);

	// Calculate Work Wait Time in number of Instruction Counts for T=0 Protocol
	t0WWT = t0WWTetu * (FCY/baudRate);

	#ifdef SC_PROTO_T1

		// Calculate Character Wait Time in number of Instruction Counts for T=1 Protocol
		t1CWT = t1CWTetu * (FCY/baudRate);

		// Calculate Block Guard Time in number of Instruction Counts for T=1 Protocol
		t1BGT = t1BGTetu * (FCY/baudRate);

		// Calculate Block Wait Time in number of Instruction Counts for T=1 Protocol
		t1BWT = t1BWTetu * (FCY/baudRate);

	#endif
}	

/*******************************************************************************
  Function:
	BOOL SC_TransactT0(SC_APDU_COMMAND* apduCommand, SC_APDU_RESPONSE* apduResponse, BYTE* apduDataBuffer)	
  
  Description:
    This function Sends the ISO 7816-4 compaliant APDU commands to the card.  
	It also receive the expected response from the card as defined by the 
	command data.

  Precondition:
    SC_DoPPS was success

  Parameters:
    SC_APDU_COMMAND* apduCommand	- Pointer to APDU Command Structure 
	SC_APDU_RESPONSE* pResp - Pointer to APDU Response structure
			BYTE* pResp - Pointer to the Command/Response Data buffer

  Return Values:
    TRUE if transaction was success, and followed the ISO 7816-4 protocol. 
	
  Remarks:
    In the APDU command structure, the LC field defines the number of bytes to 
	transmit from the APDUdat array.  This array can hold max 256 bytes, which 
	can be redefined by the user.  The LE field in APDU command defines the number
	of bytes to receive from the card.  This array can hold max 256 bytes, which 
	can be redefined by the user.
	
  *****************************************************************************/
BOOL SC_TransactT0(SC_APDU_COMMAND* apduCommand, SC_APDU_RESPONSE* apduResponse, BYTE* apduDataBuffer)
{
	BYTE* apduCommandBuffer;
	BYTE index,lc = apduCommand->LC,le = apduCommand->LE,ins = apduCommand->INS;
	BYTE rx_char;
	BYTE lcLength = 0,leLength = 0;
	unsigned int txDelay;

	// Return False if there is no Card inserted in the Slot
	if( !SCdrv_CardPresent() || gCardState != ATR_ON )
	{
		scLastError = SC_ERR_CARD_NOT_PRESENT;
		return FALSE;	
	}	

	// Clear APDU Response data if present before getting the new one's
	memset( apduResponse, 0, sizeof(SC_APDU_RESPONSE) );
	
	apduCommandBuffer = (BYTE*)apduCommand;
	
	txDelay = cgt/8;

	//Send the Command Bytes: CLA INS P1 P2
	for( index = 0; index < 4; index++ )
	{
		SCdrv_SendTxData( apduCommandBuffer[index] );
		SC_Delay(txDelay);
	}	
	
	//Now transmit LE or LC field if non zero
	if( lc )
		SCdrv_SendTxData( lc );
	else if( le )
		SCdrv_SendTxData( le );

	while (1)
	{
    	// Get Procedure byte
		if(!SCdrv_GetRxData( &rx_char, t0WWT ) ) //wait for data byte from CARD
		{
			scLastError = SC_ERR_CARD_NO_RESPONSE;
			return FALSE;	//no response received
		}	

		// Process Procedure Byte
		if (rx_char == 0x60)
		{
			// Do Nothing
    	}
    	else if (((rx_char & 0xF0) == 0x60) || ((rx_char & 0xF0) == 0x90))
    	{
      		// SW1, get SW2
			apduResponse->SW1 = rx_char; //save SW1
			
			//now receive SW2
			if( SCdrv_GetRxData( &rx_char, t0WWT ) ) //wait for data byte from CARD
				apduResponse->SW2 = rx_char;
			else
			{
				scLastError = SC_ERR_CARD_NO_RESPONSE;
				return FALSE;	//no response received
			}

			break;
    	}
		else if(rx_char == ins)
		{
			// Send all remaining bytes
			if( lcLength < lc)		//transmit app data if any
			{
				WaitMicroSec( 700 );	//cannot send the message data right away after the initial response
				SC_Delay(txDelay);

				for(;lcLength < lc; lcLength++ )
				{	
					SCdrv_SendTxData( apduDataBuffer[lcLength] );
					SC_Delay(txDelay);
				}
			}
			else
			{
				// Recive all remaining bytes
				for(;leLength < le; leLength++ )
				{	
					if( SCdrv_GetRxData( &rx_char, t0WWT ) ) //wait for data byte from CARD
						apduDataBuffer[leLength] = rx_char;	
					else
					{
						scLastError = SC_ERR_CARD_NO_RESPONSE;
						return FALSE;	//no response received
					}
				}		
			}
		}
		else if(rx_char == ~ins)
		{
        	// ACK, send one byte if remaining
    		if (lcLength < lc)
      		{
				SC_Delay(txDelay);

				SCdrv_SendTxData( apduDataBuffer[lcLength++] );
      		}
      		else
      		{
				//wait for data byte from CARD or timeout
				if( SCdrv_GetRxData( &rx_char, t0WWT ) ) 
					apduDataBuffer[leLength++] = rx_char;	
				else
				{
					scLastError = SC_ERR_CARD_NO_RESPONSE;
					return FALSE;	//no response received
				}
			}
		}
		else
		{
			// Do Nothing
		}
	}

	// Store the number of recieved data bytes other than the
	// status codes to make the life of Smart Card Reader easier
	apduResponse->RXDATALEN = leLength;

	return TRUE;
}

#ifdef SC_PROTO_T1

/*******************************************************************************
  Function:
    void SC_UpdateCRC(void)
	
  Description:
    This function calculates 16 bit CRC for T=1 Protocol

  Precondition:
    Initial value of crc should be 0xFFFF.

  Parameters:
    BYTE data - Data that has to be used to update CRC.
	WORD *edc - Pointer to CRC

  Return Values:
    WORD - updated CRC
	
  Remarks:
    CRC 16 - X^16 + X^12 + X^5 + 1

  *****************************************************************************/
static WORD SC_UpdateCRC(BYTE data,WORD crc)
{
	WORD index;
	WORD tempData = (WORD)data << 8;

	// Update the CRC & return it Back
	for (index = 0;index < 8;index++)
	{
		if ((crc ^ tempData) & 0x8000)
		{
			crc <<= 1;
			crc ^= (WORD)0x1021; // X^12 + X^5 + 1
		}
		else
		{
			crc <<= 1;
		}
		
		tempData <<= 1;
	}
	
	return(crc);
}

/*******************************************************************************
  Function:
    void SC_UpdateEDC(BYTE data,WORD *edc)
	
  Description:
    This function updates Error Data Check value depending on the EDC type
    for T=1 Protocol

  Precondition:
    None.

  Parameters:
    BYTE data - Data that has to be used to update EDC.
	WORD *edc - Pointer to EDC

  Return Values:
    None
	
  Remarks:
    None

*****************************************************************************/
static void SC_UpdateEDC(BYTE data,WORD *edc)
{
	// Store the updated LRC/CRC in the EDC
	if (edcType == SC_CRC_TYPE_EDC)	// type = CRC
	{
		*edc = SC_UpdateCRC(data,*edc);
	}
	else // type = LRC
	{
		*edc = *edc ^ data;
	}
}

/*******************************************************************************
  Function:
    static void SC_SendT1Block(BYTE nad,BYTE pcb,WORD length,BYTE *buffer)
	
  Description:
    This function transmits a T=1 formatted block

  Precondition:
    Complete ATR...

  Parameters:
    BYTE nad - NAD to be transmitted to the card
    BYTE pcb - PCB to be transmitted to the card
    WORD length - Length of I-Field transmitted to the card
    BYTE *buffer - Pointer to data that is to be transmitted to the card

  Return Values:
    None
	
  Remarks:
    None

*****************************************************************************/
static void SC_SendT1Block(BYTE nad,BYTE pcb,WORD length,BYTE *buffer)
{
	WORD index;
	WORD edc;

	// Choose the initial value of edc depending upon LRC or CRC
	if (edcType == SC_CRC_TYPE_EDC)
	{
		edc = 0xFFFF;
	}
	else
	{
		edc = 0;
	}
	// Update the edc for Node Address Data Byte
	SC_UpdateEDC(nad,&edc);

	// Update the edc for Protocol Control Byte
	SC_UpdateEDC(pcb,&edc);

	// Update the edc for length of tx Bytes
	SC_UpdateEDC(length,&edc);

	// Update the edc for the data to be transmitted
	for (index=0;index<length;index++)
	{
		SC_UpdateEDC(buffer[index],&edc);
	}

	// Transmit Node Address
	SCdrv_SendTxData(nad);

	// Transmit Protocol Control Byte	
	SCdrv_SendTxData(pcb);

	// Transmit length of Data Byte	
	SCdrv_SendTxData(length);

	// Transmit Data Bytes
	for (index=0;index<length;index++)
	{
		SCdrv_SendTxData(buffer[index]);
	}

	// Transmit EDC
	if (edcType == SC_LRC_TYPE_EDC)
	{
		SCdrv_SendTxData(edc);
	}
	else
	{
		SCdrv_SendTxData(edc);
	    SCdrv_SendTxData(edc>>8);
	}
}

/*******************************************************************************
  Function:
    void SC_ReceiveT1Block(void)
	
  Description:
    This function receives a T=1 formatted block

  Precondition:
    Transmit a block before expecting the response...

  Parameters:
    BYTE *rxNAD - Pointer to NAD recieved from the card
    BYTE *rxPCB - Pointer to PCB recieved from the card
    BYTE *rxLength - Pointer to Length of I-Field recieved from the card
    BYTE *buffer - Pointer to data recieved from the card
	unsigned long blockWaitTime - value of Block Wait Time

  Return Values:
    TRUE if block recieve is successful, and follows the ISO 7816-4 protocol. 
	
  Remarks:
    None
*****************************************************************************/
static BOOL SC_ReceiveT1Block(BYTE *rxNAD,BYTE *rxPCB,BYTE *rxLength,BYTE *buffer,unsigned long blockWaitTime)
{
	WORD edc;
	WORD index;
	BYTE expectedLength;

  	// Get NAD
	if(!SCdrv_GetRxData( rxNAD, blockWaitTime ))
	{
		scLastError = SC_ERR_CARD_NO_RESPONSE;
		return FALSE;
	}

  	// Get PCB
	if(!SCdrv_GetRxData( rxPCB, t1CWT ))
	{
		scLastError = SC_ERR_CARD_NO_RESPONSE;
		return FALSE;
	}

  	// Get Length	
	if(!SCdrv_GetRxData( rxLength, t1CWT ))
	{
		scLastError = SC_ERR_CARD_NO_RESPONSE;
		return FALSE;
	}

	// Add one to the expected length for LRC
	expectedLength = *rxLength + 1;

	// Add additional byte to the length if using CRC
	if (edcType == SC_CRC_TYPE_EDC)
		expectedLength++;

	// Get all the data bytes plus EDC (1 or 2 bytes at end)
	for (index = 0;index < expectedLength;)
	{
		if(!SCdrv_GetRxData( buffer + index, t1CWT ))
		{
			scLastError = SC_ERR_CARD_NO_RESPONSE;
			return FALSE;
		}
		
		++index;
	}

	// Check the LRC or CRC Error
	if (edcType == SC_LRC_TYPE_EDC)
	{
		edc = 0;
		SC_UpdateEDC(*rxNAD,&edc);
		SC_UpdateEDC(*rxPCB,&edc);
		SC_UpdateEDC(*rxLength,&edc);
		for (index = 0;index < expectedLength;)
		{
			SC_UpdateEDC(*(buffer + index),&edc);
			++index;
		}

		if (edc != 0)
		{
			scLastError = SC_ERR_RECEIVE_LRC;
			return FALSE;
		}
	}
	else // EDC is CRC
	{
		edc = 0xFFFF;
		SC_UpdateEDC(*rxNAD,&edc);
		SC_UpdateEDC(*rxPCB,&edc);
		SC_UpdateEDC(*rxLength,&edc);
		for (index = 0;index < (expectedLength-2);)
		{
			SC_UpdateEDC(*(buffer + index),&edc);
			++index;
		}

		if (((edc >> 8) != buffer[expectedLength-2]) || ((edc & 0xFF) != buffer[expectedLength-1]))
	    {
			scLastError = SC_ERR_RECEIVE_CRC;
			return FALSE;
		}
	}
	
	// Return TRUE if there is no LRC or CRC error & data bytes are recieved sucessfully
	return TRUE;
}

/*******************************************************************************
  Function:
	BOOL SC_TransactT1(SC_T1_PROLOGUE_FIELD* pfield,BYTE* iField,SC_APDU_RESPONSE* apduResponse)
  
  Description:
    This function Sends the ISO 7816-4 compaliant APDU commands to the card.  
	It also receive the expected response from the card as defined by the 
	command data.

  Precondition:
    SC_DoPPS was success

  Parameters:
    SC_T1_PROLOGUE_FIELD* pfield - Pointer to Prologue Field 
	BYTE* iField - Pointer to the Information Field of Tx/Rx Data
	SC_APDU_RESPONSE* apduResponse - Pointer to APDU Response structure

  Return Values:
    TRUE if transaction was success, and followed the ISO 7816-4 protocol. 
	
  Remarks:    	
  *****************************************************************************/

BOOL SC_TransactT1(SC_T1_PROLOGUE_FIELD* pfield,BYTE* iField,SC_APDU_RESPONSE* apduResponse)
{
	BOOL	t1TransactCompleted = FALSE,txMbit = FALSE;
	BOOL	rxMbit = FALSE,rxSbit = FALSE,transmitNextSegment = TRUE;
	BYTE	txLength,txPCB = pfield->PCB,rxNAD,rxPCB,rxLEN;
	BYTE	initialLength = pfield->LENGTH,iFieldLength,retryR = 0,retrySync = 0;
	WORD	rxLength = 0;
	BYTE*	rxField = iField;
	BYTE*	txField = iField;
	BYTE*	initialField = iField;
	unsigned long currT1BWT = t1BWT;
	T1BLOCK_TYPE t1TxBlockType,currentT1RxBlockType;

	iFieldLength = initialLength;

	// Determine which type of block is to be transmitted to the card
	if((txPCB & 0x80) == 0x00)
	{
		// I-Block
		t1TxBlockType = I_BLOCK;

		if(txSbit)
		{
			txPCB = txPCB & S_BIT_CLR;
			txSbit = FALSE;
		}
		else
		{
			txPCB = txPCB | S_BIT_SET;
			txSbit = TRUE;
		}
	}
	else if((txPCB & 0xC0) == 0xC0)
	{
		// S-Block
		t1TxBlockType = S_BLOCK;
	}
	else if((txPCB & 0xC0) == 0x80)
	{
		// R-Block
		t1TxBlockType = R_BLOCK;
	}
	else
	{
		// INVALID BLOCK
		return FALSE;
	}

	// Go to appropriate case depending upon the type of block
	switch(t1TxBlockType)
	{
		case	I_BLOCK:
							// Continue Untill Transaction is Passed or Failed...
							while (!t1TransactCompleted)
							{
								// If Next segment has to be transmitted to the card
								if(transmitNextSegment)
								{
									txMbit = FALSE;

									if(iFieldLength > maxSegmentLength)
									{
										txLength = maxSegmentLength;
										txMbit = TRUE;
										txPCB = txPCB | M_BIT_SET;
									}
									else
									{
										txLength = iFieldLength;
										txPCB = txPCB & M_BIT_CLR;
									}
								
									txField = iField;
								}

								// Send block with chaining mode, current sequence number, and maximum length.
								SC_SendT1Block(pfield->NAD,txPCB,txLength,txField);
								
								// Recieve the Block
								if(SC_ReceiveT1Block(&rxNAD,&rxPCB,&rxLEN,rxField,currT1BWT))
								{
									// Determine the type of Block recieved from the card
									if((rxPCB & 0x80) == 0x00)
									{
										// I-Block
										currentT1RxBlockType = I_BLOCK;
										
										if((rxPCB & 0x20) == 0x20)
											rxMbit = TRUE;
										else
											rxMbit = FALSE;

										if((rxPCB & 0x40) == 0x40)
											rxSbit = TRUE;
										else
											rxSbit = FALSE;

										transmitNextSegment = FALSE;
										
										retryR = 0;retrySync = 0;
									}
									else if((rxPCB & 0xC0) == 0xC0)
									{
										// S-Block
										currentT1RxBlockType = S_BLOCK;
										
										retryR = 0;retrySync = 0;
									}
									else if((rxPCB & 0xC0) == 0x80)
									{
										// R-Block
										currentT1RxBlockType = R_BLOCK;
										
										retryR = 0;retrySync = 0;
									}
									else
									{
										// INVALID BLOCK
										currentT1RxBlockType = INVALID_BLOCK;
									}
								}
								else
								{
									// No Block Recieved or Error Block Recieved
									currentT1RxBlockType = INVALID_BLOCK;
								}
						
								currT1BWT = t1BWT;
								
								switch(currentT1RxBlockType)
								{
									case	I_BLOCK	:
														rxField = rxField + (BYTE)rxLEN;
														rxLength = rxLength + rxLEN;
														iFieldLength = 0;

														// If More Bit is set by the card,
														// send the apprpriate R Block
														if(rxMbit)
														{
															// Transmit R(N) - Expected Seq
															txLength = 0x00;
															
															if(rxSbit)
																txPCB = 0x80;
															else
																txPCB = 0x90;
														}
														else
														{
															// No More Bit set from the card,
															// Data is recieved with the status
															// codes...we are done
															if(rxLEN)
															{
																// We are Done here
																t1TransactCompleted = TRUE;
																if(rxLength >= 2)
																{
																	apduResponse->RXDATALEN = rxLength - 2;
																	apduResponse->SW1 = *(initialField + (BYTE)rxLength - (BYTE)2);
																	apduResponse->SW2 = *(initialField + (BYTE)rxLength - (BYTE)1);
																}
															}
															else
															{
																// Transmit Forced Acknowledge I Block
																txLength = 0x00;
															
																if(txSbit)
																{
																	txPCB = 0x00;
																	txSbit = FALSE;
																}
																else
																{
																	txPCB = 0x40;
																	txSbit = TRUE;
																}
															}
														}

														break;

									case	S_BLOCK	:
														// Card can only send Resync Response...
														// Card cant do Resync request
														if((rxPCB & 0x3F) == 0x20) // Resync Response from the card
														{
															txSbit = FALSE;
															return FALSE;
														}
														else if((rxPCB & 0x3F) == 0x01) // Request IFS Change
														{
															txPCB = SC_IFS_RESPONSE;
															txLength = 1;
															txField = rxField;
															maxSegmentLength = *rxField;
															transmitNextSegment = FALSE;
															continue;
														}
														else if((rxPCB & 0x3F) == 0x03) // Request Wait time Extension
														{
															currT1BWT = t1BWT * *rxField;
															txPCB = SC_WAIT_TIME_EXT_RESPONSE;
															txLength = 1;
															txField = rxField;
															transmitNextSegment = FALSE;
															continue;
														}
														else if((rxPCB & 0x3F) == 0x24) // VPP Error Response
														{
															scLastError = SC_CARD_VPP_ERR;
															return FALSE;
														}
														else if((rxPCB & 0x3F) == 0x02) // Abort Request
														{
															txPCB = SC_ABORT_RESPONSE;
															txLength = 0;
															if(txMbit)
															{
																// Do this so that there is last byte transmission to terminate
																// the communication
																iFieldLength = maxSegmentLength + 1;
															}
															transmitNextSegment = FALSE;
															continue;
														}
														break;

									case	R_BLOCK	:
														// If Recieved Seq Number not equal
														// to transmitted Seq Number
														if(rxSbit != txSbit)
														{
															// If More Bit is set by the reader
															if(txMbit)
															{
																// Transmission of previous segment was
																// succesful. Transmit next segment.
																transmitNextSegment = TRUE;

																iFieldLength = iFieldLength - maxSegmentLength;
																iField = iField + maxSegmentLength;

																// Toggle the Sequence Bit
																if(txSbit)
																{
																	txPCB = 0x00;
																	txSbit = FALSE;
																}
																else
																{
																	txPCB = 0x40;
																	txSbit = TRUE;
																}
															}
															else
															{
																// There was some error, trasmit previous
																// block
																transmitNextSegment = FALSE;
															}
														}
														else
														{
															// Retransmit the I-Block
															transmitNextSegment = TRUE;
														}
														
														break;

									case	INVALID_BLOCK	:
																// If 1st Block transaction itself 
																// is failing transmit R(0)
																if(initialLength == iFieldLength)
																{
																	txPCB = 0x82;
																	txLength = 0x00;
																	transmitNextSegment = FALSE;
																	retryR++;
																	retrySync = 0;

																	// Try transmitting R(0) twice
																	// before telling error to the
																	// Smart Card Reader
																	if(retryR > 2)
																	{
																		scLastError = SC_ERR_CARD_NO_RESPONSE;
																		return FALSE;
																	}
																}
																else
																{
																	transmitNextSegment = FALSE;
																	
																	// Try transmitting R(0) twice																															// Try transmitting R(0) twice
																	// before transmitting ReSync
																	// Request to the card
																	if(retryR < 2)
																	{
																		if(rxMbit)
																			txPCB = 0x82;
																		else
																			txPCB = 0x92;
																	
																		txLength = 0x00;
																		retryR++;
																		retrySync = 0;
																	}
																	else
																	{
																		txPCB = 0xC0;
																		txLength = 0x00;
																		retrySync++;
																																		// Try transmitting R(0) twice																															// Try transmitting R(0) twice
																		// Try transmitting Resync Request
																		// thrice before telling error to the
																		// Smart Card Reader
																		if(retrySync > 3)
																		{
																			scLastError = SC_ERR_CARD_NO_RESPONSE;
																			return FALSE;
																		}
																	}
																}
																
																continue;
																
																break;
								}
							}	
							
							break;

		case	S_BLOCK:
							// Continue Untill Transaction is Passed or Failed...
							while (!t1TransactCompleted)
							{
								// Send mode, current sequence number, and maximum length.
								SC_SendT1Block(pfield->NAD,txPCB,0,txField);

								// Recieve the Block	
								if(SC_ReceiveT1Block(&rxNAD,&rxPCB,&rxLEN,rxField,currT1BWT))
								{
									// Determine the type of Block recieved from the card
									if((rxPCB & 0x80) == 0x00)
									{
										// I-Block
										currentT1RxBlockType = I_BLOCK;
									}
									else if((rxPCB & 0xC0) == 0xC0)
									{
										// S-Block
										currentT1RxBlockType = S_BLOCK;
									}
									else if((rxPCB & 0xC0) == 0x80)
									{
										// R-Block
										currentT1RxBlockType = R_BLOCK;
									}
									else
									{
										// INVALID BLOCK
										currentT1RxBlockType = INVALID_BLOCK;
									}
								}
								else
								{
									// No Block Recieved or Error Block Recieved
									currentT1RxBlockType = INVALID_BLOCK;
								}
								
								switch(currentT1RxBlockType)
								{
									case	S_BLOCK	:
														// If Acknowledged properly, return 
														// TRUE to the card reader
														if((txPCB | 0x20) == rxPCB)
														{
															t1TransactCompleted = TRUE;
															break;
														}
														else
														{
															// Try transmitting thrice before
															// telling error to the Smart
															// Card Reader
															retrySync++;
															
															if(retrySync > 3)
															{
																scLastError = SC_ERR_CARD_NO_RESPONSE;
																return FALSE;
															}

															continue;
														}
														break;
									case	R_BLOCK	:
									case	I_BLOCK	:
									case	INVALID_BLOCK	:
															// Try transmitting thrice before
															// telling error to the Smart
															// Card Reader
															retrySync++;
															if(retrySync > 3)
															{
																scLastError = SC_ERR_CARD_NO_RESPONSE;
																return FALSE;
															}
															continue;
															break;
								}
							}
							break;
		
		case	R_BLOCK:
		default:
						break;
	}

	// Return TRUE if everything is fine
	return TRUE;
}

#endif


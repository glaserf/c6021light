/*
*****************************************************************************
  *		XpressNetMaster.h - library for XpressNet protocoll
  *		Copyright (c) 08/2016 - 2020 Philipp Gahtow  All right reserved.
  *
*****************************************************************************
  * FUNKTIONS:
  *
  *		Build a XpressNet Master for driving and switch
  *		Support for Roco LokMaus2 and Multimaus
  *		Basic support for direct CV programming (read and write)
  *		Check XOR before packet dekoding
  * 
  * 	-> Please contact Lenz Inc. for more details about XpressNet.
  *
  * not yet Supported:
  *	- Emergency stop a locomotive: 0x92 AddrH AddrL [XOR]
  *	- Emergency stop a locomotive: 0x91 loco_addr [XOR] (v2)
  *	- Service Mode: only direct Mode
  *	- DCC extended accessory command: 0x13 0x01 B+AddrH AddrL
  *	- DCC FAST CLOCK: 0x01 0xF2 0xF3
  *	- Command Tunnel: 0x3* ** ** [XOR]
  *	- BiDi messages: 0x7* ** ** [XOR]
  *	- Library Entry: 0xE9 0xF1 AddrH AddrL IDX SIZE [NAME][XOR]
  *	- Set function state: 0xE4 0x2* AddrH AddrL Group [XOR]
*****************************************************************************
*/

// include this library's description file
#include "XpressNetMaster.h"

#if defined(__AVR__)
#include <avr/interrupt.h>
XpressNetMasterClass *XpressNetMasterClass::active_object = 0;	//Static
#elif defined(STM32F1)
XpressNetMasterClass *XpressNetMasterClass::active_object = 0;	//Static
#endif

// Local instantiation
XpressNetMasterClass XpressNet = XpressNetMasterClass();

// Constructor /////////////////////////////////////////////////////////////////
// Function that handles the creation and setup of instances

XpressNetMasterClass::XpressNetMasterClass()
{
	// initialize this instance's variables 
	XNetAdr = 0;	//Startaddresse des ersten XNet Device
	XNetSlaveMode = 0x00;	//Start in MASTER MODE
	XNetSlaveInit = true;	//if change to Slave Mode make initialize
	Railpower = csTrackVoltageOff;
	Fahrstufe = Loco128;
	
	XNetBufferSend = 0;	//start position to read data from the buffer
	XNetBufferSend_data = 0;	//position of byte that we are sending
	XNetBufferStore = 0; //start position to store data in buffer
	
	for (byte s = 0; s < 32; s++) { //clear busy slots
		SlotLokUse[s] = 0xFFFF;	//slot is inactiv
	}

	for (byte b = 0; b < XNetBufferSize; b++) {	//clear send buffer
		XNetBuffer[b].length = 0x00;
		for (byte d = 0; d < XNetBufferMaxData; d++) {
			XNetBuffer[b].data[d] = 0x00;
		}
	}

	XNet_state = XNet_get_callbyte;	//set the start state
	XNetRxMsgRd = 0;
	XNetRxMsgWr = 0;
	XNetMsg = XNetRxMsgBuf[0];
	XNetMsgBuffer[XNetBufferlength] = 0x00;	//reset buffer
	
	XNetCVAdr = 0;	//no CV read
	XNetCVvalue = 0;	//no CV value
}

//******************************************Serial*******************************************
void XpressNetMasterClass::setup(uint8_t FStufen, uint8_t  XControl)  //Initialisierung Serial
{
	Fahrstufe = FStufen;
	MAX485_CONTROL = XControl;
	
#if defined(RAW_DATA_MODE)
	// LISTEN_MODE 
	pinMode(MAX485_CONTROL, OUTPUT);
	digitalWrite (MAX485_CONTROL, LOW);	//RECEIVE_MODE - SLAVE
	
#elif defined(__AVR__)  //Configuration for 8-Bit MCU	
	// LISTEN_MODE 
	pinMode(MAX485_CONTROL, OUTPUT);
	digitalWrite (MAX485_CONTROL, LOW);	//RECEIVE_MODE - SLAVE

	//Set up on 62500 Baud
	cli();  //disable interrupts while initializing the USART
	#if defined(__AVR_ATmega328p__)
	 UBRRH = 0;
	 UBRRL = 0x0F;
	 UCSRA = 0;
	 UCSRB = (1<<RXEN) | (1<<TXEN) | (1<<RXCIE) | (1<<TXCIE) | (1<<UCSZ2);
	 UCSRC = (1<<UCSZ1) | (1<<UCSZ0);
	#elif defined(SERIAL_PORT_0)
	 UBRR0H = 0;
	 UBRR0L = 0x0F;
	 UCSR0A = 0;
	 UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0) | (1<<TXCIE0)| (1<<UCSZ02);
	 UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
	#else
	 UBRR1H = 0;
	 UBRR1L = 0x0F;
	 UCSR1A = 0;
	 UCSR1B = (1<<RXEN1) | (1<<TXEN1) | (1<<RXCIE1) | (1<<TXCIE1) | (1<<UCSZ12);
	 UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);
	#endif 
	 sei(); // Enable the Global Interrupt Enable flag so that interrupts can be processed 
	 /*
	 *  Enable reception (RXEN = 1).
	 *  Enable transmission (TXEN0 = 1). 
	 *	Enable Receive Interrupt (RXCIE = 1).
	 *  Enable Transmit Interrupt (TXCIE = 1).
	 *  Set 9-bit character mode (UCSZ00, UCSZ01, and UCSZ02 together control this, 
	 *  But UCSZ00, UCSZ01 are in Register UCSR0C).
	 */
	active_object = this;		//hold Object to call it back in ISR
	 
#elif defined(ARDUINO_ESP8266_ESP01) || defined(ARDUINO_ESP8266_WEMOS_D1MINI)
	// start RS485 9 bit interface on 62500 baud rate
	rs485.setup(XNetRS485_RX, XNetRS485_TX, false, 32);
	rs485.begin(62500, 9);
	//Set RS485 control pin to 5 and not invert (LOW = receive, HIGH = send)
	rs485.setTransmitEnablePin(MAX485_CONTROL, false);

#elif defined(STM32F1)
	gpio_clear(XN_TXEN_BANK, XN_TXEN_PIN);	// start as slave in receive mode

	gpio_set_mode(XN_USART_BANK, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, XN_USART_TX_PIN);
	gpio_set_mode(XN_USART_BANK, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, XN_USART_RX_PIN);

	usart_disable(XN_USART_INST);

	// config XN UART to 9N1, 62.5 kBaud
	usart_set_baudrate(XN_USART_INST, 62500);
	usart_set_databits(XN_USART_INST, 9);
	usart_set_stopbits(XN_USART_INST, USART_STOPBITS_1);
	usart_set_parity(XN_USART_INST, USART_PARITY_NONE);
	usart_set_mode(XN_USART_INST, USART_MODE_TX_RX);
	usart_set_flow_control(XN_USART_INST, USART_FLOWCONTROL_NONE);

	usart_enable_rx_interrupt(XN_USART_INST);
	// no tx interrupt enable at this point

	nvic_set_priority(XN_USART_IRQ, configMAX_SYSCALL_INTERRUPT_PRIORITY + 64);
  	nvic_enable_irq(XN_USART_IRQ);

	usart_enable(XN_USART_INST);

	active_object = this; // hold object to call it back in ISR

#endif	 

}

// Public Methods //////////////////////////////////////////////////////////////
// Functions available in Wiring sketches, this library, and other libraries

//*******************************************************************************************
//Daten ermitteln
#if defined(RAW_DATA_MODE)
void XpressNetMasterClass::RAW_serial_read(unsigned int data) 
{
		//read data from soft_uart
		// Filter the 9th bit, then return 
		if (data > 0xFF) {
			XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] = data & 0xFF;	//only store data
			XNetSlaveMode = XNetSlaveCycle;	//reactivate SLAVE MODE
		//	XNetMsg[XNetlength] = 0x00;
			if (XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] == MY_ADDRESS) {
				// the master called our send slot, send out whatever is in the tx buffer
				XNetSendNext();	//start sending out by interrupt
			}
		}
		else {
			XNetMsgBuffer[XNetBufferlength]++;	    //weitere Nachrichtendaten
			XNetMsgBuffer[XNetMsgBuffer[XNetBufferlength]] = data & 0xFF;                   // Zeichen aus UDR an Aufrufer zurueckgeben
		}
	
		if (XNetMsgBuffer[XNetBufferlength] >= 2) { // header and one data byte or more received
			//Check length - length is inside header but without header and xor!
			if (((XNetMsgBuffer[1] & 0x0F) + 2) == XNetMsgBuffer[XNetBufferlength]) {	//reach data length?
				for (byte i = 0; i < XNetMsgBuffer[XNetBufferlength]; i++) {
					XNetRxMsgBuf[XNetRxMsgWr][i] = XNetMsgBuffer[i+1];
				}
				// Increase the write pointer
				XNetRxMsgWr = (XNetRxMsgWr + 1) % XnetRxBufSize;
				XNetMsgBuffer[XNetBufferlength] = 0x00;	//clear!
			}
			if (XNetMsgBuffer[XNetBufferlength] >= (XNetMaxDataLength) ) {	//overflow!!!
				XNetMsgBuffer[XNetBufferlength] = 0x00;	//clear!
			}	
		}
}
#endif

//*******************************************************************************************
//Daten Auswerten
void XpressNetMasterClass::update(void) 
{
	#if defined(ARDUINO_ESP8266_ESP01) || defined(ARDUINO_ESP8266_WEMOS_D1MINI)
		XNetReceive();
	#endif
	
	uint32_t time_diff;

	//XpressNet State Machine:
	switch (XNet_state) {
		case XNet_send_callbyte:
			//wait until data sending is ready.
			break;
		case XNet_wait_receive:	//wait for client answer, max 120 microsekunden
			if (micros() - XSendCount >= XNetTimeUntilNext) {	//Timeout?
				XNet_state = XNet_get_callbyte;
			}
			if (XNetMsgBuffer[XNetBufferlength] != 0x00) {
				#if defined (XNetDEBUGTime)
				XNetSerial.print("Reply after: ");
				XNetSerial.println(micros() - XSendCount);
				#endif
				XNet_state = XNet_receive_data;
				XSendCount = micros();	//save time for receive the first byte
			}
			break;
		case XNet_receive_data:	//read client data, max 500ms
			time_diff = micros() - XSendCount;
			if (XNetSlaveMode == 0x00 && time_diff >= XNetTimeReadData) {	// Timeout? only check in master mode!
				XNet_state = XNet_get_callbyte;
				XNetMsgBuffer[XNetBufferlength] = 0x00;	//reset buffer
				break;
			}
			// check if there is data in the receive buffer
			if (XNetRxMsgRd == XNetRxMsgWr) {
				break;
			}
			#if defined (XNetDEBUGTime)
			XNetSerial.print("Paket time: ");
			XNetSerial.println(micros() - XSendCount);
			#endif
			// Point the msg-analyze function to the correct part of the buffer
			XNetMsg = XNetRxMsgBuf[XNetRxMsgRd];
			XNetMsgCallByte = XNetRxMsgBuf[XNetRxMsgRd][XNetMaxDataLength];
			if (XNetCheckXOR())	//Checks the XOR
				XNetAnalyseReceived();	//Auswerten der empfangenen Daten
			// increase read pointer
			XNetRxMsgRd = (XNetRxMsgRd + 1) % XnetRxBufSize;
			if (XNetSlaveMode == 0x00) {		//MASTER MODE
				XNet_state = XNet_send_data;
				XNetSendNext();	//start sending out by interrupt
			} else {
				XNet_state = XNet_get_callbyte;
			}
			break;
		case XNet_send_data:
			break;
		case XNet_get_callbyte:	//address the next client
			if (XNetSlaveMode == 0x00) {		//MASTER MODE
				//we are not sending out more Data, get next CallByte
				XNet_state = XNet_send_callbyte;
				getNextXNetAdr();	//Send next CallByte
				XNetSendNext();	//start sending out by interrupt
			}
			else {
				//count cycles that we run in SLAVE MODE
				XNetSlaveMode--;	//stay only in SLAVE MODE if we receive CallBytes
				XNet_state = XNet_receive_data;
				//we need to initialize the SLAVE MODE?
				if (XNetSlaveInit) {
					XNetSlaveInit = false;
					uint8_t Init[] = { 0x00, 0x21, 0x24, 0x05 };	
					XNetsend(Init, 4);	//send initsequence
					#if defined (XNetDEBUG)
					XNetSerial.print("Slave INIT: ");
					XNetSerial.println(XNetSlaveMode);
					#endif
				}
				if (XNetSlaveMode == 0x00) {	//we will leave the Slave-Mode?
					XNetSlaveInit = true;
					XNetMsgBuffer[XNetBufferlength] = 0x00;	//clear!
				}
			}
			
			XSendCount = micros(); //save time for callbyte send!
			break;
	}
}

//--------------------------------------------------------------------------------------------
//Checks the XOR
bool XpressNetMasterClass::XNetCheckXOR(void) {
	byte rxXOR = 0x00;			//store the read in XOR
	for (byte i = 0; i < ((XNetMsg[XNetheader] & 0x0F) + 2); i++) {
		rxXOR = rxXOR ^ XNetMsg[i];
	}
	if (rxXOR == 0x00) {	//XOR is 0x00?
		return true;
	}
	//�bertragungsfehler:
	if (XNetSlaveMode == 0x00) {		//MASTER MODE
		uint8_t ERR[] = {DirectedOps, 0x61, 0x80, 0xE1 };	
		XNetsend(ERR, 4);
	}
	return false;
}

//--------------------------------------------------------------------------------------------
//Daten Auswerten
void XpressNetMasterClass::XNetAnalyseReceived(void) {		//work on received data

	#if defined (XNetDEBUG)
	if (XNetSlaveMode == 0x00) {
		XNetSerial.print("MRX: 0x1");
		XNetSerial.print(CallByteInquiry, HEX);	//MASTER MODE - CallByte 8 Bit only
	}
	else {
		XNetSerial.print("SRX: 0x1");
		XNetSerial.print(XNetMsgCallByte, HEX);	//SLAVE MODE - CallByte 8 Bit only
	}
	for (byte i = 0; i < ((XNetMsg[XNetheader] & 0x0F) + 2); i++) {
		if (XNetMsg[i] < 0x10)
			XNetSerial.print(" 0x0");
		else XNetSerial.print(" 0x");
		XNetSerial.print(XNetMsg[i], HEX);
	}
	XNetSerial.println();
	#endif

	#if defined(STM32F1)
	printf("XN rx: 0x1%02x", XNetMsgCallByte);
	for (byte i = 0; i < ((XNetMsg[XNetheader] & 0x0F) + 2); i++) printf(" %02x", XNetMsg[i]);
	printf("\n");
	#endif
	
	switch (XNetMsg[XNetheader]) {	
		case 0x21:
			if (XNetSlaveMode == 0x00)	{
				if (XNetMsg[XNetdata1] == 0x24 && XNetMsg[XNetdata2] == 0x05) {		//Command station status indication response
					/*
					Bit 0: =1 - Command station is in emergency off (Nothalt)
					Bit 1: =1 - Command station is in emergency stop (Notaus)
					Bit 2: Command station-Start mode (0 = manual mode, 1 = automatic mode)
					Automatic Mode: All locomotives start at their last known speed each
					time the command station is powered up
					Manual Mode: All locomotives have a speed of 0 and functions out on
					command station power up
					Bit 3: = 1 - The command station is in service mode
					Bit 4: reserved
					Bit 5: reserved
					Bit 6: = 1 - The command station is performing a power up.
					Bit 7: = 1 - There was a RAM check error in the command station
					*/
					byte status = 0x01;	//csTrackVoltageOff = B1;
					switch (Railpower) {
						case csNormal:			status = 0; break;
						case csEmergencyStop:	status = 0x01; break;
						case csServiceMode:		status = 0x08; break;
						case csShortCircuit:	status = 0x02; break;
					}
					uint8_t sendStatus[] = { DirectedOps, 0x62, 0x22, status, 0x00 };
					getXOR(sendStatus, 5);
					XNetsend(sendStatus, 5);
				}
				if (XNetMsg[XNetdata1] == 0x21 && XNetMsg[XNetdata2] == 0x00) {		//Command station softwareversion response
					uint8_t sendVersion[] = { DirectedOps, 0x63, 0x21, XNetVersion, XNetID, 0x00 }; //63-21 36 0 74
					getXOR(sendVersion, 6);
					XNetsend(sendVersion, 6);
				}
				if (XNetMsg[XNetdata1] == 0x80 && XNetMsg[XNetdata2] == 0xA1) {		//Alles Aus (Notaus)
					setPower(csTrackVoltageOff);
				}
				if (XNetMsg[XNetdata1] == 0x81 && XNetMsg[XNetdata2] == 0xA0) {		//Alles An
					setPower(csNormal);
				}
				if (XNetMsg[XNetdata1] == 0x10 && XNetMsg[XNetdata2] == 0x31) {	//Request for Service Mode results 
					if (XNetCVAdr != 0) {
						uint8_t sendStatus[] = { DirectedOps, 0x63, 0x14, XNetCVAdr, XNetCVvalue, 0x00};	//Service Mode response for Direct CV mode   
						getXOR(sendStatus, 6);
						XNetsend(sendStatus, 6);
						XNetCVAdr = 0;	//reset CV read Adr
						XNetCVvalue = 0;//reset CV value
					}
					else {
						// Programming info. "Command station busy" 
						uint8_t sendStatus[] = { DirectedOps, 0x61, 0x1F, 0x00 };
						if (XNetCVvalue == 0xFF) { //"no ACK"
							sendStatus[2] = 0x13; //Programming info. "Data byte not found"
							XNetCVvalue = 0; //reset error!
						}
						// Programming info. "Command station ready " 
						//uint8_t sendStatus[] = { DirectedOps, 0x61, 0x11, 0x00 };
						getXOR(sendStatus, 4);
						XNetsend(sendStatus, 4);	
					}
				}
				if (SlotLokUse[DirectedOps & 0x1F] == 0xFFFF)
					SlotLokUse[DirectedOps & 0x1F] = 0;	//mark Slot as activ
			}
			break;
		case 0x22: //Start Programming
			if (XNetMsg[XNetdata1] == 0x11) {	//Register Mode read request (Register Mode) 
			}
			if (XNetMsg[XNetdata1] == 0x14) {	//Paged Mode read request (Paged Mode) 
			}
			if (XNetMsg[XNetdata1] == 0x15) {	//Direct Mode CV read request (CV mode)
				XNetCVAdr = 0;	//no CV read
				XNetCVvalue = 0;	//no CV value	
				if (notifyXNetDirectReadCV)
					notifyXNetDirectReadCV(XNetMsg[XNetdata2]-1);	//try to read the CV 1..255
				break;
			}
			unknown(); //unbekannte Anfrage
			break;
		case 0x23:
			if (XNetMsg[XNetdata1] == 0x12) { //Register Mode write request (Register Mode) 
			}
			if (XNetMsg[XNetdata1] == 0x16) { //Direct Mode CV write request (CV mode) 
				XNetCVAdr = 0;	//no CV read
				XNetCVvalue = 0;	//no CV value
				if (notifyXNetDirectCV)
					notifyXNetDirectCV(XNetMsg[XNetdata2]-1, XNetMsg[XNetdata3]);
				break;
			}
			if (XNetMsg[XNetdata1] == 0x17) { //0x23 0x17 CV DAT[XOR] - Paged Mode write request(Paged mode)

			}
			unknown(); //unbekannte Anfrage
			break;
		case 0xE6: {	//POM CV write MultiMaus
			if (XNetMsg[XNetdata1] == 0x30) { 
				uint16_t Adr = ((XNetMsg[XNetdata2] & 0x3F) << 8) + XNetMsg[XNetdata3];
				uint16_t CVAdr = ((XNetMsg[XNetdata4] & 0b11) << 8) + XNetMsg[XNetdata5];
				if ((XNetMsg[XNetdata4] & 0xFC) == 0xEC) { //set byte
					if (notifyXNetPOMwriteByte)
						notifyXNetPOMwriteByte (Adr, CVAdr, XNetMsg[XNetdata6]);
				}
				if ((XNetMsg[XNetdata4] & 0xFC) == 0xE8) { //set bit
					if (notifyXNetPOMwriteBit)
						notifyXNetPOMwriteBit (Adr, CVAdr, (XNetMsg[XNetdata6] & 0x0F));
				}
			}
			break; }
		case 0x80:
			if (XNetMsg[XNetdata1] == 0x80) {		//EmStop
				setPower(csEmergencyStop);
				break;
			}
			unknown(); //unbekannte Anfrage
			break;
		case 0xE3: {
			if (XNetSlaveMode == 0x00)	{
				switch (XNetMsg[XNetdata1]) {
					case 0x00: //Lokdaten anfordern & F0 bis F12 anfordern
								if (notifyXNetgiveLocoInfo)
									notifyXNetgiveLocoInfo(DirectedOps, word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]));
								break;
					case 0x07: { //Funktionsstatus F0 bis F12 anfordern (Funktion ist tastend oder nicht tastend)
								//0x07, sonst ist LokMaus2 langsam!
								uint8_t LocoFkt[] = { DirectedOps, 0xE3, 0x50, 0x00, 0x00, 0x00 };
								getXOR(LocoFkt, 6);
								XNetsend(LocoFkt, 6);	
								break;
								}
					case 0x08: { //Funktionsstatus F13 bis F28 anfordern (Funktion ist tastend oder nicht tastend)
								uint8_t LocoFkt[] = { DirectedOps, 0xE3, 0x51, 0x00, 0x00, 0x00 };
								getXOR(LocoFkt, 6);
								XNetsend(LocoFkt, 6);
								break;
								}
					case 0x09: //Funktionszustand F13 bis F28 anfordern
								if (notifyXNetgiveLocoFunc)
									notifyXNetgiveLocoFunc(DirectedOps, word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]));
								break;
					case 0xF0: //Lok und Funktionszustand MultiMaus anfordern
								if (notifyXNetgiveLocoMM)
									notifyXNetgiveLocoMM(DirectedOps, word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]));
								break;
					default: unknown(); //unbekannte Anfrage
				}
			}
			break;
			}
		case 0xE4: {	//Fahrbefehle
			AddBusySlot(DirectedOps, word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]));	//set Busy
			if (XNetMsg[XNetdata1] == 0x10) {	//14 Fahrstufen
				if (notifyXNetLocoDrive14)
					notifyXNetLocoDrive14(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x11) {	//27 Fahrstufen
				if (notifyXNetLocoDrive27)
					notifyXNetLocoDrive27(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x12) {	//28 Fahrstufen
				if (notifyXNetLocoDrive28)
					notifyXNetLocoDrive28(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x13) {	//128 Fahrstufen
				if (notifyXNetLocoDrive128)
					notifyXNetLocoDrive128(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x20) {	//Funktionsbefehl Gruppe1 0 0 0 F0 F4 F3 F2 F1
				if (notifyXNetLocoFunc1)
					notifyXNetLocoFunc1(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x21) {	//Funktionsbefehl Gruppe2 0000 F8 F7 F6 F5
				if (notifyXNetLocoFunc2)
					notifyXNetLocoFunc2(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x22) {	//Funktionsbefehl Gruppe3 0000 F12 F11 F10 F9
				if (notifyXNetLocoFunc3)
					notifyXNetLocoFunc3(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x23 || XNetMsg[XNetdata1] == 0xF3) {	//Funktionsbefehl Gruppe4 F20-F13
				//0xF3 = undocumented command is used when a mulitMAUS is controlling functions f20..f13. 
				if (notifyXNetLocoFunc4)
					notifyXNetLocoFunc4(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else if (XNetMsg[XNetdata1] == 0x28) {	//Funktionsbefehl Gruppe5 F28-F21
				if (notifyXNetLocoFunc5)
					notifyXNetLocoFunc5(word(XNetMsg[XNetdata2] & 0x3F, XNetMsg[XNetdata3]), XNetMsg[XNetdata4]);
			}
			else unknown(); //unbekannte Anfrage
			break;
			}
		case 0x42:	//Accessory Decoder information request
			if (notifyXNetTrntInfo && XNetSlaveMode == 0x00)
				notifyXNetTrntInfo(DirectedOps, XNetMsg[XNetdata1], XNetMsg[XNetdata2]);
			break;
		case 0x52:	//Accessory Decoder operation request
			if (notifyXNetTrnt)
				notifyXNetTrnt((XNetMsg[XNetdata1] << 2) | ((XNetMsg[XNetdata2] & 0b110) >> 1), XNetMsg[XNetdata2]);
				//XNetMsg[XNetdata2] = 0000 ABBP
				//A = Weichenausgang(Spulenspannung EIN/AUS)
				//BB = Adresse des Dekoderport 1..4
				//P = Ausgang (Gerade = 0 / Abzweigen = 1)
			break;
		default:  //Befehl in Zentrale nicht vorhanden
			unknown(); //unbekannte Anfrage
	}
	
	if (XNetSlaveMode != 0x00) {		//SLAVE-MODE
		if (XNetMsgCallByte == GENERAL_BROADCAST) {		//Central Station broadcast data
			if (XNetMsg[XNetheader] == 0x61) {
			  if ((XNetMsg[XNetdata1] == 0x01) && (XNetMsg[XNetdata2] == 0x60)) {
				// Normal Operation Resumed
				Railpower = csNormal;
				if (notifyXNetPower)
					notifyXNetPower(Railpower);
			  } 
			  else if ((XNetMsg[XNetdata1] == 0x00) && (XNetMsg[XNetdata2] == 0x61)) {
				// Track power off
				Railpower = csTrackVoltageOff;
				if (notifyXNetPower)
					notifyXNetPower(Railpower);
			  }
			  else if ((XNetMsg[XNetdata1] == 0x08)) {
				  // Track Short
				  Railpower = csShortCircuit;
				  if (notifyXNetPower)
					notifyXNetPower(Railpower);
			  }
			  else if ((XNetMsg[XNetdata1] == 0x02) && (XNetMsg[XNetdata2] == 0x63)) {
				// Service Mode Entry
				Railpower = csServiceMode;
				if (notifyXNetPower)
					notifyXNetPower(Railpower);
			  }
			} 
			else if (XNetMsg[XNetheader] == 0x81) {
				if ((XNetMsg[XNetdata1] == 0x00) && (XNetMsg[XNetdata2] == 0x81)) {
					//Emergency Stop
					Railpower = csEmergencyStop;
					if (notifyXNetPower)
						notifyXNetPower(Railpower);
				}
			}
			else if ((XNetMsg[XNetheader] & 0xF0) == 0x40) {
				//R�ckmeldung Schaltinformation
				byte len = (XNetMsg[XNetheader] & 0x0F) / 2;	//each Adr and Data
				for (byte i = 1; i <= len; i++) {
					notifyXNetFeedback((XNetMsg[XNetheader+(i*2)-1] << 2) | ((XNetMsg[XNetheader+(i*2)] & 0b110) >> 1), XNetMsg[XNetheader+(i*2)]);
					//XNetMsg[XNetdata2] = 0000 ABBP
					//A = Weichenausgang(Spulenspannung EIN/AUS)
					//BB = Adresse des Dekoderport 1..4
					//P = Ausgang (Gerade = 0 / Abzweigen = 1)
				}
			}
			else if (XNetMsg[XNetheader] == 0x05 && XNetMsg[XNetdata1] == 0xF1) {
			  //DCC FAST CLOCK set request
			  /* 0x05 0xF1 TCODE1 TCODE2 TCODE3 TCODE4 [XOR]
				00mmmmmm	TCODE1, mmmmmm = denotes minutes, range 0...59.
				100HHHHH	TCODE2, HHHHH = denotes hours, range 0...23.
				01000www	TCODE3, www = denotes day of week, 0=monday, 1=tuesday, a.s.o.
				110fffff	TCODE4, fffff = denotes speed ratio, range 0..31. (0=stopped)
				*/
			}
		}	//Broadcast END
		else if (XNetMsgCallByte == ACK_REQ) {	 	//Central Station ask client for ACK?
			uint8_t AckSeq[] = {0x00, 0x20, 0x20};
			XNetsend (AckSeq, 3);
		}	//ACK END
		// TODO here the slave address actually needs to be checked!!
		else /* if (XNetMsgCallByte == MY_ADDRESS*) */ {	//Central Station send data to us?
			switch (XNetMsg[XNetheader]) {	
				case 0x52:	// Some other device asked for an accessory change
						break;
				case 0x61:  //Zustand
						break;
				case 0x62:	//Version
						if (XNetMsg[XNetdata1] == 0x22) {
							switch (XNetMsg[XNetdata2]) {
								case 0x00:	//csNormal
											Railpower = csNormal;
											if (notifyXNetPower)
												notifyXNetPower(Railpower);
											break;
								case 0x02:	//csTrackVoltageOff
											Railpower = csTrackVoltageOff;
											if (notifyXNetPower)
												notifyXNetPower(Railpower);
											break;
								case 0x01:	//csEmergencyStop
											Railpower = csEmergencyStop;
											if (notifyXNetPower)
												notifyXNetPower(Railpower);
											break;
								case 0x08:	//csServiceMode
											Railpower = csServiceMode;
											if (notifyXNetPower)
												notifyXNetPower(Railpower);
											break;
							}
							uint8_t commandVersionSequence[] = { 0x00, 0x21, 0x21, 0x00};
							XNetsend (commandVersionSequence, 4);
						}
						break;
				case 0x63:	
						break;
				case 0xE3:	//Antwort abgefrage Funktionen F13-F28
						break;
				case 0xE4:	//Antwort der abgefragen Lok
						break;
				case 0x42:	//Antwort Schaltinformation
						break;
				case 0xE1:  //Err Lok control
						break;
			}	//switch HEADER END
		}	//MY_ADDRESS END
	}	//ENDE SLAVE MODE
}

//--------------------------------------------------------------------------------------------
//Befehl in Zentrale nicht vorhanden
void XpressNetMasterClass::unknown(void)		//unbekannte Anfrage
{
	if (XNetSlaveMode == 0x00) {		//MASTER MODE
		uint8_t NotIn[] = {DirectedOps, 0x61, 0x82, 0xE3 };	
		XNetsend(NotIn, 4);
	}
}

//--------------------------------------------------------------------------------------------
void XpressNetMasterClass::getNextXNetAdr(void)
{
	XNetAdr++;		//n�chste Adresse im XNet
	if (XNetAdr > 31) {	//wenn letzte erreicht von Beginn!
		//search for used slots to call them more then unused:
		while((SlotLokUse[XNetAdr % 32] == 0xFFFF) && (XNetAdr > 31)) {	//slot used an loco address?
			XNetAdr++;
		}
		if ((XNetAdr % 32) == 0)
			XNetAdr = 1;	//start at the beginning

	}
	/*
	AAAAA = XNetAdr % 32 (Slot 1 to 31)
	Call Byte Definitions: 
	Call_Response 					P11A AAAA
	Call_Inquiry					P10A AAAA
	Call_Request_Ack_From_Device	P00A AAAA
	Call_Broadcast					P110 0000
	Call_Transfer_Err				P11A AAAA
	*/
	//Generate byte for the new window:
	CallByteInquiry = callByteParity((XNetAdr % 32) | 0x40); // | 0x100;	// the address for Call Byte Window
	RequestAck = callByteParity((XNetAdr % 32)| 0x00); // | 0x100;		// the address for a request acknowlegement sent
	DirectedOps = callByteParity((XNetAdr % 32)| 0x60); // | 0x100;		// the address when we are sending ops
	
	//Send CallByteInquiry for next Addr:
	uint8_t NormalInquiry[] = { CallByteInquiry };
	XNetsend(NormalInquiry, 1);
}

//--------------------------------------------------------------------------------------------
//Zustand der Gleisversorgung setzten
void XpressNetMasterClass::setPower(byte Power) 
{
	  switch (Power) {	
	  case csNormal: {
			if (XNetSlaveMode == 0x00) {		//MASTER MODE
				uint8_t NormalPower[] = { GENERAL_BROADCAST, 0x61, 0x01, 0x60 };
				XNetsend(NormalPower, 4);
			}
			else {
				uint8_t PowerAn[] = {0x00, 0x21, 0x81, 0xA0 };
				XNetsend(PowerAn, 4);
			}
			break;
		}
	  case csEmergencyStop: {
			if (XNetSlaveMode == 0x00) {		//MASTER MODE
				uint8_t EStopPower[] = { GENERAL_BROADCAST, 0x81, 0x00, 0x81 };
				XNetsend(EStopPower, 4);
			}
			else {
				uint8_t EmStop[] = {0x00, 0x80, 0x80 };
				XNetsend(EmStop, 3);
			}
			break;
		}
	  case csTrackVoltageOff: {
			if (XNetSlaveMode == 0x00) {		//MASTER MODE
				uint8_t OffPower[] = { GENERAL_BROADCAST, 0x61, 0x00, 0x61 };
				XNetsend(OffPower, 4);
			}
			else {
				uint8_t PowerAus[] = {0x00, 0x21, 0x80, 0xA1 };
				XNetsend(PowerAus, 4);
			}
			break;
		}
	  case csShortCircuit: {
			uint8_t ShortPower[] = { GENERAL_BROADCAST, 0x61, 0x08, 0x69 };
			XNetsend(ShortPower, 4);
			break;
		}
	  case csServiceMode: {
			uint8_t ServicePower[] = { GENERAL_BROADCAST, 0x61, 0x02, 0x63 };
			XNetsend(ServicePower, 4);
			break;
		}
	  }
	  
	  if (Railpower != Power) {
			Railpower = Power;	//Save new Power State
			if (notifyXNetPower)
				notifyXNetPower(Railpower);
	  }
	  
}

//--------------------------------------------------------------------------------------------
//R�ckmeldung verteilen
void XpressNetMasterClass::setBCFeedback(byte data1, byte data2) 
{
	uint8_t Feedback[] = { GENERAL_BROADCAST, 0x42, data1, data2, 0x00};
	XNetsend(Feedback, 5);
}

//--------------------------------------------------------------------------------------------
//Lok in use (Request/Vormerken)
void XpressNetMasterClass::ReqLocoBusy(uint16_t Adr) {
	//SetLocoBusyAdr = Adr;
	//BusyAdrCount = 0;
	//AddBusySlot(0, Adr);
	
	for (byte s = 1; s < 32; s++) {
		if (SlotLokUse[s] == Adr) {	//if in use from X-Net device -> set busy
			SetLocoBusy(callByteParity(s | 0x60), SlotLokUse[s]);
			SlotLokUse[s] = 0;	//clean slot
		}
	}
	SlotLokUse[0] = Adr;
}

//--------------------------------------------------------------------------------------------
//Lok in use (Busy)
void XpressNetMasterClass::SetLocoBusy(uint8_t UserOps, uint16_t Adr) {
	uint8_t LocoInfo[] = { UserOps, 0xE3, 0x40, 0x00, 0x00, 0x00 };
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfo[3] = (Adr >> 8) | 0xC0;
	else LocoInfo[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfo[4] = Adr & 0xFF;
	getXOR(LocoInfo, 6);
	XNetsend(LocoInfo, 6);
}

//--------------------------------------------------------------------------------------------
//Lokinfo an XNet Melden
void XpressNetMasterClass::SetLocoInfo(uint8_t UserOps, uint8_t Speed, uint8_t F0, uint8_t F1) {
	SetLocoInfo(UserOps, Fahrstufe, Speed, F0, F1);
}

void XpressNetMasterClass::SetLocoInfo(uint8_t UserOps, uint8_t Steps, uint8_t Speed, uint8_t F0, uint8_t F1) {
	//0xE4	| 0000 BFFF | RVVV VVVV | 000F FFFF | FFFF FFFF | XOr
	// B = 0 free; B = 1 controlled by another Device
	// FFF -> 000 = 14; 001 = 27; 010 = 28; 100 = 128
	//LokInfo Identification Speed FA FB
	byte v = Speed;
	if (Steps == Loco28 || Steps == Loco27) {
		v = (Speed & 0x0F) << 1;	//Speed Bit
		v |= (Speed >> 4) & 0x01;	//Addition Speed Bit
		v |= 0x80 & Speed;		//Dir
	}
	uint8_t LocoInfo[] = {UserOps, 0xE4, Steps, v, F0, F1, 0x00 };	
	if (SlotLokUse[UserOps & 0x1F] == 0x00)	//has Slot a Address? 
		LocoInfo[2] |= 0x08;	//set BUSY
	getXOR(LocoInfo, 7);
	XNetsend(LocoInfo, 7);
}

//--------------------------------------------------------------------------------------------
//LokFkt an XNet Melden
void XpressNetMasterClass::SetFktStatus(uint8_t UserOps, uint8_t F4, uint8_t F5) {
	//F4 = F20-F13
	//F5 = F28-F21
	uint8_t LocoFkt[] = {UserOps, 0xE3, 0x52, F4, F5, 0x00 };
	getXOR(LocoFkt, 6);
	XNetsend(LocoFkt, 6);
}

//--------------------------------------------------------------------------------------------
//Lokinfo for MultiMaus
void XpressNetMasterClass::SetLocoInfoMM(uint8_t UserOps, uint8_t Steps, uint8_t Speed, uint8_t F0, uint8_t F1, uint8_t F2, uint8_t F3) {
	//E7 0x04=128 Steps||0x0C=128+Busy (Dir,Speed) (F0,F4,F3,F2,F1) F5-F12 F13-F20 0x00 0x00 CRC
	byte v = Speed;
	if (Steps == Loco28 || Steps == Loco27) {
		v = (Speed & 0x0F) << 1;	//Speed Bit
		v |= (Speed >> 4) & 0x01;	//Addition Speed Bit
		v |= 0x80 & Speed;		//Dir
	}
	uint8_t LocoInfo[] = {UserOps,  0xE7, /*0x04*/ Steps, v, 0x20, F1, F2, F3, 0x00, 0x00 };
	/*ERROR: Steps change form > 99 to 28steps only!!! why?? (This info comes from DCC:"notifyLokAll(..)"*/
	LocoInfo[4] |= F0;
	getXOR(LocoInfo, 10);
	XNetsend(LocoInfo, 10);
}

//--------------------------------------------------------------------------------------------
void XpressNetMasterClass::setSpeed(uint16_t Adr, uint8_t Steps, uint8_t Speed) {
	//Locomotive speed and direction operation
	// 0xE4 | Ident | AH | AL | RV | XOr
	// Ident: 0x10 = F14; 0x11 = F27; 0x12 = F28; 0x13 = F128
	// RV = RVVV VVVV Dirction and Speed
	byte v = Speed;
	if (Steps == Loco28 || Steps == Loco27) {
		v = (Speed & 0x0F) << 1;	//Speed Bit
		v |= (Speed >> 4) & 0x01;	//Addition Speed Bit
		v |= 0x80 & Speed;		//Dir
	}
	uint8_t LocoInfo[] = {0x00, 0xE4, 0x13, 0x00, 0x00, v, 0x00 }; //default to 128 Steps!
	switch (Steps) {
		case 14: LocoInfo[2] = 0x10; break;
		case 27: LocoInfo[2] = 0x11; break;
		case 28: LocoInfo[2] = 0x12; break;
	}
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfo[3] = (Adr >> 8) | 0xC0;
	else LocoInfo[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfo[4] = Adr & 0xFF;
	getXOR(LocoInfo, 7);
	XNetsend(LocoInfo, 7);
}

//--------------------------------------------------------------------------------------------
//Gruppe 1: 0 0 0 F0 F4 F3 F2 F1
void XpressNetMasterClass::setFunc0to4(uint16_t Adr, uint8_t G1) { 
	uint8_t LocoInfo[] = {0x00, 0xE4, 0x20, 0x00, 0x00, G1, 0x00 };
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfo[3] = (Adr >> 8) | 0xC0;
	else LocoInfo[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfo[4] = Adr & 0xFF;
	getXOR(LocoInfo, 7);
	XNetsend(LocoInfo, 7);
}

//--------------------------------------------------------------------------------------------
//Gruppe 2: 0 0 0 0 F8 F7 F6 F5 
void XpressNetMasterClass::setFunc5to8(uint16_t Adr, uint8_t G2) { 
	uint8_t LocoInfo[] = {0x00, 0xE4, 0x21, 0x00, 0x00, G2, 0x00 };
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfo[3] = (Adr >> 8) | 0xC0;
	else LocoInfo[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfo[4] = Adr & 0xFF;
	getXOR(LocoInfo, 7);
	XNetsend(LocoInfo, 7);
}

//--------------------------------------------------------------------------------------------
//Gruppe 3: 0 0 0 0 F12 F11 F10 F9 
void XpressNetMasterClass::setFunc9to12(uint16_t Adr, uint8_t G3) { 
	uint8_t LocoInfo[] = {0x00, 0xE4, 0x22, 0x00, 0x00, G3, 0x00 };
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfo[3] = (Adr >> 8) | 0xC0;
	else LocoInfo[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfo[4] = Adr & 0xFF;
	getXOR(LocoInfo, 7);
	XNetsend(LocoInfo, 7);
}

//--------------------------------------------------------------------------------------------
//Gruppe 4: F20 F19 F18 F17 F16 F15 F14 F13  
void XpressNetMasterClass::setFunc13to20(uint16_t Adr, uint8_t G4) { 
	uint8_t LocoInfoMM[] = {0x00, 0xE4, 0xF3, 0x00, 0x00, G4, 0x00 };	//normal: 0x23!
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfoMM[3] = (Adr >> 8) | 0xC0;
	else LocoInfoMM[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfoMM[4] = Adr & 0xFF;
	getXOR(LocoInfoMM, 7);
	XNetsend(LocoInfoMM, 7);

	LocoInfoMM[0] = 0x00;
	LocoInfoMM[1] = 0xE4;
	LocoInfoMM[2] = 0x23; 	//MultiMaus only
	LocoInfoMM[3] = Adr >> 8;
	LocoInfoMM[4] = Adr & 0xFF;
	LocoInfoMM[5] = G4;
	getXOR(LocoInfoMM, 7);
	XNetsend(LocoInfoMM, 7);
}

//--------------------------------------------------------------------------------------------
//Gruppe 5: F28 F27 F26 F25 F24 F23 F22 F21  
void XpressNetMasterClass::setFunc21to28(uint16_t Adr, uint8_t G5) { 
	uint8_t LocoInfo[] = {0x00, 0xE4, 0x28, 0x00, 0x00, G5, 0x00 };
	if (Adr > 99) //Xpressnet long addresses (100 to 9999: AH/AL = 0xC064 to 0xE707)
		LocoInfo[3] = (Adr >> 8) | 0xC0;
	else LocoInfo[3] = Adr >> 8;   //short addresses (0 to 99: AH = 0x0000 and AL = 0x0000 to 0x0063)
	LocoInfo[4] = Adr & 0xFF;
	getXOR(LocoInfo, 7);
	XNetsend(LocoInfo, 7);
}

//--------------------------------------------------------------------------------------------
//check if there are slots with the same loco, set them to busy
void XpressNetMasterClass::SetBusy(uint8_t slot) {
	for (byte s = 1; s < 32; s++) {
		if ((s != slot) && (SlotLokUse[s] == SlotLokUse[slot])) {	//if in other Slot -> set busy
			SetLocoBusy(callByteParity(s | 0x60), SlotLokUse[s]);
			SlotLokUse[s] = 0;	//clean slot that informed as busy & let it activ
		}
	}
}

//--------------------------------------------------------------------------------------------
//Add loco to slot. Slot 0 is reserved for non XpressNet Device
void XpressNetMasterClass::AddBusySlot(uint8_t UserOps, uint16_t Adr) {
	if (Adr == 0 || SlotLokUse[UserOps & 0x1F] == Adr)	//skip if already in store!
		return;
	SlotLokUse[UserOps & 0x1F] = Adr;	//store loco that is used
	SetBusy(UserOps & 0x1F);	//make other busy
}


//--------------------------------------------------------------------------------------------
//TrntPos request return
void XpressNetMasterClass::SetTrntStatus(uint8_t UserOps, uint8_t Address, uint8_t Data) {
	if (XNetSlaveMode == 0x00) {		//MASTER MODE
		// data=ITTN ZZZZ	Z=Weichenausgang1+2 (Aktive/Inaktive);
		//0x42 AAAA AAAA ITTN ZZZZ
		//I = is 1, the switching command requested has not been completed
		//TT = turnout group (0-3)
		//N = N=0 is the lower nibble, N=1 the upper nibble
		//Z3 Z2 Z1 Z0 = (Z1,Z0|Z3,Z2) 01 "left", 10 "right"
		uint8_t TrntInfo[] = {UserOps, 0x42, 0x00, 0x00, 0x00 };
		TrntInfo[2] = Address;
		TrntInfo[3] = Data;
		getXOR(TrntInfo, 5);
		XNetsend(TrntInfo, 5);
	}
}

//--------------------------------------------------------------------------------------------
//Trnt Change position
void XpressNetMasterClass::SetTrntPos(uint16_t Address, uint8_t state, uint8_t active) {
	//Accessory Decoder operation request (0x52 | AAAA AAAA | 1000 ABBP | XOr)
	//1. Byte = AAAA AAAA -> (Adresse >> 2) - ohne Stupenauswahl
	//2. Byte = 1000 ABBP
	//A = Weichenausgang(Spulenspannung EIN/AUS)
	//BB = Adresse des Dekoderport 1..4
	//P = Ausgang (Gerade = 0 / Abzweigen = 1)
	uint8_t TrntInfo[] = {0x00, 0x52, 0x00, 0x00, 0x00 };
	TrntInfo[2] = Address >> 2;
	TrntInfo[3] = 0x80;
	TrntInfo[3] |= (Address & 0x03) << 1;
	TrntInfo[3] |= (active & 0x01) << 3;
	TrntInfo[3] |= state & 0x01;
	getXOR(TrntInfo, 5);
	XNetsend(TrntInfo, 5);
}

//--------------------------------------------------------------------------------------------
//return a CV data read
void XpressNetMasterClass::setCVReadValue(uint8_t cvAdr, uint8_t value) {
	//Save?:
	if (cvAdr != 0xFF) {
		XNetCVAdr = cvAdr+1;
		XNetCVvalue = value;
	}
}

//--------------------------------------------------------------------------------------------
//no ACK
void XpressNetMasterClass::setCVNack(void) {	
	XNetCVAdr = 0;
	XNetCVvalue = 0xFF;
}

//--------------------------------------------------------------------------------------------
// send along a bunch of bytes to the Command Station
void XpressNetMasterClass::XNetsend(byte *dataString, byte byteCount) {
	#if defined (XNetDEBUG)
	if (byteCount > 1) {
		XNetSerial.print("XTX: ");
	}
	#endif
	
	XNetBuffer[XNetBufferStore].length = byteCount;
	
	for (byte i = 0; i < byteCount; i++) {
		XNetBuffer[XNetBufferStore].data[i] = *dataString;	//add data to Buffer
		dataString++;

		#if defined (XNetDEBUG)
		if (byteCount > 1) {
			if (XNetBuffer[XNetBufferStore].data[i] < 0x10)
				XNetSerial.print(" 0x0");
			else XNetSerial.print(" 0x");
			if (i == 0 && XNetSlaveMode == 0x00)  //MASTER-MODE
				XNetSerial.print("1");
			XNetSerial.print(XNetBuffer[XNetBufferStore].data[i], HEX);
		}
		#endif
	}
	
	XNetBufferStore++;
	if (XNetBufferStore >= XNetBufferSize)
		XNetBufferStore = 0;
		
	#if defined (XNetDEBUG)
	if (byteCount > 1)
		XNetSerial.println();
	#endif
}

//--------------------------------------------------------------------------------------------
// calculate the XOR
void XpressNetMasterClass::getXOR (uint8_t *data, byte length) {
	byte XOR = 0x00;
	data++;		//without CallByte
	for (byte i = 0; i < (length-2); i++) { 
        XOR = XOR ^ *data;
		data++;
    }
	*data = XOR;
}

//--------------------------------------------------------------------------------------------
// calculate the parity bit in the call byte for this guy
byte XpressNetMasterClass::callByteParity (byte me) {
 byte parity = (1==0);
 byte vv;
 me &= 0x7f;
 vv = me;

 while (vv) {
       parity = !parity;
       vv &= (vv-1);
 }
 if (parity) me |= 0x80;
 return me;
}

//--------------------------------------------------------------------------------------------
#if defined(__AVR__)
//Interrupt routine for writing via Serial
#if defined(__AVR_ATmega8__)
ISR(USART_TX_vect) {
	XpressNetMasterClass::handle_TX_interrupt();	 //weiterreichen an die Funktion
}
#elif defined(SERIAL_PORT_0)
	ISR(USART_TX_vect) {
		XpressNetMasterClass::handle_TX_interrupt();	 //weiterreichen an die Funktion
	}
#else
	ISR(USART1_TX_vect) {
		XpressNetMasterClass::handle_TX_interrupt();	 //weiterreichen an die Funktion
	}
#endif

// Interrupt handling for receive Data
// static 
inline void XpressNetMasterClass::handle_TX_interrupt()
{
  if (active_object)
  {
    active_object->XNetSendNext();	//n�chste Byte Senden
  }
}
#endif

#if defined(STM32F1)
extern "C" void	usart2_isr (void) {
	// check with which type of interrupt we are dealing
	if (usart_get_flag(XN_USART_INST, USART_SR_RXNE)) {
		XpressNetMasterClass::handle_RX_interrupt();
	}

	// only listen to the TC flag if we enabled the interrupt before..
	if ((USART_CR1(XN_USART_INST) & USART_CR1_TCIE) && usart_get_flag(XN_USART_INST, USART_SR_TC)) {
		XpressNetMasterClass::handle_TX_interrupt();
	}
}

inline void XpressNetMasterClass::handle_TX_interrupt() {
  if (active_object) active_object->XNetSendNext();	// send next byte
}
#endif

//--------------------------------------------------------------------------------------------
void XpressNetMasterClass::XNetSendNext(void) {
	uint16_t data9 = XNetReadBuffer();
	
	if (data9 > 0x1FF) {
		//nothing left to send out. Re-enable receiver.
		digitalWrite(MAX485_CONTROL, LOW); 	//RECEIVE_MODE
		usart_set_mode(XN_USART_INST, USART_MODE_TX_RX);
	#if defined(STM32F1)
		// disable Tx empty interrupt, otherwise it goes haywire
		//usart_disable_tx_interrupt(XN_USART_INST);
		/* TODO we need another hal function, like
		 * usart_enable/disable_tc_interrupt(), the rx-empty
		 * interrupt messes with the last byte due to the switch to
		 * receive mode above.
		 */
		USART_CR1(XN_USART_INST) &= ~USART_CR1_TCIE;
	#endif
		if (XNet_state == XNet_send_data && XNetSlaveMode == 0x00) {
			XNet_state = XNet_get_callbyte;
		} else {
			XNet_state = XNet_wait_receive;	//wait for receive from client
		}

		return;
	}

	#if defined(RAW_DATA_MODE)
		digitalWrite(MAX485_CONTROL, HIGH); 	//SEND_MODE
		if (XpressNet_RAW_send)
			XpressNet_RAW_send(data9);
		XNetSendNext();
			
	#elif defined(__AVR__)	
		digitalWrite(MAX485_CONTROL, HIGH); 	//SEND_MODE
		#ifdef __AVR_ATmega8__
			/* wait for empty transmit buffer */
			//while (!(UCSR0A & (1 << UDRE0))) {}
			/* put the data into buffer, and send */
			UCSRB &= ~(1 << TXB8);
			if (data9 & 0x100) //is there a 9th bit?
				UCSRB |= (1 << TXB8);
			UDR = data9;
		#elif defined(SERIAL_PORT_0)
			/* wait for empty transmit buffer */
			//while (!(UCSR0A & (1 << UDRE0))) {}
			/* put the data into buffer, and send */
			UCSR0B &= ~(1 << TXB80);
			if (data9 & 0x100) //is there a 9th bit?
				UCSR0B |= (1 << TXB80);
			UDR0 = data9;
		#else
			/* wait for empty transmit buffer */
			//while (!(UCSR1A & (1 << UDRE1))) {}
			/* put the data into buffer, and send */
			UCSR1B &= ~(1 << TXB81);
			if (data9 & 0x100)	//is there a 9th bit?
				UCSR1B |= (1 << TXB81);
			UDR1 = data9;
		#endif	
	
	#elif defined(ARDUINO_ESP8266_ESP01) || defined(ARDUINO_ESP8266_WEMOS_D1MINI)
		rs485.write(data9);
		XNetSendNext();

	#elif defined(STM32F1)
		// disable UART receiver to avoid rx errors during tx (low rx line)
		usart_set_mode(XN_USART_INST, USART_MODE_TX);
		digitalWrite(MAX485_CONTROL, HIGH); 	//SEND_MODE
		/* put the data into buffer, and send */
		usart_send(XN_USART_INST, data9);
		// enable transfer complete interrupt
		// TODO see comment about missing hal function above
		USART_CR1(XN_USART_INST) |= USART_CR1_TCIE;
		
	#endif
}

//--------------------------------------------------------------------------------------------
uint16_t XpressNetMasterClass::XNetReadBuffer() {
	if (XNetBuffer[XNetBufferSend].length == 0x00)
		return 0xFFFF;	//no data in Buffer!
	
	uint16_t data = XNetBuffer[XNetBufferSend].data[XNetBufferSend_data];
	if (XNetBufferSend_data == 0x00) {	//it is a CALLBYTE and we are MASTER!
		data |= 0x100;	//add 9th bit

		if (XNetSlaveMode != 0x00) { //SLAVE-MODE?
			XNetBufferSend_data++;	//next data byte
			return 0xFFFF;	//stop here, just send one packet as answer!
		}
	}

	XNetBufferSend_data++;	//next data byte
	if (XNetBufferSend_data >= XNetBuffer[XNetBufferSend].length) {	//any byte left to send?
		XNetBufferSend_data = 0;	//Reset data counter
		XNetBuffer[XNetBufferSend].length = 0x00; //Reset Bufferstore
		XNetBufferSend++;	//next byte
		if (XNetBufferSend >= XNetBufferSize)	//overflow?
			XNetBufferSend = 0;	//start value
	}
	return data;
}

#if defined(__AVR__)
//Interrupt reading data!	
//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
//Interrupt routine for reading via Serial
#ifdef __AVR_ATmega8__
ISR(USART_RX_vect)  {
	XpressNetMasterClass::handle_RX_interrupt();	 //weiterreichen an die Funktion
}
#elif defined(SERIAL_PORT_0)
	ISR(USART_RX_vect)  {
		XpressNetMasterClass::handle_RX_interrupt();	 //weiterreichen an die Funktion
	}

#else
	ISR(USART1_RX_vect) {
		XpressNetMasterClass::handle_RX_interrupt();	 //weiterreichen an die Funktion
	}
#endif

// Interrupt handling for receive Data
// static 
inline void XpressNetMasterClass::handle_RX_interrupt()
{
  if (active_object)
  {
	active_object->XNetReceive();	//Byte lesen
  }
}
#endif

#if defined(STM32F1)
inline void XpressNetMasterClass::handle_RX_interrupt() {
  if (active_object) active_object->XNetReceive(); // read byte
}
#endif

//--------------------------------------------------------------------------------------------
//Serial einlesen:
void XpressNetMasterClass::XNetReceive(void)
{
	#if defined(__AVR__)	
		
		// if there's any serial available, read it:
		#ifdef __AVR_ATmega8__
			// Filter the 9th bit, then return 
			if (UCSRB & (1 << RXB8)) {
				XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] = UDR;	//only store data
				XNetSlaveMode = XNetSlaveCycle;	//reactivate SLAVE MODE
			//	XNetMsg[XNetlength] = 0x00;
				if (XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] == MY_ADDRESS)
					XNetSendNext();	//start sending out by interrupt
			}
			else {
				XNetMsgBuffer[XNetBufferlength]++;	    //weitere Nachrichtendaten
				XNetMsgBuffer[XNetMsgBuffer[XNetBufferlength]] = UDR;                   // Zeichen aus UDR an Aufrufer zurueckgeben
			}
		#elif defined(SERIAL_PORT_0)
			// Filter the 9th bit, then return 
			if (UCSR0B & (1 << RXB80)) {
				XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] = UDR0;	//only store data
				XNetSlaveMode = XNetSlaveCycle;	//reactivate SLAVE MODE
			//	XNetMsg[XNetlength] = 0x00;
				if (XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] == MY_ADDRESS)
					XNetSendNext();	//start sending out by interrupt
			}
			else {
				XNetMsgBuffer[XNetBufferlength]++;	    //weitere Nachrichtendaten
				XNetMsgBuffer[XNetMsgBuffer[XNetBufferlength]] = UDR0;                   // Zeichen aus UDR an Aufrufer zurueckgeben
			}
		#else
			// Filter the 9th bit, then return 
			if (UCSR1B & (1 << RXB81)) {
				XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] = UDR1;	//only store data
				XNetSlaveMode = XNetSlaveCycle;	//reactivate SLAVE MODE
			//	XNetMsg[XNetlength] = 0x00;
				if (XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] == MY_ADDRESS)
					XNetSendNext();	//start sending out by interrupt
			}
			else {
				XNetMsgBuffer[XNetBufferlength]++;	    //weitere Nachrichtendaten
				XNetMsgBuffer[XNetMsgBuffer[XNetBufferlength]] = UDR1;                   // Zeichen aus UDR an Aufrufer zurueckgeben
			}
		#endif
	#elif defined(ARDUINO_ESP8266_ESP01) || defined(ARDUINO_ESP8266_WEMOS_D1MINI)
		if (rs485.available()) {
			int data = rs485.read();
			if (data > 0xFF) {
				XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] = data;
				XNetSlaveMode = XNetSlaveCycle;	//reactivate SLAVE MODE
				if (XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] == MY_ADDRESS)
					XNetSendNext();	//start sending out by interrupt
			}
			else {
				XNetMsgBuffer[XNetBufferlength]++;	    //weitere Nachrichtendaten
				XNetMsgBuffer[XNetMsgBuffer[XNetBufferlength]] = data;                   // Zeichen aus UDR an Aufrufer zurueckgeben
			}
		}
	#elif defined(STM32F1)
		// Filter the 9th bit, then return call byte
		uint16_t rx_data_9b = usart_recv(XN_USART_INST);
		if (rx_data_9b & (1 << 8)) {
			XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] = (uint8_t) (rx_data_9b & 0xFF);	// only store callbyte body
			XNetSlaveMode = XNetSlaveCycle;	//reactivate SLAVE MODE
			if (XNetRxMsgBuf[XNetRxMsgWr][XNetMaxDataLength] == MY_ADDRESS) {
				// the master called our send slot, send out whatever is in the tx buffer
				XNetSendNext();
			}
		}
		else {
			XNetMsgBuffer[XNetBufferlength]++;	    //weitere Nachrichtendaten
			XNetMsgBuffer[XNetMsgBuffer[XNetBufferlength]] = (uint8_t) (rx_data_9b & 0xFF);  // Zeichen aus UDR an Aufrufer zurueckgeben
		}
	#endif
	
	if (XNetMsgBuffer[XNetBufferlength] >= 2) { // header and one data byte or more received
		//Check length - length is inside header but without header and xor!
		if (((XNetMsgBuffer[1] & 0x0F) + 2) == XNetMsgBuffer[XNetBufferlength]) {	//reach data length?
			// TODO use memcpy()
			for (byte i = 0; i < XNetMsgBuffer[XNetBufferlength]; i++) {
				//XNetMsg[i] = XNetMsgBuffer[i+1];
				XNetRxMsgBuf[XNetRxMsgWr][i] = XNetMsgBuffer[i+1];
			}
			// Increase the write pointer
			XNetRxMsgWr = (XNetRxMsgWr + 1) % XnetRxBufSize;
			XNetMsgBuffer[XNetBufferlength] = 0x00;	//clear!
		}
		if (XNetMsgBuffer[XNetBufferlength] >= (XNetMaxDataLength) ) {	//overflow!!!
			XNetMsgBuffer[XNetBufferlength] = 0x00;	//clear!
		}	
	}
}

#include <stdlib.h>
#include <errno.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#include <string.h>
#include <stdint.h>

//Metering Hardware
#include "arduino/WProgram.h"
#include "SPI/SPI.h"
#include "prescaler.h"
#include "ReturnCode/returncode.h"
#include "DbgTel/DbgTel.h"
#include "ADE7753/ADE7753.h"
#include "ShiftRegister/shiftregister.h"
#include "Demux/Demux.h"
#include "Select/select.h"
#include "sd-reader/sd_raw.h"
#include "Switches/switches.h"

//Metering logic
#include "Circuit/circuit.h"
#include "Circuit/calibration.h"

//definition of serial ports for debug, sheeva communication, and telit communication
#define dbg Serial1
#define debugPort Serial1
#define sheevaPort Serial2
#define telitPort Serial3

#define DEBUG_BAUD_RATE 9600
#define SHEEVA_BAUD_RATE 9600
#define TELIT_BAUD_RATE 115200
#define verbose 1


Circuit ckts[NCIRCUITS];

//JR
#include <avr/wdt.h>
void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3")));

void wdt_init(void)
{
	MCUSR = 0;
	wdt_disable();
	return;     
}


int _testChannel = 1;

void setup();
void loop();
void softSetup();
void displayChannelInfo(); 
void displayEnabled(const int8_t enabledC[WIDTH]);
int8_t getChannelID();
void testHardware();
void parseBerkeley();
void parseColumbia();
String getValueForKey(String key, String commandString);
String getSMSText(String commandString);
void meter(String commandString);
void modem(String commandString);
String readSheevaPort();
String readTelitPort();
void chooseDestination(String destination, String commandString);
void turnOnTelit();

void setup()
{

	setClockPrescaler(CLOCK_PRESCALER_1);//Disable prescaler.


	dbg.begin(9600);		//Debug serial
	dbg.write("\r\n\r\ntelduino power up\r\n");
    dbg.write("last compilation\r\n");
    dbg.write(__DATE__);
    dbg.write("\r\n");
    dbg.write(__TIME__);
    dbg.write("\r\n");

	pinMode(37, OUTPUT);	//Level shifters
	digitalWrite(37,HIGH);	//Level shifters
	initDbgTel();			//Blink leds
	SRinit();				//Shift registers
	initDemux();			//Muxers
	initSelect();			//Select Circuit
	sd_raw_init();			//SDCard
	SPI.begin();			//SPI


	telitPort.begin(TELIT_BAUD_RATE);	//Telit serial
	 
	sheevaPort.begin(SHEEVA_BAUD_RATE);

	SWallOff();
	_testChannel = 20;
	dbg.print("\r\nstart loop()\r\n");
} //end of setup section


void loop()
{	
	//parseBerkeley();
	parseColumbia();
} //end of main loop

/**
 *	single character serial interface for interaction with telduino
 */
void parseBerkeley() 
{
	setDbgLeds(GYRPAT);
	// Look for incoming single character command on dbg line
	if (dbg.available() > 0) {
		char incoming = dbg.read(); 
		if (incoming == 'A') {
			softSetup();					//Set calibration values for ADE
		} else if (incoming == 'C') {
			_testChannel = getChannelID();	
		} else if (incoming == 'c') {
			displayChannelInfo();
		} else if (incoming == 'S') {
			int8_t ID = getChannelID();		//Toggle channel circuit
											//dbg.print("!SWisOn:");
											//dbg.print(!SWisOn(ID),DEC);
											//dbg.println(RCstr());
			SWset(ID,!SWisOn(ID));
		} else if (incoming == 's') {
			displayEnabled(SWgetSwitchState());	
		} else if (incoming == 'T'){
			testHardware();
		} else if (incoming == 'm') {
			/*
			GSMb.sendRecATCommand("AT+CCLK?");
			GSMb.sendRecATCommand("AT+CSQ");
			 */
		} else if (incoming == 'R') {
			wdt_enable((WDTO_4S));			//Do a soft reset
			Serial1.println("resetting in 4s.");
		} else if (incoming == 'F') {
			//Forward a command to the modem
			char command[256];
			dbg.println("Enter command:");
			int i;
			ifsuccess(CLgetString(&dbg,command,sizeof(command))) {  
				wdt_enable(WDTO_8S);
				/*
				GSMb.sendRecQuickATCommand(command);
				 */
				wdt_disable();
			} else {
				dbg.println("Buffer overflow: command too long.");
			}
		} else if (incoming == 'a') {
			dbg.println("Enter name of register to read:");
			char buff[64] = {0};
			CLgetString(&dbg,buff,sizeof(buff));
			dbg.println();
			/*
			 char c = buff[0];
			 int i =0;
			 dbg.println("***");
			 while (c != '\0') {
			 c = buff[i];
			 dbg.print("'");
			 dbg.print(c,HEX);
			 dbg.println("'");
			 i++;
			 }
			 dbg.println("***");
			 */
			
			int32_t regData = 0;
			for (int i=0; i < sizeof(regList)/sizeof(regList[0]); i++) {
				//dbg.println(regList[i]->name);
				//dbg.println(strcmp(regList[i]->name,buff));
				if (strcmp(regList[i]->name,buff) == 0){
					CSSelectDevice(_testChannel);
					dbg.print("regData:");
					dbg.print(RCstr(ADEgetRegister(*regList[i],&regData)));
					dbg.print(":");
					dbg.println(regData,HEX);
					CSSelectDevice(DEVDISABLE);
					break;
				} 
			}
		} else if (incoming == 'P') {
			//Initialize all circuits to have the same parameters
			//using the new circuits code
			for (int i = 0; i < NCIRCUITS; i++) {
				Circuit *c = &(ckts[i]);
				CsetDefaults(c,i);
				int8_t retCode = Cprogram(c);
				dbg.println(RCstr(retCode));
				dbg.println("*****");
				ifnsuccess(retCode) {
					break;
				}
			}
		}
		else {
			//Indicate received character
			dbg.print("\n\rNot_Recognized:");
			dbg.print(incoming,BIN);
			dbg.print(":");
			dbg.print("'");
			dbg.print(incoming);
			dbg.println("\'");
		}
	}
	setDbgLeds(0);
}

void parseColumbia()
{
    if (verbose > 1) {
        debugPort.println("top of loop()");
        debugPort.println(millis());
    }
    
    String commandString;
    String destination;
	
    commandString = readSheevaPort();
    destination = getValueForKey("cmp", commandString);
    chooseDestination(destination, commandString);    
	
    String modemString = "";
    modemString = readTelitPort();
    modemString = modemString.trim();
    if (modemString.length() != 0) {
        String responseString = "";
        responseString += "cmp=mdm&text=";
        responseString += '"';
        responseString += modemString;
        responseString += '"';
        sheevaPort.println(responseString);
        
        debugPort.println("string received from telit");
        debugPort.println(modemString);
    }
}

void softSetup() 
{
	int32_t data = 0;

	dbg.print("\n\n\rSetting Channel:");
	dbg.println(_testChannel,DEC);
	
	CSSelectDevice(_testChannel); //start SPI comm with the test device channel
	//Enable Digital Integrator for _testChannel
	int8_t ch1os=0,enableBit=1;

	dbg.print("set CH1OS:");
	dbg.println(RCstr(ADEsetCHXOS(1,&enableBit,&ch1os)));
	dbg.print("get CH1OS:");
	dbg.println(RCstr(ADEgetCHXOS(1,&enableBit,&ch1os)));
	dbg.print("enabled: ");
	dbg.println(enableBit,BIN);
	dbg.print("offset: ");
	dbg.println(ch1os);

	//set the gain to 2 for channel _testChannel since the sensitivity appears to be 0.02157 V/Amp
	int32_t gainVal = 1;

	dbg.print("BIN GAIN (set,get):");
	dbg.print(RCstr(ADEsetRegister(GAIN,&gainVal)));
	dbg.print(",");
	dbg.print(RCstr(ADEgetRegister(GAIN,&gainVal)));
	dbg.print(":");
	dbg.println(gainVal,BIN);
	
	//Set the IRMSOS to 0d444 or 0x01BC. This is the measured offset value.
	int32_t iRmsOsVal = 0x01BC;
	ADEsetRegister(IRMSOS,&iRmsOsVal);
	ADEgetRegister(IRMSOS,&iRmsOsVal);
	dbg.print("hex IRMSOS:");
	dbg.println(iRmsOsVal, HEX);
	
	//WHAT'S GOING ON WITH THIS OFFSET? THE COMMENT DOESN'T MATCH THE VALUE
	//Set the VRMSOS to -0d549. This is the measured offset value.
	int32_t vRmsOsVal = 0x07FF;//F800
	ADEsetRegister(VRMSOS,&vRmsOsVal);
	ADEgetRegister(VRMSOS,&vRmsOsVal);
	dbg.print("hex VRMSOS read from register:");
	dbg.println(vRmsOsVal, HEX);
	
	//set the number of cycles to wait before taking a reading
	int32_t linecycVal = 200;
	ADEsetRegister(LINECYC,&linecycVal);
	ADEgetRegister(LINECYC,&linecycVal);
	dbg.print("int linecycVal:");
	dbg.println(linecycVal);
	
	//read and set the CYCMODE bit on the MODE register
	int32_t modeReg = 0;
	ADEgetRegister(MODE,&modeReg);
	dbg.print("bin MODE register before setting CYCMODE:");
	dbg.println(modeReg, BIN);
	modeReg |= CYCMODE;	 //set the line cycle accumulation mode bit
	ADEsetRegister(MODE,&modeReg);
	ADEgetRegister(MODE,&modeReg);
	dbg.print("bin MODE register after setting CYCMODE:");
	dbg.println(modeReg, BIN);
	
	//reset the Interrupt status register
	ADEgetRegister(RSTSTATUS, &data);
	dbg.print("bin Interrupt Status Register:");
	dbg.println(data, BIN);

	CSSelectDevice(DEVDISABLE); //end SPI comm with the selected device	
}

void displayChannelInfo() {
	int8_t retCode;
	int32_t val;
	uint32_t iRMS = 0;
	uint32_t vRMS = 0;
	uint32_t lineAccAppEnergy = 0;
	uint32_t lineAccActiveEnergy = 0;
	int32_t interruptStatus = 0;
	uint32_t iRMSSlope = 164;
	uint32_t vRMSSlope = 4700;
	uint32_t appEnergyDiv = 5;
	uint32_t energyJoules = 0;

	//Select the Device
	CSSelectDevice(_testChannel);
	
	//Read and clear the Interrupt Status Register
	ADEgetRegister(RSTSTATUS, &interruptStatus);
	
	if (0 /*loopCounter%4096*/ ){
		dbg.print("bin Interrupt Status Register:");
		dbg.println(interruptStatus, BIN);
		
	}	//endif
	
	//if the CYCEND bit of the Interrupt Status Registers is flagged
	dbg.print("Waiting for next cycle: ");
	retCode = ADEwaitForInterrupt(CYCEND,4000);
	dbg.println(RCstr(retCode));

	ifsuccess(retCode) {
		setDbgLeds(GYRPAT);

		dbg.print("_testChannel:");
		dbg.println(_testChannel,DEC);

		dbg.print("bin Interrupt Status Register:");
		dbg.println(interruptStatus, BIN);
		
		//IRMS SECTION
		dbg.print("mAmps IRMS:");
		dbg.println( RCstr(ADEgetRegister(IRMS,&val)) );
		iRMS = val/iRMSSlope;//data*1000/40172/4;
		dbg.println(iRMS);
		
		//VRMS SECTION
		dbg.print("VRMS:");
		dbg.println(RCstr(ADEgetRegister(VRMS,&val)));

		vRMS = val/vRMSSlope; //old value:9142
		dbg.print("Volts VRMS:");
		dbg.println(vRMS);

		
		//APPARENT ENERGY SECTION
		ADEgetRegister(LVAENERGY,&val);
		dbg.print("int Line Cycle Apparent Energy after 200 half-cycles:");
		dbg.println(val);
		energyJoules = val*2014/10000;
		dbg.print("Apparent Energy in Joules over the past 2 seconds:");
		dbg.println(energyJoules);
		dbg.print("Calculated apparent power usage:");
		dbg.println(energyJoules/2);
		
		//THIS IS NOT WORKING FOR SOME REASON
		//WE NEED TO FIX THE ACTIVE ENERGY REGISTER AT SOME POINT
		//ACTIVE ENERGY SECTION
		ifsuccess(ADEgetRegister(LAENERGY,&val)) {
			dbg.print("int Line Cycle Active Energy after 200 half-cycles:");
			dbg.println(val);
		} else {
			dbg.println("Line Cycle Active Energy read failed.");
		}
		
/*		iRMS = data/161;//data*1000/40172/4;
		dbg.print("mAmps IRMS:");
		dbg.println(iRMS);
*/
		
		delay(500);
	} //end of if statement

	CSSelectDevice(DEVDISABLE);
}

int8_t getChannelID() 
{
	int ID = -1;
	while (ID == -1) {
		dbg.print("Waiting for ID (0-20):");		
		ifnsuccess(CLgetInt(&dbg,&ID)) ID = -1;
		if (ID < 0 || 20 < ID ) {
			dbg.print("Incorrect ID:");
			dbg.println(ID,DEC);
			ID = -1;
		} else {
			dbg.println((int8_t)ID,DEC);
		}
	}
	return (int8_t)ID;
}

void testHardware() {
	int8_t enabledC[WIDTH] = {0};
	int32_t val;

	dbg.print("\n\rTest switches\n\r");
	//Shut off/on all circuits
	for (int i =0; i < 1; i++){
		SWallOn();
		delay(50);
		SWallOff();
		delay(50);
	}
	//Start turning each switch on with 1 second in between
	for (int i = 0; i < WIDTH; i++) {
		enabledC[i] = 1;
		delay(1000);
		SWsetSwitches(enabledC);
	}
	delay(1000);
	SWallOff();

	//Test communications with each ADE
	for (int i = 0; i < 21; i++) {
		CSSelectDevice(i);
		
		dbg.print("Can communicate with channel ");
		dbg.print(i,DEC);
		dbg.print(": ");

		int retCode = ADEgetRegister(DIEREV,&val);
		ifnsuccess(retCode) {
			dbg.print("NO-");
			dbg.println(RCstr(retCode));
		} else {
			dbg.print("YES-DIEREV:");
			dbg.println(val,DEC);
		}
		CSSelectDevice(DEVDISABLE);
	}
}
	
void displayEnabled(const int8_t enabledC[WIDTH])
{
	dbg.println("Enabled Channels:");
	for (int i =0; i < WIDTH; i++) {
		dbg.print(i);
		dbg.print(":");
		dbg.print(enabledC[i],DEC);
		if (i%4 == 3) {
			dbg.println();
		} else {
			dbg.print('\t');
		}
	}
	dbg.println();
}

//JR needed to make compiler happy
extern "C" {
void __cxa_pure_virtual(void) 
{
	while(1) {
		setDbgLeds(RPAT);
		delay(332);
		setDbgLeds(YPAT);
		delay(332);
		setDbgLeds(GPAT);
		delay(332);
	}
}
}

/**
 *	given a String for the key value, this function returns the String corresponding
 *	to the value for the key by reading until the next '&' or the end of the string.
 */
String getValueForKey(String key, String commandString) {
    int keyIndex = commandString.indexOf(key);
    int valIndex = keyIndex + key.length() + 1;
    int ampersandIndex = commandString.indexOf("&",valIndex);
    // if ampersand not found, go until end of string
    if (ampersandIndex == -1) {
        ampersandIndex = commandString.length();
    }
    String val = commandString.substring(valIndex, ampersandIndex);
    return val;
}

/**
 *	this function is called when a cmp=mdm string is sent to the telduino.  the text 
 *	surrounded by parenthesis is returned.  this message will be sent to the modem as
 *	a raw string command.  
 */
String getSMSText(String commandString) {
    int firstDelimiterIndex = commandString.indexOf('(');
    int secondDelimiterIndex = commandString.indexOf(')', firstDelimiterIndex + 1);
    String smsText = commandString.substring(firstDelimiterIndex + 1, secondDelimiterIndex);
    return smsText;
}

/**
 *	this function takes care of parsing commands where cmp=mtr.
 */
void meter(String commandString) {
    String job = getValueForKey("job", commandString);
    String cid = getValueForKey("cid", commandString);
	
	int32_t val = 0;
	int32_t gainVal = 0x0F;
	
	// is there a better way to convert the cid string to int?
	char cidChar[3];
	cid.toCharArray(cidChar, 3);
	int icid = atoi(cidChar);
	
    if (verbose > 0) {
        debugPort.println();
        debugPort.println("entered void meter()");
        debugPort.print("executing job type - ");
        debugPort.println(job);
        debugPort.print("on circuit id - ");
        debugPort.println(cid);
        debugPort.println();
    }
    
	if (job == "con") {
		debugPort.println("execute con job");
		SWset(icid,1);
		debugPort.print("switch ");
		debugPort.print(icid, DEC);
		if (SWisOn(icid)) {
			debugPort.println(" is on");
		} else {
			debugPort.println(" is off");
		}
	}
	else if (job == "coff") {
		debugPort.println("execute coff job");
		SWset(icid,0);
		debugPort.print("switch ");
		debugPort.print(icid, DEC);
		if (SWisOn(icid)) {
			debugPort.println(" is on");
		} else {
			debugPort.println(" is off");
		}
	}
	else if (job == "read") {
		debugPort.println("reading circuit job");
		// actually do something here soon
		// read circuit energy or something using icid
	}
	else if (job == 'A') {
		_testChannel = icid;
		softSetup();
	}
	else if (job == 'c') {
		_testChannel = icid;
		displayChannelInfo();		
	}
	else if (job == 'T') {
		testHardware();
	}
	else if (job == 'R') {
		wdt_enable((WDTO_4S));
		debugPort.println("resetting in 4s.");
	}
}

/**
 *	this function takes care of parsing commands where cmp=mdm.
 */
void modem(String commandString) {
    String smsText = getSMSText(commandString);
	
    if (verbose > 0) {
        debugPort.println();
        debugPort.println("entered void modem()");
        debugPort.print("sms text - ");
        debugPort.println(smsText);
        debugPort.println();
    }
	
	// send string to telit with a \r\n character appended to the string
	// todo - is it safer to send the char values for carriage return and linefeed?
    telitPort.print(smsText);
    telitPort.print("\r\n");
    
}

/**
 *	this function reads the sheevaPort (Serial2) for incoming commands
 *	and returns them as String objects.
 */
String readSheevaPort() {
    char incomingByte = ';';    
    String commandString = "";
    while ((sheevaPort.available() > 0) || ((incomingByte != ';') && (incomingByte != '\n'))) {
        incomingByte = sheevaPort.read();
        if (incomingByte != -1) {      
            if (verbose > 1) {
                debugPort.print(incomingByte);
            }
            commandString += incomingByte;
        }
        if (incomingByte == ';') {
            commandString = commandString.substring(0, commandString.length() - 1);
            break;
        }
    }   
    commandString = commandString.trim();
    return commandString;
}

/**
 *	this function reads the telitPort (Serial3) for incoming commands
 *	and returns them as String objects.
 */
String readTelitPort() {
    char incomingByte = '\n';
    String commandString = "";
    while ((telitPort.available() > 0) || (incomingByte != '\n')) {
        incomingByte = telitPort.read();
        if (incomingByte != -1) {      
            commandString += incomingByte;
        }
    }
    return commandString;
}

/**
 *	based on the value for the cmp key, this calls the function
 *	meter if cmp=mtr
 *	and
 *  modem if cmp=mdm
 */
void chooseDestination(String destination, String commandString) {
    if (destination == "mtr") {
        meter(commandString);
    }
    else if (destination == "mdm") {
        modem(commandString);
    }
}

/**
 *	Pull telit on/off pin high for 3 seconds to start up telit modem
 */
void turnOnTelit() {
	pinMode(22, OUTPUT);
	digitalWrite(22, HIGH);
	delay(3000);
	digitalWrite(22, LOW);
}
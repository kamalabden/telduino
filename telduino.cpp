#include "arduino/WProgram.h"

//JR-Cal
#include "SDRaw/sd_raw.h"
#include "ADE7753/ADE7753.h"
#include "DbgTel/DbgTel.h"
#include "Select/select.h"
#include "prescaler.h"
#include "ReturnCode/returncode.h"


const ADEReg *regList[] = { &WAVEFORM, &AENERGY, &RAENERGY, &LAENERGY, &VAENERGY, &RVAENERGY, &LVAENERGY, &LVARENERGY, &MODE, &IRQEN, &STATUS, &RSTSTATUS, &CH1OS, &CH2OS, &GAIN, &PHCAL, &APOS, &WGAIN, &WDIV, &CFNUM, &CFDEN, &IRMS, &VRMS, &IRMSOS, &VRMSOS, &VAGAIN, &VADIV, &LINECYC, &ZXTOUT, &SAGCYC, &SAGLVL, &IPKLVL, &VPKLVL, &IPEAK, &RSTIPEAK, &VPEAK, &TEMP, &PERIOD, &TMODE, &CHKSUM, &DIEREV };

char ctrlz = 26;


#define testChannel 1

//#define CYCEND 0x04 //bit 2 of the Interrupt Status register
#define CYCMODE 0x80 //bit 7 of the MODE register


void setup();
void loop();
void softSetup();

//JR needed to make compiler happy
extern "C" {
#include "ShiftRegister/ShiftRegister.h"
#include "Demux/Demux.h"
	void __cxa_pure_virtual(void) {
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

void setup()
{
	setClockPrescaler(CLOCK_PRESCALER_1); //Get rid of prescaler.
	
	//Level shifters
	pinMode(37, OUTPUT);
	digitalWrite(37,HIGH);

	Serial1.begin(9600); //Serial port
	Serial1.print("\n\n\rHello World!\n\n\r");
	initDbgTel(); //Blink leds
	initShiftRegister(); //Shift registers
	initDemux(); //Muxers
	initSelect(); //Select
	SPI.begin(); //SPI
	sd_raw_init(); //SDCard

	//SPI.setClockDivider(0);
	
	//Set the ch1 digital integrator on
	//#define regist CH1OS

	Serial1.print("\n\n\rStart Program\n\n\r");
	
	softSetup();
	
} //end of setup section

//Declare the variables used in the loop
int32_t val;
uint32_t iRMS = 0;
uint32_t vRMS = 0;
uint32_t lineAccAppEnergy = 0;
uint32_t lineAccActiveEnergy = 0;
int32_t interruptStatus = 0;
uint32_t loopCounter = 0;
int incomingByte = 0;
uint32_t iRMSSlope = 164;
uint32_t vRMSSlope = 4700;
uint32_t appEnergyDiv = 5;
uint32_t energyJoules = 0;

void loop()
{	
	// Look for incoming data on Serial1 line
	if (Serial1.available() > 0) {
		// read the incoming byte:
		incomingByte = Serial1.read();

		// say what you got:
		//Serial1.print("\n\n\rI received: ");
		//Serial1.print(incomingByte);
		if(incomingByte == 'r')
			softSetup(); //do a soft reset.
	}
	
	//Select the Device
	CSSelectDevice(testChannel);
	
	//Read the Interrupt Status Register
	ADEgetRegister(RSTSTATUS, &interruptStatus);
	
	//Serial1.println(interruptStatus,BIN);
	
	if(0 /*loopCounter%4096*/ ){
		Serial1.print("bin Interrupt Status Register:");
		Serial1.println(interruptStatus, BIN);
		
	}	//endif
	
	//if the CYCEND bit of the Interrupt Status Registers is flagged
	if(interruptStatus & CYCEND){
		
		setDbgLeds(GYRPAT);
		Serial1.print("\n\rIn Loop Number:");
		Serial1.println(loopCounter);
		Serial1.print("bin Interrupt Status Register:");
		Serial1.println(interruptStatus, BIN);
		
		//IRMS SECTION
		Serial1.print("getReg mAmps IRMS:");
		Serial1.println( RCstr(ADEgetRegister(IRMS,&val)) );
		iRMS = val/iRMSSlope;//data*1000/40172/4;
		Serial1.println(iRMS);
		
		//VRMS SECTION
		Serial1.print("VRMS getReg:");
		Serial1.println(RCstr(ADEgetRegister(VRMS,&val)));

		vRMS = val/vRMSSlope; //old value:9142
		Serial1.print("Volts VRMS:");
		Serial1.println(vRMS);

		
		//APPARENT ENERGY SECTION
		ADEgetRegister(LVAENERGY,&val);
		Serial1.print("int Line Cycle Apparent Energy after 200 half-cycles:");
		Serial1.println(val);
		energyJoules = val*2014/10000;
		Serial1.print("Energy in Joules over the past 2 seconds:");
		Serial1.println(energyJoules);
		Serial1.print("Calculated apparent power usage:");
		Serial1.println(energyJoules/2);
		
		//THIS IS NOT WORKING FOR SOME REASON
		//WE NEED TO FIX THE ACTIVE ENERGY REGISTER AT SOME POINT
		//ACTIVE ENERGY SECTION
		ADEgetRegister(LAENERGY,&val);
		Serial1.print("int Line Cycle Active Energy after 200 half-cycles:");
		Serial1.println(val);
		
/*		iRMS = data/161;//data*1000/40172/4;
		Serial1.print("mAmps IRMS:");
		Serial1.println(iRMS);
*/
		
		delay(500);
		/*
		ADEreadData(RSTSTATUS, &data);
		interruptStatus = data >> 16; //need only 16 bits for the status
		Serial1.print("bin Interrupt Status Register after Reset:");
		Serial1.println(interruptStatus, BIN);*/
		
	} //end of if statement

	//Serial1.flush();
	CSSelectDevice(DEVDISABLE);
	
	setDbgLeds(0);
	loopCounter++;

} //end of main loop


void softSetup() 
{
	int32_t data = 0;

	//int32_t data2 = 0;
	//int32_t ch1osVal2 = 0x00000000;

	Serial1.print("\n\n\rReStarted Program\n\n\r");
	
	CSSelectDevice(testChannel); //start SPI comm with the test device channel
	//Turn on the Digital Integrator for Channel 1
	int32_t ch1osVal2 =(1 << 7);
	int32_t data2;
	int8_t ch1os,enableBit;

	Serial1.print("set CH1OS:");
	Serial1.println(RCstr(ADEsetRegister(CH1OS,&ch1osVal2)));
	Serial1.print("get CH1OS:");
	Serial1.println(RCstr(ADEgetCHXOS(1,&enableBit,&ch1os)));
	Serial1.print("enabled: ");
	Serial1.println(enableBit,BIN);
	Serial1.print("offset: ");
	Serial1.println(ch1os);

	//set the gain to 2 for channel 1 since the sensitivity appears to be 0.02157 V/Amp
	int32_t gainVal = 1;

	Serial1.print("BIN GAIN:");
	Serial1.print(RCstr(ADEsetRegister(GAIN,&gainVal)));
	RCstr(ADEgetRegister(GAIN,&gainVal));
	Serial1.println(gainVal,BIN);
	
	//Set the IRMSOS to 0d444 or 0x01BC. This is the measured offset value.
	int32_t iRmsOsVal = 0x01BC;
	ADEsetRegister(IRMSOS,&iRmsOsVal);
	ADEgetRegister(IRMSOS,&iRmsOsVal);
	Serial1.print("hex IRMSOS:");
	Serial1.println(iRmsOsVal, HEX);
	
	//WHAT'S GOING ON WITH THIS OFFSET? THE COMMENT DOESN'T MATCH THE VALUE
	//Set the VRMSOS to -0d549. This is the measured offset value.
	int32_t vRmsOsVal = 0x07FF;//F800
	ADEsetRegister(VRMSOS,&vRmsOsVal);
	ADEgetRegister(VRMSOS,&vRmsOsVal);
	Serial1.print("hex VRMSOS read from register:");
	Serial1.println(vRmsOsVal, HEX);
	
	//set the number of cycles to wait before taking a reading
	int32_t linecycVal = 0xC8;
	ADEsetRegister(LINECYC,&linecycVal);
	ADEgetRegister(LINECYC,&linecycVal);
	Serial1.print("int linecycVal:");
	Serial1.println(linecycVal);
	
	//read and set the CYCMODE bit on the MODE register
	int32_t modeReg = 0;
	ADEgetRegister(MODE,&modeReg);
	Serial1.print("bin MODE register before setting CYCMODE:");
	Serial1.println(modeReg, BIN);
	modeReg |= CYCMODE;	 //set the line cycle accumulation mode bit
	ADEsetRegister(MODE,&modeReg);
	ADEgetRegister(MODE,&modeReg);
	Serial1.print("bin MODE register after setting CYCMODE:");
	Serial1.println(modeReg, BIN);
	
	//reset the Interrupt status register
	ADEgetRegister(RSTSTATUS, &data);
	Serial1.print("bin Interrupt Status Register:");
	Serial1.println(data, BIN);
	
	
	CSSelectDevice(DEVDISABLE); //end SPI comm with the selected device	
	
}


//*****Commented Section Below*****	
	
	/* Shift Reg.
	 * 
	int8_t first1 = 20;
	setEnabled(true);
	for (int8_t i = first1; i < first1 + 1; i++){
		shiftBit(true);
		//shiftBit(false);
		//latch();
	}
	for(int i = 0; i < first1; i++){
		shiftBit(false);
	}
	latch();
	delay(1000);
	clearShiftRegister();
	latch();
	*/

	/* Demux*/
	/*
	//muxSetEnabled(true);
	muxSelect(16);
	for (int i = 0; i < 21; i++){
		//muxSelect(i);
		//delay(1000);
	}*/

	/* Select*/
	/*
	CSSelectDevice(SDCARD);
	delay(1000);
	for(int i =0; i < 21; i++){
		CSSelectDevice(i);
		delay(500);
	}
	*/
	
	//SD Card
	/*
	SPI.setDataMode(SPI_MODE0);
	CSSelectDevice(SDCARD);
	struct sd_raw_info info = {0}; 
	sd_raw_get_info(&info);
	if(info.manufacturer || info.revision) {
		setDbgLeds(GPAT);
		Serial1.println("manufacturer");
		Serial1.println(info.manufacturer,BIN);
		Serial1.println("revision");
		Serial1.println(info.revision,BIN);
	} else { 
		Serial1.println("Nothing From SD Card Received");
	}
	delay(1000);
	*/
	

	/* ADE*/
	//INIT SPI
	//SPI

/*
	int32_t iData = 0;
	CSSelectDevice(testChannel);
	ADEgetRegister(regist, &iData);
	CSSelectDevice(DEVDISABLE);

	Serial1.print("int iData:");
	Serial1.println(iData);
	Serial1.print("BIN iData:");
	Serial1.println(iData,BIN);
*/


	/*
	int8_t PAT = 0;
	if(data[0]){
		PAT |= GRNPAT;
	}
	if(data[1]){
		PAT |= YELPAT;
	}
	if(data[2]){
		PAT |=REDPAT;
	}
	setDbgLeds(PAT);
	delay(10000);
	*/
	/*
	Serial1.print("CLKPR:");
	Serial1.println(CLKPR, BIN);
	Serial1.print("SPCR:");
	Serial1.println(SPCR,BIN);
	Serial1.print("SPDR:");
	Serial1.println(SPDR,BIN);
	
	1000100011010 
	*/
	


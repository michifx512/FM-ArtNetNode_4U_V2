/*
	Project Name: 	ArtNet Node 4U V2
	Description:   	Four Universe ArtNet to DMX Node based on Raspberry Pi Pico
	Authors:   		[Francesco Michieletto @ https://github.com/michifx512, Francesco Santovito] @ EFF Service
	Date: 			05/09/2023
	Version:   		2.1.0

	Hardware Components:
		- Custom PCB
		- Raspberry Pi Pico RP2040
		- WizNet W5500 Lite Ethernet Module
		- 4x MAX485 TTL modules
		- Logic level shifters, bunch of resistors and capacitors, etc.

	Libraries Used:
		- ArtNet @ https://github.com/hideakitai/ArtNet
		- Ethernet, SPI
		- PICO-DMX @ https://github.com/jostlowe/Pico-DMX
		- FastLED timers by Marc Miller, @ https://github.com/mattncsu/FastLED-Timers

	Changes needed in Ethernet library:
		- Change SPI Speed to 62.5 MHz
		- Define ETHERNET_LARGE_BUFFERS
		- Change the MAX_SOCK_NUM to 1

*/

/*
	TODO: ADD ARTPOLLREPLY (changed with ArtNet Library V 0.4.x), WEB SERVER for SETUP
*/

// ----- ----- ----- ----- ----- Libraries ----- ----- ----- ----- -----
#include "FastLED_timers.h"
#include <ArtnetEther.h>
#include <DmxOutput.h>
#include <Ethernet.h>
#include <SPI.h>

// ----- ----- ----- ----- ----- Ethernet Stuff  ----- ----- ----- ----- -----
#define W5500_RESET_PIN 22
byte mac[] = { 0x00, 0x08, 0xDC, 0x00, 0x00, 0x01 };  // 00:08:DC::: is the WizNet MAC address prefix, just for consinstency with standards
byte IP[] = { 2, 0, 0, 11 };
byte SUBNET[] = { 255, 0, 0, 0 };
byte DNS[] = { 2, 0, 0, 1 };
byte GATEWAY[] = { 2, 0, 0, 1 };

bool ethConnected = false;

// ----- ----- ----- ----- ----- ArtNet & DMX Stuff ----- ----- ----- ----- -----
ArtnetReceiver artnet;
String artnetLongname = "EFF Service ArtNet Node 4U V2";
String artnetShortname = "ArtNetNode_4U_V2";
#define NUM_PORTS 4
#define UNIVERSE_LENGTH 512
byte universes[NUM_PORTS] = { 0, 1, 2, 3 };
byte dmxData[NUM_PORTS][UNIVERSE_LENGTH + 1];
DmxOutput dmxOutputs[NUM_PORTS];

byte nPackets[NUM_PORTS] = { 0 };  // count for packets received

// ----- MAX485 Pins -----
const byte txPins[NUM_PORTS] = { 0, 3, 8, 13 };
const byte enPins[NUM_PORTS] = { 1, 6, 9, 14 };
const byte rxPins[NUM_PORTS] = { 2, 7, 12, 15 };

unsigned long lastDMXUpdate[NUM_PORTS] = { 0 };  // Used for constant 40fps output on the ports

unsigned long currTime = millis();
unsigned long lastSerialPrintUpdate = millis();

unsigned long lastFrame[NUM_PORTS] = { 0 };
unsigned long lastArtFrame = millis();

bool ledState = false;
// bool blinkArtEnable=false;
// bool blinkEnable[NUM_PORTS] = {1};

// ----- ----- ----- ----- ----- Status LEDs Stuff ----- ----- ----- ----- -----
#define LED_ETH_PIN 21
#define LED_ART_PIN 20
const byte ledPins[NUM_PORTS] = { 4, 5, 10, 11 };
byte ledBrightness = 255;          // 8-bit value, Duty Cycle
byte ledBlink_FullCycleTime = 50;  //milliseconds
byte ethLedBrightness = 10;        // this led is too bright

//  ----- ----- ----- ----- ----- Functions  ----- ----- ----- ----- -----
void BeginStatusLEDs() {
    // Just a pinMode setup and a quick flash test at power on
    pinMode(LED_ART_PIN, OUTPUT);
    digitalWrite(LED_ART_PIN, HIGH);
    pinMode(LED_ETH_PIN, OUTPUT);
    digitalWrite(LED_ETH_PIN, HIGH);
    for (byte i = 0; i < NUM_PORTS; i++) {
        pinMode(ledPins[i], OUTPUT);
        //analogWrite(ledPins[i], 255);
        digitalWrite(ledPins[i], HIGH);
    }
    delay(200);
    digitalWrite(LED_ART_PIN, LOW);
    digitalWrite(LED_ETH_PIN, LOW);
    for (byte i = 0; i < NUM_PORTS; i++) {
        digitalWrite(ledPins[i], LOW);
    }
}

void W5500_Reset() {
    Serial.println("Resetting Ethernet Chip ...");
    pinMode(W5500_RESET_PIN, OUTPUT);
    digitalWrite(W5500_RESET_PIN, LOW);
    delay(1);
    digitalWrite(W5500_RESET_PIN, HIGH);
    delay(100);
    Serial.println("Ethernet Reset Done.");
}

void BeginEthernet() {
    // delay(1000);
    Serial.println("Begin Ethernet");
    Ethernet.init(SS);
    IPAddress ip(IP[0], IP[1], IP[2], IP[3]);
    IPAddress subnet(SUBNET[0], SUBNET[1], SUBNET[2], SUBNET[3]);
    IPAddress dns(DNS[0], DNS[1], DNS[2], DNS[3]);
    IPAddress gateway(GATEWAY[0], GATEWAY[1], GATEWAY[2], GATEWAY[3]);
    Ethernet.begin(mac, ip, dns, gateway, subnet);
    Serial.println(Ethernet.localIP());

    ethConnected = (Ethernet.linkStatus() == LinkON);
    analogWrite(LED_ETH_PIN, byte(ethConnected) * ethLedBrightness);
    //digitalWrite(LED_ETH_PIN, Ethernet.linkStatus());
}

void BeginArtNet() {
    // delay(1000);
    Serial.println("Begin ArtNet");
    artnet.begin();
    analogWrite(LED_ART_PIN, ledBrightness);
}

void BeginDMX() {
    // delay(1000);
    Serial.println("Begin DMX..");
    for (byte i = 0; i < NUM_PORTS; i++) {
        pinMode(enPins[i], OUTPUT);
        digitalWrite(enPins[i], HIGH);

        dmxOutputs[i].begin(txPins[i], pio0);
        //analogWrite(ledPins[i], ledBrightness);
        digitalWrite(ledPins[i], HIGH);
    }
}

void DMXOut() {
    // to have a constant framerate DMX output of ~40fps
    // with this method, the last frame is stored in memory and is contiunued to be output if ethernet cable disconnected or artnet signal missing
    for (byte i = 0; i < NUM_PORTS; i++) {
        if ((millis() - lastDMXUpdate[i]) > 24) {
            lastDMXUpdate[i] = millis();
            dmxOutputs[i].write(dmxData[i], UNIVERSE_LENGTH + 1);
        }
    }
}

void PrintFPStoSerial() {
    // Print FPS received from ArtNet to the Serial port, for debug
    if (currTime - lastSerialPrintUpdate > 1000) {
        lastSerialPrintUpdate = currTime;
        if (ethConnected) {
            for (byte i = 0; i < NUM_PORTS; i++) {
                Serial.print(nPackets[i]);
                Serial.print(";\t");
                nPackets[i] = 0;
            }
            Serial.println(" fps");
        } else
            Serial.println("Ethernet link not connected.");
    }
}

void EthLedManagement() {
    ethConnected = (Ethernet.linkStatus() == LinkON);
    analogWrite(LED_ETH_PIN, ethConnected * ethLedBrightness);
    //digitalWrite(LED_ETH_PIN, ethConnected);
}

//  ----- ----- ----- ----- ----- Setup  ----- ----- ----- ----- -----
void setup() {
    Serial.begin(115200);
    BeginStatusLEDs();
    W5500_Reset();
    BeginEthernet();
    // TODO FIX ARTPOLLREPLY WITH NEW CODE (ArtNet Library Version 0.4.X)
    //artnet.longname(artnetLongname);
    //artnet.shortname(artnetShortname);
    BeginArtNet();
    BeginDMX();
    artnet.setArtPollReplyConfig(0x00FF, 0x0000, 0x60, 0x80, artnetShortname, artnetLongname, "");
    
    //artnet.subscribeArtDmxUniverse([&](const uint32_t univ, const uint8_t *recData, const uint16_t size)
    artnet.subscribeArtDmx([&](const uint8_t* data, uint16_t size, const ArtDmxMetadata& metadata, const ArtNetRemoteInfo& remote) {
        for (byte i = 0; i < NUM_PORTS; i++) {
            if (universes[i] == metadata.universe) {
                memcpy(&dmxData[i][1], data, size);
                nPackets[i]++;

                if (millis() - lastFrame[i] >= ledBlink_FullCycleTime) {
                    digitalWrite(ledPins[i], 0);
                    lastFrame[i] = millis();
                }
                //Serial.print("Source IP: ");
                //Serial.println(remote.ip);

                // blinkEnable[i] = true;

                // dmxOutputs[i].write(data[i], UNIVERSE_LENGTH + 1); // to directly output a frame when received. use this if you want to have variable framerate, not reccomended since some lights may have issues
            }
        }
        if (millis() - lastArtFrame >= ledBlink_FullCycleTime) {
            digitalWrite(LED_ART_PIN, 0);
            lastArtFrame = millis();
        }
        //blinkArtEnable = true;
    });
}

void loop() {
    while (true) {
        artnet.parse();
        EthLedManagement();
        DMXOut();
        PrintFPStoSerial();
        currTime = millis();


        for (byte i = 0; i < NUM_PORTS; i++) {
            if (currTime - lastFrame[i] > (ledBlink_FullCycleTime / 2)) digitalWrite(ledPins[i], HIGH);
        }
        if (currTime - lastArtFrame > (ledBlink_FullCycleTime / 2)) digitalWrite(LED_ART_PIN, HIGH);

        /*

        EVERY_N_MILLISECONDS(25){
			ledState = !ledState;
			if(blinkArtEnable) analogWrite(LED_ART_PIN, ledBrightness * ledState);
			for(byte i=0; i<NUM_PORTS; i++){
				if(blinkEnable[i]) analogWrite(ledPins[i], ledBrightness * ledState);
			}
		}
		EVERY_N_MILLISECONDS(5){
			for(byte i=0; i<NUM_PORTS; i++){
				if(millis()-lastFrame[i]>50){
					blinkEnable[i] = false;
					analogWrite(ledPins[i], ledBrightness);
				}
			}
			if(millis()-lastArtFrame>50){
				blinkArtEnable = false;
				analogWrite(LED_ART_PIN, ledBrightness);
			}
		}
		*/
    }
}
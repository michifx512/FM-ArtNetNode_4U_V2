/*
	Project Name: 	ArtNet Node 4U V2
	Description:   	Four Universe ArtNet to DMX Node based on Raspberry Pi Pico
	Authors:   		[Francesco Michieletto @ https://github.com/michifx512] @ EFF Service
	Creation Date:  05/09/2023
	Version:   		2.2.0 BETA



    !!!!!!!!! WARNING: THIS IS A BETA VERSION, NEW FEATURES MAY NOT WORK PROPERLY !!!!!!!!!



	Hardware Components:
		- Custom PCB
		- Raspberry Pi Pico RP2040
		- WizNet W5500 Lite Ethernet Module
		- 4x MAX485 TTL modulesa
		- Logic level shifters, bunch of resistors and capacitors, etc.

	Libraries Used:
		- ArtNet @ https://github.com/hideakitai/ArtNet (v0.4.4 Working) //TEST 0.8.0
		- Ethernet @ https://www.arduino.cc/reference/en/libraries/ethernet/ (v2.0.2 working)
    - SPI
		- PICO-DMX @ https://github.com/jostlowe/Pico-DMX (v3.1.0 Working)
		- FastLED timers by Marc Miller, @ https://github.com/mattncsu/FastLED-Timers

	Changes needed in Ethernet library:
		- Change SPI Speed to 62.5 MHz (w5100.h file)
		- Define ETHERNET_LARGE_BUFFERS
		- Change the MAX_SOCK_NUM to 1

*/

/*
	TODO: FIX ARTPOLLREPLY (changed with ArtNet Library V 0.4.x), WEB SERVER for SETUP
    // TEST 
*/

#define PROJECT_VERSION "2.2.0 BETA"

// ----- ----- ----- ----- ----- Libraries ----- ----- ----- ----- -----
#include "FastLED_timers.h"
#include <ArtnetEther.h>
#include <DmxOutput.h>
#include <Ethernet.h>
#include <SPI.h>
#include <EEPROM.h>

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

byte nPackets[NUM_PORTS] = { 0 };  // count for packets received on each universe

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

// ----- ----- ----- ----- ----- Status LEDs Stuff ----- ----- ----- ----- -----
#define LED_ETH_PIN 21
#define LED_ART_PIN 20
const byte ledPins[NUM_PORTS] = { 4, 5, 10, 11 };
byte ledBrightness = 255;          // PWM Duty Cycle !!!! TO BE FIXED !!!!
byte ethLedBrightness = 10;        // kept separated because the ethernet led is too bright
byte ledBlink_FullCycleTime = 50;  // milliseconds

// ----- ----- ----- ----- ----- Functions  ----- ----- ----- ----- -----
void BeginStatusLEDs() {
    // Just a pinMode setup and a quick flash test at power on
    pinMode(LED_ART_PIN, OUTPUT);
    pinMode(LED_ETH_PIN, OUTPUT);
    digitalWrite(LED_ART_PIN, HIGH);
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
    Serial.println("Begin ArtNet");
    artnet.begin();
    analogWrite(LED_ART_PIN, ledBrightness);
}

void BeginDMX() {
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
    // also, the last frame is stored in the memory and is continued to be output if input signal is lost (e.g. cable disconnected or output disabled)
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

void SaveDMXToEEPROM() {
    for (byte i = 0; i < NUM_PORTS; i++) {
        for (int j = 0; j < UNIVERSE_LENGTH + 1; j++) {
            EEPROM.update(i * (UNIVERSE_LENGTH + 1) + j, dmxData[i][j]);
        }
    }
    EEPROM.commit();
    Serial.println("DMX data saved to EEPROM.");
}

void LoadDMXFromEEPROM() {
    for (byte i = 0; i < NUM_PORTS; i++) {
        for (int j = 0; j < UNIVERSE_LENGTH + 1; j++) {
            dmxData[i][j] = EEPROM.read(i * (UNIVERSE_LENGTH + 1) + j);
        }
    }
    Serial.println("DMX data loaded from EEPROM.");
}

// ----- ----- ----- ----- ----- Setup  ----- ----- ----- ----- -----
void setup() {
    Serial.begin(115200);
    EEPROM.begin(NUM_PORTS * (UNIVERSE_LENGTH + 1));
    LoadDMXFromEEPROM();
    BeginStatusLEDs();
    W5500_Reset();
    BeginEthernet();
    // TODO FIX ARTPOLLREPLY WITH NEW CODE (ArtNet Library Version 0.4.X)
    //artnet.longname(artnetLongname);
    //artnet.shortname(artnetShortname);
    BeginArtNet();
    BeginDMX();
    artnet.setArtPollReplyConfig(0x00FF, 0x0000, 0x60, 0x80, artnetShortname, artnetLongname, "", uint8_t sw_in[NUM_PORTS] = {0});
    
    //artnet.subscribeArtDmxUniverse([&](const uint32_t univ, const uint8_t *recData, const uint16_t size)
    artnet.subscribeArtDmx([&](const uint8_t* data, uint16_t size, const ArtDmxMetadata& metadata, const ArtNetRemoteInfo& remote) {
        for (byte i = 0; i < NUM_PORTS; i++) {
            if (universes[i] == metadata.universe+(metadata.subnet*16)) {
                memcpy(&dmxData[i][1], data, size);
                nPackets[i]++;
                //Serial.println(String("universe:") + String(metadata.universe) + String("\tsubnet:") + String(metadata.subnet));
                if (millis() - lastFrame[i] >= ledBlink_FullCycleTime) {
                    digitalWrite(ledPins[i], 0);
                    lastFrame[i] = millis();
                }
                //Serial.print("Source IP: ");
                //Serial.println(remote.ip);
                // dmxOutputs[i].write(data[i], UNIVERSE_LENGTH + 1); // to directly output a frame when received. use this if you want to have variable framerate, not reccomended since some lights may have issues
            }
        }
        if (millis() - lastArtFrame >= ledBlink_FullCycleTime) {
            digitalWrite(LED_ART_PIN, 0);
            lastArtFrame = millis();
        }
    });
}
// ----- ----- ----- ----- ----- Loop  ----- ----- ----- ----- -----
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

        // Check for custom serial command to save DMX data, used in case of power loss
        if (Serial.available()) {
            String command = Serial.readStringUntil('\n');
            if (command == "/SaveDMX") {
                SaveDMXToEEPROM();
            }
        }
    }
}
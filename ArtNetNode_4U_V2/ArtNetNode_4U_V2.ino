/*
    Project Title: EFF Service ArtNet Node 4U V2.0
    Description:   4 Universe ArtNet to DMX Node
    Author:        Francesco Michieletto @ https://github.com/michifx512, Francesco Santovito @ EFF Service
    Date:          05/09/2023
    Version:       2.0.2

    Hardware Components:
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

    TODO: ADD ARTPOLLREPLY,  ADD STATUS LEDS CODE

*/

// ----- ----- ----- ----- ----- Libraries ----- ----- ----- ----- -----
#include <ArtnetEther.h>
#include <SPI.h>
#include <Ethernet.h>
#include <DmxOutput.h>
#include "FastLED_timers.h"

// ----- ----- ----- ----- ----- Ethernet Stuff  ----- ----- ----- ----- -----
#define W5500_RESET_PIN 22
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte IP[] = { 10, 0, 0, 10 };
byte SUBNET[] = { 255, 0, 0, 0 };
byte DNS[] = { 10, 0, 0, 1 };
byte GATEWAY[] = { 10, 0, 0, 1 };
bool ethConnected = false;

// ----- ----- ----- ----- ----- ArtNet & DMX Stuff ----- ----- ----- ----- -----
#define NUM_PORTS 4
byte universes[NUM_PORTS] = { 0, 1, 2, 3 };
ArtnetReceiver artnet;
#define UNIVERSE_LENGTH 512
byte data[NUM_PORTS][UNIVERSE_LENGTH + 1];

int nPackets[NUM_PORTS] = { 0 };  // used for debugging

#define LED_ETH_PIN 21
#define LED_ART_PIN 20

byte txPins[NUM_PORTS] = { 0, 3, 8, 13 };
byte enPins[NUM_PORTS] = { 1, 6, 9, 14 };
byte rxPins[NUM_PORTS] = { 2, 7, 12, 15 };
byte ledPins[NUM_PORTS] = { 4, 5, 10, 11 };

DmxOutput dmxOutputs[NUM_PORTS];

unsigned long lastUpdate = millis();
unsigned long lastDMXUpdate[NUM_PORTS] = { 0 };
unsigned long lastFrame[NUM_PORTS] = { 0 };

//  ----- ----- ----- ----- ----- Function declarations  ----- ----- ----- ----- -----
void PrintSPIPins() {
    Serial.println("\n\n\nSPI PINS:");
    Serial.print("SCK Pin: ");
    Serial.println(SCK);
    Serial.print("CS Pin: ");
    Serial.println(SS);
    Serial.print("MISO Pin: ");
    Serial.println(MISO);
    Serial.print("MOSI Pin: ");
    Serial.println(MOSI);
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
    //delay(1000);
    Serial.println("Begin Ethernet");
    Ethernet.init(SS);
    IPAddress ip(IP[0], IP[1], IP[2], IP[3]);
    IPAddress subnet(SUBNET[0], SUBNET[1], SUBNET[2], SUBNET[3]);
    IPAddress dns(DNS[0], DNS[1], DNS[2], DNS[3]);
    IPAddress gateway(GATEWAY[0], GATEWAY[1], GATEWAY[2], GATEWAY[3]);
    Ethernet.begin(mac, ip, dns, gateway, subnet);
    Serial.println(Ethernet.localIP());
}

void BeginArtNet() {
    //delay(1000);
    Serial.println("Begin ArtNet");
    artnet.begin(0, 0);
}

void BeginDMX() {
    //delay(1000);
    Serial.println("Begin DMX");
    for (int i = 0; i < NUM_PORTS; i++) {
        digitalWrite(enPins[i], HIGH);
        dmxOutputs[i].begin(txPins[i], pio0);
    }
}

/*
void DMXPacketMemory(){
    // function to keep sending out last dmx packet recieved if artnet disconnected or no frames recieved, keeps lights on etc.
    for(byte i=0; i<NUM_PORTS; i++) {
        if((millis() - lastFrame[i])>500){
            if((millis()-lastDMXUpdate[i])>21){
                lastDMXUpdate[i] = millis();
                Serial.println("DMX MEMORY");
                dmxOutputs[i].write(data[i], UNIVERSE_LENGTH + 1);
            }
        }
    }
}
*/

void DMXOut() {
    for (byte i = 0; i < NUM_PORTS; i++) {
        if ((millis() - lastDMXUpdate[i]) > 24) {
            lastDMXUpdate[i] = millis();
            dmxOutputs[i].write(data[i], UNIVERSE_LENGTH + 1);
        }
    }
}

void PrintFPStoSerial() {
    unsigned long time = millis();
    if (time - lastUpdate > 1000) {
        lastUpdate = time;
        if (ethConnected) {
            for (byte i = 0; i < NUM_PORTS; i++) {
                Serial.print(nPackets[i]);
                Serial.print(";\t");
            }
            Serial.println(" fps");
            for (byte i = 0; i < NUM_PORTS; i++) {
                nPackets[i] = 0;
            }
        }else Serial.println("Ethernet link not connected.");
    }
}

//  ----- ----- ----- ----- ----- Setup  ----- ----- ----- ----- -----
void setup() {

    // testing for more than 4 univ
    /*for(byte i=0; i<NUM_PORTS; i++){
        universes[i] = i;
    }*/

    Serial.begin(115200);
    //while (!Serial) { ; }

    PrintSPIPins();
    W5500_Reset();
    BeginEthernet();
    artnet.longname("EFF Service ArtNet Node 4 V2");
    artnet.shortname("ArtNetNode4");
    BeginArtNet();
    BeginDMX();

    /*
    artnet.subscribe(universes[0],[&](const uint8_t* recData, const uint16_t size){
        memcpy(&data[universes[0]][1], recData, size);
        nPackets[universes[0]]++;
        dmxOutputs[universes[0]].write(data[universes[0]], UNIVERSE_LENGTH + 1);
    });
    */

    artnet.subscribe([&](const uint32_t univ, const uint8_t* recData, const uint16_t size) {
        // new with universe choice
        for (byte i = 0; i < NUM_PORTS; i++) {
            if (universes[i] == univ) {
                memcpy(&data[i][1], recData, size);
                nPackets[i]++;
                //lastFrame[i] = millis();
                //dmxOutputs[i].write(data[i], UNIVERSE_LENGTH + 1);
            }
        }

        //old for test
        /*
        if (univ < NUM_PORTS) {
            memcpy(&data[univ][1], recData, size);
            nPackets[univ]++;
            dmxOutputs[univ].write(data[univ], UNIVERSE_LENGTH + 1);
        }*/
    });
}

void loop() {
    while (true) {
        artnet.parse();
        //DMXPacketMemory();
        EVERY_N_MILLISECONDS(250) {
            ethConnected = (Ethernet.linkStatus() == LinkON);
            if (Ethernet.linkStatus() == LinkON) {
                EVERY_N_MILLISECONDS(1000) {
                    Serial.println("Link On");
                }
            }
        }
        DMXOut();
        PrintFPStoSerial();
    }
}

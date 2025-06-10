/*
   WiFi SIXFOUR - A virtual WiFi modem based on the ESP 8266 chipset
   Copyright (C) 2016 Paul Rickards <rickards@gmail.com>
   Added EEPROM read/write, status/help pages, busy answering of incoming calls
   uses the readily available Sparkfun ESP8266 WiFi Shield which has 5v level
   shifters and 3.3v voltage regulation present-- easily connect to a C64
   https://www.sparkfun.com/products/13287

   based on
   ESP8266 based virtual modem
   Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>

   https://github.com/jsalin/esp8266_modem

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

   NOTES ON HARDWARE
   The LED_PIN LED should be connected to vcc so an ON is "LED off".

   TROUBLESHOOTING
   Turn off debugging output, or low speeds, ie 300baud will trigger the watchdog timer.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <EEPROM.h>

#include <mySD.h>

#include <Arduino_GFX_Library.h>
#include "graphics.h"


#define VERSIONA 0
#define VERSIONB 2
#define VERSION_ADDRESS 0    // EEPROM address
#define VERSION_LEN     2    // Length in bytes
#define SSID_ADDRESS    2
#define SSID_LEN        32
#define PASS_ADDRESS    34
#define PASS_LEN        63
#define IP_TYPE_ADDRESS 97   // for future use
#define STATIC_IP_ADDRESS 98 // length 4, for future use
#define STATIC_GW       102  // length 4, for future use
#define STATIC_DNS      106  // length 4, for future use
#define STATIC_MASK     110  // length 4, for future use
#define BAUD_ADDRESS    111
#define ECHO_ADDRESS    112
#define TELNET_ADDRESS  116     // 1 byte
#define VERBOSE_ADDRESS 117
#define FLOW_CONTROL_ADDRESS 119
#define PIN_POLARITY_ADDRESS 120
#define DIAL0_ADDRESS   200
#define DIAL1_ADDRESS   250
#define DIAL2_ADDRESS   300
#define DIAL3_ADDRESS   350
#define DIAL4_ADDRESS   400
#define DIAL5_ADDRESS   450
#define DIAL6_ADDRESS   500
#define DIAL7_ADDRESS   550
#define DIAL8_ADDRESS   600
#define DIAL9_ADDRESS   650
#define LAST_ADDRESS    780

#define SWITCH_PIN 0       // GPIO0 (programmind mode pin)
#define DCD_PIN 35          // DCD Carrier Status
#define RTS_PIN 25         // RTS Request to Send, connect to host's CTS pin, output
#define CTS_PIN 32         // CTS Clear to Send, connect to host's RTS pin, input

#define RXD2_PIN 26
#define TXD2_PIN 27

enum ModemState {
  MODEM_COMMAND,
  MODEM_CONNECT,
  MODEM_SAVING,
};

// Global variables
String build = "1";
String cmd = "";           // Gather a new AT command to this string from serial
ModemState modemState = MODEM_COMMAND;       // Are we in AT command mode or connected mode
bool callConnected = false;// Are we currently in a call
bool telnet = false;       // Is telnet control code handling enabled
bool verboseResults = false;
String filename; // filename for streaming save or load
//#define DEBUG 1          // Print additional debug information to serial channel
#undef DEBUG
#define MAX_CMD_LENGTH 256 // Maximum length for AT command
char plusCount = 0;        // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;// When did we last receive a "+++" sequence
#define LED_TIME 1000         // How many ms to keep LED on at activity
unsigned long ledTime = 0;
#define TX_BUF_SIZE 256    // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];
const int speedDialAddresses[] = { DIAL0_ADDRESS, DIAL1_ADDRESS, DIAL2_ADDRESS, DIAL3_ADDRESS, DIAL4_ADDRESS, DIAL5_ADDRESS, DIAL6_ADDRESS, DIAL7_ADDRESS, DIAL8_ADDRESS, DIAL9_ADDRESS };
String speedDials[10];
const int bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
byte serialspeed;
bool echo = true;
bool autoAnswer = false;
String ssid, password;
byte ringCount = 0;
String resultCodes[] = { "OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "NO DIALTONE", "BUSY", "NO ANSWER" };
enum resultCodes_t { R_MODEM_OK, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NONE, R_NODIALTONE, R_BUSY, R_NOANSWER };
unsigned long connectTime = 0;
bool hex = false;
enum flowControl_t { F_NONE, F_HARDWARE, F_SOFTWARE };
byte flowControl = F_NONE;      // Use flow control
bool txPaused = false;          // Has flow control asked us to pause?
enum pinPolarity_t { P_INVERTED, P_NORMAL }; // Is LOW (0) or HIGH (1) active?
byte pinPolarity = P_INVERTED;

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

WiFiClientSecure tcpClient;

/*
 * Written by Ahmad Shamshiri
  * with lots of research, this sources was used:
 * https://support.randomsolutions.nl/827069-Best-dBm-Values-for-Wifi 
 * This is approximate percentage calculation of RSSI
 * Wifi Signal Strength Calculation
 * Written Aug 08, 2019 at 21:45 in Ajax, Ontario, Canada
 */

const int RSSI_MAX =-50;// define maximum straighten of signal in dBm
const int RSSI_MIN =-100;// define minimum strength of signal in dBm

int dBmtoPercentage(int dBm)
{
  int quality;
    if(dBm <= RSSI_MIN)
    {
        quality = 0;
    }
    else if(dBm >= RSSI_MAX)
    {  
        quality = 100;
    }
    else
    {
        quality = 2 * (dBm + 100);
   }

     return quality;
}//dBmtoPercentage 

String connectTimeString() {
  unsigned long now = millis();
  int secs = (now - connectTime) / 1000;
  int mins = secs / 60;
  int hours = mins / 60;
  String out = "";
  if (hours < 10) out.concat("0");
  out.concat(String(hours));
  out.concat(":");
  if (mins % 60 < 10) out.concat("0");
  out.concat(String(mins % 60));
  out.concat(":");
  if (secs % 60 < 10) out.concat("0");
  out.concat(String(secs % 60));
  return out;
}

void resetBaud(int baudRate) {
  graphics_showLabel(0, RGB565(128, 128, 255), String(baudRate));
  Serial2.begin(baudRate, SERIAL_8N1, RXD2_PIN, TXD2_PIN);
}

void writeSettings() {
  setEEPROM(ssid, SSID_ADDRESS, SSID_LEN);
  setEEPROM(password, PASS_ADDRESS, PASS_LEN);

  EEPROM.write(BAUD_ADDRESS, serialspeed);
  EEPROM.write(ECHO_ADDRESS, byte(echo));
  EEPROM.write(TELNET_ADDRESS, byte(telnet));
  EEPROM.write(VERBOSE_ADDRESS, byte(verboseResults));
  EEPROM.write(FLOW_CONTROL_ADDRESS, byte(flowControl));
  EEPROM.write(PIN_POLARITY_ADDRESS, byte(pinPolarity));

  for (int i = 0; i < 10; i++) {
    setEEPROM(speedDials[i], speedDialAddresses[i], 50);
  }
  EEPROM.commit();
}

void readSettings() {
  echo = EEPROM.read(ECHO_ADDRESS);
  // serialspeed = EEPROM.read(BAUD_ADDRESS);

  ssid = getEEPROM(SSID_ADDRESS, SSID_LEN);
  password = getEEPROM(PASS_ADDRESS, PASS_LEN);
  telnet = EEPROM.read(TELNET_ADDRESS);
  verboseResults = EEPROM.read(VERBOSE_ADDRESS);
  flowControl = EEPROM.read(FLOW_CONTROL_ADDRESS);
  pinPolarity = EEPROM.read(PIN_POLARITY_ADDRESS);

  for (int i = 0; i < 10; i++) {
    speedDials[i] = getEEPROM(speedDialAddresses[i], 50);
  }
}

void defaultEEPROM() {
  EEPROM.write(VERSION_ADDRESS, VERSIONA);
  EEPROM.write(VERSION_ADDRESS + 1, VERSIONB);

  setEEPROM("", SSID_ADDRESS, SSID_LEN);
  setEEPROM("", PASS_ADDRESS, PASS_LEN);
  setEEPROM("d", IP_TYPE_ADDRESS, 1);

  EEPROM.write(BAUD_ADDRESS, 0x00);
  EEPROM.write(ECHO_ADDRESS, 0x01);
  EEPROM.write(TELNET_ADDRESS, 0x00);
  EEPROM.write(VERBOSE_ADDRESS, 0x01);
  EEPROM.write(FLOW_CONTROL_ADDRESS, 0x00);
  EEPROM.write(PIN_POLARITY_ADDRESS, 0x01);

  int addressIndex = 0;
  setEEPROM("bbs.fozztexx.com:23", speedDialAddresses[addressIndex++], 50);
  setEEPROM("cottonwoodbbs.dyndns.org:6502", speedDialAddresses[addressIndex++], 50);
  setEEPROM("borderlinebbs.dyndns.org:6400", speedDialAddresses[addressIndex++], 50);
  setEEPROM("particlesbbs.dyndns.org:6400", speedDialAddresses[addressIndex++], 50);
  setEEPROM("reflections.servebbs.com:23", speedDialAddresses[addressIndex++], 50);
  setEEPROM("heatwavebbs.com:9640", speedDialAddresses[addressIndex++], 50);
  setEEPROM("20forbeers.com:1337", speedDialAddresses[addressIndex++], 50);
  setEEPROM("bbs.retrocampus.com:23", speedDialAddresses[addressIndex++], 50);
  
  for (int i = addressIndex; i < 10; i++) {
    setEEPROM("", speedDialAddresses[i], 50);
  }

  EEPROM.commit();
}

String getEEPROM(int startAddress, int len) {
  String myString;

  for (int i = startAddress; i < startAddress + len; i++) {
    if (EEPROM.read(i) == 0x00) {
      break;
    }
    myString += char(EEPROM.read(i));
    //Serial2.print(char(EEPROM.read(i)));
  }
  //Serial2.println();
  return myString;
}

void setEEPROM(String inString, int startAddress, int maxLen) {
  for (int i = startAddress; i < inString.length() + startAddress; i++) {
    EEPROM.write(i, inString[i - startAddress]);
    //Serial2.print(i, DEC); Serial2.print(": "); Serial2.println(inString[i - startAddress]);
    //if (EEPROM.read(i) != inString[i - startAddress]) { Serial2.print(" (!)"); }
    //Serial2.println();
  }
  // null pad the remainder of the memory space
  for (int i = inString.length() + startAddress; i < maxLen + startAddress; i++) {
    EEPROM.write(i, 0x00);
    //Serial2.print(i, DEC); Serial2.println(": 0x00");
  }
}

void sendResult(int resultCode) {
  Serial2.print("\r\n");
  if (verboseResults == 0) {
    Serial2.println(resultCode);
    return;
  }
  if (resultCode == R_CONNECT) {
    Serial2.print(String(resultCodes[R_CONNECT]) + " " + String(bauds[serialspeed]));
  } else if (resultCode == R_NOCARRIER) {
    Serial2.print(String(resultCodes[R_NOCARRIER]) + " (" + connectTimeString() + ")");
  } else {
    Serial2.print(String(resultCodes[resultCode]));
  }
  Serial2.print("\r\n");
}

void sendString(String msg) {
  Serial2.print("\r\n");
  Serial2.print(msg);
  Serial2.print("\r\n");
}

void statusLEDToggle() {
  static bool led;
  graphics_showLabel(1, RGB565(128, 128, 128), String(led?".":""));
  led = !led;
}

// Hold for 5 seconds to switch to 300 baud
// Slow flash: keep holding
// Fast flash: let go
int checkButton() {
  long time = millis();
  while (digitalRead(SWITCH_PIN) == LOW && millis() - time < 5000) {
    delay(250);
    statusLEDToggle();
    yield();
  }
  if (millis() - time > 3000) {
    Serial2.flush();
    Serial2.end();
    serialspeed = 0;
    delay(100);
    resetBaud(bauds[serialspeed]);
    sendResult(R_MODEM_OK);
    while (digitalRead(SWITCH_PIN) == LOW) {
      delay(50);
      statusLEDToggle();
      yield();
    }
    return 1;
  } else {
    return 0;
  }
}

void connectWiFi() {
  if (ssid == "" || password == "") {
    Serial2.println("CONFIGURE SSID AND PASSWORD. TYPE AT? FOR HELP.");
    return;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial2.print("\nCONNECTING TO SSID "); Serial2.print(ssid);
  uint8_t i = 0;
  graphics_showLabel(1, RGB565(255, 255, 0), "c");
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    statusLEDToggle();
    Serial2.print(".");
    delay(500);
  }
  Serial2.println();
  if (i == 21) {
    Serial2.print("COULD NOT CONNECT TO "); Serial2.println(ssid);
    WiFi.disconnect();
    updateLed();
  } else {
    Serial2.print("CONNECTED TO "); Serial2.println(WiFi.SSID());
    Serial2.print("IP ADDRESS: "); Serial2.println(WiFi.localIP());
    updateLed();
  }
}

void updateLed() {
  if (WiFi.status() == WL_CONNECTED) {
  graphics_showLabel(1, RGB565(0, 255, 0), "C");
  } else {
    graphics_showLabel(1, RGB565(255, 0, 0), "D");
  }
}

void disconnectWiFi() {
  WiFi.disconnect();
  updateLed();
}

void setBaudRate(int inSpeed) {
  if (inSpeed == 0) {
    sendResult(R_ERROR);
    return;
  }
  int foundBaud = -1;
  for (int i = 0; i < sizeof(bauds); i++) {
    if (inSpeed == bauds[i]) {
      foundBaud = i;
      break;
    }
  }
  // requested baud rate not found, return error
  if (foundBaud == -1) {
    sendResult(R_ERROR);
    return;
  }
  if (foundBaud == serialspeed) {
    sendResult(R_MODEM_OK);
    return;
  }
  Serial2.print("SWITCHING SERIAL PORT TO ");
  Serial2.print(inSpeed);
  Serial2.println(" IN 5 SECONDS");
  delay(5000);
  Serial2.end();
  delay(200);
  resetBaud(bauds[foundBaud]);
  serialspeed = foundBaud;
  delay(200);
  sendResult(R_MODEM_OK);
}

void setCarrier(byte carrier) {
  if (pinPolarity == P_NORMAL) carrier = !carrier;
  digitalWrite(DCD_PIN, carrier);
}

void sendScanWifi() {
    Serial2.println("SEARCHING WIFI ACCESS POINTS...");
    int n = WiFi.scanNetworks();
      if (n == 0) {
    Serial2.println("NONE FOUND");
  } else {
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial2.print(i + 1);
      Serial2.print(". ");
      Serial2.print(WiFi.SSID(i));
      Serial2.print(" ");
      Serial2.print(WiFi.RSSI(i));
      Serial2.print("dBm (");
      Serial2.print(dBmtoPercentage(WiFi.RSSI(i)));
      Serial2.print("%)"); 
      if(WiFi.encryptionType(i) == WIFI_AUTH_OPEN)
      {
          Serial2.println(" <UNSECURED>");        
      }else{
          Serial2.println();        
      }
      delay(10);
    }
  }
  WiFi.scanDelete();  
}

void displayNetworkStatus() {
  Serial2.print("WIFI STATUS: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial2.println("CONNECTED");
  }
  if (WiFi.status() == WL_IDLE_STATUS) {
    Serial2.println("OFFLINE");
  }
  if (WiFi.status() == WL_CONNECT_FAILED) {
    Serial2.println("CONNECT FAILED");
  }
  if (WiFi.status() == WL_NO_SSID_AVAIL) {
    Serial2.println("SSID UNAVAILABLE");
  }
  if (WiFi.status() == WL_CONNECTION_LOST) {
    Serial2.println("CONNECTION LOST");
  }
  if (WiFi.status() == WL_DISCONNECTED) {
    Serial2.println("DISCONNECTED");
  }
  if (WiFi.status() == WL_SCAN_COMPLETED) {
    Serial2.println("SCAN COMPLETED");
  }
  yield();

  Serial2.print("SSID.......: "); Serial2.println(WiFi.SSID());

  byte mac[6];
  WiFi.macAddress(mac);
  Serial2.print("MAC ADDRESS: ");
  Serial2.print(mac[0], HEX);
  Serial2.print(":");
  Serial2.print(mac[1], HEX);
  Serial2.print(":");
  Serial2.print(mac[2], HEX);
  Serial2.print(":");
  Serial2.print(mac[3], HEX);
  Serial2.print(":");
  Serial2.print(mac[4], HEX);
  Serial2.print(":");
  Serial2.println(mac[5], HEX);
  yield();

  Serial2.print("IP ADDRESS.: "); Serial2.println(WiFi.localIP()); yield();
  Serial2.print("GATEWAY....: "); Serial2.println(WiFi.gatewayIP()); yield();
  Serial2.print("SUBNET MASK: "); Serial2.println(WiFi.subnetMask()); yield();
  Serial2.print("WEB CONFIG.: HTTP://"); Serial2.println(WiFi.localIP()); yield();
  Serial2.print("CALL STATUS: "); yield();
  if (callConnected) {
    Serial2.print("CONNECTED TO "); Serial2.println(ipToString(tcpClient.remoteIP())); yield();
    Serial2.print("CALL LENGTH: "); Serial2.println(connectTimeString()); yield();
  } else {
    Serial2.println("NOT CONNECTED");
  }
}

void displayCurrentSettings() {
  Serial2.println("ACTIVE PROFILE:"); yield();
  Serial2.print("BAUD: "); Serial2.println(bauds[serialspeed]); yield();
  Serial2.print("SSID: "); Serial2.println(ssid); yield();
  Serial2.print("PASS: "); Serial2.println(password); yield();
  Serial2.print("E"); Serial2.print(echo); Serial2.print(" "); yield();
  Serial2.print("V"); Serial2.print(verboseResults); Serial2.print(" "); yield();
  Serial2.print("&K"); Serial2.print(flowControl); Serial2.print(" "); yield();
  Serial2.print("&P"); Serial2.print(pinPolarity); Serial2.print(" "); yield();
  Serial2.print("NET"); Serial2.print(telnet); Serial2.print(" "); yield();
  Serial2.print("S0:"); Serial2.print(autoAnswer); Serial2.print(" "); yield();
  Serial2.println(); yield();

  Serial2.println("SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    Serial2.print(i); Serial2.print(": "); Serial2.println(speedDials[i]);
    yield();
  }
  Serial2.println();
}

void displayStoredSettings() {
  Serial2.println("STORED PROFILE:"); yield();
  Serial2.print("BAUD: "); Serial2.println(bauds[EEPROM.read(BAUD_ADDRESS)]); yield();
  Serial2.print("SSID: "); Serial2.println(getEEPROM(SSID_ADDRESS, SSID_LEN)); yield();
  Serial2.print("PASS: "); Serial2.println(getEEPROM(PASS_ADDRESS, PASS_LEN)); yield();
  Serial2.print("E"); Serial2.print(EEPROM.read(ECHO_ADDRESS)); Serial2.print(" "); yield();
  Serial2.print("V"); Serial2.print(EEPROM.read(VERBOSE_ADDRESS)); Serial2.print(" "); yield();
  Serial2.print("&K"); Serial2.print(EEPROM.read(FLOW_CONTROL_ADDRESS)); Serial2.print(" "); yield();
  Serial2.print("&P"); Serial2.print(EEPROM.read(PIN_POLARITY_ADDRESS)); Serial2.print(" "); yield();
  Serial2.print("NET"); Serial2.print(EEPROM.read(TELNET_ADDRESS)); Serial2.print(" "); yield();
  Serial2.println(); yield();

  Serial2.println("STORED SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    Serial2.print(i); Serial2.print(": "); Serial2.println(getEEPROM(speedDialAddresses[i], 50));
    yield();
  }
  Serial2.println();
}

void waitForSpace() {
  Serial2.print("PRESS SPACE");
  char c = 0;
  while (c != 0x20) {
    if (Serial2.available() > 0) {
      c = Serial2.read();
    }
  }
  Serial2.print("\r");
}

void displayHelp() {
  welcome();
  Serial2.println("AT COMMAND SUMMARY:"); yield();
  Serial2.println("DIAL HOST.....: ATDTHOST:PORT"); yield();
  Serial2.println("SPEED DIAL....: ATDSN (N=0-9)"); yield();
  Serial2.println("SET SPEED DIAL: AT&ZN=HOST:PORT (N=0-9)"); yield();
  Serial2.println("NETWORK INFO..: ATI"); yield();
  Serial2.println("HTTP GET......: ATGET<URL>"); yield();
  //Serial2.println("SERVER PORT...: AT$SP=N (N=1-65535)"); yield();
  Serial2.println("AUTO ANSWER...: ATS0=N (N=0,1)"); yield();
  Serial2.println("LOAD NVRAM....: ATZ"); yield();
  Serial2.println("SAVE TO NVRAM.: AT&W"); yield();
  Serial2.println("SHOW SETTINGS.: AT&V"); yield();
  Serial2.println("FACT. DEFAULTS: AT&F"); yield();
  Serial2.println("PIN POLARITY..: AT&PN (N=0/INV,1/NORM)"); yield();
  Serial2.println("ECHO OFF/ON...: ATE0 / ATE1"); yield();
  Serial2.println("VERBOSE OFF/ON: ATV0 / ATV1"); yield();
  Serial2.println("SET SSID......: AT$SSID=WIFISSID"); yield();
  Serial2.println("SET PASSWORD..: AT$PASS=WIFIPASSWORD"); yield();
  Serial2.println("SET BAUD RATE.: AT$SB=N (3,12,24,48,96"); yield();
  Serial2.println("                192,384,576,1152)*100"); yield();
  waitForSpace();
  Serial2.println("FLOW CONTROL..: AT&KN (N=0/N,1/HW,2/SW)"); yield();
  Serial2.println("WIFI OFF/ON...: ATC0 / ATC1"); yield();
  Serial2.println("WIFI AP SCAN..: ATSCAN"); yield();
  Serial2.println("FILE SAVE.....: ATSAVE<FILENAME>"); yield();
  Serial2.println("FILE LOAD.....: ATLOAD<FILENAME>"); yield();
  Serial2.println("FILE LIST.....: ATLIST"); yield();
  Serial2.println("HANGUP........: ATH"); yield();
  Serial2.println("ENTER CMD MODE: +++"); yield();
  Serial2.println("EXIT CMD MODE.: ATO"); yield();
  Serial2.println("QUERY MOST COMMANDS FOLLOWED BY '?'"); yield();
}

void storeSpeedDial(byte num, String location) {
  //if (num < 0 || num > 9) { return; }
  speedDials[num] = location;
  //Serial2.print("STORED "); Serial2.print(num); Serial2.print(": "); Serial2.println(location);
}

void welcome() {
  Serial2.println();
  Serial2.println("ESP32 SUPERMODEM WIFI BUILD " + build);
}

/**
   Arduino main init function
*/
void setup() {

  pinMode(SWITCH_PIN, INPUT);
  digitalWrite(SWITCH_PIN, HIGH);
  pinMode(DCD_PIN, OUTPUT);
  pinMode(RTS_PIN, OUTPUT);
  digitalWrite(RTS_PIN, LOW); // ready to receive data
  pinMode(CTS_PIN, INPUT);
  //digitalWrite(CTS_PIN, HIGH); // pull up
  setCarrier(false);

  EEPROM.begin(LAST_ADDRESS + 1);
  delay(10);

  if (EEPROM.read(VERSION_ADDRESS) != VERSIONA || EEPROM.read(VERSION_ADDRESS + 1) != VERSIONB) {
    defaultEEPROM();
  }

  readSettings();
  // Fetch baud rate from EEPROM
  serialspeed = EEPROM.read(BAUD_ADDRESS);
  // Check if it's out of bounds-- we have to be able to talk
  if (serialspeed > sizeof(bauds)) {
    serialspeed = 0;
  }

  graphics_init();

  int baud = bauds[serialspeed];
  resetBaud(baud); // Do not use gfx

  Serial2.println("gfx setup"); Serial2.flush();
  graphics_println("Setting up..");
  Serial2.println("written."); Serial2.flush();

  welcome();

  if (!SD.begin(23,5,19,18)) { // For TTGO T2 v1.2
    Serial2.println("No SD card");
    Serial2.flush();
    graphics_println("No SD card");
  }
  else {
    Serial2.println("SD card found");
    Serial2.flush();
    graphics_println("SD card found");
  }

  // WiFi.mode(WIFI_STA);
  // connectWiFi();
  sendResult(R_MODEM_OK);

  graphics_println("done!");
}

String ipToString(IPAddress ip) {
  char s[16];
  sprintf(s, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return s;
}

void hangUp() {
  tcpClient.stop();
  callConnected = false;
  setCarrier(callConnected);
  sendResult(R_NOCARRIER);
  connectTime = 0;
}


/**
   Turn on the LED and store the time, so the LED will be shortly after turned off
*/
void led_on(String s)
{
  graphics_showLabel(2, RGB565(0, 0, 255), s);
  ledTime = millis();
}


void dialOut(String upCmd) {
  // Can't place a call while in a call
  if (callConnected) {
    sendResult(R_ERROR);
    return;
  }
  String host, port;
  int portIndex;
  // Dialing a stored number
  if (upCmd.indexOf("ATDS") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    portIndex = speedDials[speedNum].indexOf(':');
    if (portIndex != -1) {
      host = speedDials[speedNum].substring(0, portIndex);
      port = speedDials[speedNum].substring(portIndex + 1);
    } else {
      port = "23";
    }
  } else {
    // Dialing an ad-hoc number
    int portIndex = cmd.indexOf(":");
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
    }
  }
  host.trim(); // remove leading or trailing spaces
  port.trim();
  Serial2.print("DIALING "); Serial2.print(host); Serial2.print(":"); Serial2.println(port);
  char *hostChr = new char[host.length() + 1];
  host.toCharArray(hostChr, host.length() + 1);
  int portInt = port.toInt();

  tcpClient.setInsecure(); // @todo client.setCACert(to a certificate);
  tcpClient.setPlainStart(); // Allows us to use as standard connections


  //tcpClient.setNoDelay(true); // Try to disable naggle
  if (tcpClient.connect(hostChr, portInt))
  {
    tcpClient.setNoDelay(true); // Try to disable naggle
    sendResult(R_CONNECT);
    connectTime = millis();
    modemState = MODEM_CONNECT;
    Serial2.flush();
    callConnected = true;
    setCarrier(callConnected);
    //if (tcpServerPort > 0) tcpServer.stop();
  }
  else
  {
    sendResult(R_NOANSWER);
    callConnected = false;
    setCarrier(callConnected);
  }
  delete[] hostChr;
}

/**
   Perform a command given in command mode
*/
void command()
{
  Serial2.println();
  String upCmd = cmd;
  upCmd.toUpperCase();

  // we strip out C and BASIC comments, so we can embed modem commands in pocket computer BASIC and C programs.
  while(isDigit(upCmd[0]) || upCmd[0] == ' ') {
    // Remove line numbers from command if this is a BASIC script being interpreted as a command.
    upCmd.remove(0,1);
  }

  if (upCmd.indexOf("/*") == 0) {
    // Remove C comments
    upCmd.remove(0,2);
  }

  int endDelimiter = upCmd.indexOf("*/");
  if (endDelimiter >= 0) {
    upCmd.remove(endDelimiter,2);
  }

  if (upCmd.indexOf("REM ") == 0) {
    // Remove BASIC comments
    upCmd.remove(0,4);
  }

  upCmd.trim();
  if (upCmd == "") return;


  /**** Just AT ****/
  if (upCmd == "AT") {
    sendResult(R_MODEM_OK);
  }

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0) || (upCmd.indexOf("ATDS") == 0))
  {
    dialOut(upCmd);
  }

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    sendResult(R_MODEM_OK);
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    sendResult(R_MODEM_OK);
  }

  else if (upCmd == "ATNET?") {
    Serial2.println(String(telnet));
    sendResult(R_MODEM_OK);
  }

  /**** Display Help ****/
  else if (upCmd == "AT?" || upCmd == "ATHELP") {
    displayHelp();
    sendResult(R_MODEM_OK);
  }

  /**** Reset, reload settings from EEPROM ****/
  else if (upCmd == "ATZ") {
    readSettings();
    sendResult(R_MODEM_OK);
  }

  /**** Disconnect WiFi ****/
  else if (upCmd == "ATC0") {
    disconnectWiFi();
    sendResult(R_MODEM_OK);
  }

  /**** Connect WiFi ****/
  else if (upCmd == "ATC1") {
    connectWiFi();
    sendResult(R_MODEM_OK);
  }

  /**** Control local echo in command mode ****/
  else if (upCmd.indexOf("ATE") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(echo));
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(3, 4) == "0") {
      echo = 0;
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(3, 4) == "1") {
      echo = 1;
      sendResult(R_MODEM_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control verbosity ****/
  else if (upCmd.indexOf("ATV") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(verboseResults));
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(3, 4) == "0") {
      verboseResults = 0;
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(3, 4) == "1") {
      verboseResults = 1;
      sendResult(R_MODEM_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control pin polarity of CTS, RTS, DCD ****/
  else if (upCmd.indexOf("AT&P") == 0) {
    if (upCmd.substring(4, 5) == "?") {
      sendString(String(pinPolarity));
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(4, 5) == "0") {
      pinPolarity = P_INVERTED;
      sendResult(R_MODEM_OK);
      setCarrier(callConnected);
    }
    else if (upCmd.substring(4, 5) == "1") {
      pinPolarity = P_NORMAL;
      sendResult(R_MODEM_OK);
      setCarrier(callConnected);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control Flow Control ****/
  else if (upCmd.indexOf("AT&K") == 0) {
    if (upCmd.substring(4, 5) == "?") {
      sendString(String(flowControl));
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(4, 5) == "0") {
      flowControl = 0;
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(4, 5) == "1") {
      flowControl = 1;
      sendResult(R_MODEM_OK);
    }
    else if (upCmd.substring(4, 5) == "2") {
      flowControl = 2;
      sendResult(R_MODEM_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Set current baud rate ****/
  else if (upCmd.indexOf("AT$SB=") == 0) {
    setBaudRate(upCmd.substring(6).toInt());
  }

  /**** Display current baud rate ****/
  else if (upCmd.indexOf("AT$SB?") == 0) {
    sendString(String(bauds[serialspeed]));;
  }

  /**** Display Network settings ****/
  else if (upCmd == "ATI") {
    displayNetworkStatus();
    sendResult(R_MODEM_OK);
  }

  /**** Display profile settings ****/
  else if (upCmd == "AT&V") {
    displayCurrentSettings();
    waitForSpace();
    displayStoredSettings();
    sendResult(R_MODEM_OK);
  }

  /**** Save (write) current settings to EEPROM ****/
  else if (upCmd == "AT&W") {
    writeSettings();
    sendResult(R_MODEM_OK);
  }

  /**** Set or display a speed dial number ****/
  else if (upCmd.indexOf("AT&Z") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    if (speedNum <= 9) {
      if (upCmd.substring(5, 6) == "=") {
        String speedDial = cmd;
        storeSpeedDial(speedNum, speedDial.substring(6));
        sendResult(R_MODEM_OK);
      }
      if (upCmd.substring(5, 6) == "?") {
        sendString(speedDials[speedNum]);
        sendResult(R_MODEM_OK);
      }
    } else {
      sendResult(R_ERROR);
    }
  }

  /**** Set WiFi SSID ****/
  else if (upCmd.indexOf("AT$SSID=") == 0) {
    ssid = cmd.substring(8);
    sendResult(R_MODEM_OK);
  }

  /**** Display WiFi SSID ****/
  else if (upCmd == "AT$SSID?") {
    sendString(ssid);
    sendResult(R_MODEM_OK);
  }

  /**** Set WiFi Password ****/
  else if (upCmd.indexOf("AT$PASS=") == 0) {
    password = cmd.substring(8);
    sendResult(R_MODEM_OK);
  }

  /**** Display WiFi Password ****/
  else if (upCmd == "AT$PASS?") {
    sendString(password);
    sendResult(R_MODEM_OK);
  }

  /**** Reset EEPROM and current settings to factory defaults ****/
  else if (upCmd == "AT&F") {
    defaultEEPROM();
    readSettings();
    sendResult(R_MODEM_OK);
  }

  /**** Set auto answer off ****/
  else if (upCmd == "ATS0=0") {
    autoAnswer = false;
    sendResult(R_MODEM_OK);
  }

  /**** Set auto answer on ****/
  else if (upCmd == "ATS0=1") {
    autoAnswer = true;
    sendResult(R_MODEM_OK);
  }

  /**** Display auto answer setting ****/
  else if (upCmd == "ATS0?") {
    sendString(String(autoAnswer));
    sendResult(R_MODEM_OK);
  }

  /**** Set HEX Translate On ****/
  else if (upCmd == "ATHEX=1") {
    hex = true;
    sendResult(R_MODEM_OK);
  }

  /**** Set HEX Translate Off ****/
  else if (upCmd == "ATHEX=0") {
    hex = false;
    sendResult(R_MODEM_OK);
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("ATH") == 0) {
    hangUp();
  }

  /**** Reset modem ****/
  else if (upCmd.indexOf("AT$RB") == 0) {
    sendResult(R_MODEM_OK);
    Serial2.flush();
    delay(500);
    ESP.restart();
  }

  /**** Exit modem command mode, go online ****/
  else if (upCmd == "ATO") {
    if (callConnected == 1) {
      sendResult(R_CONNECT);
    modemState = MODEM_COMMAND;
    } else {
      sendResult(R_ERROR);
    }
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP?")
  {
    Serial2.println(WiFi.localIP());
    sendResult(R_MODEM_OK);
  }

  /**** HTTP/S GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
          /**** HTTPS GET request ****/
      // Use the higher-level HTTP client for our code to make optional use of use SSL.
      const int addressIndex = 5; // start of URL in "ATGEThttps://"
      
      tcpClient.setInsecure();
      tcpClient.startTLS();
  
      // Under the hood, this should connect to the correct socket URL and send the HTTP GET information.
      HTTPClient https;
      https.setReuse(true);
      https.setTimeout(6000);
      https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

      // HTTPS connection
      if (https.begin(tcpClient, cmd.substring(addressIndex, cmd.length()))) {  
    
        // start connection and send HTTP header
        int httpCode = https.GET();
    
        // httpCode will be negative on error
        if (httpCode > 0) {
    
          
          // Path found at server or there was a redirect:
          if (httpCode == HTTP_CODE_OK) {
            sendString(https.getString());
          }
          else  {
    
              sendString("HTTP STATUS: ");
              sendString(String(httpCode));
              //sendString(https.headers());
          }
    
        } else {
          sendString("Failed with error:\n");
          sendString(https.errorToString(httpCode));
        }
    
        https.end();  
        tcpClient.stop();
      } else {
        sendString("HTTPS client unable to connect.");
      }
  }
  /**** HTTP POST request ****/
  else if (upCmd.indexOf("ATPOST") == 0) {
    sendString("POST not implemented");
  }
  else if (upCmd.indexOf("ATSAVE") == 0) {
    filename = cmd.substring(6, cmd.length());
    modemState = MODEM_SAVING;
    File myFile = SD.open(filename.c_str(), FILE_WRITE);
    if (!myFile) {
      sendResult(R_ERROR);
    }
    else {
      sendResult(R_MODEM_OK);
      myFile.close();
    }
  }
    else if (upCmd.indexOf("ATLOAD") == 0) {
    filename = cmd.substring(6, cmd.length());
    File myFile = SD.open(filename.c_str(), FILE_READ);
    if (!myFile) {
      sendResult(R_ERROR);
    }
    else {
      graphics_println("Loading file...");
      byte serbuff[1024];
      int len = -1;
      while (len != 0) {
        len = myFile.read(&serbuff[0], 1024);
        Serial2.write(&serbuff[0], len);
      }
      myFile.close();
      graphics_println("File loaded");
    }
  }
    else if (upCmd == "ATLIST") {
    File myFile = SD.open("/");
    if (!myFile) {
      sendResult(R_ERROR);
    }
    else {
      while(true) {
        File entry =  myFile.openNextFile();
        if (!entry) {
          break;
        }
        const char * name = entry.name();
        Serial2.print(name);
        graphics_println(name);
      }
      sendResult(R_MODEM_OK);
      myFile.close();
    }
  }

  /**** wifi scan ****/
  else if (upCmd.indexOf("ATSCAN") == 0) {
    sendScanWifi();
    sendResult(R_MODEM_OK);
  }

  /**** Unknown command ****/
  else {
    sendResult(R_ERROR);
  }

  cmd = "";
}

// RTS/CTS protocol is a method of handshaking which uses one wire in each direction to allow each
// device to indicate to the other whether or not it is ready to receive data at any given moment.
// One device sends on RTS and listens on CTS; the other does the reverse. A device should drive
// its handshake-output wire low when it is ready to receive data, and high when it is not. A device
// that wishes to send data should not start sending any bytes while the handshake-input wire is low;
// if it sees the handshake wire go high, it should finish transmitting the current byte and then wait
// for the handshake wire to go low before transmitting any more.
// http://electronics.stackexchange.com/questions/38022/what-is-rts-and-cts-flow-control
void handleFlowControl() {
  if (flowControl == F_NONE) return;
  if (flowControl == F_HARDWARE) {
    if (digitalRead(CTS_PIN) == pinPolarity) {
      txPaused = true;
    }
    else {
      txPaused = false;
    }
    graphics_showLabel(3, RGB565(255,128,0), txPaused?"F":" ");
  }
  if (flowControl == F_SOFTWARE) {
    
  }
}

/**
   Arduino main loop function
*/
void loop()
{
  // Check flow control
  handleFlowControl();
    
  // Check to see if user is requesting rate change to 300 baud
  checkButton();

  /**** AT command mode ****/
  if (modemState == MODEM_COMMAND)
  {

    // In command mode - don't exchange with TCP but gather characters to a string
    if (Serial2.available())
    {
      char chr = Serial2.read();

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
        command();
        graphics_println(cmd);
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127) || (chr == 20))
      {
        cmd.remove(cmd.length() - 1);
        if (echo == true) {
          Serial2.write(chr);
        }
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        if (echo == true) {
          Serial2.write(chr);
        }
        if (hex) {
          Serial2.print(chr, HEX);
        }
      }
    }
  }
  /**** Connected mode ****/
  else if (modemState == MODEM_CONNECT)
  {
    // Transmit from terminal to TCP
    if (Serial2.available())
    {
      led_on("Tx");

      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      size_t len = std::min(Serial2.available(), max_buf_size);
      Serial2.readBytes(&txBuf[0], len);

      // Enter command mode with "+++" sequence
      for (int i = 0; i < (int)len; i++)
      {
        if (txBuf[i] == '+') plusCount++; else plusCount = 0;
        if (plusCount >= 3)
        {
          plusTime = millis();
        }
        if (txBuf[i] != '+')
        {
          plusCount = 0;
        }
      }

      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true)
      {
        for (int i = len - 1; i >= 0; i--)
        {
          if (txBuf[i] == 0xff)
          {
            for (int j = TX_BUF_SIZE - 1; j > i; j--)
            {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }

      // Write the buffer to TCP finally
      tcpClient.write(&txBuf[0], len);
      yield();
    }

    // Transmit from TCP to terminal
    while (tcpClient.available() && txPaused == false)
    {
      led_on("Rx");
      uint8_t rxByte = tcpClient.read();

      // Is a telnet control code starting?
      if ((telnet == true) && (rxByte == 0xff))
      {
#ifdef DEBUG
        Serial2.print("<t>");
#endif
        rxByte = tcpClient.read();
        if (rxByte == 0xff)
        {
          // 2 times 0xff is just an escaped real 0xff
          Serial2.write(0xff); Serial2.flush();
        }
        else
        {
          // rxByte has now the first byte of the actual non-escaped control code
#ifdef DEBUG
          Serial2.print(rxByte);
          Serial2.print(",");
#endif
          uint8_t cmdByte1 = rxByte;
          rxByte = tcpClient.read();
          uint8_t cmdByte2 = rxByte;
          // rxByte has now the second byte of the actual non-escaped control code
#ifdef DEBUG
          Serial2.print(rxByte); Serial2.flush();
#endif
          // We are asked to do some option, respond we won't
          if (cmdByte1 == DO)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)WONT); tcpClient.write(cmdByte2);
          }
          // Server wants to do any option, allow it
          else if (cmdByte1 == WILL)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)DO); tcpClient.write(cmdByte2);
          }
        }
#ifdef DEBUG
        Serial2.print("</t>");
#endif
      }
      else
      {
        // Non-control codes pass through freely
        Serial2.write(rxByte); yield(); Serial2.flush(); yield();
      }
      handleFlowControl();
    }
  }
  else if (modemState == MODEM_SAVING) {
    if (Serial2.available()) {
      File myFile = SD.open(filename.c_str(), O_ACCMODE);
      if (!myFile) {
        modemState = MODEM_COMMAND;
        Serial2.println("Failed to save file.");
        sendResult(R_ERROR);
      }
      else {
        led_on("SD");
        size_t len = std::min(Serial2.available(), TX_BUF_SIZE);
        Serial2.readBytes(&txBuf[0], len);
        myFile.write(&txBuf[0], len);
        myFile.close();
      }
    }
  }

  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes
  if (plusCount >= 3)
  {
    if (millis() - plusTime > 1000)
    {
      //tcpClient.stop();
      modemState = MODEM_COMMAND;
      sendResult(R_MODEM_OK);
      plusCount = 0;
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if ((!tcpClient.connected()) && (modemState == MODEM_CONNECT) && callConnected == true)
  {
    modemState = MODEM_COMMAND;
    sendResult(R_NOCARRIER);
    connectTime = 0;
    callConnected = false;
    setCarrier(callConnected);
  }

  // Turn off tx/rx led if it has been lit long enough to be visible
  if (millis() - ledTime > LED_TIME) {
    led_on("");
  }
}


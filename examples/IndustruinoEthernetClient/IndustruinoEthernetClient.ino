/*
   Industruino Ethernet example sketch for:
   > Industruino D21G PROTO or IND.I/O
   > Industruino Ethernet module

   Libraries needed:
   > Ethernet2: Industruino version at https://github.com/Industruino/Ethernet2
   > UC1701: https://github.com/Industruino/UC1701
   > Adafruit_SleepyDog: https://github.com/adafruit/Adafruit_SleepyDog

   The D21G topboard has a built-in RTC (MCP79402) with a EUI-64 number that can be used as MAC address
   The Ethernet module has an SD card and 8KB non-volatile FRAM memory

   This sketch:
   -reads 6-byte (48-bit) MAC address from RTC EEPROM over I2C
   -checks if Ethernet module is connected (by writing/reading a flag in FRAM)
   -starts Ethernet with MAC address and static IP address or DHCP
   -sends an HTTP GET request to a server and shows the reply status
   -if the code hangs anywhere for more than 20 seconds, the watchdog timer resets the unit

   Tom Tobback June 2019 for Industruino
*/



////////////////// RTC MAC ////////////////////////////////////////
#include <Wire.h>                               // for RTC MAC
byte mac[6];                                    // read from RTC

////////////////// ETHERNET //////////////////////////////////////
#include <SPI.h>                                // for Ethernet
#include <SD.h>                                 // for FRAM to work
#include <Ethernet2.h>                          // use Industruino version
EthernetClient client;

////////////////// USER CONFIGURATION /////////////////////////////
#define USE_DHCP 0                              // set to 1 for DHCP
IPAddress industruino_ip (192, 168, 1, 100);    // example
#define TCP_SERVER "www.arduino.cc"
#define TCP_SERVER_PORT 80                     // for http

//////////////////////// FRAM ///////////////////////////////////
const byte FRAM_CMD_WREN = 0x06; //0000 0110 Set Write Enable Latch
const byte FRAM_CMD_WRDI = 0x04; //0000 0100 Write Disable
const byte FRAM_CMD_RDSR = 0x05; //0000 0101 Read Status Register
const byte FRAM_CMD_WRSR = 0x01; //0000 0001 Write Status Register
const byte FRAM_CMD_READ = 0x03; //0000 0011 Read Memory Data
const byte FRAM_CMD_WRITE = 0x02; //0000 0010 Write Memory Data
const int FRAM_CS1 = 6; //chip select 1

//////////////////// INDUSTRUINO LCD //////////////////////////////
#include <UC1701.h>
static UC1701 lcd;
#define LCD_PIN 26

/////////////////////// WATCHDOG TIMER ////////////////////////////
#include <Adafruit_SleepyDog.h>
const unsigned int watchdog_interval = 20000;     // 20 seconds: if no wdt reset within this interval, wdt will reset the unit

///////////////////////////////////////////////////////////////////
/// FRAM functions need to be included before setup()
///////////////////////////////////////////////////////////////////
/**
   Write to FRAM (assuming 2 FM25C160 are used)
   addr: starting address
   buf: pointer to data
   count: data length.
          If this parameter is omitted, it is defaulted to one byte.
   returns: 0 operation is successful
            1 address out of range
*/
int FRAMWrite(int addr, byte *buf, int count = 1) {
  if (addr > 0x7ff) return -1;
  byte addrMSB = (addr >> 8) & 0xff;
  byte addrLSB = addr & 0xff;
  digitalWrite(FRAM_CS1, LOW);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(FRAM_CMD_WREN);  //write enable
  digitalWrite(FRAM_CS1, HIGH);
  digitalWrite(FRAM_CS1, LOW);
  SPI.transfer(FRAM_CMD_WRITE); //write command
  SPI.transfer(addrMSB);
  SPI.transfer(addrLSB);
  for (int i = 0; i < count; i++) SPI.transfer(buf[i]);
  digitalWrite(FRAM_CS1, HIGH);
  return 0;
}

///////////////////////////////////////////////////////////////////
/**
   Read from FRAM (assuming 2 FM25C160 are used)
   addr: starting address
   buf: pointer to data
   count: data length.
          If this parameter is omitted, it is defaulted to one byte.
   returns: 0 operation is successful
            1 address out of range
*/
int FRAMRead(int addr, byte *buf, int count = 1) {
  if (addr > 0x7ff) return -1;
  byte addrMSB = (addr >> 8) & 0xff;
  byte addrLSB = addr & 0xff;
  digitalWrite(FRAM_CS1, LOW);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(FRAM_CMD_READ);
  SPI.transfer(addrMSB);
  SPI.transfer(addrLSB);
  for (int i = 0; i < count; i++) buf[i] = SPI.transfer(0x00);
  digitalWrite(FRAM_CS1, HIGH);
  return 0;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

void setup() {

  // setup LCD
  pinMode(LCD_PIN, OUTPUT);                     // LCD backlight
  digitalWrite(LCD_PIN, HIGH);                  // LCD backlight ON
  lcd.begin();
  lcd.print("INDUSTRUINO ETHERNET");

  // start SerialUSB for Serial Monitor
  SerialUSB.begin(115200);
  //while (!SerialUSB);                           // wait for Serial Monitor to connect, for testing only
  SerialUSB.println("===================================");
  SerialUSB.println("== INDUSTRUINO ETHERNET EXAMPLE ===");
  SerialUSB.println("===================================");
  printTime(); SerialUSB.println("SETUP");

  // start watchdog timer
  Watchdog.enable(watchdog_interval);
  SerialUSB.println("Watchdog timer started");

  // get MAC address from RTC
  printTime();
  readMACfromRTC();             // MAC stored in RTC eeprom
  lcd.setCursor(0, 1);
  lcd.print("MAC ");
  for (int u = 0; u < 6; u++) {
    lcd.print(mac[u], HEX);
    if (u < 5) lcd.print(":");
  }

  // setup Ethernet module: SPI chip select pins
  pinMode(FRAM_CS1, OUTPUT);    // for FRAM
  digitalWrite(FRAM_CS1, HIGH);
  pinMode(4, OUTPUT);           // for SD
  digitalWrite(4, HIGH);
  pinMode(10, OUTPUT);          // for Ethernet
  digitalWrite(10, HIGH);

  // check if Ethernet module is connected (by write/read FRAM flag)
  lcd.setCursor(0, 2);
  printTime();
  if (ethernetModule()) {
    SerialUSB.println("Ethernet module found");
    lcd.print("ETH module found");
  } else {
    SerialUSB.println("Ethernet module not found, stop here");
    lcd.print("ETH module NOT found");
    lcd.setCursor(0, 3);
    lcd.print("STOP");
    while (1);  // stay here forever, this will trigger the watchdog timer to reset the unit
  }

  // start Ethernet
  if (USE_DHCP) {
    SerialUSB.print("requesting IP address from DHCP... ");
    if (!Ethernet.begin(mac)) {
      SerialUSB.println("could not get IP address over DHCP, stop here");
      lcd.setCursor(0, 3);
      lcd.print("DHCP failed, STOP");
      while (1);  // stay here forever, this will trigger the watchdog timer to reset the unit
    }
    SerialUSB.println("OK");
  } else {        // static IP
    Ethernet.begin(mac, industruino_ip);
  }
  printTime();
  SerialUSB.print("Ethernet started with above MAC and IP: ");
  SerialUSB.println(Ethernet.localIP());
  lcd.setCursor(0, 3);
  lcd.print("IP ");
  lcd.print(Ethernet.localIP());
  if (USE_DHCP) lcd.print(" DHCP");

  printTime();
  SerialUSB.println("END OF SETUP");
  SerialUSB.println();

}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

void loop() {

  Watchdog.reset();                          // needed to avoid watchdog reset

  printTime();
  SerialUSB.print("connecting to server ");
  SerialUSB.print(TCP_SERVER);
  SerialUSB.print(" on port ");
  SerialUSB.print(TCP_SERVER_PORT);
  SerialUSB.print("... ");
  lcd.setCursor(0, 4);
  lcd.print("connecting to:");
  lcd.setCursor(0, 5);
  lcd.print(TCP_SERVER);

  if (client.connect(TCP_SERVER, TCP_SERVER_PORT)) {
    SerialUSB.println("connected");
    lcd.print(" OK");
    // example for HTTP GET REQUEST
    client.println("GET /latest.txt HTTP/1.1");
    client.print("Host: ");
    client.println(TCP_SERVER);
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println();

    /*
      // this block outputs the reply
      unsigned long timestamp = millis();
      while (client.available() || (millis() - timestamp < 1000)) {
      if (client.available()) {
        SerialUSB.write(client.read());
      }
      }
    */

    // this block just looks for '200 OK' in the reply
    lcd.setCursor(0, 6);
    printTime();
    if (client.find("200 OK")) {
      SerialUSB.println("server replies OK");
      lcd.print("server replies OK ");
    } else {
      SerialUSB.println("no OK received from server");
      lcd.print("no OK received    ");
    }

    client.stop();

  } else {
    printTime();
    SerialUSB.println("failed to connect, restarting Ethernet");
    lcd.setCursor(0, 6);
    lcd.print("failed to connect    ");
    lcd.setCursor(0, 7);
    lcd.print("restart Ethernet...  ");

    if (USE_DHCP) {
      SerialUSB.print("requesting IP address from DHCP... ");
      if (!Ethernet.begin(mac)) {
        SerialUSB.println("could not get IP address over DHCP, stop here");
        lcd.setCursor(0, 3);
        lcd.print("DHCP failed, STOP");
        while (1);  // stay here forever, this will trigger the watchdog timer to reset the unit
      }
      SerialUSB.println("OK");
    } else {   // static IP
      Ethernet.begin(mac, industruino_ip);
    }
    // refresh IP on LCD
    lcd.setCursor(0, 3);
    lcd.print("IP ");
    lcd.print(Ethernet.localIP());
    if (USE_DHCP) lcd.print(" DHCP");

  }
  SerialUSB.println();

  delay(5000);
  // clear LCD lines about connection
  lcd.setCursor(0,5);
  lcd.print("                    ");
  lcd.setCursor(0,6);
  lcd.print("                    ");
  lcd.setCursor(0,7);
  lcd.print("                    ");
  
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

//////////////////////////// MAC ADDRESS ////////////////////////////////////////////
// the RTC has a MAC address stored in EEPROM - 8 bytes 0xf0 to 0xf7
void readMACfromRTC() {
  Wire.begin();                                 // I2C for RTC MAC
  SerialUSB.print("Reading MAC from RTC EEPROM: ");
  int mac_index = 0;
  for (int i = 0; i < 8; i++) {   // read 8 bytes of 64-bit MAC address, 3 bytes valid OUI, 5 bytes unique EI
    byte m = readByte(0x57, 0xf0 + i);
    SerialUSB.print(m, HEX);
    if (i < 7) SerialUSB.print(":");
    if (i != 3 && i != 4) {       // for 6-bytes MAC, skip first 2 bytes of EI
      mac[mac_index] = m;
      mac_index++;
    }
  }
  SerialUSB.println();
  SerialUSB.print("Extracted 6-byte MAC address: ");
  for (int u = 0; u < 6; u++) {
    SerialUSB.print(mac[u], HEX);
    if (u < 5) SerialUSB.print(":");
  }
  SerialUSB.println();
}

//////////////////////////// MAC ADDRESS /////////////////////////////////////////////
// the RTC has a MAC address stored in EEPROM
uint8_t readByte(uint8_t i2cAddr, uint8_t dataAddr) {
  Wire.beginTransmission(i2cAddr);
  Wire.write(dataAddr);
  Wire.endTransmission(false); // don't send stop
  Wire.requestFrom(i2cAddr, 1);
  return Wire.read();
}

///////////////////////// CHECK MODULE //////////////////////////////////////////////
boolean ethernetModule() {
  SD.begin(4);  // FRAM seems to work only after SD is initialised
  byte set_flag[] = {1};
  FRAMWrite(0, set_flag, 1);
  byte read_flag[1];
  FRAMRead(0, read_flag, 1);
  if (read_flag[0] == 1) return true;
  else return false;
}

///////////////////////// PRINT TIME ////////////////////////////////////////////////
void printTime() {
  SerialUSB.print("[" + String(millis() / 1000.0) + "] ");
}


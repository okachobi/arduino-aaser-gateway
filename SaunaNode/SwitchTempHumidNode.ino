//
// SwitchTempHumidNode
//
// An Relay Switch/Temperature/Humidity implementation written for jeenode+relay board and LowPowerLabs RFM12B library
// Run this sketch on jeenodes wired to a relay board to allow them to receive
// state changes from the gateway node (implemented via serial, jeenode usb, or jeenode jeelink)
// Note this should work with any arduino wired to an RFM12B, a DHT22 on Pin 4, and Relay switches on pins 5, and 15
//
// This implements version 1.0 of the Arduino Automation Serial protocol (AASER/1.0)
//

#include <Arduino.h>
#include <pins_arduino.h>
#include <stdlib.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <string.h>
#include <RFM12B.h>
#include <DHT.h>

#define DHTTYPE DHT22

enum {
  DEV_LIGHT_TEMP_HUMIDITY = 1
};

// Definitions for Jeenode ports
#define JNPORT1_D0       4
#define JNPORT1_D1       14
#define JNPORT1_A0       A0

#define JNPORT2_D0       5      // PWM Timer 0
#define JNPORT2_D1       15
#define JNPORT2_A0       A1

#define JNPORT3_D0       6      // PWM Timer 0
#define JNPORT3_D1       16
#define JNPORT3_A0       A2

#define JNPORT4_D0       7
#define JNPORT4_D1       17
#define JNPORT4_A0       A3

#define SERIAL_BAUD      115200  // Used only for initial configuration - nodes typically aren't tethered
#define NODEID           05      // Default node ID for this node - overriden by flash (use node command to change)
#define NETWORKID        50      // Network ID
#define FREQUENCY  RF12_433MHZ   // Match this with the version of your Moteino/Jeenode! (others: RF12_433MHZ, RF12_915MHZ)
#define KEY  "ABCDABCDABCDABCD"  // encryption key
#define ACK_TIME         200      // # of ms to wait for an ack
#define BUFSIZE          25      // Maximum command length (allocated from SRAM, so be conservative)
#define REPORT_TIME      3000    // How often to report status to the master
#define DEFAULT_DIGITAL_PIN1     JNPORT3_D0 // The default pin to use
#define DEFAULT_DIGITAL_PIN2     JNPORT3_D1 // The default pin to use
#define SAVE_DELAY       2000    // Save after 2 seconds

#define DEFAULT_DHT_DATA_PIN      JNPORT1_D0

#define POLL_DELAY      2000

#define FS(x) (__FlashStringHelper*)(x)

const char PROGMEM AASERHDR[]  = { "AASER/1.0 " };
uint8_t input[RF12_MAXDATA];
uint8_t input_len = 0;
RFM12B radio;
uint8_t node = NODEID;
uint8_t switch_pin[2];
uint8_t switch_state[2];
uint8_t master_node = 0;
long last_command = 0;
boolean settings_saved = true;

DHT *sensor;
float temperature = 0.0;
float humidity = 0.0;

void setup()
{
  switch_pin[0] = DEFAULT_DIGITAL_PIN1;
  switch_pin[1] = DEFAULT_DIGITAL_PIN2;
  switch_state[0] = switch_state[1] = 0;
  
  if( factoryResetRequired() ) resetFactoryDefaults();
 
  loadEEPROMSettings();
  
  randomSeed(analogRead(0));
  
  last_command = millis();
  
  radio.Initialize(node, FREQUENCY, NETWORKID, 0, 35); // 0 = high power, 0x08 = 38.4, 17 = 19.2 kbps, 35 = 9600
  radio.Encrypt((byte*)KEY);
  Serial.begin(SERIAL_BAUD);
  
  // Restore switch to state...
  pinMode( switch_pin[0], OUTPUT );
  pinMode( switch_pin[1], OUTPUT );
  digitalWrite( switch_pin[0], switch_state[0] );
  digitalWrite( switch_pin[1], switch_state[1] );
  
  sensor = new DHT(DEFAULT_DHT_DATA_PIN, DHTTYPE);
  sensor->begin();
  
  sendOK(Serial);
}

static void sendOK(Stream& port)
{
  char buff[5];
  port.print(FS(AASERHDR));
  port.print(F("200 Node "));
  port.print(node, DEC);
  port.print(F(" Listening at "));
  itoa( FREQUENCY == RF12_433MHZ ? 433 : FREQUENCY== RF12_868MHZ ? 868 : 915, buff, 10 );
  port.print(buff);
  port.println(F(" Mhz..."));  
}

static int F_strncmp(const char *s1, const __FlashStringHelper *s2, size_t len)
{
  const char PROGMEM *p = (const char PROGMEM *)s2;
  return strncmp_P( s1, p, len );
}

static void sendPkt( RFM12B& radio, Stream& port, byte node, char *pkt, byte size, boolean reqAck = true ) 
{
  const static byte retries = 5;
  byte cnt = 0;
  
  do {
    if(cnt >= retries) {
      // Node down?
      port.print(FS(AASERHDR));
      port.print(F("504 Unable to reach node "));
      port.println(node, DEC);
      return;
    }
    
    radio.Send(node, pkt, size, reqAck);
    
    // Randomize the delay so as to avoid repeated collisions
    if(cnt > 0) delay(random(10,50));
    cnt++;
    
  } while(reqAck && !waitForAck(node));
}

static int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

// Tokenize the input string, giving false if
// args are missing...
static bool getArgs(char **arg, byte cnt, char *delim ) {
    for(byte i=0; i < cnt; i++) {
      if( (arg[i] = strtok(NULL, delim)) == NULL) return false;
    }
    
    return true;
}

static void software_Reboot()
{
  wdt_enable(WDTO_15MS);
  while(1)
  {
  }
}

static void processRequest(Stream& port, char *cmd)
{
  // Format of Input: COMMAND ID ARG1,ARG2,ARG3,...
  char *space = " \n";
  char *comma = ",\n";
  char *args[5];
  byte node = 0;
  
  char *command = strtok(cmd, space);
  char *id = NULL;
  if( command != NULL ) {
    id = strtok(NULL, space);
  }

  if( command == NULL || (id != NULL && !isdigit(id[0])) ) {
    goto parse_error;
  }
  
  node = (id==NULL?0:atoi( id ));
  command = strupr( command );
  
  // Light controllers will store last LVL and COLOR
  if(F_strncmp(command, F("NODE"), 1) == 0) {
    
    // Set our node number 
    if(!getArgs(args, 1, space)) goto parse_error;
    
    node = atoi( args[0] );
    
    EEPROM.write(0, node);

    port.print(FS(AASERHDR));
    port.print(F("200 Node changed to "));
    port.print(node, DEC);
    port.println(" : Reset initiated");
    
    delay( 3000 );
    software_Reboot();
    
    return;
    
  } else if(F_strncmp(command, F("ON"), 2) == 0) {
    // <ID> ON <switch id: 0|1>
    // Turn on switch ID 0, or 1
    // if( !getArgs( args, 1, space) || !getArgs( &args[1], 3, comma ) ) goto parse_error;
    if(!getArgs(args, 1, space)) goto parse_error;
    
    // May want to actually check the value and error here instead of silently constraining
    uint8_t switch_idx = constrain( atoi( args[0] ) - 1, 0, 1);
    toggleSwitch(port, switch_idx, 1);
    return;    
  } else if(F_strncmp(command, F("OFF"), 2) == 0) {
    if(!getArgs(args, 1, space)) goto parse_error;
    
    // May want to actually check the value and error here instead of silently constraining
    uint8_t switch_idx = constrain( atoi( args[0] ) - 1, 0, 1);
    toggleSwitch(port, switch_idx, 0);
    return;
  } else if(F_strncmp(command, F("FACTORY"), 2) == 0) {
    port.print(FS(AASERHDR));
    port.println(F("200 Factory Reset initiated"));
    
    resetFactoryDefaults();
    delay( 3000 );
    software_Reboot();    
  } else if(F_strncmp(command, F("FLASH"), 1) == 0) {
    // Set the light in flash mode (with On/Off interval)
    return;    
  } else if(F_strncmp(command, F("LVL"), 1) == 0) {
    return;    
  } else if(F_strncmp(command, F("STATUS"), 2) == 0) {
    // Retrieve the status of the light
    return;    
  } else if(F_strncmp(command, F("PSCENE"), 1) == 0) {
    // Program a Scene setting (EEPROM)
    return;    
  } else if(F_strncmp(command, F("SCENE"), 1) == 0) {
    // Select a Scene setting (will turn on if off)
    return;    
  } else if(F_strncmp(command, F("MEM"), 1) == 0) {
    port.print( FS(AASERHDR) );
    port.print( F("200 ") );
    port.print( freeRam(), DEC );
    port.println( F(" Bytes Free Memory") );
    return;
  } else if(F_strncmp(command, F("PIN"), 2) == 0) {
    // Set the switch pin
    if( !getArgs( args, 1, space) || !getArgs( &args[1], 1, space ) ) goto parse_error;

    uint8_t switch_idx = constrain( atoi( args[0] ) - 1, 0, 1);
    switch_pin[switch_idx] = constrain( atoi( args[1] ), 3, 22 );
    EEPROM.write(1 + (2*switch_idx), 0); // set to off
    EEPROM.write(2 + (2*switch_idx), switch_pin[switch_idx]); // save pin
    delay( 3000 );
    software_Reboot();
   
  } 
  
  parse_error:
      port.print( FS(AASERHDR) );
      port.println( F("400 Invalid input") );
      return;
  
}

static void toggleSwitch(Stream& port, uint8_t index, uint8_t state)
{
    switch_state[index] = state;
    digitalWrite( switch_pin[index], switch_state[index] );
    invalidate();
    
    port.print(FS(AASERHDR));
    port.print(F("200 Switch "));
    port.print( index+1, DEC);
    port.print(F(" turned "));
    port.println( (switch_state[index] == 0)?F("Off"):F("On") );
}

static void checkInput(Stream& port)
{
  // Not safe for multiple ports right now...
  static char clientline[BUFSIZE+1] = "";
  static uint8_t index = 0;
  
  while (port.available()) {
        char c = port.read();

        //  fill url the buffer
        if(c != '\n' && c != '\r' && index < BUFSIZE){ 
          // Reads until either an eol character is reached or the buffer is full
          clientline[index++] = c;
          clientline[index] = 0;
          continue;
        }

        if(index != 0) {
          processRequest( port, clientline );
          index = 0;
        } else sendOK( port );
        break;
  }
  
}

void checkTempSensor(DHT *sensor)
{
  static long last = millis();
  
  if(millis() - last > POLL_DELAY) {
    float h = sensor->readHumidity();
    float t = sensor->readTemperature(false);
    if(!isnan(h) and !isnan(t)) {
      temperature = t;
      humidity = h;
      //Serial.print("Temperature: ");
      //Serial.print(t);
      //Serial.print(" Humidity: ");
      //Serial.println(h);
    } else {
      Serial.println("Could not read sensor");
    }
    last = millis();
  }
}

void loop()
{
  checkTempSensor(sensor);
  checkInput( Serial );
  
  if (radio.ReceiveComplete()) {
    
    if (radio.CRCPass()) {
      
      byte theNodeID = radio.GetSender();
      
      // Make packet copy here since ACK will erase buffers
      memcpy( (void*)input, (const void *)radio.Data, *radio.DataLen );
      input_len = *radio.DataLen;
            
      // Always ack immediately
      if (radio.ACKRequested()) {
        radio.SendACK();
        delay(10);
      }
      
      if( processNodePkt( radio, Serial, theNodeID, (char *) input, input_len ) ) {
        waitForAck(theNodeID);
        delay(5);
      }
      
    } else {
      // Just ignore bad packets....
      //Serial.print("BAD-CRC");
    }
  }
  
  //adjustLightLevel();
  attemptFlashStore();
}

// wait a few milliseconds for proper ACK to me, return true if indeed received
static bool waitForAck(byte theNodeID) {
  long now = millis();
  while (millis() - now <= ACK_TIME) {
    if (radio.ACKReceived(theNodeID))
      return true;
  }
  return false;
}

static void invalidate()
{
  last_command = millis();
  settings_saved = false;
}

// Save the current settings to flash to restore later
static void attemptFlashStore()
{
  if(settings_saved) return;
  if(millis() - last_command > SAVE_DELAY) {
	EEPROM.write(1, switch_state[0]);
  EEPROM.write(2, switch_pin[0]);
	EEPROM.write(3, switch_state[1]);
  EEPROM.write(4, switch_pin[1]);        
	settings_saved = true;
  }
  
}

//
// If we receive data, it gets handled here...
//
static bool processNodePkt( RFM12B& radio, Stream& port, byte sender, char *cmd, byte size )
{
  switch( cmd[0] ) {
    case 'S': // Status report
      if(cmd[1] == 'R') {
        char resp[12];
        
        union u_tag {
         byte b[4];
         float fval;
        } u;
        
        port.print( FS(AASERHDR) );
        port.print( F("200 Received status request from ") );
        port.println( sender, DEC );
      
        resp[0] = 'S';
        resp[1] = (char) DEV_LIGHT_TEMP_HUMIDITY;
        resp[2] = (char) switch_state[0];
        resp[3] = (char) switch_state[1];
        
        u.fval = temperature;
        
        resp[4] = (char) u.b[0];
        resp[5] = (char) u.b[1];
        resp[6] = (char) u.b[2];
        resp[7] = (char) u.b[3];
        
        u.fval = humidity;
        
        resp[8] = (char) u.b[0];
        resp[9] = (char) u.b[1];
        resp[10] = (char) u.b[2];
        resp[11] = (char) u.b[3];
        
        sendPkt( radio, port, sender, resp, 12, true);
        return true;
      }
      break;
    case 'c': // Change light level/color
    case 'o': // Switch light on/off
    {
      uint8_t idx = cmd[1]==0?0:1;
      uint8_t state = cmd[2]==0?0:1;
      toggleSwitch(port, idx, state);
    }
      break;
    case 'M': // Master broadcast
      port.print( FS(AASERHDR) );
      port.print( F("200 Detected master at ") );
      port.println( (byte) cmd[1], DEC );
      master_node = (byte) cmd[1];
      break;
    default:
      port.println( "Unknown?" );
      break;
  }
  
  return false;
}

static void loadEEPROMSettings()
{
  // EEPROM Map:
  // Addr	Use
  // =====  ===================================
  //	0   Node address
  //    1   Switch on/off state
  //    2   Switch digital pin
  //    3   Switch 2 on/off state
  //    4   Switch 2 digital pin
  //  5-8   On Timer (To be implemented)
  // 9-12   Off Timer (to be implemented)
  
  node = EEPROM.read(0);
  switch_state[0] = EEPROM.read(1);
  switch_pin[0] = EEPROM.read(2);
  switch_state[1] = EEPROM.read(3);
  switch_pin[1] = EEPROM.read(4);
}

static bool factoryResetRequired()
{
	byte magic[4];
	
	// The last 4 bytes of flash
	// will tell us if this board
	// has every been setup...this is a cheap
  // substitute for storing a crc checksum
	magic[3] = EEPROM.read(1020);
	magic[2] = EEPROM.read(1021);
	magic[1] = EEPROM.read(1022);
	magic[0] = EEPROM.read(1023);
	
	bool bVirgin = (magic[0] != 0xfa) || (magic[1] != 0xce) ||
				   (magic[2] != 0xfe) || (magic[3] != 0xed);
	
	return(bVirgin);
}

static void resetFactoryDefaults()
{	
	// Set node id back to default
	EEPROM.write(0, NODEID);

        // Switch states
  	EEPROM.write(1, 0); // Off
        EEPROM.write(2, DEFAULT_DIGITAL_PIN1); 
        
        EEPROM.write(3, 0); // off
        EEPROM.write(4, DEFAULT_DIGITAL_PIN2);
        
        // Switch Timers
        EEPROM.write(5, 0); // Timer on
        EEPROM.write(6, 0); // Timer on        
        EEPROM.write(7, 0); // Timer on        
        EEPROM.write(8, 0); // Timer on
        
        EEPROM.write(9, 0); // Timer off
        EEPROM.write(10, 0); // Timer off       
        EEPROM.write(11, 0); // Timer off       
        EEPROM.write(12, 0); // Timer off
        
	// Clear virgin DWORD (face feed)
	EEPROM.write(1020, 0xed);
	EEPROM.write(1021, 0xfe);
	EEPROM.write(1022, 0xce);
	EEPROM.write(1023, 0xfa);
				   
}



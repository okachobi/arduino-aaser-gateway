
//
// LEDNode
//
// An RGB light controller implementation written for jeenode LED and LowPowerLabs RFM12B library
// Run this sketch on jeenode LED Nodes wired to RGB LED/LED-Strips to allow them to receive
// state changes from the gateway node (implemented via serial, jeenode usb, or jeenode jeelink)
// Note this should work with any arduino wired to an RFM12B.
//
// This implements version 1.0 of the Arduino Automation Serial protocol (AASER/1.0)
// and adheres to BlinkM/cyzrgb i2c style command packets over RF. 
//


#include <stdlib.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <Wire.h>
#include <BlinkM_funcs.h>
#include <string.h>
#include <RFM12B.h>

#define SERIAL_BAUD      115200  // Used only for initial configuration - nodes typically aren't tethered
#define NODEID           9      // Default node ID for this node - overriden by flash (use node command to change)
#define NETWORKID        50      // Network ID
#define FREQUENCY  RF12_433MHZ   // Match this with the version of your Moteino/Jeenode! (others: RF12_433MHZ, RF12_915MHZ)
#define KEY  "ABCDABCDABCDABCD"  // encryption key
#define ACK_TIME         50      // # of ms to wait for an ack
#define BUFSIZE          25      // Maximum command length (allocated from SRAM, so be conservative)
#define REPORT_TIME      3000    // How often to report status to the master
#define RPIN             6       // Red Pin
#define GPIN             9       // Green Pin
#define BPIN             5       // Blue Pin
#define SAVE_DELAY       2000    // Save after 2 seconds

#define FS(x) (__FlashStringHelper*)(x)

const char AASERHDR[]  PROGMEM  = { "AASER/1.0 " };
uint8_t input[RF12_MAXDATA];
uint8_t input_len = 0;
RFM12B radio;
uint8_t node = NODEID;
uint8_t master_node = 0;
uint8_t verbosity = 0; // serial verbosity level
long last_command = 0;
boolean settings_saved = true;
byte tr = 0;
byte tg = 0;
byte tb = 0;

/* No longer have to fade manually...
byte cr = 0;
byte cg = 0;
byte cb = 0;
*/
byte blinkid = 0;
byte script = 254;

void setup()
{
  
  if( factoryResetRequired() ) resetFactoryDefaults();
 
  loadEEPROMSettings();

  delay ( 1000 ); // Allow BlinkM to boot
  
  pinMode( 9, OUTPUT );
  
  randomSeed(analogRead(0));
  
  last_command = millis();

  // This code only works with 1 i2c BlinkM device
  BlinkM_begin();
  blinkid = BlinkM_findFirstI2CDevice();
  
  if(tr != 0 || tg != 0 || tb != 0) {
    BlinkM_fadeToRGB(  blinkid, tr, tg, tb );
  } else if(script != 254) {
    BlinkM_playScript( blinkid, script, 0, 0 );
  }
  
  radio.Initialize(node, FREQUENCY, NETWORKID, 0, 35); // 0 = high power, 0x08 = 38.4, 17 = 19.2 kbps, 35 = 9600
  radio.Encrypt((byte*)KEY);
  Serial.begin(SERIAL_BAUD);
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

//
// Some of these commands won't make a lot of sense from a Node right now
// but the code was based on the same as the Gateway- some cleanup
// will need to be done...
//
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
    
  } else if(F_strncmp(command, F("ON"), 1) == 0) {
    // <ID> ON [<LVL>] [<R,G,B>]
    // Lvl = 0 to 100 (%) - 0 indicates no change to current level
    // R,G,B values range from 0 to 255 per color - optional, but must be a triplet
    // 10 ON 100 255,255,255 = All White
    if( !getArgs( args, 1, space) || !getArgs( &args[1], 3, command ) ) goto parse_error;
    
    float lvl = atof( args[0] ) / 100.0f;
    byte r = atof( args[1] ) * lvl;
    byte g = atof( args[2] ) * lvl;
    byte b = atof( args[3] ) * lvl;
    
    char buf[] = {'c', r, g, b };
    sendPkt( radio, port, node, buf, 4 );
    
    return;    
  } else if(F_strncmp(command, F("FLASH"), 1) == 0) {
    // Set the light in flash mode (with On/Off interval)
    return;
    
  } else if(F_strncmp(command, F("VERBOSITY"), 1) == 0) {
    // Change serial verbosity level
    if( !getArgs( args, 1, space) ) goto parse_error;
    verbosity = atoi(args[0]);
    EEPROM.write(5, verbosity);
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
  }
  
  parse_error:
      port.print( FS(AASERHDR) );
      port.println( F("400 Invalid input") );
      return;
  
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


void loop()
{
  // announce(radio, Serial);
  checkInput( Serial );
  
  if (radio.ReceiveComplete()) {
    
    if (radio.CRCPass()) {
      
      byte theNodeID = radio.GetSender();
      
      // Make packet copy here since ACK will erase buffers
      memcpy( (void*)input, (const void *)radio.Data, *radio.DataLen );
      input_len = *radio.DataLen;
      
      // Always ack immediately if requested
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
  
  // adjustLightLevel();
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
	EEPROM.write(1, tr);
        EEPROM.write(2, tg);
        EEPROM.write(3, tb);
        EEPROM.write(4, script);
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
      // Unimplemented
      break;
    case 'c': // Change light level/color
      // 'c', <r>, <g>, <b>
      tr = cmd[1];
      tg = cmd[2];
      tb = cmd[3];
      BlinkM_stopScript( blinkid );
      BlinkM_fadeToRGB(  blinkid, tr, tg, tb);
      script = 254;
      invalidate();
      break;
    case 'p': // Play a particular script
      // 'p', <scriptid>
      script = cmd[1];
      tr = tg = tb = 0;
      BlinkM_playScript( blinkid, script, 0, 0);
      // Save the settings in BlinkM case of power cycle
      BlinkM_setStartupParams( blinkid, 0x01, script, 0, 0x08, 0x00 );
      invalidate();
      break;
    case 'M': // Master broadcast
      if( (byte) cmd[1] != master_node || verbosity > 1) {
        port.print( FS(AASERHDR) );
        port.print( F("200 Detected master at ") );
        port.println( (byte) cmd[1], DEC );
        master_node = (byte) cmd[1];
      }
      break;
    default:
      port.print( "Unknown? : " );
      port.println( cmd[0], DEC );
      break;
  }
  
  return false;
}

static void loadEEPROMSettings()
{
  // EEPROM Map:
  // Addr	Use
  // =====  ===================================
  //	0	Node address

  node = EEPROM.read(0);
  tr = EEPROM.read(1);
  tg = EEPROM.read(2);
  tb = EEPROM.read(3);
  script = constrain( EEPROM.read(4), 0, 18 );
  verbosity = constrain( EEPROM.read(5), 0, 5 );
  
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

        // Light Levels
  	EEPROM.write(1, 0); // Red
        EEPROM.write(2, 0); // Green
        EEPROM.write(3, 0); // Blue
        
        EEPROM.write(4, 254); // Script
        
        EEPROM.write(5, 0); // Verbosity
        
	// Clear virgin DWORD (face feed)
	EEPROM.write(1020, 0xed);
	EEPROM.write(1021, 0xfe);
	EEPROM.write(1022, 0xce);
	EEPROM.write(1023, 0xfa);
				   
}

/*
static void doFlash()
{
    unsigned long now = millis();
    unsigned long useDelay = (flashState==0?offTime:onTime);
    
    if( now - lastChange > useDelay) {
        lastChange = now;
        flashState++;
        flashState %= 2;
        digitalWrite( LED, flashState );
    }
    
}
*/

/*
void adjustLightLevel()
{
unsigned long now = millis();

    if(tr != cr || tg != cg || tb != cb) {
        unsigned int step = round((((float) (now - lastColorChange)) / 3000.0f) * 255.0f);
        byte newr, newg, newb;
        
        if(step == 0) {
            return;
        }
        
        if(cr < tr) {
          newr = min( ((byte) cr) + step, (byte) tr);
        } else {
          newr = max( ((byte) cr) - step, (byte) tr);
        }

        if(cg < tg) {
          newg = min( ((byte) cg) + step, (byte) tg);
        } else {
          newg = max( ((byte) cg) - step, (byte) tg);
        }

        if(cb < tb) {
          newb = min( ((byte) cb) + step, (byte) tb);
        } else {
          newb = max( ((byte) cb) - step, (byte) tb);
        }
        
        cr = newr;
        cg = newg;
        cb = newb;
        
        lastColorChange = now;

        analogWrite( RPIN, cr);
        analogWrite( GPIN, cg);
        analogWrite( BPIN, cb);        
    }
    
}
*/

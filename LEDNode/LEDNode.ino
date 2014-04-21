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


#include <RFM12B.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <string.h>

#define SERIAL_BAUD      115200  // Used only for initial configuration - nodes typically aren't tethered
#define NODEID           02      // Default node ID for this node - overriden by flash (use node command to change)
#define NETWORKID        50      // Network ID
#define FREQUENCY  RF12_433MHZ   // Match this with the version of your Moteino/Jeenode! (others: RF12_433MHZ, RF12_915MHZ)
#define KEY  "ABCDABCDABCDABCD"  // encryption key
#define ACK_TIME         50      // # of ms to wait for an ack
#define BUFSIZE          25      // Maximum command length (allocated from SRAM, so be conservative)
#define REPORT_TIME      3000    // How often to report status to the master
#define RPIN             6       // Red Pin
#define GPIN             9       // Green Pin
#define BPIN             5       // Blue Pin

#define FS(x) (__FlashStringHelper*)(x)

const char AASERHDR[]  PROGMEM  = { "AASER/1.0 " };
uint8_t input[RF12_MAXDATA];
RFM12B radio;
uint8_t node = NODEID;
byte tr = 0;
byte tg = 0;
byte tb = 0;
byte cr = 0;
byte cg = 0;
byte cb = 0;

void setup()
{
  if( factoryResetRequired() ) resetFactoryDefaults();
 
  loadEEPROMSettings();
  
  randomSeed(analogRead(0));
  
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

static void processRequest(Stream& port, char *cmd)
{
  // Format of Input: ID COMMAND ARG1,ARG2,ARG3,...
  char *space = " ";
  char *comma = ",";
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
    
    port.print(FS(AASERHDR));
    port.print(F("200 Node changed to "));
    port.println(node, DEC);
    
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
      
      // Always ack immediately
      if (radio.ACKRequested()) {
        radio.SendACK();
        delay(10);
      }
      
      if( processNodePkt( radio, Serial, theNodeID, (char *) radio.Data, *radio.DataLen ) ) {
        waitForAck(theNodeID);
        delay(5);
      }
      
    } else {
      // Just ignore bad packets....
      //Serial.print("BAD-CRC");
    }
  }
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


//
// If we receive data, it gets handled here...
//
static bool processNodePkt( RFM12B& radio, Stream& port, byte sender, char *cmd, byte size )
{
  switch( cmd[0] ) {
    case 'S': // Status report
      break;
    case 'c': // Change light level/color
      tr = cmd[1];
      tg = cmd[2];
      tb = cmd[3];
      break;
      
    case 'M': // Master broadcast
      port.print( FS(AASERHDR) );
      port.print( F("200 Detected master at ") );
      port.println( (byte) cmd[1], DEC );
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
  //	0	Node address

  node = EEPROM.read(0);
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
        
	// Clear virgin DWORD (face feed)
	EEPROM.write(4092, 0xed);
	EEPROM.write(4093, 0xfe);
	EEPROM.write(4094, 0xce);
	EEPROM.write(4095, 0xfa);
				   
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

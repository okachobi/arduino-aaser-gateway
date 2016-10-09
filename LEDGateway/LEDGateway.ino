//
// LEDGateway
//
// RGB LED Gateway to pass commands from USB Serial to LEDSlaves over RFM12B
//
// This implements version 1.0 of the Arduino Automation Serial protocol (AASER/1.0)
// and adheres to BlinkM/cyzrgb i2c style command packets over RF. 
//
// Parts of this code are based on LowPowerLabs RFM12B Library examples
//

#include <RFM12B.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <string.h>

enum {
  DEV_LIGHT_TEMP_HUMIDITY = 1
};

#define SERIAL_BAUD 115200
#define NODEID            1         // Node ID used for this Gateway Unit (1 is standard)
#define NETWORKID        50         // Network ID
#define FREQUENCY  RF12_433MHZ      // Match this with the version of your Moteino! (others: RF12_433MHZ, RF12_915MHZ)
#define KEY  "ABCDABCDABCDABCD"     // encryption key
#define ACK_TIME         200         // # of ms to wait for an ack
#define RETRIES          5          // # of retries before giving up on a node
#define BUFSIZE          25         // Maximum command length (allocated from SRAM, so be conservative)
#define BLED             9          // LED for status
#define ANNOUNCE_TIME    3000       // How often in ms to announce the master to the nodes

#define FS(x) (__FlashStringHelper*)(x)

const char AASERHDR[]  PROGMEM  = { "AASER/1.0 " };
uint8_t input[RF12_MAXDATA];
uint8_t input_len = 0;
RFM12B radio;

static void sendOK(Stream& port)
{
  char buff[5];
  port.print(FS(AASERHDR));
  port.print(F("200 Gateway Listening at "));
  itoa( FREQUENCY == RF12_433MHZ ? 433 : FREQUENCY== RF12_868MHZ ? 868 : 915, buff, 10 );
  port.print(buff);
  port.println(F(" Mhz..."));
}

void setup()
{
  randomSeed(analogRead(0));
  pinMode(BLED, OUTPUT);
  radio.Initialize(NODEID, FREQUENCY, NETWORKID, 0, 35); // 0 = high power, 0x08 = 38.4, 17 = 19.2 kbps, 35 = 9600
  radio.Encrypt((byte*)KEY);
  Serial.begin(SERIAL_BAUD);
  sendOK(Serial);
}

static int F_strncmp(const char *s1, const __FlashStringHelper *s2, size_t len)
{
  const char PROGMEM *p = (const char PROGMEM *)s2;
  return strncmp_P( s1, p, len );
}

static void sendPkt( RFM12B& radio, Stream& port, byte node, char *pkt, byte size, boolean reqAck = true ) 
{
  const static byte retries = RETRIES;
  byte cnt = 0;
  
  do {
    if(cnt >= retries) {
      // Node down?
      port.print(FS(AASERHDR));
      port.print(F("504 Unable to reach node "));
      port.println( node, DEC);
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
  // Format of Input: COMMAND ID ARG1[,ARG2,ARG3,...]
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
  if(F_strncmp(command, F("OFF"), 2) == 0) {
    
    // Turn the light OFF
    char buf[] = {'c',0,0,0};
    sendPkt( radio, port, node, buf, 4 );
    sendOK( port );
    return;
    
  } else if(F_strncmp(command, F("ON"), 1) == 0) {
    // ON <ID> [<LVL>] [<R,G,B>]
    // Lvl = 0 to 100 (%) - 0 indicates no change to current level
    // R,G,B values range from 0 to 255 per color - optional, but must be a triplet
    // ON 10 100 255,255,255 = All White
    if( !getArgs( args, 1, space) || !getArgs( &args[1], 3, comma ) ) goto parse_error;
    
    // WIP - do we support persistent level?  Need to be done
    // at node, not gateway unless gateway tracks it...
    //uint_8 newlvl = atoi( args[0] )
    //float lvl = 1.0;
    //if(newlvl != 0) {}
    
    float lvl = atof( args[0] ) / 100.0f;
    byte r = atoi( args[1] ) * lvl;
    byte g = atoi( args[2] ) * lvl;
    byte b = atoi( args[3] ) * lvl;
    
    char buf[] = {'c', r, g, b };
    sendPkt( radio, port, node, buf, 4 );
    sendOK( port );    
    return;    
  } else if(F_strncmp(command, F("FLASH"), 1) == 0) {
    // Set the light in flash mode (with On/Off interval)
    return;    
  } else if(F_strncmp(command, F("LVL"), 1) == 0) {
    return;    
  } else if(F_strncmp(command, F("STATUS"), 2) == 0) {
    // Retrieve the status of the light
    char buf[] = {'S','R'}; // status request
    sendPkt( radio, port, node, buf, 2 );
    sendOK( port );
    return;    
  } else if(F_strncmp(command, F("PSCENE"), 1) == 0) {
    // Program a Scene setting (EEPROM)
    return;    
  } else if(F_strncmp(command, F("SCENE"), 1) == 0) {
    // Select a Scene setting (will turn on if off)
    if( !getArgs( args, 1, space) ) goto parse_error;
    
    byte scene = atoi( args[0] );
    
    char buf[] = {'p', (char) scene }; // status request
    sendPkt( radio, port, node, buf, 2 );
    sendOK( port );
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
  announce(radio, Serial);
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

static void announce( RFM12B& radio, Stream& port )
{
  static long lastAnnounce = 0;
  
  long now = millis();
  if( now - lastAnnounce > ANNOUNCE_TIME ) {
    announceMaster( radio, port );
    lastAnnounce = now;
  }
}

//
// Tell all slave nodes that we are the master periodically
//

static void announceMaster( RFM12B& radio, Stream& port )
{
  char buf[] = {'M',NODEID};
  sendPkt( radio, port, 0, buf, 2, false );
}

//
// If we receive data, it gets handled here...
//
static bool processNodePkt( RFM12B& radio, Stream& port, byte sender, char *cmd, byte size )
{
  switch( cmd[0] ) {
    case 'S': // Status report
        // When we receive a status report, we should store the status!? Or just print it?
        if(cmd[1] != 'R') {
          port.print( FS(AASERHDR) );
          byte dev_type = (byte) cmd[1];
          switch( dev_type ) {
            case DEV_LIGHT_TEMP_HUMIDITY:
              {
                union u_tag {
                 byte b[4];
                 float fval;
                } u;
                byte switch1 = (byte) cmd[2];
                byte switch2 = (byte) cmd[3];
                
                u.b[0] = (byte) cmd[4];
                u.b[1] = (byte) cmd[5];
                u.b[2] = (byte) cmd[6];
                u.b[3] = (byte) cmd[7];
                float temperature = u.fval;

                u.b[0] = (byte) cmd[8];
                u.b[1] = (byte) cmd[9];
                u.b[2] = (byte) cmd[10];
                u.b[3] = (byte) cmd[11];
                float humidity = u.fval;
                
                port.print( F("200 STATUS "));
                port.print( sender, DEC);
                port.print( F(" ") );
                port.print( dev_type, DEC);
                port.print( F(" SW1=") );
                port.print( switch1, DEC );
                port.print( F(",SW2=") );
                port.print( switch2, DEC );
                port.print( F(",T=") );
                port.print( temperature );
                port.print( F(",H=") );
                port.println( humidity );
              }
              break;
             default:
               port.println( F("404 Device Type not found") );
             break;
          }
       }
      break;
    case 'M': // Master broadcast
      port.print( FS(AASERHDR) );
      port.print( F("502 Detected duplicate master at ") );
      port.println( (byte) cmd[1], DEC );
      break;
    default:
      break;
  }
  
  return false;
}

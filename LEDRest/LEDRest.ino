//
// LEDRest
//
// A TCP/IP ethernet JSON REST service for turning an arduino into a non-rgb LED Controller
// the service will register with DHCP and respond to http://lights.local/
// This can control light strips with a simply breakout board witha n-type mosfet and power supply
// This has successfully been integrated with OpenRemote as a sensor and device
//
// Supported methods include:
// 	/status 			- return on/off status
// 	/ 				- return brightness
// 	/flash/<on_time>/<off_time>	- flash the lights with a specific on/off interval
// 	/<byte> 			- set level (0 to 255)
// 	/on 				- turn on
// 	/off 				- turn off
//

#include <SyncLED.h>
#include <Bounce.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>

#define DEBUG false
#define STATICIP false
//  url buffer size
#define BUFSIZE 80
// Toggle case sensitivity
#define CASESENSE true
#define BUTTON 5
#define LED 3

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

#if STATICIP
byte ip[] = {192,168,0,11};
#endif

#if defined(ARDUINO) && ARDUINO >= 100
EthernetServer server(80);
#else
Server server(80);
#endif

byte lightLevel = 0; // OFF
byte currentLevel = 0;
unsigned long lastChange = millis();
byte flashMode = 0;
unsigned long onTime = 1000;
unsigned long offTime = 1000;
byte flashState = 0;

void setup() {
#if DEBUG
  //  turn on serial (for debuggin)
  Serial.begin(9600);
#endif

  // start the Ethernet connection and the server:
#if STATICIP
  Ethernet.begin(mac, ip);
#else
  if (Ethernet.begin(mac) == 0) {
#if DEBUG
    Serial.println("Unable to set server IP address using DHCP");
#endif
    for(;;)
      ;
  }
#if DEBUG
  // report the dhcp IP address:
  Serial.println(Ethernet.localIP());
#endif
#endif

  server.begin();
  
  EthernetBonjour.begin("lights");
  
  pinMode(BUTTON,INPUT);
  pinMode(LED,OUTPUT);
  analogWrite( LED, 0 );
}

void loop() {
  // needed to continue Bonjour/Zeroconf name registration
  EthernetBonjour.run();
  
  if(flashMode) {
    doFlash();
  } else {
    adjustLightLevel();
  }
  
  char clientline[BUFSIZE];
  int index = 0;
  // listen for incoming clients
#if defined(ARDUINO) && ARDUINO >= 100
  EthernetClient client = server.available();
#else
  Client client = server.available();
#endif

  if (client) {

    //  reset input buffer
    index = 0;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        //  fill url the buffer
        if(c != '\n' && c != '\r' && index < BUFSIZE){ // Reads until either an eol character is reached or the buffer is full
          clientline[index++] = c;
          continue;
        }  

        processRequest( client, clientline );
        
        break;
      }
    } // while connected

    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    client.stop();
    while(client.status() != 0){
      delay(5);
    }
    
  } // if client
 
 
}

void doFlash()
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

void adjustLightLevel()
{
unsigned long now = millis();

    if(lightLevel != currentLevel) {
        unsigned int step = round((((float) (now - lastChange)) / 3000.0f) * 255.0f);
        unsigned int newlevel;
        
        if(step == 0) {
            delay(10);
            return;
        }
        
        if(currentLevel < lightLevel) {
            newlevel = min( ((int) currentLevel) + step, (int) lightLevel);
        } else {
            newlevel = max( ((int)currentLevel) - step, (int) lightLevel);
        }
        currentLevel = (byte) newlevel;
        lastChange = now;
        analogWrite( LED, currentLevel);
        delay(10);
    }
    
}

void statusOK( Client &client )
{
        client.println(F("HTTP/1.1 200 OK"));
        client.println(F("Content-Type: text/html"));
        client.println(F("Access-Control-Allow-Origin: *"));
        client.println();  
}

void processRequest( Client &client, char *clientline )
{
        // Url client line looks something like:
        // GET /255          - Max Brightness
        // GET /ON           - Max Brightness
        // GET /OFF          - Off
        // GET /             - Get current level 0-255
        // GET /FLASH/90/90  - Flash 90 ms on/90 ms off
        // GET /STATUS       - Return Status (ON or OFF)
        
        String urlString = String(clientline);

        // String op = urlString.substring(0,urlString.indexOf(' '));
        urlString = urlString.substring(urlString.indexOf('/'), urlString.indexOf(' ', urlString.indexOf('/')));

#if CASESENSE
        urlString.toUpperCase();
#endif
        urlString.toCharArray(clientline, BUFSIZE);

        //  get the first two parameters
        char *arg1 = strtok(clientline,"/");
        char *arg2 = strtok(NULL,"/");
        char *arg3 = NULL;
        if(arg2 != NULL) arg3 = strtok(NULL, "/");
        String jsonOut = String();

        if(arg1 != NULL) {
            byte level = min(atoi(arg1), 255);

            if(!isdigit(arg1[0])) {
                if(strncmp(arg1, "ON", 2) == 0) {
                    flashMode = 0;
                    lightLevel = 255;
                } else if(strncmp(arg1, "OFF", 3) == 0) {
                    flashMode = 0;                
                    lightLevel = 0;
                } else if(strncmp(arg1, "FLASH", 5) == 0) {
                    if( arg2 != NULL) {
                        flashMode = 1;
                        onTime = atoi( arg2 );
                        if(arg3 != NULL) {
                            offTime = atoi( arg3 );
                        } else {
                            offTime = onTime;
                        }
                    }
                } else if(strncmp(arg1, "STATUS", 6) == 0) {
                  //  return the current value
                  jsonOut += "{\"state\":\"";
                  if( currentLevel > 0 ) {
                    jsonOut += "on";
                  } else {
                    jsonOut += "off";
                  }
                  jsonOut += "\"}";
                  statusOK( client );
                  
                  client.println(jsonOut);
                  goto jret;
                } else {
                  client.println(F("HTTP/1.1 404 Not Found"));
                  client.println(F("Content-Type: text/html"));
                  client.println();
                  goto jret;
                }
            } else {
                   flashMode = 0;
                   lightLevel = level;            
            }
            lastChange = millis();
            statusOK(client);            
            
            jret:
            ;
        } else {
            //  return the current value
            jsonOut += "{\"value\":\"";
            jsonOut += currentLevel;
            jsonOut += "\"}";

            //  return value with wildcarded Cross-origin policy
            statusOK( client );

            client.println(jsonOut);
        }
                    
}





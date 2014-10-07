#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>

#define DEBUG 0
#define STATICIP 1
//  url buffer size
#define BUFSIZE 40
// Toggle case sensitivity
#define CASESENSE true

#define AC_PIN 2

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0xAD};

#if STATICIP
byte ip[] = {192,168,0,72};
// the router's gateway address:
byte gateway[] = { 192, 168, 0, 1 };
// the subnet:
byte subnet[] = { 255, 255, 255, 0 };
#endif

#define LISTEN_PORT           80
#define COMPRESSOR_RESET_TIME  (5 * 60000) // 5 minutes should suffice
#define AUTO_TIMEOUT    (15 * 60000)       // 15 minutes is the default run period if non requested

// Please note that if you are wiring into your AC unit, some (most?) units have built in compressor
// protection to prevent a thermostat from retriggering before the compressor has safely recharged
// However, it is possible that no such protection exists, and if wiring alongside your commercial
// Thermostat then you may find them competing for control and triggering a start cycle right after
// a stop.  This code has no sensing at this time to detect when the AC is started by another means,
// so USE AT YOUR OWN RISK!  You can damage your AC unit.

#if defined(ARDUINO) && ARDUINO >= 100
EthernetServer server(LISTEN_PORT);
#else
Server server(LISTEN_PORT);
#endif

// Is the AC on? 0 or 1
byte currentLevel = 0;
// When was the AC last on? Assume on reset that it wasn't...(maybe bad)
long lastOn = millis() - COMPRESSOR_RESET_TIME;
// How long to leave the AC on in milliseconds
long onTime = 0;

char clientline[BUFSIZE];

void setup() {
  pinMode( 10, OUTPUT );
  pinMode( 4, OUTPUT );
  digitalWrite( 10, HIGH);
  digitalWrite( 4, LOW);
  
  Serial.begin(115200);
  pinMode( AC_PIN, OUTPUT );
  Serial.print(F("Free RAM: ")); 
  Serial.println(getFreeRam(), DEC);
  
  /* Initialise the module */
  Serial.println(F("\nInitializing..."));

  // start the Ethernet connection and the server:
#if STATICIP
  Ethernet.begin(mac, ip, gateway, subnet);
#else
  if (Ethernet.begin(mac) == 0) {
#if DEBUG
    Serial.println(F("Unable to set server IP address using DHCP"));
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
  
  EthernetBonjour.begin("ac");

  Serial.println(F("Listening for connections..."));
    Serial.println(Ethernet.localIP());

}

void loop() {
  // needed to continue Bonjour/Zeroconf name registration
  EthernetBonjour.run();
  
  // needed to continue Bonjour/Zeroconf name registration
  // mdns.update();
  autoOff(millis());
  byte index = 0;
  byte line = 0;
  byte crlf = 0;
  
  // listen for incoming clients
#if defined(ARDUINO) && ARDUINO >= 100
  EthernetClient client = server.available();
#else
  Client client = server.available();
#endif

  if (client) {

    //  reset input buffer
    index = 0;
    crlf = 0;
    line = 1;
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        //  fill url the buffer
        if(c != '\n' && c != '\r') {
         if( line == 1 && (index-1) < BUFSIZE) {
          clientline[index++] = c;
          //continue;
         }
         // if not on line 1, then we discard the characters, but reset the crlf counter
         crlf = 0;
        } else if( c == '\n' ) {
          crlf++;
          line++;
        }

        if(crlf == 2) {
          clientline[index] = 0;
          processRequest( client, clientline );
          crlf = 0;
          line = 0;
          break;
        }
      }
    } // while connected

    // give the web browser time to receive the data
    delay(30);

    // close the connection: - or maybe just let the client be reaped on its own???
    client.stop();
    while(client.status() != 0){
      delay(5);
    }
    
  } // if client
 
 
}

void autoOff(long now)
{
  if( currentLevel > 0) {
    if( now - lastOn >= onTime ) {
      digitalWrite( AC_PIN, LOW );
      lastOn = now;
      onTime = 0;
      currentLevel = 0;
    }
  }
  
}

void statusOK( EthernetClient &client )
{
        client.println(F("HTTP/1.1 200 OK"));
        client.println(F("Content-Type: text/html"));
        client.println(F("Access-Control-Allow-Origin: *"));
        client.println();  
}

void processRequest( EthernetClient &client, char *clientline )
{
        // Url client line looks something like:
        // GET /ON           - AC is turned on
        // GET /OFF          - AC is turned off
        // GET /STATUS       - Return Status (ON or OFF) - note some numbers quoted for use in OpenRemote
        //    { "state" : "<on|off>", "remaining" : "<remaining run time in secs>", 
        //      "cmp" : "<compressor protection reset time remaining>", "mem" : <memory remaining> }
        
        String urlString = String(clientline);

        byte start = urlString.indexOf('/');
        byte finish = urlString.indexOf(' ', start);
        urlString = urlString.substring(start, finish);

        long now = millis();
        
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
                   // Only turn on if the compressor was allowed to reset
                   if(now - lastOn >= COMPRESSOR_RESET_TIME) {
                     digitalWrite( AC_PIN, HIGH );
                     currentLevel = 1;
                     lastOn = now;
                     // Turn off automatically...
                     if( arg2 != NULL) {
                       onTime = atol( arg2 );
                     } else {
                       onTime = AUTO_TIMEOUT;
                     }
                   }
                } else if(strncmp(arg1, "OFF", 3) == 0) {
                   digitalWrite( AC_PIN, LOW );
                   lastOn = now;
                   onTime = 0;
                   currentLevel = 0;
                } else if(strncmp(arg1, "STATUS", 6) == 0) {
                  //  return the current value
                  jsonOut += "{\"state\":\"";
                  if( currentLevel > 0 ) {
                    jsonOut += "on";
                  } else {
                    jsonOut += "off";
                  }
                  jsonOut += "\",\"remaining\":\"";
                  if(currentLevel > 0 ) {
                    jsonOut += (onTime - (now - lastOn) ) / 1000;
                  } else {
                    jsonOut += 0;
                  }
                  jsonOut += "\",\"cmp\":\"";
                  if(currentLevel == 0 ) {
                    jsonOut += ( (now - lastOn) >= COMPRESSOR_RESET_TIME?0:(COMPRESSOR_RESET_TIME-(now - lastOn)) / 1000);
                  } else {
                    jsonOut += 0;
                  }                  
                  jsonOut += "\", \"mem\":";
                  jsonOut += getFreeRam();
                  jsonOut += "}";
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
            }

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

int getFreeRam(void)
{
  extern int  __bss_end;
  extern int  *__brkval;
  int free_memory;
  if((int)__brkval == 0) {
    free_memory = ((int)&free_memory) - ((int)&__bss_end);
  }
  else {
    free_memory = ((int)&free_memory) - ((int)__brkval);
  }

  return free_memory;
}



#!/usr/bin/env python
import web
import time
import serial

# Urls we will handle
urls = (
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/color/([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])/([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])/([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])', 'set_color',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/sceneid/([0-1]?[0-8])', 'set_sceneid',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/scene/(user|rgb|flash|blink|redflash|greenflash|blueflash|cyanflash|magentaflash|yellowflash|black|off|huecycle|moodlight|candle|water|neon|seasons|thunderstorm=|storm|stoplight|sos)', 'set_scene',
)

# Examples
# /lightgw/<Address>/color/<red>/<green>/<blue>
# /lightgw/<Address>/sceneid/<scene id>
# /lightgw/<Address>/sceneid/<scene name>
# /lightgw/<Address>/scene/<scene name>
# To Be implemented....
# /lightgw/<Address>/on/<level>/<red>/<green>/<blue>
# /lightgw/<Address>/level/<level>
# /lightgw/<Address>/off
# /lightgw/<Address>/flash/<on time>/<off time>
# /lightgw/<Address>/flashcolor/<on time>/<off time>/<red>/<green>/<blue>
#
# where Address is jeenode address 0 - 127
#       red/green/blue is a value between 0 and 255
#       scene id is between 0 and 18

scenes = dict( user=0, rgb=1, flash=2, blink=2, redflash=3, greenflash=4, blueflash=5, cyanflash=6, magentaflash=7,
    yellowflash=8, black=9, off=9, huecycle=10, moodlight=11, candle=12, water=13, neon=14, seasons=15, 
    thunderstorm=16, storm=16, stoplight=17, sos=18)

# configure the serial connections (the parameters differs on the device you are connecting to)
ser = serial.Serial( port='/dev/tty.usbserial-AD02ES1G', baudrate=115200, timeout=1 )

app = web.application(urls, globals())

def consumeWaiting():
    while ser.inWaiting() > 0:
        ser.readline()

class set_color:        
    def GET(self,address,r,g,b):
        output = '{ "status": "ok" }'
        print "Channel = {0} Color=({1},{2},{3})".format(address, r, g, b)
        ser.write( "ON {0} 100 {1},{2},{3}\n".format(address, r, g, b) )
        time.sleep( 0.5 )
        consumeWaiting()
        return output

class set_sceneid:
    def GET(self, address, sceneid):
        output = '{ "status": "ok" }'
        print "Channel = {0} SceneID={1}".format(address, sceneid)
        ser.write( "SCENE {0} {1}\n".format(address, sceneid) )
        time.sleep( 0.5 )
        consumeWaiting()
        return output

class set_scene:
    def GET(self, address,scenename):
        output = '{ "status": "ok" }'
        print "Channel = {0} SceneID={1}".format(address, scenename)
        ser.write( "SCENE {0} {1}\n".format(address, scenes[ scenename ]) )
        time.sleep( 0.5 )
        consumeWaiting()
        return output

def main():
    print('lightgw service is starting...')
    # ser.open()
    
    # Wakeup the JeeLink
    ser.write('\n')
    time.sleep(0.500)
    consumeWaiting()

    app.run()

if __name__ == '__main__':
    main()

#!/usr/bin/env python
import web
import time
import serial
from collections import deque

# Urls we will handle
urls = (
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/color/([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])/([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])/([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])', 'set_color',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/sceneid/([0-1]?[0-8])', 'set_sceneid',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/scene/(user|rgb|flash|blink|redflash|greenflash|blueflash|cyanflash|magentaflash|yellowflash|black|off|huecycle|moodlight|candle|water|neon|seasons|thunderstorm=|storm|stoplight|sos)', 'set_scene',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/on/([0-1])', 'light_on',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/off/([0-1])', 'light_off',
    '/lightgw/(0?[0-9]?[0-9]|1[01][0-9]|12[0-7])/status', 'get_status',
)


# Examples
# /lightgw/<Address>/color/<red>/<green>/<blue>
# /lightgw/<Address>/sceneid/<scene id>
# /lightgw/<Address>/sceneid/<scene name>
# /lightgw/<Address>/scene/<scene name>
# /lightgw/<Address>/on/<switch 0|1>
# /lightgw/<Address>/status
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
messages = deque([])

# configure the serial connections (the parameters differs on the device you are connecting to)
#ser = serial.Serial( port='/dev/tty.usbserial-AD02ES1G', baudrate=115200, timeout=1 )
ser = serial.Serial( port='/dev/ttyUSB0', baudrate=115200, timeout=1 )

app = web.application(urls, globals())

def enqueWaiting():
    while ser.inWaiting() > 0:
        serstring = ''
        serstring = ser.readline()
        if(not(len(serstring)==0)):
            messages.append(serstring)

def consumeWaiting():
    while ser.inWaiting() > 0:
        ser.readline()

def process_status(msg):
    if "AASER/1.0 200 STATUS" in msg:
        i = msg.find("STATUS")
	i = i + 7
	tokens = msg[i:].split()
        # 0 = sender
        # 1 = devtype
        # 2 = command-delim status tokens
	status = dict(u.split('=') for u in tokens[2].split(","))
        output = '{{ "status": "ok", "sender": {0}, "devtype": {1}'.format(tokens[0],tokens[1])
        for k,v in status.iteritems():
            output+=', "{0}": {1} '.format(k,v)
        output += "}"
        return output
    return None

response_handlers = {
    'AASER/1.0 200 STATUS': process_status
}

def processMessage(msg):
    for k,v in response_handlers.iteritems():
        if k in msg:
            resp = v(msg)
            if resp:
                return resp
    return None

def processQueue():
    enqueWaiting()
    while len(messages) > 0:
        msg = messages.popleft()
        output = processMessage(msg)

def processSelectQueue(match):
    enqueWaiting()
    for i in messages:
        if match in i:
            messages.remove(i)
            return response_handlers[match](i)
    return None

class set_color:
    def GET(self,address,r,g,b):
        output = '{ "status": "ok" }'
        print "Channel = {0} Color=({1},{2},{3})".format(address, r, g, b)
        ser.write( "ON {0} 100 {1},{2},{3}\n".format(address, r, g, b) )
        time.sleep( 0.5 )
        consumeWaiting()
        return output

class light_on:
    def GET(self, address, switchid):
        output = '{ "status": "ok" }'
        print "Channel = {0} Switch={1}".format(address, switchid)
        ser.write( "ON {0} 100 {1},1,1\n".format(address, switchid) )
        time.sleep( 0.5 )
        consumeWaiting()
        return output

class light_off:
    def GET(self, address, switchid):
        output = '{ "status": "ok" }'
        print "Channel = {0} Switch={1}".format(address, switchid)
        ser.write( "ON {0} 100 {1},0,0\n".format(address, switchid) )
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

class get_status:
    def GET(self, address):
        total_retry = 0
        while True:
            output = None
            print "Status Channel = {0}".format(address)
            ser.write( "STATUS {0}\n".format(address) )
            time.sleep( 0.5 )
            retry = 0
            while True:
                lastread = None
                while True:
                    lastread = processSelectQueue("AASER/1.0 200 STATUS")
                    if not lastread:
                        break
                    output = lastread;
                if output or retry > 5:
                    break
                time.sleep( 0.5 )
                retry+=1
            if output or retry > 3:
                break
            time.sleep( 2.0 )
            total_retry+=1
            consumeWaiting()
        return output or '{ "status": "error" }'

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

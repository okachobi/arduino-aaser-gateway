SaunaNode
=========

This arduino firmware implements a AASER microservice for reading a temperature/humidity sensor with the [S]tatus command and allow for switching of 2 switches via the ON/OFF commands. It is designed to integrate with the [LEDServer](https://github.com/okachobi/arduino-aaser-gateway/tree/master/LEDServer) and a JeeNode-JeeLink on the USB port running the [LEDGateway firmware](https://github.com/okachobi/arduino-aaser-gateway/tree/master/LEDGateway)

The service reports temperature and humidity from the server when the status command is issued, while also allowing toggling of 2 relay switches to control lights or the sauna itself (if you are brave...all safety disclaimers apply). It was specifically created to integrate with the [Huginn automation scripts](https://github.com/okachobi/arduino-aaser-gateway/tree/master/Huginn) for notifying when a Sauna reaches a usable temperature, and also works with my Amazon Echo for controlling the Sauna light via voice utilizing the [amazon-echo-ha-bridge](https://github.com/armzilla/amazon-echo-ha-bridge)

This firmware requires the [Adafruit DHT22 library](https://github.com/adafruit/DHT-sensor-library)

TL;DR
-----------
Get an email and blink some lights when the sauna gets to a temperature you like.

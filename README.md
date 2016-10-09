# arduino-aaser-gateway

The code here is part of a home project I started some years ago to web enable various devices in my house without spending a lot of money on commercial gateways.

The platform requirements are a USB JeeNode Jeelink (or clone) and one or mode Jeednodes.  A Jeednode is an arduino compatible device that generally includes a wireless RFM12B radio that can broadcast over the 433Mhz spectrum.  Its a $6 radio that can send low rate data across your whole home.

The AASER protocol is one I created for communication between these devices and resembles a packetized HTTP protocol.  The LEDServer is actually a general purpose Python server that implements HTTP microservices to communicate over the 433Mhz AASER network. 

It is primary designed for LEDs and switches, but I've extended to the control Air Conditioning and also read temperature sensors.  The protocol is fairly extensible and can cover any number of devices.

The code was written with little effort to make it commercial grade.  In other words, its a bit hacky.  I wrote this quicky in my spare time and didn't spend a lot of time refining things.  Security on the server is mostly non-existent, though the communication with devices is encrypted.

I'm happy to take improvements, or fork if you like and make it better.

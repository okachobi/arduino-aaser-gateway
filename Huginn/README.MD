AASER Huginn Examples
=====================
The AASER gateway is designed with home automation in mind.  [Huginn](https://github.com/cantino/huginn) is a great IFTTT-like tool in Ruby for doing data-driven and event-based automation.  These files may be imported into Huginn to define scenarios that can be customized for your own uses. 

notify-me-when-sauna-heats-up.json
----------------------------------
This json file is intended to be imported into Huginn- an IFTTT-like tool for doing your own automation.  It reads a custom arduino sensor I made for temperature and switch control that works with the AASER protocol and lightgw.py.  It calls other services on the AASER network to change lights, but also texts me.  As configured, it expects the status command on the sensor to report a line similar to:

```{ "status": "ok", "sender": 5, "devtype": 1, "SW1": 0 , "H": 45.20 , "SW2": 0 , "T": 22.00 }```


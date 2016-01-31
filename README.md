esp8266-mqtt-nexa-bridge
------------------------

A simple MQTT to NEXA bridge using the Esperssif ESP8266 SoC and a 433MHz AM module. Currently only the transmit function is implemented.

The ESP8266 GPIO_2 shall be connected to the data pin of the transmitter module.  

The MQTT message should be formatted as the following JSON example:
```JSON
{ 
    "id": 123456,
    "group": 0,
    "onoff": 1,
    "channel": 0,
    "repeat": 0
}
```
Or for a dimmer
```JSON
{
	"id": 123456
	"group": 0,
	"onoff": 0,
	"dim": 0,
	"channel": 0,
	"repeat": 5
}
```
Where 
- id, 26 bit (0 - 67108863) unique identifier code, also known as "home code" in decimal format
- group, 1 bit (0 - 1) indicateing if it's a group command or not
- onoff, 1 bit (0 - 1), on or off
- channel, 4 bit (0 - 15) channel code

MQTT and WIFI settings are configured in the user_config.h file  


Implementation based on protcol information from these sites  
http://elektronikforumet.com/wiki/index.php/RF_Protokoll_-_Nexa_sj%C3%A4lvl%C3%A4rande  
http://elektronikforumet.com/wiki/index.php/RF_Protokoll_-_JULA-Anslut  
http://playground.arduino.cc/Code/HomeEasy  


SDK for ESP8266, https://github.com/pfalcon/esp-open-sdk  
JSON Parser, https://github.com/zserge/jsmn  
MQTT client from Tuan PM, https://github.com/tuanpmt/esp_mqtt  

433MHz AM Transmiter module, http://www.velleman.eu/products/view/?country=fr&lang=en&id=350619  
Espressif ESP8266 SoC, http://espressif.com/en/products/esp8266/  
NEXA, http://www.nexa.se/  

/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "jsmn.h"

MQTT_Client mqttClient;
ETSTimer bitTimer;

uint32_t symbolTime = 250;
bool nexaTxBusy = false;
bool nexaTxStart = false;
uint8_t nexaRawFrame[43] = {0};
uint32_t nexaRawFrameLength = 0;
uint32_t nexaRawFrameCounter = 0;
uint8_t nexaRawFrameRepeatCounter = 0;

typedef enum {
    FRC1_SOURCE = 0,
    NMI_SOURCE = 1,
} FRC1_TIMER_SOURCE_TYPE;

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status){
	if(status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args){
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Connected\n");
	MQTT_Subscribe(client, "/89548efd-c75f-4128-9629-095e8b51f885/nexabridge/send", 0);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args){
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\n");
}

void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args){
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\n");
}

bool ICACHE_FLASH_ATTR testBit32( int A[],  int k ){
	return (bool)( (A[k/32] & (1 << (k%32) )) != 0 ) ;     
}

bool testBit8(uint8_t A[],  int k ){
	return (bool)( (A[k/8] & (1 << (k%8) )) != 0 ) ;     
}

bool ICACHE_FLASH_ATTR jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	return (bool)(tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start && strncmpi(json + tok->start, s, tok->end - tok->start) == 0);
}

void ICACHE_FLASH_ATTR createNexaFrame(int version, int id, int group, int onOff, int channel, int dim, int repeat){
	uint8_t header[2] = {0x00, 0x04};
	uint8_t zeroBit = 0xA0;
	uint8_t oneBit = 0x82;
	uint8_t trailer[5] = {0x00, 0x00, 0x00, 0x00, 0x80};

	uint32_t i;
	int f = 0;

	if(!nexaTxBusy){
		if(version == 1){
			id = (id >= 0 && id <= 0x3FFFFFF) ? id : 0;
			group = (group >= 0 && group <= 1) ? group : 0;
			onOff = (onOff >= 0 && onOff <= 1) ? onOff : 0;
			channel = (channel >= 0 && channel <= 16) ? channel : 0;
			dim = (dim >= 0 && dim <= 16) ? dim : 0;
			repeat = (repeat >= 0 && repeat <= 256) ? repeat : 0;

			f = id & 0xFFFFFFC0;
			f |= ((group & 1) << 5);
			f |= ((onOff & 1) << 4);
			f |= ((~channel) & 15);

			INFO("NEXA word: %X\n", f);

			os_memcpy(nexaRawFrame, trailer, 5);

			for(i=0;i<32;i++){
				nexaRawFrame[i+5] = (testBit32(&f, i)) ? oneBit : zeroBit;
			}

			os_memcpy(nexaRawFrame+i+5, header, 2);

			nexaRawFrameLength = (2 + 32 + 5) * 8;
			nexaRawFrameCounter = 0;
			nexaRawFrameRepeatCounter = (uint8_t) repeat;

// 			INFO("NEXA RAW Frame repeat: %d\n", nexaRawFrameRepeatCounter);
// 
// 			INFO("NEXA RAW Frame: \n");
// 			for(i=0; i < 39;i++){
// 				INFO("%X", nexaRawFrame[38-i]);
// 			}
// 
// 			INFO("\n");
// 
// 			for(i=0; i < nexaRawFrameLength;i++){
// 				INFO("%d", (testBit8(nexaRawFrame, nexaRawFrameLength-1-i)) ? 1 : 0);
// 			}
// 
// 			INFO("\n");
			nexaTxBusy = true;
			nexaTxStart = true;
		}
	}
	else{
		INFO("NEXA Transmitter busy!");
	}
}

void ICACHE_FLASH_ATTR mqttParseMessage(const char* topic, uint32_t topic_len, const char *data, uint32_t data_len){
	int i;
	int r;
	jsmn_parser p;
	jsmntok_t t[15];

	bool versionAssigned = false;
	bool idAssigned = false;
	bool groupAssigned = false;
	bool onffAssigned = false;
	bool channelAssigned = false;
	bool dimAssigned = false;
	bool repeatAssigned = false;
	int version, id, group, onoff, channel, dim = 0, repeat = 0;

	if(strcmpi(topic, "/89548efd-c75f-4128-9629-095e8b51f885/nexabridge/send") == 0)	{
		jsmn_init(&p);
		r = jsmn_parse(&p, data, data_len, t, sizeof(t)/sizeof(t[0]));

		if (r < 0) {
			INFO("Failed to parse JSON: %d\n", r);
			return;
		}

		if (r < 1 || t[0].type != JSMN_OBJECT) {
			INFO("Object expected\n");
			return;
		}

		if(r < 11 || r > 15){
			INFO("Missing token(s): %d\n", r);
			return;
		}

		for (i = 1; i < r; i++) {
			if (jsoneq(data, &t[i], "version")) {
				version = strtol(data + t[i+1].start, NULL, 10);
				versionAssigned = true;
				INFO("- Version: %d\n", version);
				i++;
			} else if (jsoneq(data, &t[i], "id")) {
				id = strtol(data + t[i+1].start, NULL, 10);
				idAssigned = true;
				INFO("- Id: %d\n", id);
				i++;
			} else if (jsoneq(data, &t[i], "group")) {
				group = strtol(data + t[i+1].start, NULL, 10);				
				groupAssigned = true;
				INFO("- Group: %d\n", group);
				i++;
			} else if (jsoneq(data, &t[i], "onoff")) {
				onoff = strtol(data + t[i+1].start, NULL, 10);
				onffAssigned = true;
				INFO("- OnOff: %d\n", onoff);
				i++;
			} else if (jsoneq(data, &t[i], "channel")) {
				channel = strtol(data + t[i+1].start, NULL, 10);
				channelAssigned = true;
				INFO("- Channel: %d\n", channel);
				i++;
			} else if (jsoneq(data, &t[i], "dim")) {
				dim = strtol(data + t[i+1].start, NULL, 10);
				dimAssigned = true;
				INFO("- Dim: %d\n", dim);
				i++;
			} else if (jsoneq(data, &t[i], "repeat")) {
				repeat = strtol(data + t[i+1].start, NULL, 10);
				repeatAssigned = true;
				INFO("- Repeat: %d\n", repeat);
				i++;
			} else {
				INFO("Unexpected key\n");
			}	
		}

		if(versionAssigned && idAssigned && groupAssigned && onffAssigned && channelAssigned){
			INFO("All parameters assigned\n");

			createNexaFrame(version, id, group, onoff, channel, dim, repeat);
		}
	}
	else{
		INFO("Unknown topic\n");
	}
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len){
	char *topicBuf = (char*)os_zalloc(topic_len+1);
	char *dataBuf = (char*)os_zalloc(data_len+1);

	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	INFO("Receive topic: %s, data: %s\n", topicBuf, dataBuf);

	mqttParseMessage(topicBuf, topic_len, dataBuf, data_len);

	os_free(topicBuf);
	os_free(dataBuf);
}

void symbolTimerCb(void){
	if((nexaRawFrameCounter < nexaRawFrameLength) && nexaTxStart){
		if(testBit8(nexaRawFrame, nexaRawFrameLength-nexaRawFrameCounter-1))	{
			gpio_output_set(BIT2, 0, BIT2, 0);
		}
		else{
			gpio_output_set(0, BIT2, BIT2, 0);
		}	
		nexaRawFrameCounter++;
	}
	else if((nexaRawFrameRepeatCounter > 0) && nexaTxStart){
		nexaRawFrameCounter = 0;
		nexaRawFrameRepeatCounter--;
	}
	else{
		gpio_output_set(0, BIT2, BIT2, 0);
		nexaTxStart = false;
		nexaTxBusy = false;
	}
}

void ICACHE_FLASH_ATTR GPIO_Setup(void){
    gpio_init();
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	gpio_output_set(0, BIT2, BIT2, 0);
}

void ICACHE_FLASH_ATTR Timers_Setup(void){
 	hw_timer_init(FRC1_SOURCE, 1);
 	hw_timer_set_func(symbolTimerCb);
 	hw_timer_arm(symbolTime);
}

void ICACHE_FLASH_ATTR user_init(void){
	system_timer_reinit();

	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	os_delay_us(1000000);

	INFO("SDK version: %s\n", system_get_sdk_version());
	INFO("System init ...\n");

	//CFG_Load();

	GPIO_Setup();

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
	MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);

	Timers_Setup();

	INFO("\nSystem started ...\n");
}

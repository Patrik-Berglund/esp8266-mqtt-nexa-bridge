/*
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

MQTT_Client mqttClient;

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
	MQTT_Subscribe(client, MQTT_TOPIC_NEXA_BRIDGE_SEND, 0);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args){
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\n");
}

void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args){
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\n");
}

void ICACHE_FLASH_ATTR mqttParseMessage(const char* topic, uint32_t topic_len, const char *data, uint32_t data_len){
	if(strcmpi(topic, MQTT_TOPIC_NEXA_BRIDGE_SEND) == 0){
        parseNexaSendMessage(data, data_len);
	}
	else{
		INFO("MQTT: Unknown topic\n");
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

	INFO("MQTT: Receive topic: %s, data: %s\n", topicBuf, dataBuf);

	mqttParseMessage(topicBuf, topic_len, dataBuf, data_len);

	os_free(topicBuf);
	os_free(dataBuf);
}

void ICACHE_FLASH_ATTR parseNexaSendMessage(const char *data, uint32_t data_len){
    uint32_t i;
	int32_t r;
	jsmn_parser p;
	jsmntok_t t[15];

	bool versionAssigned = false;
	bool idAssigned = false;
	bool groupAssigned = false;
	bool onffAssigned = false;
	bool channelAssigned = false;
	bool dimAssigned = false;
	bool repeatAssigned = false;
	int32_t version = 1, id, group, onoff, channel, dim = 0, repeat = 4;

    jsmn_init(&p);
    r = jsmn_parse(&p, data, data_len, t, sizeof(t)/sizeof(t[0]));

    if (r < 0) {
        INFO("NEXA: JSON, Parser error: %d\n", r);
        return;
    }

    if (r < 1 || t[0].type != JSMN_OBJECT) {
        INFO("NEXA: JSON, Object expected\n");
        return;
    }

    if((r != 9) && (r != 11) && (r != 13) && (r != 15)){
        INFO("NEXA: JSON, Missing token(s): %d\n", r);
        return;
    }

    INFO("NEXA: JSON payload \n");

    for (i = 1; i < r; i++) {
        if (jsonEq(data, &t[i], "version")) {
            version = strtol(data + t[i+1].start, NULL, 10);
            versionAssigned = true;
            INFO("\tVersion: %d\n", version);
            i++;
        } else if (jsonEq(data, &t[i], "id")) {
            id = strtol(data + t[i+1].start, NULL, 10);
            idAssigned = true;
            INFO("\tId: %d\n", id);
            i++;
        } else if (jsonEq(data, &t[i], "group")) {
            group = strtol(data + t[i+1].start, NULL, 10);				
            groupAssigned = true;
            INFO("\tGroup: %d\n", group);
            i++;
        } else if (jsonEq(data, &t[i], "onoff")) {
            onoff = strtol(data + t[i+1].start, NULL, 10);
            onffAssigned = true;
            INFO("\tOnOff: %d\n", onoff);
            i++;
        } else if (jsonEq(data, &t[i], "channel")) {
            channel = strtol(data + t[i+1].start, NULL, 10);
            channelAssigned = true;
            INFO("\tChannel: %d\n", channel);
            i++;
        } else if (jsonEq(data, &t[i], "dim")) {
            dim = strtol(data + t[i+1].start, NULL, 10);
            dimAssigned = true;
            INFO("\tDim: %d\n", dim);
            i++;
        } else if (jsonEq(data, &t[i], "repeat")) {
            repeat = strtol(data + t[i+1].start, NULL, 10);
            repeatAssigned = true;
            INFO("\tRepeat: %d\n", repeat);
            i++;
        } else {
            INFO("\tUnexpected key\n");
        }	
    }

    if(idAssigned && groupAssigned && onffAssigned && channelAssigned){
        INFO("NEXA: JSON, All parameters assigned for on/off breaker\n");
        createNexaFrame(version, id, group, onoff, channel, -1, repeat);
    }else if(idAssigned && groupAssigned && channelAssigned && dimAssigned){
        INFO("NEXA: JSON, All parameters assigned for dimmer \n");
        createNexaFrame(version, id, group, 0, channel, dim, repeat);
    }
}

void ICACHE_FLASH_ATTR createNexaFrame(int32_t version, int32_t id, int32_t group, int32_t onOff, int32_t channel, int32_t dim, int32_t repeat){
	uint8_t header[2] = {0x00, 0x04};
	uint8_t zeroBit = 0xA0;
	uint8_t oneBit = 0x82;
	uint8_t trailer[5] = {0x00, 0x00, 0x00, 0x00, 0x80};

  	uint32_t i;
	uint64_t f = 0;
    uint8_t fLength = 0;    

	if(!nexaTxBusy){
		if(version == 1){        
			os_memcpy(nexaRawFrame, trailer, 5);
            fLength+=5;

			f = (id & 0x3FFFFFF) << 6;
			f |= ((group & 1) << 5);
			f |= ((onOff & 1) << 4);
			f |= ((~channel) & 15);            
            fLength+=32;

            if(dim >= 0){
                f <<=4;
                f |= (dim & 15);
                fLength+=4;
            }
            
            INFO("NEXA WORD: %08x %08x\n", *(((uint32_t*)(&f))+1), f);
            
			for(i=5;i<fLength;i++){
				nexaRawFrame[i] = (testBit64(&f, i-5)) ? oneBit : zeroBit;
			}           

			os_memcpy(nexaRawFrame+fLength, header, 2);
            fLength+=2;

			nexaRawFrameLength = fLength * 8;
			nexaRawFrameCounter = 0;
			nexaRawFrameRepeatCounter = (uint8_t) repeat;
            
            INFO("NEXA: RAW Frame length: %u \n", nexaRawFrameLength);
			INFO("NEXA: RAW Frame [HEX]: \n");
			for(i=0; i < fLength;i++){
				INFO("%02X ", nexaRawFrame[fLength-1-i]);
			}
			INFO("\n");

            if(dim >= 0){
                nexaRawFrame[13] = 0xA0;                
                for(i=13;i > 0;i--){                    
                    nexaRawFrame[i] &= 0xF0; 
                    nexaRawFrame[i] |= ((nexaRawFrame[i-1] & 0xF0) >> 4);
                    nexaRawFrame[i-1] <<= 4;
                }
                
                INFO("NEXA: After dim, RAW Frame [HEX]: \n");
                for(i=0; i < fLength;i++){
                    INFO("%02X ", nexaRawFrame[fLength-1-i]);
                }
                INFO("\n");
            }

            INFO("NEXA: RAW Frame [BIN]: \n");
			for(i=0; i < nexaRawFrameLength;i++){
				INFO("%d", (testBit8(nexaRawFrame, nexaRawFrameLength-1-i)) ? 1 : 0);
			}
			INFO("\n");
            
			nexaTxBusy = true;
			nexaTxStart = true;
        }
        
	}
	else{
		INFO("NEXA: Error, Transmitter busy!");
	}
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

bool testBit8(uint8_t A[],  int32_t k ){
	return (bool)( (A[k/8] & ((uint8_t)1 << (k%8) )) != 0 );     
}

bool ICACHE_FLASH_ATTR testBit32( uint32_t A[],  int32_t k ){
	return (bool)( (A[k/32] & ((uint32_t)1 << (k%32) )) != 0 );     
}

bool ICACHE_FLASH_ATTR testBit64( uint64_t A[],  int32_t k ){
	return (bool)( (A[k/64] & ((uint64_t)1 << (k%64) )) != 0 );     
}

bool ICACHE_FLASH_ATTR jsonEq(const char *json, jsmntok_t *tok, const char *s) {
	return (bool)(tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start && strncmpi(json + tok->start, s, tok->end - tok->start) == 0);
}

void ICACHE_FLASH_ATTR gpioSetup(void){
    gpio_init();
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	gpio_output_set(0, BIT2, BIT2, 0);
}

void ICACHE_FLASH_ATTR timerSetup(void){
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

	CFG_Load();

	gpioSetup();

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
	MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);

	timerSetup();

	INFO("\nSystem started ...\n");
}
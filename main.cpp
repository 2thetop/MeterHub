// ----------------------------------------------------------------------------
// Copyright 2016-2018 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------
#ifndef MBED_TEST_MODE

#include "mbed.h"
#include "rtos.h"
#include "simple-mbed-cloud-client.h"
#include "FATFileSystem.h"
#include "LittleFileSystem.h"

#include "platform/CircularBuffer.h"


#define UART1_BUF_SIZE    512
#define UART2_BUF_SIZE    512
#define UART3_BUF_SIZE    512


// Default network interface object. Don't forget to change the WiFi SSID/password in mbed_app.json if you're using WiFi.
NetworkInterface *net;

// Default block device available on the target board
BlockDevice* bd = BlockDevice::get_default_instance();
SlicingBlockDevice sd(bd, 0, 2*1024*1024);

//#if COMPONENT_SD || COMPONENT_NUSD
//// Use FATFileSystem for SD card type blockdevices
//FATFileSystem fs("fs");
//#else
// Use LittleFileSystem for non-SD block devices to enable wear leveling and other functions
LittleFileSystem fs("fs");
//#endif

#if USE_BUTTON == 1
InterruptIn button(BUTTON1);
#endif /* USE_BUTTON */

// Default LED to use for PUT/POST example
DigitalOut led(LED1, 0);
DigitalOut led2(LED2, 0);

// Declaring pointers for access to Pelion Device Management Client resources outside of main()
MbedCloudClientResource *button_res;
MbedCloudClientResource *led_res;
MbedCloudClientResource *post_res;


MbedCloudClientResource *seoul_water_meter_res;
MbedCloudClientResource *power_meter_res;
MbedCloudClientResource *gas_meter_res;
MbedCloudClientResource *water_meter_res;
MbedCloudClientResource *hot_water_meter_res;
MbedCloudClientResource *heat_meter_res;

// An event queue is a very useful structure to debounce information between contexts (e.g. ISR and normal threads)
// This is great because things such as network operations are illegal in ISR, so updating a resource in a button's fall() function is not allowed
EventQueue eventQueue;

#if 1
Thread threadSeoulWaterMeter;
Thread threadPowerMeter;
Thread threadOtherMeters;

EventFlags uart1_Flags;
EventFlags uart2_Flags;
EventFlags uart3_Flags;

CircularBuffer<char, UART1_BUF_SIZE> bufUart1;
CircularBuffer<char, UART2_BUF_SIZE> bufUart2;
CircularBuffer<char, UART3_BUF_SIZE> bufUart3;



RawSerial uart1SeoulWaterMater(PC_1, PC_0);    // 1200 BPS
RawSerial uart2OtherMater(PA_2, PA_3);         // 4800 BPS
RawSerial uart3PowerMeter(PC_4, PC_5);         // 9600 BPS
#endif


static int button_press_count = 0;

void request_OtherMeters(uint8_t meterType);
void request_SeoulWaterMeter();

void request_WaterMeter();
void request_HotWaterMeter();
void request_GasMeter();
void request_HeatMeter();

// When the device is registered, this variable will be used to access various useful information, like device ID etc.
static const ConnectorClientEndpointInfo* endpointInfo;

/**
 * PUT handler - sets the value of the built-in LED
 * @param resource The resource that triggered the callback
 * @param newValue Updated value for the resource
 */
void put_callback(MbedCloudClientResource *resource, m2m::String newValue) {
    printf("PUT received. New value: %s\n", newValue.c_str());
    led = atoi(newValue.c_str());
}

/**
 * POST handler - prints the content of the payload
 * @param resource The resource that triggered the callback
 * @param buffer If a body was passed to the POST function, this contains the data.
 *               Note that the buffer is deallocated after leaving this function, so copy it if you need it longer.
 * @param size Size of the body
 */
void post_callback(MbedCloudClientResource *resource, const uint8_t *buffer, uint16_t size) {
    printf("POST received (length %u). Payload: ", size);
    for (size_t ix = 0; ix < size; ix++) {
        printf("%02x ", buffer[ix]);
    }
    printf("\n");
}

/**
 * Button handler
 * This function will be triggered either by a physical button press or by a ticker every 5 seconds (see below)
 */
void button_press() {
    int v = button_res->get_value_int() + 1;
    button_res->set_value(v);
    printf("Button clicked %d times\n", v);

    if(0 == button_press_count) {
        request_SeoulWaterMeter();
    }
    else if(1 == button_press_count) {
        request_WaterMeter();
    }
     else if(2 == button_press_count) {
        request_HotWaterMeter();
    }
     else if(3 == button_press_count) {
        request_GasMeter();
    }
     else if(4 == button_press_count) {
        request_HeatMeter();
    }
#if 0  
    else if(5 == button_press_count) {
        request_PowerMeter();
    }
    button_press_count++;
    button_press_count %= 6;
#else
    button_press_count++;
    button_press_count %= 5;
#endif

}

/**
 * Notification callback handler
 * @param resource The resource that triggered the callback
 * @param status The delivery status of the notification
 */
void button_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Button notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}

/**
 * Registration callback handler
 * @param endpoint Information about the registered endpoint such as the name (so you can find it back in portal)
 */
void registered(const ConnectorClientEndpointInfo *endpoint) {
    printf("Registered to Pelion Device Management. Endpoint Name: %s\n", endpoint->internal_endpoint_name.c_str());
    endpointInfo = endpoint;
}

void seoul_water_meter_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Seoul-Water-Meter notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}

void power_meter_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Power-Meter notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}

void gas_meter_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Gas-Meter notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}

void water_meter_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Water-Meter notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}
void hot_water_meter_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Hot-Water-Meter notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}

void heat_meter_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Heat-Water-Meter notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}


#if 1

enum
{
	PSTEC_REQUEST_STX                                                     = 0xC0,
	PSTEC_REQUEST_ETX                                                     = 0xD0,
	PSTEC_RESPONSE_STX                                                    = PSTEC_REQUEST_STX,
	PSTEC_RESPONSE_ETX                                                    = PSTEC_REQUEST_ETX,
	PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_WATER                              = 0x02,
	PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HOT_WATER                          = 0x03,
	PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_GAS                                = 0x04,
	PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HEAT                               = 0x05,
	PSTEC_MAGIC_CODE_1ST_BYTE                                             = 0x22,
	PSTEC_MAGIC_CODE_2ND_BYTE                                             = 0x69,
	PSTEC_MAGIC_CODE_3RD_BYTE                                             = 0x6E,
	PSTEC_MAGIC_CODE_4TH_BYTE                                             = 0x73,
	PSTEC_REQUEST_PACKET_LENGTH                                           = 0x04,  // 4 bytes
	PSTEC_RESPONSE_PACKET_LENGTH_NORMAL_ACCUM                             = 0x0B,  // 11 bytes
	PSTEC_RESPONSE_PACKET_APDU_LENGTH_NORMAL_ACCUM                        = 0x03,  //  3 bytes

	SEOUL_REQUEST_STX           = 0x10,
	SEOUL_REQUEST_ETX           = 0x16,
	SEOUL_RESPONSE_STX          = 0x68,
	SEOUL_RESPONSE_ETX          = SEOUL_REQUEST_ETX,
	SEOUL_REQUEST_PACKET_LENGTH = 0x05  // 4 bytes
};

typedef enum
{
	INVALID_PACKET_STATE = 0,

	PSTEC_PACKET_TX_STX = 90,
	PSTEC_PACKET_TX_ID,
	PSTEC_PACKET_TX_BCC,
	PSTEC_PACKET_TX_ETX,
	PSTEC_PACKET_RX_STX,
	PSTEC_PACKET_RX_MAGIC_CODE_1ST_BYTE,
	PSTEC_PACKET_RX_MAGIC_CODE_2ND_BYTE,
	PSTEC_PACKET_RX_MAGIC_CODE_3RD_BYTE,
	PSTEC_PACKET_RX_MAGIC_CODE_4TH_BYTE,
	PSTEC_PACKET_RX_ID,
	PSTEC_PACKET_RX_DATA,
	PSTEC_PACKET_RX_BCC,
	PSTEC_PACKET_RX_ETX,

	SEOUL_PACKET_TX_STX = 120,
	SEOUL_PACKET_TX_C_FIELD,
	SEOUL_PACKET_TX_A_FIELD,
	SEOUL_PACKET_TX_CHECKSUM,
	SEOUL_PACKET_TX_ETX,
	SEOUL_PACKET_RX_1ST_STX,
	SEOUL_PACKET_RX_1ST_L_FIELD,
	SEOUL_PACKET_RX_2ND_L_FIELD,
	SEOUL_PACKET_RX_2ND_STX,
	SEOUL_PACKET_RX_C_FIELD,
	SEOUL_PACKET_RX_A_FIELD,
	SEOUL_PACKET_RX_CI_FIELD,
	SEOUL_PACKET_RX_DATA,
	SEOUL_PACKET_RX_CHECKSUM,
	SEOUL_PACKET_RX_ETX
} PacketState;

enum
{
   OTHER_DEVICE      = 0x00,

   ELECTRICITY_METER = 0x02,
   GAS_METER         = 0x03,
   HEAT_METER        = 0x04,
   STEAM_METER       = 0x05,
   WARM_WATER_METER  = 0x06,
   WATER_METER       = 0x07,
   HOT_WATER_METER   = 0x15,
   COLD_WATER_METER  = 0x16,
   UNKNOWN_DEVICE    = 0x0F,

   LED_LIGHTING      = 0x40,
   LINK_TEST         = 0xFF
};

static PacketState  dplcPacketState = INVALID_PACKET_STATE;
static PacketState seoulPacketState = INVALID_PACKET_STATE;

static char dplcPacketMeterType    = 0;
static char dplcPacketApduLength   = 0;
static char dplcPacketRcvdBytes    = 0;
static char dplcPacketChecksum     = 0;

static char seoulPacketLFieldValue = 0;
static char seoulPacketUserDataLen = 0;
static char seoulPacketRcvdBytes   = 0;
static char seoulPacketChecksum    = 0;


void request_SeoulWaterMeter() {
    uint8_t bufRequestCommand[6];
    uint8_t cmdLen = 0;

    bufRequestCommand[cmdLen++] = SEOUL_REQUEST_STX;
    bufRequestCommand[cmdLen++] = 0x5B;
    bufRequestCommand[cmdLen++] = 0x01;
    bufRequestCommand[cmdLen++] = (uint8_t)(bufRequestCommand[1] + bufRequestCommand[2]);   // checksum
    bufRequestCommand[cmdLen++] = SEOUL_REQUEST_ETX;
    bufRequestCommand[cmdLen]   = 0x00;

    while (true) {
        if (uart1SeoulWaterMater.writeable()) {
            uart1SeoulWaterMater.printf((const char*)bufRequestCommand);
            break;
        }
    }
}

// 56 34 12 00
void makeBcdToInt(int &nValue, char *pBuffer, int nCount) {
    if ((NULL == pBuffer) || (nCount <= 0)) {
        nValue = 0;
        return;
    }

    int index = nCount - 1;
    nValue = 0;
    do {
        char ch = *(pBuffer + index);

        nValue *= 10;
        nValue += ((ch & 0xF0) >> 4);
        nValue *= 10;
        nValue += (ch & 0x0F);

        index--;
    }
    while(0 <= index);
}

void makeReverseBcdToInt(int &nValue, char *pBuffer, int nCount) {
    if ((NULL == pBuffer) || (nCount <= 0)) {
        nValue = 0;
        return;
    }

    int index = 0;
    nValue = 0;
    do {
        char ch = *(pBuffer + index);

        nValue *= 10;
        nValue += ((ch & 0xF0) >> 4);
        nValue *= 10;
        nValue += (ch & 0x0F);

        index++;
    }
    while(index < nCount);
}

void resetDplcResponseState(void)
{
	dplcPacketState      = PSTEC_PACKET_TX_STX;
	dplcPacketMeterType  = UNKNOWN_DEVICE;
	dplcPacketApduLength = 0;
	dplcPacketRcvdBytes  = 0;
	dplcPacketChecksum   = 0;
}

void resetSeoulResponseState(void) {
	seoulPacketState       = SEOUL_PACKET_RX_1ST_STX;
	seoulPacketLFieldValue = 0;
	seoulPacketUserDataLen = 0;
	seoulPacketRcvdBytes   = 0;
	seoulPacketChecksum    = 0;
}

void threadUart1_SeoulWaterMeter() {
	char buffer[22];

    printf("### threadUart1 - 1\n");

    while(true) {
        //printf("### threadUart1 - 4\n");

        int nCount = bufUart1.size();
         //printf("#### threadUart1 - Buffer Count : %d\n", nCount);

        if (21 <= (nCount + seoulPacketRcvdBytes)) {
            printf("#### threadUart1 - Buffer Count : %d\n", nCount);
            char ch = 0;
			for (uint8_t i = 0; i < nCount; i++) {
				bufUart1.pop(ch);

				switch (seoulPacketState)
				{
					case SEOUL_PACKET_RX_1ST_STX:
                        //printf("#### threadUart1 - 1. SEOUL_RESPONSE_STX 0\n");
						if (ch == SEOUL_RESPONSE_STX) {
							seoulPacketState = SEOUL_PACKET_RX_1ST_L_FIELD;
							buffer[seoulPacketRcvdBytes++] = ch;
                             //printf("#### threadUart1 - 1. SEOUL_RESPONSE_STX 1\n");
						}
						break;

					case SEOUL_PACKET_RX_1ST_L_FIELD:
                        //printf("#### threadUart1 - 2. SEOUL_PACKET_RX_1ST_L_FIELD 0\n");
						seoulPacketState       = SEOUL_PACKET_RX_2ND_L_FIELD;
						seoulPacketLFieldValue = ch;
						buffer[seoulPacketRcvdBytes++] = ch;
						break;

					case SEOUL_PACKET_RX_2ND_L_FIELD:
                        //printf("#### threadUart1 - 3. SEOUL_PACKET_RX_2ND_L_FIELD 0\n");
						if (ch == seoulPacketLFieldValue) {
							seoulPacketState = SEOUL_PACKET_RX_2ND_STX;
							buffer[seoulPacketRcvdBytes++] = ch;
                            //printf("#### threadUart1 - 3. SEOUL_PACKET_RX_2ND_L_FIELD 1\n");
						}
						else {
							resetSeoulResponseState();
                            printf("#### threadUart1 - 3. SEOUL_PACKET_RX_2ND_L_FIELD resetSeoulResponseState\n");
						}
						break;

					case SEOUL_PACKET_RX_2ND_STX:
                        //printf("#### threadUart1 - 4. SEOUL_PACKET_RX_2ND_STX 0\n");
						if (ch == SEOUL_RESPONSE_STX) {
							seoulPacketState = SEOUL_PACKET_RX_C_FIELD;
							buffer[seoulPacketRcvdBytes++] = ch;
                            //printf("#### threadUart1 - 4. SEOUL_PACKET_RX_2ND_STX 1\n");
						}
						break;

					case SEOUL_PACKET_RX_C_FIELD:
                        //printf("#### threadUart1 - 5. SEOUL_PACKET_RX_C_FIELD 0\n");
						if (ch < 0x80) {
							seoulPacketState    = SEOUL_PACKET_RX_A_FIELD;
							seoulPacketChecksum = ch;
							buffer[seoulPacketRcvdBytes++] = ch;
                            //printf("#### threadUart1 - 5. SEOUL_PACKET_RX_C_FIELD 1\n");
						}
						else {
							resetSeoulResponseState();
                            printf("#### threadUart1 - 5. SEOUL_PACKET_RX_C_FIELD resetSeoulResponseState\n");
						}
						break;

					case SEOUL_PACKET_RX_A_FIELD:
                        //printf("#### threadUart1 - 6. SEOUL_PACKET_RX_A_FIELD 0\n");
						seoulPacketState     = SEOUL_PACKET_RX_CI_FIELD;
						seoulPacketChecksum += ch;
						buffer[seoulPacketRcvdBytes++] = ch;
						break;

					case SEOUL_PACKET_RX_CI_FIELD:
                        //printf("#### threadUart1 - 7. SEOUL_PACKET_RX_CI_FIELD 0\n");
						seoulPacketState     = SEOUL_PACKET_RX_DATA;
						seoulPacketChecksum += ch;
						buffer[seoulPacketRcvdBytes++] = ch;
						break;

					case SEOUL_PACKET_RX_DATA:
                        //printf("#### threadUart1 - 8. SEOUL_PACKET_RX_DATA 0\n");
						seoulPacketUserDataLen++;
						seoulPacketChecksum += ch;
						buffer[seoulPacketRcvdBytes++] = ch;
                        // 3 = C (1 byte) + A (1 byte) + CI (1 byte)
						if ((seoulPacketUserDataLen + 3) > seoulPacketLFieldValue) {
							resetSeoulResponseState();
                            printf("#### threadUart1 - 8. SEOUL_PACKET_RX_DATA resetSeoulResponseState\n");
						}
						else if ((seoulPacketUserDataLen + 3) == seoulPacketLFieldValue) {
							seoulPacketState = SEOUL_PACKET_RX_CHECKSUM;
                            //printf("#### threadUart1 - 8. SEOUL_PACKET_RX_DATA 1\n");
						}
						break;

					case SEOUL_PACKET_RX_CHECKSUM:
                        //printf("#### threadUart1 - 9. SEOUL_PACKET_RX_CHECKSUM 0\n");
						//if (ch == seoulPacketChecksum) {
                        if (true) {     // Ignore checksum fail
							seoulPacketState = SEOUL_PACKET_RX_ETX;
							buffer[seoulPacketRcvdBytes++] = ch;
                            //printf("#### threadUart1 - 9. SEOUL_PACKET_RX_CHECKSUM 1\n");
						}
						else {
							resetSeoulResponseState();
                            printf("#### threadUart1 - 9. SEOUL_PACKET_RX_CHECKSUM resetSeoulResponseState ch=%02x CheckSum=%02x\n", ch, seoulPacketChecksum);
#if 0
                            int nValue = 0;
                            makeBcdToInt(nValue, (buffer+15), 4);
                            printf("#### threadUart1 - nValue : %d\n", nValue);
                            float fValue = (float)nValue / 1000;
                            seoul_water_meter_res->set_value(fValue);
                            //power_meter_res->set_value(fValue);
                            printf("#### threadUart1 - Seoul Water Meter : %02x %02x %02x %02x\n", 
                                    buffer[15], buffer[16], buffer[17], buffer[18]);
                            printf("#### threadUart1 - Seoul Water Meter : %.3f\n", fValue);
#endif
						}
						break;

					case SEOUL_PACKET_RX_ETX:
                        //printf("#### threadUart1 - 10. SEOUL_PACKET_RX_ETX 0\n");
						if (ch == SEOUL_RESPONSE_ETX) {
							buffer[seoulPacketRcvdBytes++] = ch;

                            int nValue = 0;
                            makeBcdToInt(nValue, (buffer+15), 4);
                            float fValue = (float)nValue / 1000;
                            seoul_water_meter_res->set_value(fValue);
                            printf("#### threadUart1 - Seoul Water Meter : %.3f\n", fValue);
                        }

						resetSeoulResponseState();
						break;

					default:
                        printf("#### threadUart1 - 11. default %d\n", seoulPacketState);
						break;
				}
			}
        }
        //printf("### threadUart1 - 6\n");
        //uart1_Flags.clear();
        //    printf("### threadUart1 - 7\n");
        Thread::wait(1000.0);
    }
}


void rxCallback_SeoulWaterMeter() {
    char ch = uart1SeoulWaterMater.getc();
    bufUart1.push(ch);
    led2 = !led2;
}

//meterType = PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_WATER;
//meterType = PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HOT_WATER;
//meterType = PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_GAS;
//meterType = PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HEAT;

void request_OtherMeters(uint8_t meterType) {
    uint8_t bufRequestCommand[6];
    uint8_t cmdLen = 0;

	dplcPacketMeterType = meterType;

	bufRequestCommand[cmdLen++] = PSTEC_REQUEST_STX;
	bufRequestCommand[cmdLen++] = dplcPacketMeterType;
	bufRequestCommand[cmdLen++] = (bufRequestCommand[0] + bufRequestCommand[1]) & 0x7F;	// BCC
	bufRequestCommand[cmdLen++] = PSTEC_REQUEST_ETX;

    while (true) {
        if (uart2OtherMater.writeable()) {
            uart2OtherMater.printf((const char*)bufRequestCommand);
            break;
        }
    }
}

void request_WaterMeter() {
    request_OtherMeters(PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_WATER);
}

void request_HotWaterMeter() {
    request_OtherMeters(PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HOT_WATER);
}

void request_GasMeter() {
    request_OtherMeters(PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_GAS);
}

void request_HeatMeter() {
    request_OtherMeters(PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HEAT);
}

void threadUart2_OtherMeters() {
	char buffer[22];

    printf("### threadUart2 - 1\n");

    while(true) {
        //printf("### threadUart2 - 4\n");

        int nCount = bufUart2.size();
        //printf("#### threadUart2 - Buffer Count : %d\n", nCount);

        if (11 <= (nCount + dplcPacketRcvdBytes)) {
            printf("#### threadUart2 - Buffer Count : %d\n", nCount);
            char ch = 0;
			for (uint8_t i = 0; i < nCount; i++) {
				bufUart2.pop(ch);

				switch (dplcPacketState) {
					case PSTEC_PACKET_TX_STX:
						if (ch == PSTEC_REQUEST_STX) {
							dplcPacketState    = PSTEC_PACKET_TX_ID;
							dplcPacketChecksum = ch;

							dplcPacketRcvdBytes++;
						}
						break;

					case PSTEC_PACKET_TX_ID:
						if (ch == dplcPacketMeterType) {
							dplcPacketState     = PSTEC_PACKET_TX_BCC;
							dplcPacketChecksum += ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_TX_BCC:
						dplcPacketChecksum &= 0x7F;	// trim to 7-bit BCC

						if (ch == dplcPacketChecksum) {
							dplcPacketState = PSTEC_PACKET_TX_ETX;
							dplcPacketRcvdBytes++;
						}
						else						{
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_TX_ETX:
						if (ch == PSTEC_REQUEST_ETX) {
							dplcPacketRcvdBytes++;

							if (dplcPacketRcvdBytes == PSTEC_REQUEST_PACKET_LENGTH)	{
								dplcPacketState = PSTEC_PACKET_RX_STX;
							}
							else {
								resetDplcResponseState();
							}
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_STX:
						if (ch == PSTEC_RESPONSE_STX) {
							dplcPacketState    = PSTEC_PACKET_RX_MAGIC_CODE_1ST_BYTE;
							dplcPacketChecksum = ch;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_MAGIC_CODE_1ST_BYTE:
						if (ch == PSTEC_MAGIC_CODE_1ST_BYTE) {
							dplcPacketState     = PSTEC_PACKET_RX_MAGIC_CODE_2ND_BYTE;
							dplcPacketChecksum += ch;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_MAGIC_CODE_2ND_BYTE:
						if (ch == PSTEC_MAGIC_CODE_2ND_BYTE) {
							dplcPacketState     = PSTEC_PACKET_RX_MAGIC_CODE_3RD_BYTE;
							dplcPacketChecksum += ch;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_MAGIC_CODE_3RD_BYTE:
						if (ch == PSTEC_MAGIC_CODE_3RD_BYTE) {
							dplcPacketState     = PSTEC_PACKET_RX_MAGIC_CODE_4TH_BYTE;
							dplcPacketChecksum += ch;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_MAGIC_CODE_4TH_BYTE:
						if (ch == PSTEC_MAGIC_CODE_4TH_BYTE) {
							dplcPacketState     = PSTEC_PACKET_RX_ID;
							dplcPacketChecksum += ch;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_ID:
						if (ch == dplcPacketMeterType) {
							dplcPacketState     = PSTEC_PACKET_RX_DATA;
							dplcPacketChecksum += ch;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_DATA:
						dplcPacketApduLength++;
						dplcPacketChecksum += ch;

						buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

						dplcPacketRcvdBytes++;

						if (dplcPacketApduLength > PSTEC_RESPONSE_PACKET_APDU_LENGTH_NORMAL_ACCUM) {
							resetDplcResponseState();
						}
						else if (dplcPacketApduLength == PSTEC_RESPONSE_PACKET_APDU_LENGTH_NORMAL_ACCUM) {
							dplcPacketState = PSTEC_PACKET_RX_BCC;
						}
						break;

					case PSTEC_PACKET_RX_BCC:
						dplcPacketChecksum &= 0x7F;	// trim to 7-bit BCC

						if (ch == dplcPacketChecksum) {
							dplcPacketState = PSTEC_PACKET_RX_ETX;

							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;
						}
						else {
							resetDplcResponseState();
						}
						break;

					case PSTEC_PACKET_RX_ETX:
						if (ch == PSTEC_RESPONSE_ETX) {
							buffer[dplcPacketRcvdBytes-PSTEC_REQUEST_PACKET_LENGTH] = ch;

							dplcPacketRcvdBytes++;

							if ((PSTEC_REQUEST_PACKET_LENGTH + PSTEC_RESPONSE_PACKET_LENGTH_NORMAL_ACCUM) == dplcPacketRcvdBytes) {
			//					dplcPacket.length = PSTEC_RESPONSE_PACKET_LENGTH_NORMAL_ACCUM;
			//
			//					ret = TRUE;
                                float fValue = 0.0f;
                                int nValue = 0;
                                makeReverseBcdToInt(nValue, (buffer+6), 3);
                                printf("#### threadUart2 - nValue : %d\n", nValue);
                                printf("#### threadUart2 - Other Meters : %02x %02x %02x\n", 
                                    buffer[6], buffer[7], buffer[8]);
                                
                                  if (PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_WATER == dplcPacketMeterType) {
                                    fValue = (float)nValue / 10;
                                    water_meter_res->set_value(fValue);
                                    printf("#### threadUart2 - Water Meter : %.1f\n", fValue);
                                }
                                else if (PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HOT_WATER == dplcPacketMeterType) {
                                    fValue = (float)nValue / 10;
                                    hot_water_meter_res->set_value(fValue);
                                    printf("#### threadUart2 - Hot Water Meter : %.1f\n", fValue);
                                }
                                else if (PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_GAS == dplcPacketMeterType) {
                                    fValue = (float)nValue / 10;
                                    gas_meter_res->set_value(fValue);
                                    printf("#### threadUart2 - Gas Meter : %.1f\n", fValue);
                                }
                                else if (PSTEC_NORMAL_ACCUM_ONLY_METER_TYPE_HEAT == dplcPacketMeterType) {
                                    fValue = (float)nValue / 100;
                                    heat_meter_res->set_value(fValue);
                                    printf("#### threadUart2 - Heat Meter : %.2f\n", fValue);
                                }
							}
						}

						resetDplcResponseState();
						break;

					default:
						break;
				}
			}
		}
        //printf("### threadUart2 - 6\n");
        Thread::wait(1000.0);
    }
}

void rxCallback_OtherMeters() {
    char ch = uart2OtherMater.getc();
    bufUart2.push(ch);
    led2 = !led2;
}

#endif

int main(void) {

#if 0
#if MBED_CONF_SERCOMM_TPB23_PROVIDE_DEFAULT == 1
    DigitalOut TPB23_RESET(A1);
    TPB23_RESET = 0;    /* 0: Standby 1: Reset */
    printf("\nSERCOM TPB23 Standby\n");
#elif MBED_CONF_QUECTEL_BG96_PROVIDE_DEFAULT == 1
    DigitalOut BG96_RESET(D7);
    DigitalOut BG96_PWRKEY(D9);
 
    BG96_RESET = 1;
    BG96_PWRKEY = 1;
    wait_ms(200);
 
    BG96_RESET = 0;
    BG96_PWRKEY = 0;
    wait_ms(300);
 
    BG96_RESET = 1;
    wait_ms(5000);
    printf("\nQUECTEL BG96 Standby\n");
#endif
#endif
    printf("\nStarting Simple Pelion Device Management Client example\n");

    int storage_status = fs.mount(&sd);
    if (storage_status != 0) {
        printf("Storage mounting failed.\n");
    }

#if USE_BUTTON == 1
    // If the User button is pressed ons start, then format storage.
    bool btn_pressed = (button.read() == MBED_CONF_APP_BUTTON_PRESSED_STATE);
    if (btn_pressed) {
        printf("User button is pushed on start...\n");
    }
#else
    bool btn_pressed = false;
#endif /* USE_BUTTON */

    if (storage_status || btn_pressed) {
        printf("Formatting the storage...\n");
        int storage_status = StorageHelper::format(&fs, &sd);
        if (storage_status != 0) {
            printf("ERROR: Failed to reformat the storage (%d).\n", storage_status);
        }
    } else {
        printf("You can hold the user button during boot to format the storage and change the device identity.\n");
    }

    // Connect to the internet (DHCP is expected to be on)
    printf("Connecting to the network using Wifi...\n");
    net = NetworkInterface::get_default_instance();

    nsapi_error_t net_status = -1;
    for (int tries = 0; tries < 3; tries++) {
        net_status = net->connect();
        if (net_status == NSAPI_ERROR_OK) {
            break;
        } else {
            printf("Unable to connect to network. Retrying...\n");
        }
    }

    if (net_status != NSAPI_ERROR_OK) {
        printf("ERROR: Connecting to the network failed (%d)!\n", net_status);
        return -1;
    }

    printf("Connected to the network successfully. IP address: %s\n", net->get_ip_address());

    printf("Initializing Pelion Device Management Client...\n");

    // SimpleMbedCloudClient handles registering over LwM2M to Pelion Device Management
    SimpleMbedCloudClient client(net, bd, &fs);
    int client_status = client.init();
    if (client_status != 0) {
        printf("Pelion Client initialization failed (%d)\n", client_status);
        return -1;
    }

    // Creating resources, which can be written or read from the cloud
    button_res = client.create_resource("3200/0/5501", "button_count");
    button_res->set_value(0);
    button_res->methods(M2MMethod::GET);
    button_res->observable(true);
    button_res->attach_notification_callback(button_callback);

    led_res = client.create_resource("3201/0/5853", "led_state");
    led_res->set_value(led.read());
    led_res->methods(M2MMethod::GET | M2MMethod::PUT);
    led_res->attach_put_callback(put_callback);

    post_res = client.create_resource("3300/0/5605", "execute_function");
    post_res->methods(M2MMethod::POST);
    post_res->attach_post_callback(post_callback);

#if 1
    seoul_water_meter_res = client.create_resource("4110/0/5700", "Seoul-Water-Meter");
    seoul_water_meter_res->set_value(0);
    seoul_water_meter_res->methods(M2MMethod::GET);
    seoul_water_meter_res->observable(true);
    seoul_water_meter_res->attach_notification_callback(seoul_water_meter_callback);

    power_meter_res = client.create_resource("3331/0/5805", "electricEbnergy");
    power_meter_res->set_value(0);
    power_meter_res->methods(M2MMethod::GET);
    power_meter_res->observable(true);
    power_meter_res->attach_notification_callback(power_meter_callback);

    gas_meter_res = client.create_resource("4120/0/5700", "Gas-Meter");
    gas_meter_res->set_value(0);
    gas_meter_res->methods(M2MMethod::GET);
    gas_meter_res->observable(true);
    gas_meter_res->attach_notification_callback(gas_meter_callback);

    water_meter_res = client.create_resource("4130/0/5700", "Water-Meter");
    water_meter_res->set_value(0);
    water_meter_res->methods(M2MMethod::GET);
    water_meter_res->observable(true);
    water_meter_res->attach_notification_callback(water_meter_callback);

    hot_water_meter_res = client.create_resource("4140/0/5700", "Hot-Water-Meter");
    hot_water_meter_res->set_value(0);
    hot_water_meter_res->methods(M2MMethod::GET);
    hot_water_meter_res->observable(true);
    hot_water_meter_res->attach_notification_callback(hot_water_meter_callback);

    heat_meter_res = client.create_resource("4150/0/5700", "Heat-Meter");
    heat_meter_res->set_value(0);
    heat_meter_res->methods(M2MMethod::GET);
    heat_meter_res->observable(true);
    heat_meter_res->attach_notification_callback(heat_meter_callback);
#endif

    printf("Initialized Pelion Device Management Client. Registering...\n");

    // Callback that fires when registering is complete
    client.on_registered(&registered);

    // Register with Pelion DM
    client.register_and_connect();

    int i = 600; // wait up 60 seconds before attaching sensors and button events
    while (i-- > 0 && !client.is_client_registered()) {
        wait_ms(100);
    }



#if 1
    resetSeoulResponseState();
    bufUart1.reset();
    uart1SeoulWaterMater.baud(1200);

    printf("### MainThread - 1\n");
    threadSeoulWaterMeter.start(threadUart1_SeoulWaterMeter);
    printf("### MainThread - 2\n");
    uart1SeoulWaterMater.attach(&rxCallback_SeoulWaterMeter, Serial::RxIrq);
    printf("### MainThread - 3\n");


    resetDplcResponseState();
    bufUart2.reset();
    uart2OtherMater.baud(4800);

    printf("### MainThread - 4\n");
    threadOtherMeters.start(threadUart2_OtherMeters);
    printf("### MainThread - 5\n");
    uart2OtherMater.attach(&rxCallback_OtherMeters, Serial::RxIrq);
    printf("### MainThread - 6\n"); 


    //bufUart3.reset();
    //uart3PowerMeter.baud(9600);

#endif

#if USE_BUTTON == 1
    // The button fires on an interrupt context, but debounces it to the eventqueue, so it's safe to do network operations
    button.fall(eventQueue.event(&button_press));
    printf("Press the user button to increment the LwM2M resource value...\n");
#else
    // The timer fires on an interrupt context, but debounces it to the eventqueue, so it's safe to do network operations
    Ticker timer;
    timer.attach(eventQueue.event(&button_press), 5.0);
    printf("Simulating button press every 5 seconds...\n");
#endif /* USE_BUTTON */



    // You can easily run the eventQueue in a separate thread if required
    eventQueue.dispatch_forever();
}

#endif /* MBED_TEST_MODE */

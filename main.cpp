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

#define UART1_FLALG1        (1UL << 0)
#define UART2_FLALG1        (1UL << 0)
#define UART3_FLALG1        (1UL << 0)


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

Serial uart1SeoulWaterMater(PC_1, PC_0, 1200);    // 1200 BPS
Serial uart2OtherMater(PA_2, PA_3, 4800);         // 4800 BPS
Serial uart3PowerMeter(PC_4, PC_5, 9600);         // 9600 BPS
#endif

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
//static void txUart1Complete() {
//    printf("Uart1 TX Complete\n");
//}

void request_SeoulWaterMeter() {
    char bufRequestCommand[10] = "request";
#if 0    
    uart1SeoulWaterMater.write((const uint8_t *)bufRequestCommand, 
                                sizeof(bufRequestCommand),
                                (const event_callback_t &)txUart1Complete,
                                SERIAL_EVENT_TX_COMPLETE);
#else
    while (true) {
        if (uart1SeoulWaterMater.writeable()) {           
            uart1SeoulWaterMater.printf(bufRequestCommand);
            break;
        }
    }    
#endif
}

void threadUart1_SeoulWaterMeter() {
    printf("### threadUart1 - 1\n");
    bufUart1.reset();
    //threadSeoulWaterMeter.flags_clear();
    printf("#### threadUart1 - 2\n");
    //uart1_Flags.clear();
    //    printf("#### threadUart1 - 3\n");

    while(true) {
        printf("### threadUart1 - 4\n");
        //ThisThread::flags_wait_any((int32_t)0x01);
        //uart1_Flags.wait_any(UART1_FLALG1);
        //printf("#### threadUart1 - 5\n");
        while(true) {
            if (false == uart1SeoulWaterMater.readable()) {
                break;
            }
            char ch = uart1SeoulWaterMater.getc();
            if (false == bufUart1.full()) {
                bufUart1.push(ch);
                printf("#### threadUart1 - %02x pushed\n", ch);
            }
        }

        int nCount = bufUart1.size();
        if (8 <= nCount) {
            // char ch = 0;
            // bufUart1.pop(ch)
            // bufUart1.peek(ch);

            // Process seoul water meter packet
            // int v = seoul_water_meter_value;
            // seoul_water_meter_res->set_value(v)
            //printf("#### threadUart1 - Seoul Water Meter : %d\n", v);
        }
        printf("### threadUart1 - 6\n");
        //uart1_Flags.clear();
        //    printf("### threadUart1 - 7\n");
        Thread::wait(1000.0);
    }
}

void rxCallback_SeoulWaterMeter() {
    //printf("### rxCallback - 1\n");
    char ch = uart1SeoulWaterMater.getc();
    //printf("### rxCallback - 2\n");
    if (false == bufUart1.full()) {
        bufUart1.push(ch);
        //printf("### rxCallback - 3\n");
        //threadSeoulWaterMeter.flags_set((int32_t)0x01);
        //uart1_Flags.set(UART1_FLALG1);
    }
    //printf("### rxCallback - 4\n");
    // printf("Seoul Water Mater Rx = %02x", ch);
}

#if 0
void rxOtherMeter() {
    char ch = uart2OtherMater.getc();
    printf("Other Mater Rx = %02x", ch);
}
#endif
#endif

int main(void) {
//    uart1SeoulWaterMater.SetBaud(1200);     // 1200 BPS
//    uart2OtherMater.SetBaud(4800);          // 4800 BPS
//    uart3PowerMeter.SetBaud(9600);          // 9600 BPS


    //uart2OtherMater.attach(&rxOtherMeter, Serial::RxIrq);




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
    water_meter_res = client.create_resource("4110/0/5501", "Seoul-Water-Meter");
    water_meter_res->set_value(0);
    water_meter_res->methods(M2MMethod::GET);
    water_meter_res->observable(true);
    water_meter_res->attach_notification_callback(seoul_water_meter_callback);

    power_meter_res = client.create_resource("3331/0/5805", "electricEbnergy");
    power_meter_res->set_value(0);
    power_meter_res->methods(M2MMethod::GET);
    power_meter_res->observable(true);
    power_meter_res->attach_notification_callback(power_meter_callback);

    gas_meter_res = client.create_resource("4120/0/5501", "Gas-Meter");
    gas_meter_res->set_value(0);
    gas_meter_res->methods(M2MMethod::GET);
    gas_meter_res->observable(true);
    gas_meter_res->attach_notification_callback(gas_meter_callback);

    seoul_water_meter_res = client.create_resource("4130/0/5501", "Water-Meter");
    seoul_water_meter_res->set_value(0);
    seoul_water_meter_res->methods(M2MMethod::GET);
    seoul_water_meter_res->observable(true);
    seoul_water_meter_res->attach_notification_callback(water_meter_callback);

    hot_water_meter_res = client.create_resource("4140/0/5501", "Hot-Water-Meter");
    hot_water_meter_res->set_value(0);
    hot_water_meter_res->methods(M2MMethod::GET);
    hot_water_meter_res->observable(true);
    hot_water_meter_res->attach_notification_callback(hot_water_meter_callback);

    heat_meter_res = client.create_resource("4150/0/5501", "Heat-Meter");
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

    printf("### MainThread - 1\n");
    threadSeoulWaterMeter.start(threadUart1_SeoulWaterMeter);
    printf("### MainThread - 2\n");
    //uart1SeoulWaterMater.attach(&rxCallback_SeoulWaterMeter, Serial::RxIrq);
    //printf("### MainThread - 3\n");

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

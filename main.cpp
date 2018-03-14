/* mbed Microcontroller Library
 * Copyright (c) 2006-2018 u-blox Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <events/mbed_events.h>
#include <mbed.h>
#include "ble/BLE.h"
#include "ble/Gap.h"
#include "ButtonService.h"
#include "UbloxATCellularInterfaceN2xx.h"

/* This code is intended to run on a UBLOX NINA-B1 module
 * that is powered directly from a storage device that is charged from
 * an energy harvesting board, e.g. the TI BQ25505 EVK, where the
 * VBAT_SEC_ON (bar) pin can be checked to determine if there is enough
 * energy in the system to do some real work.  If there is then a
 * a UBLOX SARA-N2xx module is powered from the stored harvested energy
 * and data is sent over a UDP connection to a server on the internet
 * and then the module is powered off once more.
 *
 * NOTE: the NINA-B1 module has a single serial port, which is connected
 * to the SARA-N2xx module, so no debugging with printf()s please, debug
 * is only through toggling GPIO NINA_B1_GPIO_1 (D9), which is where the
 * red LED is attached on a UBLOX_EVK_NINA_B1 board.
 */

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

// Define this to enable the BLE bits
//#define ENABLE_BLE

// How frequently to wake-up to see if there is enough energy
// to do anything
#define WAKEUP_INTERVAL_MS 60000

// The number of times to attempt a cellular connection
#define CONNECT_TRIES 1

// How long to wait for a network connection
#define CONNECT_TIMEOUT_SECONDS 40

// The credentials of the SIM in the board.  If PIN checking is enabled
// for your SIM card you must set this to the required PIN.
#define SIM_PIN "0000"

// Network credentials.
#define APN         NULL
#define USERNAME    NULL
#define PASSWORD    NULL

// Debug LED
#define LONG_PULSE_MS   500
#define SHORT_PULSE_MS  50
#define PULSE_GAP_MS    250

/**************************************************************************
 * LOCAL VARIABLES
 *************************************************************************/

// Input pin to detect VBAT_SEC_ON on the BQ25505 chip going low
// This is NINA_B1_GPIO_2
static DigitalIn vBatSecOnBar(D10);

// Output pin to switch Q1, and hence VOR, on on the BQ25505 EVM
// This is NINA_B1_GPIO_4
static DigitalOut vOrOnBar(D11, 1);

// Debug LED
static DigitalOut debugLedBar(LED1, 1);

// An event queue
static EventQueue eventQueue(/* event count */ 10 * EVENTS_EVENT_SIZE);

#ifdef ENABLE_BLE
// The button on the Nina-B1 EVK
static InterruptIn button(BLE_BUTTON_PIN_NAME);

// BLE stuff
const static char     DEVICE_NAME[] = "Button";
static const uint16_t uuid16_list[] = {ButtonService::BUTTON_SERVICE_UUID};
static ButtonService *pButtonService;
#endif

/**************************************************************************
 * COMPLETE THE CELLULAR CLASS WITH POWER/INIT FUNCTIONS
 *************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

void onboard_modem_init()
{
    // Nothing to do, powering on is good enough
}

void onboard_modem_deinit()
{
    // Nothing to do
}

void onboard_modem_power_up()
{
    // Power on
    vOrOnBar = 0;
}

void onboard_modem_power_down()
{
    // Power off
    vOrOnBar = 1;
}

#ifdef __cplusplus
}
#endif

/**************************************************************************
 * STATIC FUNCTIONS
 *************************************************************************/

 // Check if the stored energy is sufficient to do stuff
static bool powerIsGood()
{
    return !vBatSecOnBar;
}

// Pulse the debug LED for a number of milliseconds
static void pulseDebugLed(int milliseconds)
{
    debugLedBar = 1;
    wait_ms(milliseconds);
    debugLedBar = 0;
    wait_ms(PULSE_GAP_MS);
}

// Indicate that a bad thing has happened, where the thing
// is identified by the number of pulses
static void bad(int pulses)
{
    for (int x = 0; x < pulses; x++) {
        pulseDebugLed(LONG_PULSE_MS);
    }
}

// Get the time from an NTP server over a UDP cellular connection
static void getTime()
{
    UDPSocket sockUdp;
    SocketAddress udpServer;
    SocketAddress udpSenderAddress;
    UbloxATCellularInterfaceN2xx *pInterface;
    bool connected = false;
    char buf[1024];
    int x;

    pulseDebugLed(SHORT_PULSE_MS);
    pInterface = new UbloxATCellularInterfaceN2xx();

    pInterface->set_credentials(APN, USERNAME, PASSWORD);
    pInterface->set_network_search_timeout(CONNECT_TIMEOUT_SECONDS);

    // Set up the modem
    pulseDebugLed(SHORT_PULSE_MS);
    if (pInterface->init(SIM_PIN)) {

        // Register with the network
        for (x = 0; powerIsGood() && (x < CONNECT_TRIES) && !connected; x++) {
            pulseDebugLed(SHORT_PULSE_MS);
            connected = (pInterface->connect() == 0);
        }

        if (powerIsGood()) {
            if (connected) {
                pulseDebugLed(SHORT_PULSE_MS);
                // 195.195.221.100 is an IP address of 2.pool.ntp.org
                if (pInterface->gethostbyname("195.195.221.100", &udpServer) == 0) {
                    pulseDebugLed(SHORT_PULSE_MS);
                    udpServer.set_port(123);
                    if (sockUdp.open(pInterface) == 0) {
                        pulseDebugLed(SHORT_PULSE_MS);
                        sockUdp.set_timeout(10000);
                        memset (buf, 0, sizeof(buf));
                        *buf = '\x1b';
                        if (sockUdp.sendto(udpServer, (void *) buf, 48) == 48) {
                            pulseDebugLed(SHORT_PULSE_MS);
                            x = sockUdp.recvfrom(&udpSenderAddress, buf, sizeof (buf));
                            if (x > 0) {
                                pulseDebugLed(SHORT_PULSE_MS);
                            } else {
                               bad(8); // Did not receive
                            }
                        } else {
                           bad(7); // Unable to send
                        }
                        // No need to close socket, it is automatically closed when the interface closes
                        pInterface->disconnect();
                        pInterface->deinit();
                   } else {
                       bad(6); // Unable to open socket
                   }
               } else {
                   bad(5); // Unable to get host name (should never happen)
               }
            } else {
               bad(4);  // Interface not connected
            }
        } else {
           bad(3);  // No power
        }
    } else {
       bad(2);  // Unable to initialise modem
    }

    delete pInterface;
}

// Perform the timed event
static void eventTickCallback(void)
{
    if (powerIsGood()) {
        getTime();
        // Make sure the modem module is definitely off
        onboard_modem_power_down();
    } else {
        bad(1);
    }
}

#ifdef ENABLE_BLE
// Callback for button-down
static void buttonPressedCallback(void)
{
    eventQueue.call(Callback<void(bool)>(pButtonService, &ButtonService::updateButtonState), true);
}

// Callback for button-up
static void buttonReleasedCallback(void)
{
    eventQueue.call(Callback<void(bool)>(pButtonService, &ButtonService::updateButtonState), false);
}

// Callback for BLE disconnection
static void disconnectionCallback(const Gap::DisconnectionCallbackParams_t *params)
{
    BLE::Instance().gap().startAdvertising(); // restart advertising
}

// Callback for BLE initialisation error handling
static void onBleInitError(BLE &ble, ble_error_t error)
{
    /* Initialization error handling should go here */
}

// Callback to print the BLE MAC address
static void printMacAddress()
{
    /* Print out device MAC address to the console*/
    Gap::AddressType_t addr_type;
    Gap::Address_t address;
    BLE::Instance().gap().getAddress(&addr_type, address);
    printf("DEVICE MAC ADDRESS: ");
    for (int i = 5; i >= 1; i--) {
        printf("%02x:", address[i]);
    }
    printf("%02x\r\n", address[0]);
}

// BLE initialisation complete handler
static void bleInitComplete(BLE::InitializationCompleteCallbackContext *params)
{
    BLE& ble = params->ble;
    ble_error_t error = params->error;

    if (error != BLE_ERROR_NONE) {
        /* In case of error, forward the error handling to onBleInitError */
        onBleInitError(ble, error);
        return;
    }

    /* Ensure that it is the default instance of BLE */
    if (ble.getInstanceID() != BLE::DEFAULT_INSTANCE) {
        return;
    }

    ble.gap().onDisconnection(disconnectionCallback);

    button.fall(buttonPressedCallback);
    button.rise(buttonReleasedCallback);

    /* Setup primary service. */
    pButtonService = new ButtonService(ble, false /* initial value for button pressed */);

    /* setup advertising */
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED | GapAdvertisingData::LE_GENERAL_DISCOVERABLE);
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LIST_16BIT_SERVICE_IDS, (uint8_t *)uuid16_list, sizeof(uuid16_list));
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LOCAL_NAME, (uint8_t *)DEVICE_NAME, sizeof(DEVICE_NAME));
    ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    ble.gap().setAdvertisingInterval(1000); /* 1000ms. */
    ble.gap().startAdvertising();

    // Comment this out if you don't have a USB cable
    // connected or everything hangs at this point
    // printMacAddress();
}

// BLE events
static void scheduleBleEventsProcessing(BLE::OnEventsToProcessCallbackContext* context) {
    BLE &ble = BLE::Instance();
    eventQueue.call(Callback<void()>(&ble, &BLE::processEvents));
}
#endif // ENABLE_BLE

/**************************************************************************
 * MAIN
 *************************************************************************/

int main()
{
    // Nice long pulse at the start to make it clear we're running
    pulseDebugLed(1000);
    wait_ms(1000);

    // Call this directly once at the start since I'm an impatient sort
    eventTickCallback();

    // Now start the timed callback
    eventQueue.call_every(WAKEUP_INTERVAL_MS, eventTickCallback);

#ifdef ENABLE_BLE
    BLE &ble = BLE::Instance();
    ble.onEventsToProcess(scheduleBleEventsProcessing);
    ble.init(bleInitComplete);
#endif

    eventQueue.dispatch_forever();
}

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
#include <mbed_stats.h> // For heap stats
#include <cmsis_os.h>   // For stack stats
#include "UbloxATCellularInterfaceN2xx.h"
#include "UbloxATCellularInterface.h"
#include "ble_data_gather.h"
#include "ble_uuids.h"
#include "morse.h"
#include "utilities.h"

/* This code is intended to run on a UBLOX NINA-B1 module
 * that is powered directly from a storage device that is charged from
 * an energy harvesting board, e.g. the TI BQ25505 EVK, where the
 * VBAT_SEC_ON (bar) pin can be checked to determine if there is enough
 * energy in the system to do some real work.  If there is then a
 * a UBLOX SARA-N2xx or SARA-R4 module is powered from the stored harvested
 * energy and data is sent over a UDP connection to a server on the internet
 * and then the module is powered off once more.
 *
 * NOTE: the NINA-B1 module has a single serial port, which is connected
 * to the SARA-N2xx/Sara-R4 module, so no debugging with printf()s please, debug
 * is only through toggling GPIO NINA_B1_GPIO_1 (D9), which is where the
 * red LED is attached on a UBLOX_EVK_NINA_B1 board.
 */

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

// Define this to enable the BLE bits
#define ENABLE_BLE

// Define this to enable printing out of the serial port.  This is normally
// off because (a) the only serial port is connected to the cellular modem and
// (b) if that is not the case and you want to connect to a PC instead but
// you don't happen to have a USB cable connected at the time then everything
// will hang.
//#define ENABLE_PRINTF

// Define this to enable the Morse printing of RAM stats at each event
//#define ENABLE_RAM_STATS

// How frequently to wake-up to see if there is enough energy
// to do anything
#define WAKEUP_INTERVAL_MS 60000

// The number of times to attempt a cellular connection
#define CELLULAR_CONNECT_TRIES 1

// How long to wait for a network connection
#define CELLULAR_CONNECT_TIMEOUT_SECONDS 40

// The credentials of the SIM in the board.  If PIN checking is enabled
// for your SIM card you must set this to the required PIN.
#define SIM_PIN "0000"

// Network credentials.
#define APN         NULL
#define USERNAME    NULL
#define PASSWORD    NULL

// The prefix for BLE peer devices we want to connect to
#define BLE_PEER_DEVICE_NAME_PREFIX "NINA-B1"

// Debug LED
#define LONG_PULSE_MS        500
#define SHORT_PULSE_MS       50
#define VERY_SHORT_PULSE_MS  35 // Don't set this any smaller as this is the smallest
                                // value where individual flashes are visible on a mobile
                                // phone video
#define PULSE_GAP_MS         250

#ifdef ENABLE_PRINTF
# define PRINTF(format, ...) printf(format, ##__VA_ARGS__)
#else
# define PRINTF(...)
#endif

/**************************************************************************
 * LOCAL VARIABLES
 *************************************************************************/

// Input pin to detect VBAT_SEC_ON on the BQ25505 chip going low
// This is NINA_B1_GPIO_2
static DigitalIn vBatSecOnBar(D10);

// Output pin to switch Q1, and hence VOR, on on the BQ25505 EVM
// This is NINA_B1_GPIO_4
static DigitalOut vOrOnBar(D11, 1);

// Pin that determines whether a SARA-N2xx or SARA-R4 modem
// is attached: pulled high for R4 modem by default, GND the
// pin for N2xx modem
static DigitalIn r4ModemNotN2xxModem(D12);

// Modem power on pin (only used for SARA-R4)
static DigitalOut modemPowerOn(A5, 1);

// Modem reset pin (only used for SARA-R4)
static DigitalOut modemReset(A4, 0);

// Debug LED
static DigitalOut debugLedBar(LED1, 1);

// Flag to indicate the modem that is attached
static bool useR4Modem = false;

// The wake-up event queue
static EventQueue wakeUpEventQueue(/* event count */ 10 * EVENTS_EVENT_SIZE);

#ifdef ENABLE_RAM_STATS
// Storage for heap stats
static mbed_stats_heap_t statsHeap;

// Storage for stack stats
static mbed_stats_stack_t statsStack;
#endif

/**************************************************************************
 * COMPLETE THE CELLULAR CLASS WITH POWER/INIT FUNCTIONS
 *************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

void onboard_modem_init()
{
    if (useR4Modem) {
        // Take us out of reset
        modemReset = 1;
    } else {
        // Turn the power off and on again,
        // there is no reset line
        vOrOnBar = 1;
        wait_ms(500);
        vOrOnBar = 0;
    }
}

void onboard_modem_deinit()
{
    if (useR4Modem) {
        // Back into reset
        modemReset = 0;
    } else {
        // Nothing to do
    }
}

void onboard_modem_power_up()
{
    // Power on
    vOrOnBar = 0;
    wait_ms(50);
    
    if (useR4Modem) {
        // Keep the power line low for 1 second
        modemPowerOn = 0;
        wait_ms(1000);
        modemPowerOn = 1;
        // Give modem a little time to respond
        wait_ms(100);
    }
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
 * STATIC FUNCTIONS: DEBUG
 *************************************************************************/

// Pulse the debug LED for a number of milliseconds
static void pulseDebugLed(int milliseconds)
{
    if (!morseIsActive()) {
        debugLedBar = 1;
        wait_ms(milliseconds);
        debugLedBar = 0;
        wait_ms(PULSE_GAP_MS);
    }
}

// Victory LED pattern
static void victoryDebugLed(int count)
{
    if (!morseIsActive()) {
        for (int x = 0; x < count; x++) {
            debugLedBar = 1;
            wait_ms(VERY_SHORT_PULSE_MS);
            debugLedBar = 0;
            wait_ms(VERY_SHORT_PULSE_MS);
        }
    }
}

// Indicate that a bad thing has happened, where the thing
// is identified by the number of pulses
static void bad(int pulses)
{
    if (!morseIsActive()) {
        for (int x = 0; x < pulses; x++) {
            pulseDebugLed(LONG_PULSE_MS);
        }
    }
}

/**************************************************************************
 * STATIC FUNCTIONS: GENERAL
 *************************************************************************/

// Check if the stored energy is sufficient to do stuff
static bool powerIsGood()
{
    return !vBatSecOnBar;
}

// Print the BLE status
static void printBleStatus(void)
{
    const char *pDeviceName;
    BleData *pBleData;
#ifdef ENABLE_PRINTF
    char buf[32];
#endif
    int numDataItems;
    int numDevices = 0;
    
    for (pDeviceName = pBleGetFirstDeviceName(); pDeviceName != NULL; pDeviceName = pBleGetNextDeviceName()) {
        numDevices++;
        numDataItems = bleGetNumDataItems(pDeviceName);
        PRINTF("** BLE device %d: %s, %d data item(s)", numDevices, pDeviceName, numDataItems);
        if (numDataItems > 0) {
            PRINTF(": ");
            while ((pBleData = pBleGetFirstDataItem(pDeviceName, true)) != NULL) {
                victoryDebugLed(10);
                PRINTF("0x%.*s ", bytesToHexString(pBleData->pData, pBleData->dataLen, buf, sizeof(buf)), buf);
                free(pBleData->pData);
                free(pBleData);
            }
        }
        PRINTF("\n");
    }
    
    if (numDevices == 0) {
        PRINTF(".\n");
    }
}

// Get a response from a UDP server
static void getUdpResponse()
{
    UDPSocket sockUdp;
    SocketAddress udpServer;
    SocketAddress udpSenderAddress;
    void *pInterface;
    bool connected = false;
    char buf[1024];
    int x;

    if (useR4Modem) {
        pInterface = new UbloxATCellularInterface();
    } else {
        pInterface = new UbloxATCellularInterfaceN2xx();
    }
    pulseDebugLed(SHORT_PULSE_MS);

    if (useR4Modem) {
        ((UbloxATCellularInterface *) pInterface)->set_credentials(APN, USERNAME, PASSWORD);
        ((UbloxATCellularInterface *) pInterface)->set_network_search_timeout(CELLULAR_CONNECT_TIMEOUT_SECONDS);
        ((UbloxATCellularInterface *) pInterface)->set_release_assistance(true);
    } else {
        ((UbloxATCellularInterfaceN2xx *) pInterface)->set_credentials(APN, USERNAME, PASSWORD);
        ((UbloxATCellularInterfaceN2xx *) pInterface)->set_network_search_timeout(CELLULAR_CONNECT_TIMEOUT_SECONDS);
        ((UbloxATCellularInterfaceN2xx *) pInterface)->set_release_assistance(true);
    }

    // Set up the modem
    pulseDebugLed(SHORT_PULSE_MS);
    if (useR4Modem) {
        x = ((UbloxATCellularInterface *) pInterface)->init(SIM_PIN);
    } else {
        x = ((UbloxATCellularInterfaceN2xx *) pInterface)->init(SIM_PIN);
    }
    
    if (x) {
        // Register with the network
        for (x = 0; !connected && powerIsGood() && (x < CELLULAR_CONNECT_TRIES); x++) {
            pulseDebugLed(SHORT_PULSE_MS);
            if (useR4Modem) {
                connected = (((UbloxATCellularInterface *) pInterface)->connect() == 0);
            } else {
                connected = (((UbloxATCellularInterfaceN2xx *) pInterface)->connect() == 0);
            }
        }

        // Note: don't check for power being good again here.  The cellular modem
        // is about to transmit and the VBAT_SEC_ON line will glitch as a result
        // Better to rely on the capacity of the system to tide us over.
        if (connected) {
            pulseDebugLed(SHORT_PULSE_MS);
            // 195.195.221.100:123 is an address of 2.pool.ntp.org
            // 151.9.34.90:5060 is the address of ciot.it-sgn.u-blox.com and the port is where a UDP echo application should be listening
            // 195.34.89.241:7 is the address of the u-blox echo server and port for UDP packets
            if (useR4Modem) {
                x = ((UbloxATCellularInterface *) pInterface)->gethostbyname("151.9.34.90", &udpServer) == 0;
            } else {
                x = ((UbloxATCellularInterfaceN2xx *) pInterface)->gethostbyname("151.9.34.90", &udpServer) == 0;
            }
            if (x) {
                pulseDebugLed(SHORT_PULSE_MS);
                udpServer.set_port(5060);
                if (sockUdp.open(pInterface) == 0) {
                    pulseDebugLed(SHORT_PULSE_MS);
                    sockUdp.set_timeout(10000);
                    memset (buf, 0, sizeof(buf));
                    *buf = '\x1b';
                    if (sockUdp.sendto(udpServer, (void *) buf, 48) == 48) {
                        pulseDebugLed(SHORT_PULSE_MS);
                        x = sockUdp.recvfrom(&udpSenderAddress, buf, sizeof (buf));
                        if (x > 0) {
                            wait_ms(1000);
                            victoryDebugLed(25);
                        } else {
                           bad(7); // Did not receive
                        }
                    } else {
                       bad(6); // Unable to send
                    }
                    sockUdp.close();
                    if (useR4Modem) {
                        ((UbloxATCellularInterface *) pInterface)->disconnect();
                        ((UbloxATCellularInterface *) pInterface)->deinit();
                    } else {
                        ((UbloxATCellularInterfaceN2xx *) pInterface)->disconnect();
                        ((UbloxATCellularInterfaceN2xx *) pInterface)->deinit();
                    }
                } else {
                    bad(5); // Unable to open socket
                }
            } else {
                bad(4); // Unable to get host name (should never happen)
            }
        } else {
           bad(3);  // Interface not connected
        }
    } else {
       bad(2);  // Unable to initialise modem
    }

    if (useR4Modem) {
        delete (UbloxATCellularInterface *) pInterface;
    } else {
        delete (UbloxATCellularInterfaceN2xx *) pInterface;
    }
}

// Printf() out some RAM stats
#ifdef ENABLE_RAM_STATS
static void ramStats()
{
    mbed_stats_heap_get(&statsHeap);
    mbed_stats_stack_get(&statsStack);
    
    PRINTF("Heap left: %d byte(s), stack left %d byte(s).\n", statsHeap.reserved_size - statsHeap.max_size, statsStack.reserved_size - statsStack.max_size);
#ifndef ENABLE_PRINTF
    printfMorse("H %d S %d", statsHeap.reserved_size - statsHeap.max_size, statsStack.reserved_size - statsStack.max_size);
#endif
}
#endif

// Perform the wake-up event
static void wakeUpTickCallback(void)
{
#ifdef ENABLE_RAM_STATS
    ramStats();
#endif

    if (powerIsGood()) {
#ifdef ENABLE_BLE
        PRINTF("BLE Scanning... (if you don't see dots appear below, try restarting your serial terminal).\n");
        bleInit(BLE_PEER_DEVICE_NAME_PREFIX, TEMP_SRV_UUID_TEMP_CHAR, 100, &wakeUpEventQueue, false);
        int x = wakeUpEventQueue.call_every(1000, printBleStatus);
        bleRun(30000);
        wait_ms(30000);
        wakeUpEventQueue.cancel(x);
        bleDeinit();
#endif
        getUdpResponse();
        // Make sure the modem module is definitely off
        onboard_modem_power_down();
    } else {
        bad(1);
    }
}

/**************************************************************************
 * PUBLIC FUNCTIONS
 *************************************************************************/

// Main
int main()
{
    // Initialise Morse, in case we need it
    initMorse(&debugLedBar);
    
    // Nice long pulse at the start to make it clear we're running
    pulseDebugLed(1000);
    wait_ms(1000);
    
    // Check what kind of modem is attached
    if (r4ModemNotN2xxModem) {
        useR4Modem = true;
    }

    // Call this directly once at the start since I'm an impatient sort
    wakeUpTickCallback();

    // Now start the timed callback
    wakeUpEventQueue.call_every(WAKEUP_INTERVAL_MS, wakeUpTickCallback);
    wakeUpEventQueue.dispatch_forever();
}

// End of file
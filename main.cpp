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
#include <ctype.h>      // For toupper()
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

// Define this to enable the Morse printing of RAM stats at each event
//#define ENABLE_RAM_STATS

// Define this to divert mbed-os asserts to Morse (requires you to edit mbed_error_vfprintf()
// in mbed-os/platform/mbed_board.c and mbed_assert_internal() in mbed-os/platform/mbed_assert.c
// to be WEAK in order that they can be overridden.
//#define ENABLE_ASSERTS_IN_MORSE

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
#define LONG_PULSE_MS        500
#define SHORT_PULSE_MS       50
#define VERY_SHORT_PULSE_MS  35 // Don't set this any smaller as this is the smallest
                                // value where individual flashes are visible on a mobile
                                // phone video
#define PULSE_GAP_MS         250
#define MORSE_DOT            100
#define MORSE_DASH           500
#define MORSE_GAP            250
#define MORSE_LETTER_GAP     1250
#define MORSE_WORD_GAP       1500
#define MORSE_START_END_GAP  1500  // Must be at least as large as the letter gap

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

// A pointer to a thread to display Morse on the LED
static Thread *pMorseThread = NULL;

// Flag to indicate that Morse output is active
static bool volatile morseActive = false;

// Buffer for Morse printf()s
static char morseBuf[64];

// Morse codes
static const char morseLetters[] = {'?', '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                                    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                                    'W', 'X', 'Y', 'Z', '.', ',', '/', '1', '2', '3', '4', '5',
                                    '6', '7', '8', '9', '0'};
static const char *pMorseCodes[] = {"..--.." /* ? */, ".--.-." /* @ */, ".-" /* A */, "-..." /* B */, "-.-." /* C */, "-.." /* D */,
                                    "." /* E */, "..-." /* F */, "--." /* G */, "...." /* H */, ".." /* I */, ".---" /* J */,
                                    "-.-" /* K */, ".-.." /* L */, "--" /* M */, "-." /* N */, "---" /* O */, ".--." /* P */,
                                    "--.-" /* Q */, ".-." /* R */, "..." /* S */, "-" /* T */, "..-" /* U */, "...-" /* V */,
                                    ".--" /* W */, "-..-" /* Z */, "-.--" /* Y */, "--.." /* Z */, ".-.-.-" /* . */, "--..--" /* , */,
                                    "-..-." /* / */, ".----" /* 1 */, "..---" /* 2 */, "...--" /* 3 */, "....-" /* 4 */, "....." /* 5 */,
                                    "-...." /* 6 */, "--..." /* 7 */, "---.." /* 8 */, "----." /* 9 */, "-----" /* 0 */};

// An event queue
static EventQueue eventQueue(/* event count */ 10 * EVENTS_EVENT_SIZE);

#ifdef ENABLE_RAM_STATS
// Storage for heap stats
static mbed_stats_heap_t statsHeap;

// Storage for stack stats
static mbed_stats_stack_t statsStack;
#endif

#ifdef ENABLE_BLE
// The button on the Nina-B1 EVK
static InterruptIn button(BLE_BUTTON_PIN_NAME);

// BLE stuff
const static char     DEVICE_NAME[] = "Button";
static const uint16_t uuid16_list[] = {ButtonService::BUTTON_SERVICE_UUID};
static ButtonService *pButtonService;
#endif

/**************************************************************************
 * PUBLIC FUNCTION PROTOTYPES
 *************************************************************************/

void printfMorse(const char *pFormat, ...);
void tPrintfMorse(const char *pFormat, ...);

/**************************************************************************
 * COMPLETE THE CELLULAR CLASS WITH POWER/INIT FUNCTIONS
 *************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

void onboard_modem_init()
{
    // Power off and on again
    vOrOnBar = 1;
    wait_ms(500);
    vOrOnBar = 0;
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
 * STATIC FUNCTIONS: DEBUG
 *************************************************************************/

// Pulse the debug LED for a number of milliseconds
void pulseDebugLed(int milliseconds)
{
    if (!morseActive) {
        debugLedBar = 1;
        wait_ms(milliseconds);
        debugLedBar = 0;
        wait_ms(PULSE_GAP_MS);
    }
}

// Victory LED pattern
void victoryDebugLed(int count)
{
    if (!morseActive) {
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
    if (!morseActive) {
        for (int x = 0; x < pulses; x++) {
            pulseDebugLed(LONG_PULSE_MS);
        }
    }
}

// Flag the start or end of a Morse sequence
static void morseStartEndFlag()
{
    for (int x = 0; x < 5; x++) {
        debugLedBar = 1;
        wait_ms(VERY_SHORT_PULSE_MS);
        debugLedBar = 0;
        wait_ms(VERY_SHORT_PULSE_MS);
    }
}

// Flash out a buffer of characters in Morse
// Please call printfMorse() or tPrintfMorse(),
// see public functions below
static void morseFlash(const char *pBuf)
{
    char letter;
    const char *pMorseString;
    int morseLen;
    int len = strlen(pBuf);
    
    morseActive = true;
   // Begin with the opening sequence
    debugLedBar = 0;
    wait_ms(MORSE_START_END_GAP);
    morseStartEndFlag();
    wait_ms(MORSE_START_END_GAP);
    // Flash each character
    for (int x = 0; x < len; x++) {
        letter = toupper(*(pBuf + x));
        if ((letter == ' ') || (letter == '\n')) {
            // A gap between words, but ignoring a last '\n' or ' '
            if (x != len - 1) {
                wait_ms(MORSE_WORD_GAP);
            }
        } else {
            // A real letter
            pMorseString = NULL;
            for (unsigned int y = 0; (pMorseString == NULL) && (y < sizeof(morseLetters)); y++) {
                if (letter == morseLetters[y]) {
                    pMorseString = pMorseCodes[y];
                }
            }
            
            // If the letter is not found, put in '?'
            if (pMorseString == NULL) {
                pMorseString = pMorseCodes[0];
            }
            
            // Now flash the LED
            morseLen = strlen(pMorseString);
            for (int y = 0; y < morseLen; y++) {
                debugLedBar = 1;
                if (*(pMorseString + y) == '.') {
                    wait_ms(MORSE_DOT);
                } else if (*(pMorseString + y) == '-') {
                    wait_ms(MORSE_DASH);
                } else {
                    // Must be some mishtake
                }
                debugLedBar = 0;
                wait_ms(MORSE_GAP);
            }
            
            // Wait between letters
            wait_ms(MORSE_LETTER_GAP);
        }
    }
    wait_ms(MORSE_START_END_GAP - MORSE_LETTER_GAP);
    morseStartEndFlag();
    wait_ms(MORSE_START_END_GAP);
    morseActive = false;
}

// Flash a message in Morse on the LED; please call printfMorse() 
// or tPrintfMorse(), see public functions below
static void vPrintfMorse(bool async, const char *pFormat, va_list args)
{
    unsigned int len;
    
    // Get the string into a buffer
    len = vsnprintf(morseBuf, sizeof(morseBuf), pFormat, args);
    if (len > sizeof(morseBuf) - 1) {
        morseBuf[sizeof(morseBuf) - 1] = 0; // Ensure terminator
    }

    if (async) {
        // Only have one outstanding at a time
        if (pMorseThread != NULL) {
            pMorseThread->terminate();
            pMorseThread->join();
            delete pMorseThread;
            pMorseThread = NULL;
        }
        pMorseThread = new Thread();
        pMorseThread->start(callback(morseFlash, morseBuf));
    } else {
        morseFlash(morseBuf);
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
                                wait_ms(1000);
                                victoryDebugLed(25);
                            } else {
                               bad(8); // Did not receive
                            }
                        } else {
                           bad(7); // Unable to send
                        }
                        sockUdp.close();
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

// Printf() out some RAM stats
#ifdef ENABLE_RAM_STATS
static void ramStats()
{
    mbed_stats_heap_get(&statsHeap);
    mbed_stats_stack_get(&statsStack);
    
    printfMorse("H %d S %d", statsHeap.reserved_size - statsHeap.max_size, statsStack.reserved_size - statsStack.max_size);
}
#endif

// Perform the timed event
static void eventTickCallback(void)
{
#ifdef ENABLE_RAM_STATS
    ramStats();
#endif

    if (powerIsGood()) {
        getTime();
        // Make sure the modem module is definitely off
        onboard_modem_power_down();
    } else {
        bad(1);
    }
}

/**************************************************************************
 * STATIC FUNCTIONS: BLE
 *************************************************************************/

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
 * PUBLIC FUNCTIONS
 *************************************************************************/

// Printf() a message in Morse
void printfMorse(const char *pFormat, ...)
{
    va_list args;

    va_start(args, pFormat);
    vPrintfMorse(false, pFormat, args);
    va_end(args);
}

// Printf() a message in Morse in its own thread
// If the thread is already running it will be terminated
// and the new message will replace it
void tPrintfMorse(const char *pFormat, ...)
{
    va_list args;

    va_start(args, pFormat);
    vPrintfMorse(true, pFormat, args);
    va_end(args);
}

#ifdef ENABLE_ASSERTS_IN_MORSE
// Override the Mbed error vPrintf
void mbed_error_vfprintf(const char *pFormat, va_list args)
{
    vPrintfMorse(false, pFormat, args);
}

// Capture Mbed asserts
void mbed_assert_internal(const char *expr, const char *file, int line)
{
    while (1) {
        printfMorse("ASRT %s %s %d", expr, file, line);
    }
}
#endif

// Main
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

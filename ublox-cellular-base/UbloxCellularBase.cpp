/* Copyright (c) 2017 ublox Limited
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

#include "UARTSerial.h"
#include "APN_db.h"
#include "UbloxCellularBase.h"
#include "onboard_modem_api.h"
#ifdef FEATURE_COMMON_PAL
#include "mbed_trace.h"
#define TRACE_GROUP "UCB"
#else
#define tr_debug(format, ...) debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#define tr_info(format, ...)  debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#define tr_warn(format, ...)  debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#define tr_error(format, ...) debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#endif

/* Array to convert the 3G qual number into a median EC_NO_LEV number.
 */
                            /* 0   1   2   3   4   5   6  7 */
const int qualConvert3G[] = {44, 41, 35, 29, 23, 17, 11, 7};

/* Array to convert the 3G "rssi" number into a dBm RSCP value rounded up to the
 * nearest whole number.
 */
const int rscpConvert3G[] = {-108, -105, -103, -100,  -98,  -96,  -94,  -93,   /* 0 - 7 */
                              -91,  -89,  -88,  -85,  -83,  -80,  -78,  -76,   /* 8 - 15 */
                              -74,  -73,  -70,  -68,  -66,  -64,  -63,  -60,   /* 16 - 23 */
                              -58,  -56,  -54,  -53,  -51,  -49,  -48,  -46};  /* 24 - 31 */

/* Array to convert the LTE rssi number into a dBm value rounded up to the
 * nearest whole number.
 */
const int rssiConvertLte[] = {-118, -115, -113, -110, -108, -105, -103, -100,   /* 0 - 7 */
                               -98,  -95,  -93,  -90,  -88,  -85,  -83,  -80,   /* 8 - 15 */
                               -78,  -76,  -74,  -73,  -71,  -69,  -68,  -65,   /* 16 - 23 */
                               -63,  -61,  -60,  -59,  -58,  -55,  -53,  -48};  /* 24 - 31 */

/**********************************************************************
 * PRIVATE METHODS
 **********************************************************************/

void UbloxCellularBase::set_nwk_reg_status_csd(int status)
{
    switch (status) {
        case CSD_NOT_REGISTERED_NOT_SEARCHING:
        case CSD_NOT_REGISTERED_SEARCHING:
            tr_info("Not (yet) registered for circuit switched service");
            break;
        case CSD_REGISTERED:
        case CSD_REGISTERED_ROAMING:
            tr_info("Registered for circuit switched service");
            break;
        case CSD_REGISTRATION_DENIED:
            tr_info("Circuit switched service denied");
            break;
        case CSD_UNKNOWN_COVERAGE:
            tr_info("Out of circuit switched service coverage");
            break;
        case CSD_SMS_ONLY:
            tr_info("SMS service only");
            break;
        case CSD_SMS_ONLY_ROAMING:
            tr_info("SMS service only");
            break;
        case CSD_CSFB_NOT_PREFERRED:
            tr_info("Registered for circuit switched service with CSFB not preferred");
            break;
        default:
            tr_info("Unknown circuit switched service registration status. %d", status);
            break;
    }

    _dev_info.reg_status_csd = static_cast<NetworkRegistrationStatusCsd>(status);
}

void UbloxCellularBase::set_nwk_reg_status_psd(int status)
{
    switch (status) {
        case PSD_NOT_REGISTERED_NOT_SEARCHING:
        case PSD_NOT_REGISTERED_SEARCHING:
            tr_info("Not (yet) registered for packet switched service");
            break;
        case PSD_REGISTERED:
        case PSD_REGISTERED_ROAMING:
            tr_info("Registered for packet switched service");
            break;
        case PSD_REGISTRATION_DENIED:
            tr_info("Packet switched service denied");
            break;
        case PSD_UNKNOWN_COVERAGE:
            tr_info("Out of packet switched service coverage");
            break;
        case PSD_EMERGENCY_SERVICES_ONLY:
            tr_info("Limited access for packet switched service. Emergency use only.");
            break;
        default:
            tr_info("Unknown packet switched service registration status. %d", status);
            break;
    }

    _dev_info.reg_status_psd = static_cast<NetworkRegistrationStatusPsd>(status);
}

void UbloxCellularBase::set_nwk_reg_status_eps(int status)
{
    switch (status) {
        case EPS_NOT_REGISTERED_NOT_SEARCHING:
        case EPS_NOT_REGISTERED_SEARCHING:
            tr_info("Not (yet) registered for EPS service");
            break;
        case EPS_REGISTERED:
        case EPS_REGISTERED_ROAMING:
            tr_info("Registered for EPS service");
            break;
        case EPS_REGISTRATION_DENIED:
            tr_info("EPS service denied");
            break;
        case EPS_UNKNOWN_COVERAGE:
            tr_info("Out of EPS service coverage");
            break;
        case EPS_EMERGENCY_SERVICES_ONLY:
            tr_info("Limited access for EPS service. Emergency use only.");
            break;
        default:
            tr_info("Unknown EPS service registration status. %d", status);
            break;
    }

    _dev_info.reg_status_eps = static_cast<NetworkRegistrationStatusEps>(status);
}

void UbloxCellularBase::set_rat(int acTStatus)
{
    switch (acTStatus) {
        case GSM:
        case COMPACT_GSM:
            tr_info("Connected in GSM");
            break;
        case UTRAN:
            tr_info("Connected to UTRAN");
            break;
        case EDGE:
            tr_info("Connected to EDGE");
            break;
        case HSDPA:
            tr_info("Connected to HSDPA");
            break;
        case HSUPA:
            tr_info("Connected to HSPA");
            break;
        case HSDPA_HSUPA:
            tr_info("Connected to HDPA/HSPA");
            break;
        case LTE:
            tr_info("Connected to LTE");
            break;
        case EC_GSM_IoT:
            tr_info("Connected to EC_GSM_IoT");
            break;
        case E_UTRAN_NB_S1:
            tr_info("Connected to E_UTRAN NB1");
            break;
        default:
            tr_info("Unknown RAT %d", acTStatus);
            break;
    }

    _dev_info.rat = static_cast<RadioAccessNetworkType>(acTStatus);
}

bool UbloxCellularBase::get_iccid()
{
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card.
    // ICCID is a serial number identifying the SIM.
    // AT Command Manual UBX-13002752, section 4.12
    success = _at->send("AT+CCID") && _at->recv("+CCID: %20[^\n]\nOK\n", _dev_info.iccid);
    tr_info("DevInfo: ICCID=%s", _dev_info.iccid);

    UNLOCK();
    return success;
}

bool UbloxCellularBase::get_imsi()
{
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    // International mobile subscriber identification
    // AT Command Manual UBX-13002752, section 4.11
    success = _at->send("AT+CIMI") && _at->recv("%15[^\n]\nOK\n", _dev_info.imsi);
    tr_info("DevInfo: IMSI=%s", _dev_info.imsi);

    UNLOCK();
    return success;
}

bool UbloxCellularBase::get_imei()
{
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    success = _at->send("AT+CGSN") && _at->recv("%15[^\n]\nOK\n", _dev_info.imei);
    tr_info("DevInfo: IMEI=%s", _dev_info.imei);

    UNLOCK();
    return success;
}

bool UbloxCellularBase::get_meid()
{
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    // Mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.8
    success = _at->send("AT+GSN") && _at->recv("%18[^\n]\nOK\n", _dev_info.meid);
    tr_info("DevInfo: MEID=%s", _dev_info.meid);

    UNLOCK();
    return success;
}

bool UbloxCellularBase::set_sms()
{
    bool success = false;
    char buf[32];
    LOCK();

    MBED_ASSERT(_at != NULL);

    // Set up SMS format and enable URC
    // AT Command Manual UBX-13002752, section 11
    if (_at->send("AT+CMGF=1") && _at->recv("OK")) {
        tr_debug("SMS in text mode");
        if (_at->send("AT+CNMI=2,1") && _at->recv("OK")) {
            tr_debug("SMS URC enabled");
            // Set to CS preferred since PS preferred doesn't work
            // on some networks
            if (_at->send("AT+CGSMS=1") && _at->recv("OK")) {
                tr_debug("SMS set to CS preferred");
                success = true;
                memset (buf, 0, sizeof (buf));
                if (_at->send("AT+CSCA?") &&
                    _at->recv("+CSCA: \"%31[^\"]\"", buf) &&
                    _at->recv("OK")) {
                    tr_info("SMS Service Centre address is \"%s\"", buf);
                }
            }
        }
    }

    UNLOCK();
    return success;
}

void UbloxCellularBase::parser_abort_cb()
{
    _at->abort();
}

// Callback for CME ERROR and CMS ERROR.
void UbloxCellularBase::CMX_ERROR_URC()
{
    char buf[48];

    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        tr_debug("AT error %s", buf);
    }
    parser_abort_cb();
}

// Callback for circuit switched registration URC.
void UbloxCellularBase::CREG_URC()
{
    char buf[10];
    int status;
    int acTStatus;

    // If this is the URC it will be a single
    // digit followed by \n.  If it is the
    // answer to a CREG query, it will be
    // a ": %d,%d\n" where the second digit
    // indicates the status
    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %*d,%d,%*d,%*d,%d,", &status, &acTStatus) == 2) {
            set_nwk_reg_status_csd(status);
            set_rat(acTStatus);
        } else if (sscanf(buf, ": %*d,%d", &status) == 1) {
            set_nwk_reg_status_csd(status);
        } else if (sscanf(buf, ": %d", &status) == 1) {
            set_nwk_reg_status_csd(status);
        }
    }
}

// Callback for packet switched registration URC.
void UbloxCellularBase::CGREG_URC()
{
    char buf[10];
    int status;
    int acTStatus;

    // If this is the URC it will be a single
    // digit followed by \n.  If it is the
    // answer to a CGREG query, it will be
    // a ": %d,%d\n" where the second digit
    // indicates the status
    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %*d,%d,%*d,%*d,%d,", &status, &acTStatus) == 2) {
            set_nwk_reg_status_csd(status);
            set_rat(acTStatus);
        } else if (sscanf(buf, ": %*d,%d", &status) == 1) {
            set_nwk_reg_status_psd(status);
        } else if (sscanf(buf, ": %d", &status) == 1) {
            set_nwk_reg_status_psd(status);
        }
    }
}

// Callback for EPS registration URC.
void UbloxCellularBase::CEREG_URC()
{
    char buf[10];
    int status;
    int acTStatus;

    // If this is the URC it will be a single
    // digit followed by \n.  If it is the
    // answer to a CEREG query, it will be
    // a ": %d,%d\n" where the second digit
    // indicates the status
    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %*d,%d,%*d,%*d,%d,", &status, &acTStatus) == 2) {
            set_nwk_reg_status_csd(status);
            set_rat(acTStatus);
        } else if (sscanf(buf, ": %*d,%d", &status) == 1) {
            set_nwk_reg_status_eps(status);
        } else if (sscanf(buf, ": %d", &status) == 1) {
            set_nwk_reg_status_eps(status);
        }
    }
}

// Callback UMWI, just filtering it out.
void UbloxCellularBase::UMWI_URC()
{
    char buf[10];

    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    read_at_to_char(buf, sizeof (buf), '\n');
}

/**********************************************************************
 * PROTECTED METHODS
 **********************************************************************/

#if MODEM_ON_BOARD
void UbloxCellularBase::modem_init()
{
    ::onboard_modem_init();
}

void UbloxCellularBase::modem_deinit()
{
    ::onboard_modem_deinit();
}

void UbloxCellularBase::modem_power_up()
{
    ::onboard_modem_power_up();
}

void UbloxCellularBase::modem_power_down()
{
    ::onboard_modem_power_down();
}
#else
void UbloxCellularBase::modem_init()
{
    // Meant to be overridden
}

void UbloxCellularBase::modem_deinit()
{
    // Meant to be overridden
}

void UbloxCellularBase::modem_power_up()
{
    // Meant to be overridden
}

void UbloxCellularBase::modem_power_down()
{
    // Mmeant to be overridden
}
#endif

// Constructor.
// Note: to allow this base class to be inherited as a virtual base class
// by everyone, it takes no parameters.  See also comment above classInit()
// in the header file.
UbloxCellularBase::UbloxCellularBase()
{
    _pin = NULL;
    _at = NULL;
    _at_timeout = AT_PARSER_TIMEOUT;
    _fh = NULL;
    _modem_initialised = false;
    _sim_pin_check_enabled = false;
    _debug_trace_on = false;

    _dev_info.dev = DEV_TYPE_NONE;
    _dev_info.reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info.reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info.reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;
}

// Destructor.
UbloxCellularBase::~UbloxCellularBase()
{
    deinit();
    delete _at;
    delete _fh;
}

// Initialise the portions of this class that are parameterised.
void UbloxCellularBase::baseClassInit(PinName tx, PinName rx,
                                      int baud, bool debug_on)
{
    // Only initialise ourselves if it's not already been done
    if (_at == NULL) {
        if (_debug_trace_on == false) {
            _debug_trace_on = debug_on;
        }
        _baud = baud;

        // Set up File Handle for buffered serial comms with cellular module
        // (which will be used by the AT parser)
        // Note: the UART is initialised to run no faster than 115200 because
        // the modems cannot reliably auto-baud at faster rates.  The faster
        // rate is adopted later with a specific AT command and the
        // UARTSerial rate is adjusted at that time
        if (baud > 115200) {
            baud = 115200;
        }
        _fh = new UARTSerial(tx,  rx, baud);

        // Set up the AT parser
        _at = new ATCmdParser(_fh, OUTPUT_ENTER_KEY, AT_PARSER_BUFFER_SIZE,
                              _at_timeout, _debug_trace_on);

        // Error cases, out of band handling
        _at->oob("ERROR", callback(this, &UbloxCellularBase::parser_abort_cb));
        _at->oob("+CME ERROR", callback(this, &UbloxCellularBase::CMX_ERROR_URC));
        _at->oob("+CMS ERROR", callback(this, &UbloxCellularBase::CMX_ERROR_URC));

        // Registration status, out of band handling
        _at->oob("+CREG", callback(this, &UbloxCellularBase::CREG_URC));
        _at->oob("+CGREG", callback(this, &UbloxCellularBase::CGREG_URC));
        _at->oob("+CEREG", callback(this, &UbloxCellularBase::CEREG_URC));

        // Capture the UMWI, just to stop it getting in the way
        _at->oob("+UMWI", callback(this, &UbloxCellularBase::UMWI_URC));
    }
}

// Set the AT parser timeout.
// Note: the AT interface should be locked before this is called.
void UbloxCellularBase::at_set_timeout(int timeout) {

    MBED_ASSERT(_at != NULL);

    _at_timeout = timeout;
    _at->set_timeout(timeout);
}

// Read up to size bytes from the AT interface up to a "end".
// Note: the AT interface should be locked before this is called.
int UbloxCellularBase::read_at_to_char(char * buf, int size, char end)
{
    int count = 0;
    int x = 0;

    if (size > 0) {
        for (count = 0; (count < size) && (x >= 0) && (x != end); count++) {
            x = _at->getc();
            *(buf + count) = (char) x;
        }

        count--;
        *(buf + count) = 0;

        // Convert line endings:
        // If end was '\n' (0x0a) and the preceding character was 0x0d, then
        // overwrite that with null as well.
        if ((count > 0) && (end == '\n') && (*(buf + count - 1) == '\x0d')) {
            count--;
            *(buf + count) = 0;
        }
    }

    return count;
}

// Power up the modem.
// Enables the GPIO lines to the modem and then wriggles the power line in short pulses.
bool UbloxCellularBase::power_up()
{
    bool success = false;
    int at_timeout;
    LOCK();

    at_timeout = _at_timeout; // Has to be inside LOCK()s

    MBED_ASSERT(_at != NULL);

    /* Initialize GPIO lines */
    tr_info("Powering up modem...");
    modem_init();
    /* Give modem a little time to settle down */
    wait_ms(250);

    for (int retry_count = 0; !success && (retry_count < 20); retry_count++) {
        modem_power_up();
        wait_ms(500);
        // Modem tends to spit out noise during power up - don't confuse the parser
        _at->flush();
        at_set_timeout(1000);
        if (_at->send("AT")) {
            // C027 needs a delay here
            wait_ms(100);
            if (_at->recv("OK")) {
                success = true;
            }
        }
        at_set_timeout(at_timeout);
    }

    if (success) {
        // Set the final baud rate
        if (_at->send("AT+IPR=%d", _baud) && _at->recv("OK")) {
            // Need to wait for things to be sorted out on the modem side
            wait_ms(100);
            ((UARTSerial *)_fh)->set_baud(_baud);
        }
        
        // Turn off modem echoing and turn on verbose responses
        success = _at->send("ATE0;+CMEE=2") && _at->recv("OK") &&
                  // The following commands are best sent separately
                  _at->send("AT&K0") && _at->recv("OK") && // Turn off RTC/CTS handshaking
                  _at->send("AT&C1") && _at->recv("OK") && // Set DCD circuit(109), changes in accordance with the carrier detect status
                  _at->send("AT&D0") && _at->recv("OK"); // Set DTR circuit, we ignore the state change of DTR
    }

    if (!success) {
        tr_error("Preliminary modem setup failed.");
    }

    UNLOCK();
    return success;
}

// Power down modem via AT interface.
void UbloxCellularBase::power_down()
{
    LOCK();

    MBED_ASSERT(_at != NULL);

    // If we have been running, do a soft power-off first
    if (_modem_initialised && (_at != NULL)) {
        _at->send("AT+CPWROFF") && _at->recv("OK");
    }

    // Now do a hard power-off
    modem_power_down();
    modem_deinit();

    _dev_info.reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info.reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info.reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;

   UNLOCK();
}

// Get the device ID.
bool UbloxCellularBase::set_device_identity(DeviceType *dev)
{
    char buf[20];
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    success = _at->send("ATI") && _at->recv("%19[^\n]\nOK\n", buf);

    if (success) {
        if (strstr(buf, "SARA-G35"))
            *dev = DEV_SARA_G35;
        else if (strstr(buf, "LISA-U200-03S"))
            *dev = DEV_LISA_U2_03S;
        else if (strstr(buf, "LISA-U2"))
            *dev = DEV_LISA_U2;
        else if (strstr(buf, "SARA-U2"))
            *dev = DEV_SARA_U2;
        else if (strstr(buf, "SARA-R4"))
            *dev = DEV_SARA_R4;
        else if (strstr(buf, "LEON-G2"))
            *dev = DEV_LEON_G2;
        else if (strstr(buf, "TOBY-L2"))
            *dev = DEV_TOBY_L2;
        else if (strstr(buf, "MPCI-L2"))
            *dev = DEV_MPCI_L2;
    }

    UNLOCK();
    return success;
}

// Send initialisation AT commands that are specific to the device.
bool UbloxCellularBase::device_init(DeviceType dev)
{
    bool success = false;
    LOCK();

    MBED_ASSERT(_at != NULL);

    if ((dev == DEV_LISA_U2) || (dev == DEV_LEON_G2) || (dev == DEV_TOBY_L2)) {
        success = _at->send("AT+UGPIOC=20,2") && _at->recv("OK");
    } else if ((dev == DEV_SARA_U2) || (dev == DEV_SARA_G35)) {
        success = _at->send("AT+UGPIOC=16,2") && _at->recv("OK");
    } else {
        success = true;
    }

    UNLOCK();
    return success;
}

// Get the SIM card going.
bool UbloxCellularBase::initialise_sim_card()
{
    bool success = false;
    int retry_count = 0;
    bool done = false;
    LOCK();

    MBED_ASSERT(_at != NULL);

    /* SIM initialisation may take a significant amount, so an error is
     * kind of expected. We should retry 10 times until we succeed or timeout. */
    for (retry_count = 0; !done && (retry_count < 10); retry_count++) {
        char pinstr[16];

        if (_at->send("AT+CPIN?") && _at->recv("+CPIN: %15[^\n]\n", pinstr) &&
            _at->recv("OK")) {
            done = true;
            if (strcmp(pinstr, "SIM PIN") == 0) {
                _sim_pin_check_enabled = true;
                if (_at->send("AT+CPIN=\"%s\"", _pin)) {
                    if (_at->recv("OK")) {
                        tr_info("PIN correct");
                        success = true;
                    } else {
                        tr_error("Incorrect PIN");
                    }
                }
            } else if (strcmp(pinstr, "READY") == 0) {
                _sim_pin_check_enabled = false;
                tr_info("No PIN required");
                success = true;
            } else {
                tr_debug("Unexpected response from SIM: \"%s\"", pinstr);
            }
        }

        /* wait for a second before retry */
        wait_ms(1000);
    }

    if (done) {
        tr_info("SIM Ready.");
    } else {
        tr_error("SIM not ready.");
    }

    UNLOCK();
    return success;
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Initialise the modem.
bool UbloxCellularBase::init(const char *pin)
{
	int x;
    MBED_ASSERT(_at != NULL);

    if (!_modem_initialised) {
        if (power_up()) {
            tr_info("Modem Ready.");
            if (pin != NULL) {
                _pin = pin;
            }
            if (initialise_sim_card()) {
                if (set_device_identity(&_dev_info.dev) && // Set up device identity
                    device_init(_dev_info.dev)) {// Initialise this device
                    // Get the integrated circuit ID of the SIM
                    if (get_iccid()) {
                        // Try a few times to get the IMSI (since on some modems this can
                        // take a while to be retrieved, especially if a SIM PIN
                        // was set)
                        for (x = 0; (x < 3) && !get_imsi(); x++) {
                            wait_ms(1000);
                        }
                        
                        if (x < 3) { // If we got the IMSI, can get the others
                            if (get_imei() && // Get international mobile equipment identifier
                                get_meid() && // Probably the same as the IMEI
                                set_sms()) { // And set up SMS
                                // The modem is initialised.
                                _modem_initialised = true;
                            }
                        }
                    }
                }
            }
        }
    }

    return _modem_initialised;
}

// Perform registration.
bool UbloxCellularBase::nwk_registration(int timeoutSeconds)
{
    bool atSuccess = false;
    bool registered = false;
    int status;
    int at_timeout;
    LOCK();

    at_timeout = _at_timeout; // Has to be inside LOCK()s

    MBED_ASSERT(_at != NULL);

    if (!is_registered_psd() && !is_registered_csd() && !is_registered_eps()) {
        tr_info("Searching Network...");
        // Enable the packet switched and network registration unsolicited result codes
        if (_at->send("AT+CREG=1") && _at->recv("OK") &&
            _at->send("AT+CGREG=1") && _at->recv("OK")) {
            atSuccess = true;
            if (_at->send("AT+CEREG=1")) {
                _at->recv("OK");
                // Don't check return value as this works for LTE only
            }

            if (atSuccess) {
                // See if we are already in automatic mode
                if (_at->send("AT+COPS?") && _at->recv("+COPS: %d", &status) &&
                    _at->recv("OK")) {
                    // If not, set it
                    if (status != 0) {
                        // Don't check return code here as there's not much
                        // we can do if this fails.
                        _at->send("AT+COPS=0") && _at->recv("OK");
                    }
                }

                // Query the registration status directly as well,
                // just in case
                if (_at->send("AT+CREG?") && _at->recv("OK")) {
                    // Answer will be processed by URC
                }
                if (_at->send("AT+CGREG?") && _at->recv("OK")) {
                    // Answer will be processed by URC
                }
                if (_at->send("AT+CEREG?")) {
                    _at->recv("OK");
                    // Don't check return value as this works for LTE only
                }
            }
        }
        // Wait for registration to succeed
        at_set_timeout(1000);
        for (int waitSeconds = 0; !registered && (waitSeconds < timeoutSeconds); waitSeconds++) {
            registered = is_registered_psd() || is_registered_csd() || is_registered_eps();
            _at->recv(UNNATURAL_STRING);
        }
        at_set_timeout(at_timeout);

        if (registered) {
            // This should return quickly but sometimes the status field is not returned
            // so make the timeout short
            at_set_timeout(1000);
            if (_at->send("AT+COPS?") && _at->recv("+COPS: %*d,%*d,\"%*[^\"]\",%d\n", &status)) {
                set_rat(status);
            }
            at_set_timeout(at_timeout);
        }
    } else {
        registered = true;
    }
    
    UNLOCK();
    return registered;
}

bool UbloxCellularBase::is_registered_csd()
{
  return (_dev_info.reg_status_csd == CSD_REGISTERED) ||
          (_dev_info.reg_status_csd == CSD_REGISTERED_ROAMING) ||
          (_dev_info.reg_status_csd == CSD_CSFB_NOT_PREFERRED);
}

bool UbloxCellularBase::is_registered_psd()
{
    return (_dev_info.reg_status_psd == PSD_REGISTERED) ||
            (_dev_info.reg_status_psd == PSD_REGISTERED_ROAMING);
}

bool UbloxCellularBase::is_registered_eps()
{
    return (_dev_info.reg_status_eps == EPS_REGISTERED) ||
            (_dev_info.reg_status_eps == EPS_REGISTERED_ROAMING);
}

// Perform deregistration.
bool UbloxCellularBase::nwk_deregistration()
{
    bool success = false;
    LOCK();

    MBED_ASSERT(_at != NULL);

    if (_at->send("AT+COPS=2") && _at->recv("OK")) {
        _dev_info.reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
        _dev_info.reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
        _dev_info.reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;
        success = true;
    }

    UNLOCK();
    return success;
}

// Put the modem into its lowest power state.
void UbloxCellularBase::deinit()
{
    power_down();
    _modem_initialised = false;
}

// Set the PIN.
void UbloxCellularBase::set_pin(const char *pin) {
    _pin = pin;
}

// Enable or disable SIM pin checking.
bool UbloxCellularBase::sim_pin_check_enable(bool enableNotDisable)
{
    bool success = false;;
    LOCK();

    MBED_ASSERT(_at != NULL);

    if (_pin != NULL) {
        if (_sim_pin_check_enabled && !enableNotDisable) {
            // Disable the SIM lock
            if (_at->send("AT+CLCK=\"SC\",0,\"%s\"", _pin) && _at->recv("OK")) {
                _sim_pin_check_enabled = false;
                success = true;
            }
        } else if (!_sim_pin_check_enabled && enableNotDisable) {
            // Enable the SIM lock
            if (_at->send("AT+CLCK=\"SC\",1,\"%s\"", _pin) && _at->recv("OK")) {
                _sim_pin_check_enabled = true;
                success = true;
            }
        } else {
            success = true;
        }
    }

    UNLOCK();
    return success;
}

// Change the pin code for the SIM card.
bool UbloxCellularBase::change_sim_pin(const char *pin)
{
    bool success = false;;
    LOCK();

    MBED_ASSERT(_at != NULL);

    // Change the SIM pin
    if ((pin != NULL) && (_pin != NULL)) {
        if (_at->send("AT+CPWD=\"SC\",\"%s\",\"%s\"", _pin, pin) && _at->recv("OK")) {
            _pin = pin;
            success = true;
        }
    }

    UNLOCK();
    return success;
}

// Get the IMEI.
bool UbloxCellularBase::get_imei(char *imei_to_send, int size)
{
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    success = _at->send("AT+CGSN") && _at->recv("%15[^\n]\nOK\n", _dev_info.imei);
    tr_info("DevInfo: IMEI=%s", _dev_info.imei);

	if (success)	{
		memcpy(imei_to_send,_dev_info.imei,size);
		imei_to_send[size-1] = '\0';
	}

    UNLOCK();
    return success;
}

// Get the IMEI of the module.
const char *UbloxCellularBase::imei()
{
    return _dev_info.imei;
}

// Get the Mobile Equipment ID (which may be the same as the IMEI).
const char *UbloxCellularBase::meid()
{
    return _dev_info.meid;
}

// Get the IMSI of the SIM.
const char *UbloxCellularBase::imsi()
{
    // (try) to update the IMSI, just in case the SIM has changed
    get_imsi();
    
    return _dev_info.imsi;
}

// Get the ICCID of the SIM.
const char *UbloxCellularBase::iccid()
{
    // (try) to update the ICCID, just in case the SIM has changed
    get_iccid();
    
    return _dev_info.iccid;
}

// Get the RSSI in dBm.
int UbloxCellularBase::rssi()
{
    char buf[7] = {0};
    int rssi = 0;
    int qual = 0;
    int rssiRet = 0;
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);

    success = _at->send("AT+CSQ") && _at->recv("+CSQ: %6[^\n]\nOK\n", buf);

    if (success) {
        if (sscanf(buf, "%d,%d", &rssi, &qual) == 2) {
            // AT+CSQ returns a coded RSSI value and an RxQual value
            // For 2G an RSSI of 0 corresponds to -113 dBm or less, 
            // an RSSI of 31 corresponds to -51 dBm or less and hence
            // each value is a 2 dB step.
            // For LTE the mapping is defined in the array rssiConvertLte[].
            // For 3G the mapping to RSCP is defined in the array rscpConvert3G[]
            // and the RSSI value is then RSCP - the EC_NO_LEV number derived
            // by putting the qual number through qualConvert3G[].
            if ((rssi >= 0) && (rssi <= 31)) {
                switch (_dev_info.rat) {
                    case UTRAN:
                    case HSDPA:
                    case HSUPA:
                    case HSDPA_HSUPA:
                        // 3G
                        if ((qual >= 0) && (qual <= 7)) {
                            qual = qualConvert3G[qual];
                        }
                        rssiRet = rscpConvert3G[rssi];
                        rssiRet -= qual;
                        break;
                    case LTE:
                        // LTE
                        rssiRet = rssiConvertLte[rssi];
                        break;
                    case GSM:
                    case COMPACT_GSM:
                    case EDGE:
                    default:
                        // GSM or assumed GSM if the RAT is not known
                        rssiRet = -(113 - (rssi << 2));
                        break;
                }
            }
        }
    }

    UNLOCK();
    return rssiRet;
}

// End of File


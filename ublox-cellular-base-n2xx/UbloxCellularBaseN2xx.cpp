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
#include "UbloxCellularBaseN2xx.h"
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

#define ATOK _at->recv("OK")

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

void UbloxCellularBaseN2xx::set_nwk_reg_status_csd(int status)
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

void UbloxCellularBaseN2xx::set_nwk_reg_status_psd(int status)
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

void UbloxCellularBaseN2xx::set_nwk_reg_status_eps(int status)
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

void UbloxCellularBaseN2xx::set_rat(int AcTStatus)
{
    switch (AcTStatus) {
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
        default:
            tr_info("Unknown RAT %d", AcTStatus);
            break;
    }

    _dev_info.rat = static_cast<RadioAccessNetworkType>(AcTStatus);
}

bool UbloxCellularBaseN2xx::get_sara_n2xx_info()
{
    return (
        cgmi(_sara_n2xx_info.cgmi) &&
        cgmm(_sara_n2xx_info.cgmm) &&
        cgmr(_sara_n2xx_info.cgmr) &&
        cgsn(1, _sara_n2xx_info.cgsn)
    );
}

bool UbloxCellularBaseN2xx::at_req(const char *cmd, const char *recvFormat, const char *response) {
    bool success = false;         
    LOCK();
    
    MBED_ASSERT(_at != NULL);
    
    tr_debug("ATREQ: %s => %s", cmd, recvFormat);
    if(_at->send(cmd) && _at->recv(recvFormat, response) && ATOK) {
        tr_debug("ATRESULT: %s", response);
        success = true;        
    } else {
        tr_error("ATRESULT: No Answer!");
    }
    
    UNLOCK();     
    return success;
}

bool UbloxCellularBaseN2xx::at_req(const char *cmd, const char *recvFormat, int *response) {
    bool success = false;         
    LOCK();
    
    MBED_ASSERT(_at != NULL);

    tr_debug("ATREQ: %s => %s", cmd, recvFormat);
    if(_at->send(cmd) && _at->recv(recvFormat, response) && ATOK) {
        tr_debug("ATRESULT: %d", *response);
        success = true;        
    }  else {
        tr_error("ATRESULT: No Answer!");
    }

    UNLOCK();     
    return success;
}

bool UbloxCellularBaseN2xx::at_send(const char *cmd) {
    bool success = false;         
    LOCK();
    
    MBED_ASSERT(_at != NULL);

    tr_debug("ATSEND: %s", cmd);
    if(_at->send(cmd) && ATOK) {
        success = true;        
    } else {
        tr_error("Failed to send %s", cmd);
    }

    UNLOCK();     
    return success;
}

bool UbloxCellularBaseN2xx::at_send(const char *cmd, int n) {
    bool success = false;         
    LOCK();
    
    MBED_ASSERT(_at != NULL);

    tr_debug("ATSEND: %s, %d", cmd, n);
    if(_at->send(cmd, n) && ATOK) {
        success = true;        
    } else {
        tr_error("Failed to send %s,%d", cmd, n);
    }

    UNLOCK();     
    return success;
}

bool UbloxCellularBaseN2xx::at_send(const char *cmd, const char *arg) {
    bool success = false;         
    LOCK();
    
    MBED_ASSERT(_at != NULL);

    tr_debug("ATSEND: %s,%s", cmd, arg);
    if(_at->send(cmd, arg) && ATOK) {
        success = true;        
    } else {
        tr_error("Failed to send %s,%s", cmd, arg);
    }

    UNLOCK();     
    return success;
}

bool UbloxCellularBaseN2xx::cgmi(const char *response)
{
    return at_req("AT+CGMI", "%32[^\n]\n", response);
}

bool UbloxCellularBaseN2xx::cgmm(const char *response) {
    return at_req("AT+CGMM", "%32[^\n]\n", response);
}

bool UbloxCellularBaseN2xx::cimi(const char *response) {
    return at_req("AT+CIMI", "%32[^\n]\n", response);
}

bool UbloxCellularBaseN2xx::ccid(const char *response) {
    return at_req("AT+NCCID", "+NCCID:%32[^\n]\n", response);
}

bool UbloxCellularBaseN2xx::cgmr(const char *response) {
    return at_req("AT+CGMR", "%32[^\n]\n", response);
}

bool UbloxCellularBaseN2xx::cgsn(int snt, const char *response) {
    char cmd[10];
    sprintf(cmd, "AT+CGSN=%d", snt);
    
    return at_req(cmd, "+CGSN:%32[^\n]\n", response);
}

bool UbloxCellularBaseN2xx::cereg(int n) {        
    return at_send("AT+CEREG=%d", n);  
}

nsapi_error_t UbloxCellularBaseN2xx::get_cereg() {    
    nsapi_error_t r = NSAPI_ERROR_DEVICE_ERROR;    
    LOCK();
    
    MBED_ASSERT(_at != NULL);
    
    // The response will be handled by the CEREG URC, by waiting for the OK we know it has been serviced.
    if (at_send("AT+CEREG?")){ 
        r = _dev_info.reg_status_eps;
    }
    
    UNLOCK();
    return r;
}

nsapi_error_t UbloxCellularBaseN2xx::get_cscon() {
    char resp[3+1];
    
    int n, stat;
        
    if (at_req("AT+CSCON?", "+CSCON:%3[^\n]\n", resp) &&  
        sscanf(resp, "%d,%d", &n, &stat)) {
            return stat;
    }
    
    return NSAPI_ERROR_DEVICE_ERROR;
}

nsapi_error_t UbloxCellularBaseN2xx::get_csq() {
    char resp[5+1];    
    nsapi_error_t rssi = NSAPI_ERROR_DEVICE_ERROR;
        
    LOCK();
    MBED_ASSERT(_at != NULL);
        
    if (at_req("AT+CSQ", "+CSQ:%5[^\n]\n", resp) &&  
        sscanf(resp, "%d,%*d", &rssi)) {
            return rssi;
    }    
    
    UNLOCK();    
    return rssi;
}

bool UbloxCellularBaseN2xx::cops(const char *plmn) {    
    return at_send("AT+COPS=1,2,\"%s\"", plmn);
}

bool UbloxCellularBaseN2xx::cops(int mode) {    
    return at_send("AT+COPS=%d", mode);    
}

bool UbloxCellularBaseN2xx::get_cops(int *status) {    
    return at_req("AT+COPS?", "+COPS: %d", status);
}

bool UbloxCellularBaseN2xx::cfun(int mode) {
    return at_send("AT+CFUN=%d", mode);
}

bool UbloxCellularBaseN2xx::reboot() {
    return at_send("AT+NRB");    
}

bool UbloxCellularBaseN2xx::auto_connect(bool state) {
    return nconfig("AUTOCONNECT", state);
}

bool UbloxCellularBaseN2xx::nconfig(const char *name, bool state) {    
    char n[50];
    
    if (state)
        sprintf(n, "AT+NCONFIG=\"%s\",\"TRUE\"", name);
    else
        sprintf(n, "AT+NCONFIG=\"%s\",\"FALSE\"", name);      
    
    return at_send(n);
}

bool UbloxCellularBaseN2xx::get_imei(char *buffer, int size)
{
    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    bool success = cgsn(1, _dev_info.imei);
    tr_info("DevInfo: IMEI=%s", _dev_info.imei);

    if (success) {
        memcpy(buffer,_dev_info.imei,size);
        buffer[size-1] = '\0';
    }

    return success;
}

bool UbloxCellularBaseN2xx::get_iccid()
{    
    // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card.
    // ICCID is a serial number identifying the SIM.
    // AT Command Manual UBX-13002752, section 4.12
    bool success = ccid(_dev_info.iccid);
    tr_info("DevInfo: ICCID=%s", _dev_info.iccid);
    
    return success;
}

bool UbloxCellularBaseN2xx::get_imsi()
{    
    // International mobile subscriber identification
    // AT Command Manual UBX-13002752, section 4.11
    bool success = cimi(_dev_info.imsi);
    tr_info("DevInfo: IMSI=%s", _dev_info.imsi);

    return success;
}

bool UbloxCellularBaseN2xx::get_imei()
{
    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    bool success = cgsn(1, _dev_info.imei);
    tr_info("DevInfo: IMEI=%s", _dev_info.imei);

    return success;
}

bool UbloxCellularBaseN2xx::get_meid()
{
    // *** NOT IMPLEMENTED on SARA-N2XX
    return false;
}

bool UbloxCellularBaseN2xx::set_sms()
{
    // *** NOT IMPLEMENTED on SARA-N2XX
    return false;
}

void UbloxCellularBaseN2xx::parser_abort_cb()
{
    _at->abort(); 
}

// Callback for CME ERROR and CMS ERROR.
void UbloxCellularBaseN2xx::CMX_ERROR_URC()
{
    char buf[48];

    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        tr_debug("AT error %s", buf);
    }
    parser_abort_cb();
}

// Callback for EPS registration URC.
void UbloxCellularBaseN2xx::CEREG_URC()
{
    char buf[20];
    int status;
    int n, AcT;
    char tac[4], ci[4]; 

    // If this is the URC it will be a single
    // digit followed by \n.  If it is the
    // answer to a CEREG query, it will be
    // a ": %d,%d\n" where the second digit
    // indicates the status
    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    // We also hanlde the extended 4 or 5 argument 
    // response if cereg is set to 2.
    if (read_at_to_newline(buf, sizeof (buf)) > 0) {
        if (sscanf(buf, ":%d,%d,%[0123456789abcdef],%[0123456789abcdef],%d\n", &n, &status, tac, ci, &AcT) == 5) {
            set_nwk_reg_status_eps(status);
        } else if (sscanf(buf, ":%d,%[0123456789abcdef],%[0123456789abcdef],%d\n`", &status, tac, ci, &AcT) == 4) {
            set_nwk_reg_status_eps(status);
        } else if (sscanf(buf, ":%d,%d\n", &n, &status) == 2) {
            set_nwk_reg_status_eps(status);
        } else if (sscanf(buf, ":%d\n", &status) == 1) {
            set_nwk_reg_status_eps(status);
        }        
    }
}

/**********************************************************************
 * PROTECTED METHODS
 **********************************************************************/

#if MODEM_ON_BOARD
void UbloxCellularBaseN2xx::modem_init()
{
    ::onboard_modem_init();
}

void UbloxCellularBaseN2xx::modem_deinit()
{
    ::onboard_modem_deinit();
}

void UbloxCellularBaseN2xx::modem_power_up()
{
    ::onboard_modem_power_up();
}

void UbloxCellularBaseN2xx::modem_power_down()
{
    ::onboard_modem_power_down();
}
#else
void UbloxCellularBaseN2xx::modem_init()
{
    // Meant to be overridden
    #error need to do something here!
}

void UbloxCellularBaseN2xx::modem_deinit()
{
    // Meant to be overridden
}

void UbloxCellularBaseN2xx::modem_power_up()
{
    // Meant to be overridden
}

void UbloxCellularBaseN2xx::modem_power_down()
{
    // Mmeant to be overridden
}
#endif

// Constructor.
// Note: to allow this base class to be inherited as a virtual base class
// by everyone, it takes no parameters.  See also comment above classInit()
// in the header file.
UbloxCellularBaseN2xx::UbloxCellularBaseN2xx()
{  
    tr_debug("UbloxATCellularBaseN2xx Constructor");
    
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
UbloxCellularBaseN2xx::~UbloxCellularBaseN2xx()
{
    deinit();
    delete _at;
    delete _fh;
}

// Initialise the portions of this class that are parameterised.
void UbloxCellularBaseN2xx::baseClassInit(PinName tx, PinName rx,
                                      int baud, bool debug_on)
{
    // Only initialise ourselves if it's not already been done
    if (_at == NULL) {
        if (_debug_trace_on == false) {
            _debug_trace_on = debug_on;
        }                

        // Set up File Handle for buffered serial comms with cellular module
        // (which will be used by the AT parser)
        // Note: the UART is initialised to run no faster than 115200 because
        // the modems cannot reliably auto-baud at faster rates.  The faster
        // rate is adopted later with a specific AT command and the
        // UARTSerial rate is adjusted at that time
        if (baud > 115200) {
            baud = 115200;
        }
        _fh = new UARTSerial(tx, rx, baud);
        
        // Set up the AT parser
        _at = new ATCmdParser(_fh, OUTPUT_ENTER_KEY, AT_PARSER_BUFFER_SIZE,
                           _at_timeout, _debug_trace_on);

        // Error cases, out of band handling
        _at->oob("ERROR", callback(this, &UbloxCellularBaseN2xx::parser_abort_cb));
        _at->oob("+CME ERROR", callback(this, &UbloxCellularBaseN2xx::CMX_ERROR_URC));
        _at->oob("+CMS ERROR", callback(this, &UbloxCellularBaseN2xx::CMX_ERROR_URC));

        // Registration status, out of band handling
        _at->oob("+CEREG", callback(this, &UbloxCellularBaseN2xx::CEREG_URC));
    }
}

// Set the AT parser timeout.
// Note: the AT interface should be locked before this is called.
void UbloxCellularBaseN2xx::at_set_timeout(int timeout) {

    MBED_ASSERT(_at != NULL);

    _at_timeout = timeout;
    _at->set_timeout(timeout);
}

// Read up to size bytes from the AT interface up to a "end".
// Note: the AT interface should be locked before this is called.
int UbloxCellularBaseN2xx::read_at_to_char(char * buf, int size, char end)
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
bool UbloxCellularBaseN2xx::power_up()
{
    bool success = false;
    int at_timeout;
    LOCK();

    at_timeout = _at_timeout; // Has to be inside LOCK()s

    MBED_ASSERT(_at != NULL);

    /* Initialize GPIO lines */
    tr_info("Powering up modem...");
    onboard_modem_init();
    /* Give SARA-N2XX time to reset */
    tr_debug("Waiting for 5 seconds (booting SARA-N2xx)...");
    wait_ms(5000);

    at_set_timeout(1000);
    for (int retry_count = 0; !success && (retry_count < 20); retry_count++) {      
        _at->flush();        
        if (at_send("AT")) {
            success = true;
        }
    }
    at_set_timeout(at_timeout);

    // perform any initialisation AT commands here
    if (success) {        
        success = at_send("AT+CMEE=1"); // Turn on verbose responses
    }

    if (!success) {
        tr_error("Preliminary modem setup failed.");
    }

    UNLOCK();
    return success;
}

// Power down modem via AT interface.
void UbloxCellularBaseN2xx::power_down()
{
    LOCK();

    MBED_ASSERT(_at != NULL);

    // If we have been running, do a soft power-off first
    if (_modem_initialised && (_at != NULL)) {
        // NOT IMPLEMENTED in B656 firmware
        // at_send("AT+CPWROFF");
    }

    // Now do a hard power-off
    onboard_modem_power_down();
    onboard_modem_deinit();

    _dev_info.reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info.reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info.reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;

   UNLOCK();
}

// Get the device ID.
bool UbloxCellularBaseN2xx::set_device_identity(DeviceType *dev)
{
    char buf[20];
    bool success;
    LOCK();

    MBED_ASSERT(_at != NULL);
    
    success = at_req("AT+CGMM", "%19[^\n]\n", buf);

    if (success) {
        if (strstr(buf, "Neul Hi2110"))
            *dev = DEV_SARA_N2;           
    }

    UNLOCK();
    return success;
}

// Send initialisation AT commands that are specific to the device.
bool UbloxCellularBaseN2xx::device_init(DeviceType dev)
{
    // SARA-N2xx doesn't have anything to initialise
    return true;
}

// Get the SIM card going.
bool UbloxCellularBaseN2xx::initialise_sim_card()
{
    // SARA-N2XX doesn't have any SIM AT Commands for now.
    return true;    
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Initialise the modem.
bool UbloxCellularBaseN2xx::init(const char *pin)
{
    MBED_ASSERT(_at != NULL);

    if (!_modem_initialised) {
        tr_warn("Modem not initialised, initialising...");
        if (power_up()) {
            tr_info("Modem Powered Up.");
            if (pin != NULL) {
                _pin = pin;
            }
            
            if (initialise_sim_card()) {
                tr_info("Sim ready...");
                if (set_device_identity(&_dev_info.dev) && // Set up device identity
                    device_init(_dev_info.dev) &&
                    get_sara_n2xx_info()) 
                    {
                        tr_debug("CGMM: %s", _sara_n2xx_info.cgmm); 
                        tr_debug("CGMI: %s", _sara_n2xx_info.cgmi); 
                        tr_debug("CGMR: %s", _sara_n2xx_info.cgmr); 
                        tr_debug("CGSN: %s", _sara_n2xx_info.cgsn); 
                    
                        // The modem is initialised.  The following checks my still fail,
                        // of course, but they are all of a "fatal" nature and so we wouldn't
                        // want to retry them anyway
                        _modem_initialised = true;
                }
            }
        } else {
            tr_error("Couldn't power up modem.");
        }
    }
    else {
        tr_info("Modem already initialised.");
    }

    return _modem_initialised;
}

// Perform registration.
bool UbloxCellularBaseN2xx::nwk_registration(int timeoutSeconds)
{    
    bool registered = false;
    int status;
    int at_timeout;
    LOCK();

    at_timeout = _at_timeout; // Has to be inside LOCK()s

    MBED_ASSERT(_at != NULL);

    // Enable the packet switched and network registration unsolicited result codes
    if (cereg(1)) {        
        // See if we are already in automatic mode
        if (get_cops(&status)) {
            if (status != 0) {
                // Don't check return code here as there's not much
                // we can do if this fails.
                cops(0);
            }
        }
        
        // query cereg just in case
        get_cereg();
        registered = is_registered_eps();
        
        at_set_timeout(1000);
        for (int waitSeconds = 0; !registered && (waitSeconds < timeoutSeconds); waitSeconds++) {
            _at->recv(UNNATURAL_STRING);
            registered = is_registered_eps();        
        }
        at_set_timeout(at_timeout);
    } else {
        tr_error("Failed to set CEREG=1");
    }

    UNLOCK();
    return registered;
}

bool UbloxCellularBaseN2xx::is_registered_csd()
{
  return (_dev_info.reg_status_csd == CSD_REGISTERED) ||
          (_dev_info.reg_status_csd == CSD_REGISTERED_ROAMING) ||
          (_dev_info.reg_status_csd == CSD_CSFB_NOT_PREFERRED);
}

bool UbloxCellularBaseN2xx::is_registered_psd()
{
    return (_dev_info.reg_status_psd == PSD_REGISTERED) ||
            (_dev_info.reg_status_psd == PSD_REGISTERED_ROAMING);
}

bool UbloxCellularBaseN2xx::is_registered_eps()
{
    return (_dev_info.reg_status_eps == EPS_REGISTERED) ||
            (_dev_info.reg_status_eps == EPS_REGISTERED_ROAMING);
}

// Perform deregistration.
bool UbloxCellularBaseN2xx::nwk_deregistration()
{
    bool success = false;
   
    MBED_ASSERT(_at != NULL);

    if (cops(2)) {
        // we need to wait here so that the internal status of the module 
        wait_ms(1000);
        
        _dev_info.reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
        _dev_info.reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
        _dev_info.reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;        
        
        success = true;
    } else {
        tr_error("Failed to set COPS=2");
    }

    return success;
}

// Put the modem into its lowest power state.
void UbloxCellularBaseN2xx::deinit()
{
    power_down();
    _modem_initialised = false;
}

// Set the PIN.
void UbloxCellularBaseN2xx::set_pin(const char *pin) {
    _pin = pin;
}

// Enable or disable SIM pin checking.
bool UbloxCellularBaseN2xx:: sim_pin_check_enable(bool enableNotDisable)
{
    // *** NOT IMPLEMENTED on SARA-N2XX
    return false;
}

// Change the pin code for the SIM card.
bool UbloxCellularBaseN2xx::change_sim_pin(const char *pin)
{
    // *** NOT IMPLEMENTED on SARA-N2XX
    return false;
}

 // Read up to size bytes from the AT interface up to a newline.
 // This doesn't need a LOCK() UNLOCK() Wrapping as it's only called 
 // from the URC function, which are already in a lock
int UbloxCellularBaseN2xx::read_at_to_newline(char * buf, int size)
{
    int count = 0;
    int x = 0;

    if (size > 0) {
        for (count = 0; (count < size) && (x >= 0) && (x != '\n'); count++) {
            x = _at->getc();
            *(buf + count) = (char) x;
        }

        *(buf + count - 1) = 0;
        count--;
    }

    return count;
}

// Get the IMEI of the module.
const char *UbloxCellularBaseN2xx::imei()
{
    return _dev_info.imei;
}
 
// Get the Mobile Equipment ID (which may be the same as the IMEI).
const char *UbloxCellularBaseN2xx::meid()
{
    return _dev_info.meid;
}
 
// Get the IMSI of the SIM.
const char *UbloxCellularBaseN2xx::imsi()
{
    // (try) to update the IMSI, just in case the SIM has changed
    get_imsi();
    
    return _dev_info.imsi;
}
 
// Get the ICCID of the SIM.
const char *UbloxCellularBaseN2xx::iccid()
{
    // (try) to update the ICCID, just in case the SIM has changed
    get_iccid();
    
    return _dev_info.iccid;
}

// Get the RSSI in dBm.
int UbloxCellularBaseN2xx::rssi()
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


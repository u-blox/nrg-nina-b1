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

#include "UbloxATCellularInterfaceN2xx.h"
#include "mbed_poll.h"
#include "nsapi.h"
#include "APN_db.h"
#ifdef FEATURE_COMMON_PAL
#include "mbed_trace.h"
#define TRACE_GROUP "UACI"
#else
#define tr_debug(format, ...) debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#define tr_info(format, ...)  debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#define tr_warn(format, ...)  debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#define tr_error(format, ...) debug_if(_debug_trace_on, format "\n", ## __VA_ARGS__)
#endif

// When calling the SendTo function, the large hex string for the bytes to send is chopped into chunks 
#define SENDTO_CHUNK_SIZE 50

/**********************************************************************
 * PRIVATE METHODS
 **********************************************************************/
 
// Event thread for asynchronous received data handling.
void UbloxATCellularInterfaceN2xx::handle_event(){
    pollfh fhs;
    int count;
    int at_timeout;

    fhs.fh = _fh;
    fhs.events = POLLIN;

    while (_run_event_thread) {
        count = poll(&fhs, 1, 1000);
        if (count > 0 && (fhs.revents & POLLIN)) {
            LOCK();
            at_timeout = _at_timeout;
            at_set_timeout(10); // Avoid blocking but also make sure we don't
                                // time out if we get ahead of the serial port
            _at->debug_on(false); // Debug here screws with the test output
            // Let the URCs run
            _at->recv(UNNATURAL_STRING);
            _at->debug_on(_debug_trace_on);
            at_set_timeout(at_timeout);
            UNLOCK();
        }
    }
}

// Find or create a socket from the list.
UbloxATCellularInterfaceN2xx::SockCtrl * UbloxATCellularInterfaceN2xx::find_socket(int modem_handle)
{
    UbloxATCellularInterfaceN2xx::SockCtrl *socket = NULL;

    for (unsigned int x = 0; (socket == NULL) && (x < sizeof(_sockets) / sizeof(_sockets[0])); x++) {
        if (_sockets[x].modem_handle == modem_handle) {
            socket = &(_sockets[x]);
        }
    }

    return socket;
}

// Clear out the storage for a socket
void UbloxATCellularInterfaceN2xx::clear_socket(UbloxATCellularInterfaceN2xx::SockCtrl * socket)
{
    if (socket != NULL) {
        socket->modem_handle = SOCKET_UNUSED;
        socket->pending     = 0;
        socket->callback    = NULL;
        socket->data        = NULL;
    }
}

// Check that a socket pointer is valid
bool UbloxATCellularInterfaceN2xx::check_socket(SockCtrl * socket)
{
    bool success = false;

    if (socket != NULL) {
        for (unsigned int x = 0; !success && (x < sizeof(_sockets) / sizeof(_sockets[0])); x++) {
            if (socket == &(_sockets[x])) {
                success = true;
            }
        }
    }

    return success;
}

// Callback for Socket Read From URC.
void UbloxATCellularInterfaceN2xx::NSONMI_URC()
{
    int a;
    int b;
    char buf[32];
    SockCtrl *socket;
    
    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    // +UUSORF: <socket>,<length>
    if (read_at_to_newline(buf, sizeof (buf)) > 0) {
        tr_debug("NSONMI URC");
        if (sscanf(buf, ":%d,%d", &a, &b) == 2) {         
            socket = find_socket(a);        
            if (socket != NULL) {
                socket->pending += b;
                tr_debug("Socket 0x%08x: modem handle %d has %d byte(s) pending",
                         (unsigned int) socket, a, socket->pending);
                if (socket->callback != NULL) {
                    tr_debug("***** Calling callback...");
                    socket->callback(socket->data);
                    tr_debug("***** Callback finished");
                } else {
                    tr_debug("No callback found for socket.");
                }
            } else {
                tr_debug("Can't find socket with modem handle %d", a);
            }
        }
    }
}

/**********************************************************************
 * PROTECTED METHODS: GENERAL
 **********************************************************************/

// Get the next set of credentials, based on IMSI.
void UbloxATCellularInterfaceN2xx::get_next_credentials(const char * config)
{
    if (config) {
        _apn    = _APN_GET(config);
        _uname  = _APN_GET(config);
        _pwd    = _APN_GET(config);
    }

    _apn    = _apn     ?  _apn    : "";
    _uname  = _uname   ?  _uname  : "";
    _pwd    = _pwd     ?  _pwd    : "";
}

/**********************************************************************
 * PROTECTED METHODS: NETWORK INTERFACE and SOCKETS
 **********************************************************************/

// Gain access to us.
NetworkStack *UbloxATCellularInterfaceN2xx::get_stack()
{
    return this;
}

// Create a socket.
nsapi_error_t UbloxATCellularInterfaceN2xx::socket_open(nsapi_socket_t *handle,
                                                    nsapi_protocol_t proto)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    int modem_handle =0;
    SockCtrl *socket;
  
    if (proto != NSAPI_UDP) {
        return NSAPI_ERROR_UNSUPPORTED;
    }
   
    LOCK();

    // Find a free socket
    socket = find_socket();
    if (socket != NULL) {  
        tr_debug("socket_open(%d)", proto);    
        if (_at->send("AT+NSOCR=\"DGRAM\",17,%d", _localListenPort) &&            
            _at->recv("%d\n", &modem_handle) && 
            _at->recv("OK")) {
                tr_debug("Socket 0x%8x: handle %d was created", (unsigned int) socket, modem_handle);
                clear_socket(socket);
                socket->modem_handle = modem_handle;
                *handle = (nsapi_socket_t) socket;
                nsapi_error = NSAPI_ERROR_OK;
        } else {
            tr_error("Couldn't open socket using AT command");
        }
    } else {
        tr_error("Can't find a socket to use");
        nsapi_error = NSAPI_ERROR_NO_MEMORY;
    }

    UNLOCK();
    return nsapi_error;
}


// Close a socket.
nsapi_error_t UbloxATCellularInterfaceN2xx::socket_close(nsapi_socket_t handle)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    SockCtrl *socket = (SockCtrl *) handle;
    LOCK();

    tr_debug("socket_close(0x%08x)", (unsigned int) handle);

    MBED_ASSERT (check_socket(socket));

    if (_at->send("AT+NSOCL=%d", socket->modem_handle) &&
        _at->recv("OK")) {
        clear_socket(socket);
        nsapi_error = NSAPI_ERROR_OK;
    } else {
        tr_error("Failed to close socket %d", socket->modem_handle);
    }

    UNLOCK();
    return nsapi_error;
}

// Bind a local port to a socket.
nsapi_error_t UbloxATCellularInterfaceN2xx::socket_bind(nsapi_socket_t handle,
                                                    const SocketAddress &address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

// Connect to a socket
nsapi_error_t UbloxATCellularInterfaceN2xx::socket_connect(nsapi_socket_t handle,
                                                       const SocketAddress &address)
{
return NSAPI_ERROR_UNSUPPORTED;  
}

// Send to a socket.
nsapi_size_or_error_t UbloxATCellularInterfaceN2xx::socket_send(nsapi_socket_t handle,
                                                            const void *data,
                                                            nsapi_size_t size)
{
    return NSAPI_ERROR_UNSUPPORTED;  
}

// Send to an IP address.
nsapi_size_or_error_t UbloxATCellularInterfaceN2xx::socket_sendto(nsapi_socket_t handle,
                                                              const SocketAddress &address,
                                                              const void *data,
                                                              nsapi_size_t size)
{
    nsapi_size_or_error_t nsapi_error_size = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    const char *buf = (const char *) data;
    nsapi_size_t blk = MAX_WRITE_SIZE_N2XX;
    nsapi_size_t count = size;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_sendto(0x%8x, %s(:%d), 0x%08x, %d)", (unsigned int) handle,
             address.get_ip_address(), address.get_port(), (unsigned int) data, size);

    MBED_ASSERT (check_socket(socket));

    tr_debug("Max Write Size for SendTo: %d", MAX_WRITE_SIZE_N2XX);
    if (size > MAX_WRITE_SIZE_N2XX) {
        tr_warn("WARNING: packet length %d is too big for one UDP packet (max %d), will be fragmented.", size, MAX_WRITE_SIZE_N2XX);
    }

    while ((count > 0) && success) {
        if (count < blk) {
            blk = count;
        }
        
        // call the AT Helper function to send the bytes
        tr_debug("Sending %d bytes....", blk);
        int sent = sendto(socket, address, buf, blk);
        if (sent < 0) {
            tr_error("Something went wrong! %d", sent);
            return NSAPI_ERROR_DEVICE_ERROR;
        }
        
        buf += sent;
        count -= sent;
    }

    if (success) {
        nsapi_error_size = size - count;
        if (_debug_trace_on) {
            tr_debug("socket_sendto: %d \"%*.*s\"", size, size, size, (char *) data);
        }
    }

    return nsapi_error_size;
}

nsapi_size_or_error_t UbloxATCellularInterfaceN2xx::sendto(SockCtrl *socket, const SocketAddress &address, const char *buf, int size) {
    nsapi_size_or_error_t sent = NSAPI_ERROR_DEVICE_ERROR;
    int id;
        
    char *dataStr = (char *) malloc((size * 2) + 1);
    if (dataStr == NULL) {
        tr_error("Couldn't allocate memory for hex string conversion.");
        return NSAPI_ERROR_NO_MEMORY;
    }
    
    tr_debug("Converting byte array to hex string");
    bin_to_hex(buf, size, dataStr);
    tr_debug("Got hex string");
    
    // AT+NSOSTF= socket, remote_addr, remote_port, length, data
    tr_debug("Writing AT+NSOSTF=<sktid>,<ipaddr>,<port>,<flags>,<size>,<hex string> command...");    
    char *cmdStr = (char *) malloc(50);
    if (cmdStr == NULL) {
        tr_error("Couldn't allocate memory for AT cmd string.");
        return NSAPI_ERROR_NO_MEMORY;
    }
            
    int cmdsize = sprintf(cmdStr, "AT+NSOSTF=%d,\"%s\",%d,%s,%d,\"", socket->modem_handle, address.get_ip_address(), address.get_port(), _sendFlags, size);    
    tr_debug("%s", cmdStr);
    
    LOCK();
    if (_at->write(cmdStr, cmdsize) && sendATChopped(dataStr)) 
    {
        tr_debug("Finished sending AT+NSOST comamnd, reading back the 'sent' size...");
        if (_at->recv("%d,%d\n", &id, &sent) && _at->recv("OK")) {            
            tr_debug("Sent %d bytes on socket %d", sent, id);
        } else {
            tr_error("Didn't get the Sent size or OK");
        }
    } else {
        tr_error("Didn't send the AT command!");
    }
    UNLOCK();  

    free(cmdStr);    
    free(dataStr);
       
    return sent;
}

bool UbloxATCellularInterfaceN2xx::sendATChopped(const char *cmd)
{
    char buff[SENDTO_CHUNK_SIZE];
    tr_debug("Chopping up large AT text of %d characters.", strlen(cmd));
    
    while (*cmd != '\0')
    {
        int i=0;
        
        for (i=0; i<SENDTO_CHUNK_SIZE; i++) {
            buff[i] = *cmd;
            
            // if we have copied the NULL terminator, we can exit 
            if (*cmd == '\0')  
                break;
            
            // still more characters to copy, so move along
            cmd++;
        }
       
       // if we are at the end of the line, use the send command to 
       // provide the \r\n terminator for the AT command
        if (*cmd == '\0') {
            tr_debug("send(%d): %s", i, buff);            
            // write the last buffer...
            if (!_at->write(buff,i))
                return false;
            
            // ...send the enclosing quote to complete the AT command
            if (!_at->send("\""))
                return false;
        } else {
            tr_debug("write(50): %s", buff);
            if (!_at->write(buff, 50))
                return false;
        }
     }
     
     return true;
}

void UbloxATCellularInterfaceN2xx::bin_to_hex(const char *buff, unsigned int length, char *output)
{
    char binHex[] = "0123456789ABCDEF";
    
    *output = '\0';

    for (; length > 0; --length)
    {
        unsigned char byte = *buff++;

        *output++ = binHex[(byte >> 4) & 0x0F];
        *output++ = binHex[byte & 0x0F];
    }
    
    *output++ = '\0';
}

// Receive from a socket, TCP style.
nsapi_size_or_error_t UbloxATCellularInterfaceN2xx::socket_recv(nsapi_socket_t handle,
                                                            void *data,
                                                            nsapi_size_t size)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

// Receive a packet over a UDP socket.
nsapi_size_or_error_t UbloxATCellularInterfaceN2xx::socket_recvfrom(nsapi_socket_t handle,
                                                                SocketAddress *address,
                                                                void *data,
                                                                nsapi_size_t size)
{
    nsapi_size_or_error_t nsapi_error_size = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    char *buf = (char *) data;
    nsapi_size_t read_blk;
    nsapi_size_t count = 0;
    int at_timeout = _at_timeout;
        
    char * tmpBuf = NULL;

    Timer timer;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_recvfrom(0x%08x, 0x%08x, SIZE=%d)", (unsigned int) handle, (unsigned int) data, size);

    MBED_ASSERT (check_socket(socket));
    timer.start();

    while (success && (size > 0)) {
        LOCK();
        at_timeout = _at_timeout;
        at_set_timeout(1000);
        
        read_blk = MAX_READ_SIZE_N2XX;
        if (read_blk > size) {
            read_blk = size;
        }
        
        if (socket->pending > 0) {
            tr_debug("Socket 0x%08x: modem handle %d has %d byte(s) pending",
                     (unsigned int) socket, socket->modem_handle, socket->pending);
    
            tmpBuf = (char *) malloc(read_blk);
            if (tmpBuf == NULL) {
                return NSAPI_ERROR_NO_MEMORY;
            }
            
            // call the AT helper function to get the bytes
            nsapi_error_size = receivefrom(socket->modem_handle, address, read_blk, tmpBuf);
                    
            if (nsapi_error_size >= 0) {
                memcpy(buf, tmpBuf, nsapi_error_size);

                if (read_blk != (uint32_t) nsapi_error_size)
                    tr_debug("Requested size is not the same as the returned size.");
                
                socket->pending -= nsapi_error_size;                
                count += nsapi_error_size;
                buf += nsapi_error_size;
                size -= nsapi_error_size;
                
                if (((uint32_t) nsapi_error_size < read_blk) || (nsapi_error_size == MAX_READ_SIZE_N2XX))
                    size = 0;     // If we've received less than we asked for, or
                                  // the max size, then a whole UDP packet has arrived and
                                  // this means DONE.
            } else {
                // Should never fail to read when there is pending data
                success = false;
            }
            
            free(tmpBuf);
            
        } else if (timer.read_ms() < SOCKET_TIMEOUT) {
            // Wait for URCs
            tr_debug("Waiting for URC...");
            _at->recv(UNNATURAL_STRING);
        } else {
            tr_debug("Nothing pending...");
            if (count == 0) {
                tr_debug("Nothing received, so timeout with block");
                // Timeout with nothing received
                nsapi_error_size = NSAPI_ERROR_WOULD_BLOCK;
                success = false;
            }
            size = 0; // This simply to cause an exit
        }
        
        at_set_timeout(at_timeout);
        UNLOCK();
    }
    timer.stop();

    if (success) {
        tr_debug("socket_recvfrom: %d SUCCESS!", count);
        nsapi_error_size = count;
    } else {
       tr_debug("socket_recvfrom: FAILED");
    }
    
    return nsapi_error_size;
}

nsapi_size_or_error_t UbloxATCellularInterfaceN2xx::receivefrom(int modem_handle, SocketAddress *address, int length, char *buf) {
    char ipAddress[NSAPI_IP_SIZE];
    nsapi_size_or_error_t size;

    memset (ipAddress, 0, sizeof (ipAddress)); // Ensure terminator
    
    if (length > MAX_READ_SIZE_N2XX) {
        return NSAPI_ERROR_UNSUPPORTED;
    }
    
    char * tmpBuf = (char *) malloc(length*2);
    if (tmpBuf == NULL)
        return NSAPI_ERROR_NO_MEMORY;
    
    int remaining;
    
    _at->debug_on(false); // ABSOLUTELY no time for debug here if you want to
                      // be able to read packets of any size without
                      // losing characters in UARTSerial
    
    // Ask for x bytes from Socket 
    tr_debug("Requesting to read back %d bytes from socket %d", length, modem_handle);
    if (_at->send("AT+NSORF=%d,%d", modem_handle, length)) {
        unsigned int id, port;
        
        // ReadFrom header, to get length - if no data then this will time out
        if (_at->recv("%d,\"%15[^\"]\",%d,%d,", &id, ipAddress, &port, &size)) {
            tr_debug("Socket RecvFrom: #%d: %d", id, size);
                
            address->set_ip_address(ipAddress);
            address->set_port(port);
            
            // read the beginning quote for this data
            _at->read(tmpBuf, 1);
            
            // now read hex data
            if (_at->read(tmpBuf, size*2) == size*2) {
                
                // convert to bytes
                hex_to_bin(tmpBuf, buf, size);
             
                // read the "remaining" value - remembing there is an enclosing quote at the beginning of this read
                if (!_at->recv("\",%d\n", &remaining)) {
                    tr_error("Failed reading the 'remaining' value after the received data.");
                    size = NSAPI_ERROR_DEVICE_ERROR;
                }
            }
        }
        
        // we should get the OK (even if there is no data to read)
        if (_at->recv("OK")) {          
            tr_debug("Socket RecvFrom: Read %d bytes, %d bytes remaining.", size, remaining);
        } else {
            tr_error("Socket RecvFrom: Didn't receive OK from AT+NSORF command.");
            size = NSAPI_ERROR_DEVICE_ERROR;
        }
    }
    
    _at->debug_on(_debug_trace_on);
    free(tmpBuf);
    
    return size;
}

char UbloxATCellularInterfaceN2xx::hex_char(char c)
{
    if ('0' <= c && c <= '9') return (unsigned char)(c - '0');
    if ('A' <= c && c <= 'F') return (unsigned char)(c - 'A' + 10);
    if ('a' <= c && c <= 'f') return (unsigned char)(c - 'a' + 10);
    return 0xFF;
}

int UbloxATCellularInterfaceN2xx::hex_to_bin(const char* s, char * buff, int length)
{
    int result;
    if (!s || !buff || length <= 0) return -1;

    for (result = 0; *s; ++result)
    {
        unsigned char msn = hex_char(*s++);
        if (msn == 0xFF) return -1;
        unsigned char lsn = hex_char(*s++);
        if (lsn == 0xFF) return -1;
        unsigned char bin = (msn << 4) + lsn;

        if (length-- <= 0) return -1;
        *buff++ = bin;
    }
    return result;
}

// Attach an event callback to a socket, required for asynchronous
// data reception
void UbloxATCellularInterfaceN2xx::socket_attach(nsapi_socket_t handle,
                                             void (*callback)(void *),
                                             void *data)
{
    SockCtrl *socket = (SockCtrl *) handle;

    MBED_ASSERT (check_socket(socket));

    socket->callback = callback;
    socket->data = data;
}

// Unsupported TCP server functions.
nsapi_error_t UbloxATCellularInterfaceN2xx::socket_listen(nsapi_socket_t handle,
                                                      int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
nsapi_error_t UbloxATCellularInterfaceN2xx::socket_accept(nsapi_socket_t server,
                                                      nsapi_socket_t *handle,
                                                      SocketAddress *address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

// Unsupported option functions.
nsapi_error_t UbloxATCellularInterfaceN2xx::setsockopt(nsapi_socket_t handle,
                                                   int level, int optname,
                                                   const void *optval,
                                                   unsigned optlen)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
nsapi_error_t UbloxATCellularInterfaceN2xx::getsockopt(nsapi_socket_t handle,
                                                   int level, int optname,
                                                   void *optval,
                                                   unsigned *optlen)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Constructor.
UbloxATCellularInterfaceN2xx::UbloxATCellularInterfaceN2xx(PinName tx,
                                                   PinName rx,
                                                   int baud,
                                                   bool debug_on)
{
    _sim_pin_check_change_pending = false;
    _sim_pin_check_change_pending_enabled_value = false;
    _sim_pin_change_pending = false;
    _sim_pin_change_pending_new_pin_value = NULL;
    _run_event_thread = true;
    _apn = NULL;
    _uname = NULL;
    _pwd = NULL;
    _connection_status_cb = NULL;
    
    _localListenPort = 10000;
    _network_search_timeout_seconds = 180;
    set_release_assistance(false);
    
    tr_debug("UbloxATCellularInterfaceN2xx Constructor");

    // Initialise sockets storage
    memset(_sockets, 0, sizeof(_sockets));
    for (unsigned int socket = 0; socket < sizeof(_sockets) / sizeof(_sockets[0]); socket++) {
        _sockets[socket].modem_handle = SOCKET_UNUSED;
        _sockets[socket].callback = NULL;
        _sockets[socket].data = NULL;
    }

    // The authentication to use
    _auth = NSAPI_SECURITY_UNKNOWN;

    // Nullify the temporary IP address storage
    _ip = NULL;

    // Initialise the base class, which starts the AT parser
    baseClassInit(tx, rx, baud, debug_on);

    // Start the event handler thread for Rx data
    event_thread.start(callback(this, &UbloxATCellularInterfaceN2xx::handle_event));

    // URC handlers for sockets
    _at->oob("+NSONMI", callback(this, &UbloxATCellularInterfaceN2xx::NSONMI_URC));
}

// Destructor.
UbloxATCellularInterfaceN2xx::~UbloxATCellularInterfaceN2xx()
{
    // Let the event thread shut down tidily
    _run_event_thread = false;
    event_thread.join();

    // Free _ip if it was ever allocated
    free(_ip);
}

// Set the authentication scheme.
void UbloxATCellularInterfaceN2xx::set_authentication(nsapi_security_t auth)
{
    _auth = auth;
}

// Set APN, user name and password.
void  UbloxATCellularInterfaceN2xx::set_credentials(const char *apn,
                                                const char *uname,
                                                const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
}

// Set PIN.
void UbloxATCellularInterfaceN2xx::set_sim_pin(const char *pin) {
    set_pin(pin);
}

// Set the network search timeout.
void UbloxATCellularInterfaceN2xx::set_network_search_timeout(int timeout_seconds) {
    _network_search_timeout_seconds = timeout_seconds;
}

// Set release assistance.
void UbloxATCellularInterfaceN2xx::set_release_assistance(bool isOn) {
    if (isOn) {
        _sendFlags = "0x200";
    } else {
        _sendFlags = "0x0";
    }
}

// Get the IP address of a host.
nsapi_error_t UbloxATCellularInterfaceN2xx::gethostbyname(const char *host,
                                                      SocketAddress *address,
                                                      nsapi_version_t version)
{
    nsapi_error_t nsapiError = NSAPI_ERROR_DEVICE_ERROR;
    tr_debug("GetHostByName: host= %s", host);
    if (address->set_ip_address(host)) {
        tr_debug("OK");
        nsapiError = NSAPI_ERROR_OK;
    } else {
        tr_debug("Failed");
        nsapiError = NSAPI_ERROR_UNSUPPORTED;
    }

    return nsapiError;
}

// Make a cellular connection
nsapi_error_t UbloxATCellularInterfaceN2xx::connect(const char *sim_pin,
                                                const char *apn,
                                                const char *uname,
                                                const char *pwd)
{
    nsapi_error_t nsapi_error;

    if (sim_pin != NULL) {
        _pin = sim_pin;
    }

    if (apn != NULL) {
        _apn = apn;
    }

    if ((uname != NULL) && (pwd != NULL)) {
        _uname = uname;
        _pwd = pwd;
    } else {
        _uname = NULL;
        _pwd = NULL;
    }

    tr_debug("SIM, APN, UName & pwd set, now calling connect()");
    nsapi_error = connect();

    return nsapi_error;
}

bool UbloxATCellularInterfaceN2xx::initialise()
{
    return init();
}

// Make a cellular connection using the IP stack on board the cellular modem
nsapi_error_t UbloxATCellularInterfaceN2xx::connect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    bool registered = false;

    // Set up modem and then register with the network
    if (initialise()) {
        
        tr_debug("Trying to register...");
        nsapi_error = NSAPI_ERROR_NO_CONNECTION;    
        //for (int retries = 0; !registered && (retries < 3); retries++) {
            if (nwk_registration(_network_search_timeout_seconds)) {
                registered = true;
            }
        //}
    }

    // Attempt to establish a connection
    if (registered) {
        nsapi_error = NSAPI_ERROR_OK;
    } else {
        tr_debug("Failed to register.");
    }

    return nsapi_error;
}

// User initiated disconnect.
nsapi_error_t UbloxATCellularInterfaceN2xx::disconnect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;

    if (nwk_deregistration()) {
        nsapi_error = NSAPI_ERROR_OK;
        
        if (_connection_status_cb) {
            _connection_status_cb(NSAPI_ERROR_CONNECTION_LOST);
        }
    }

    return nsapi_error;
}

// Enable or disable SIM PIN check lock.
nsapi_error_t UbloxATCellularInterfaceN2xx::set_sim_pin_check(bool set,
                                                          bool immediate,
                                                          const char *sim_pin)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

// Change the PIN code for the SIM card.
nsapi_error_t UbloxATCellularInterfaceN2xx::set_new_sim_pin(const char *new_pin,
                                                        bool immediate,
                                                        const char *old_pin)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

// Determine if the connection is up.
bool UbloxATCellularInterfaceN2xx::is_connected()
{
    return get_ip_address() != NULL;
}

// Get the IP address of the on-board modem IP stack.
const char * UbloxATCellularInterfaceN2xx::get_ip_address()
{
    SocketAddress address;
    int id;
    LOCK();

    if (_ip == NULL)
    {
        // Temporary storage for an IP address string with terminator
        _ip = (char *) malloc(NSAPI_IP_SIZE);
    }
    
    if (_ip != NULL) {                        
        memset(_ip, 0, NSAPI_IP_SIZE); // Ensure a terminator
        // +CGPADDR - returns a list of IP Addresses per context, just pick the first one as SARA-N2xx only allows 1 context.
        if (!_at->send("AT+CGPADDR") ||
            !_at->recv("+CGPADDR:%d,%15[^\n]\n", &id, _ip) ||
            ! _at->recv("OK") ||
            !address.set_ip_address(_ip) ||
            !address) {            
                free (_ip);
                _ip = NULL;
        }
    }

    UNLOCK();
    return _ip;
}

// Get the local network mask.
const char *UbloxATCellularInterfaceN2xx::get_netmask()
{
    // Not implemented.
    return NULL;
}

// Get the local gateways.
const char *UbloxATCellularInterfaceN2xx::get_gateway()
{
    return get_ip_address();
}

void UbloxATCellularInterfaceN2xx::set_LocalListenPort(int port) {
    _localListenPort = port;
}

// Callback in case the connection is lost.
void UbloxATCellularInterfaceN2xx::connection_status_cb(Callback<void(nsapi_error_t)> cb)
{
    _connection_status_cb = cb;
}

// End of file


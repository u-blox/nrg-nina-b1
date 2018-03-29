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

#include "UbloxATCellularInterface.h"
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

/**********************************************************************
 * PRIVATE METHODS
 **********************************************************************/

// Event thread for asynchronous received data handling.
void UbloxATCellularInterface::handle_event(){
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
UbloxATCellularInterface::SockCtrl * UbloxATCellularInterface::find_socket(int modem_handle)
{
    UbloxATCellularInterface::SockCtrl *socket = NULL;

    for (unsigned int x = 0; (socket == NULL) && (x < sizeof(_sockets) / sizeof(_sockets[0])); x++) {
        if (_sockets[x].modem_handle == modem_handle) {
            socket = &(_sockets[x]);
        }
    }

    return socket;
}

// Clear out the storage for a socket
void UbloxATCellularInterface::clear_socket(UbloxATCellularInterface::SockCtrl * socket)
{
    if (socket != NULL) {
        socket->modem_handle = SOCKET_UNUSED;
        socket->pending     = 0;
        socket->callback    = NULL;
        socket->data        = NULL;
    }
}

// Check that a socket pointer is valid
bool UbloxATCellularInterface::check_socket(SockCtrl * socket)
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

// Convert nsapi_security_t to the modem security numbers
int UbloxATCellularInterface::nsapi_security_to_modem_security(nsapi_security_t nsapi_security)
{
    int modem_security = 3;

    switch (nsapi_security)
    {
        case NSAPI_SECURITY_NONE:
            modem_security = 0;
            break;
        case NSAPI_SECURITY_PAP:
            modem_security = 1;
            break;
        case NSAPI_SECURITY_CHAP:
            modem_security = 2;
            break;
        case NSAPI_SECURITY_UNKNOWN:
            modem_security = 3;
            break;
        default:
            modem_security = 3;
            break;
    }

    return modem_security;
}

// Callback for Socket Read URC.
void UbloxATCellularInterface::UUSORD_URC()
{
    int a;
    int b;
    char buf[32];
    SockCtrl *socket;

    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    // +UUSORD: <socket>,<length>
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %d,%d", &a, &b) == 2) {
            socket = find_socket(a);
            if (socket != NULL) {
                socket->pending = b;
                // No debug prints here as they can affect timing
                // and cause data loss in UARTSerial
                if (socket->callback != NULL) {
                    socket->callback(socket->data);
                }
            }
        }
    }
}

// Callback for Socket Read From URC.
void UbloxATCellularInterface::UUSORF_URC()
{
    int a;
    int b;
    char buf[32];
    SockCtrl *socket;

    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    // +UUSORF: <socket>,<length>
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %d,%d", &a, &b) == 2) {
            socket = find_socket(a);
            if (socket != NULL) {
                socket->pending = b;
                // No debug prints here as they can affect timing
                // and cause data loss in UARTSerial
                if (socket->callback != NULL) {
                    socket->callback(socket->data);
                }
            }
        }
    }
}

// Callback for Socket Close URC.
void UbloxATCellularInterface::UUSOCL_URC()
{
    int a;
    char buf[32];
    SockCtrl *socket;

    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    // +UUSOCL: <socket>
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %d", &a) == 1) {
            socket = find_socket(a);
            tr_debug("Socket 0x%08x: handle %d closed by remote host",
                     (unsigned int) socket, a);
            clear_socket(socket);
        }
    }
}

// Callback for UUPSDD.
void UbloxATCellularInterface::UUPSDD_URC()
{
    int a;
    char buf[32];
    SockCtrl *socket;

    // Note: not calling _at->recv() from here as we're
    // already in an _at->recv()
    // +UUPSDD: <socket>
    if (read_at_to_char(buf, sizeof (buf), '\n') > 0) {
        if (sscanf(buf, ": %d", &a) == 1) {
            socket = find_socket(a);
            tr_debug("Socket 0x%08x: handle %d connection lost",
                     (unsigned int) socket, a);
            clear_socket(socket);
            if (_connection_status_cb) {
                _connection_status_cb(NSAPI_ERROR_CONNECTION_LOST);
            }
        }
    }
}

/**********************************************************************
 * PROTECTED METHODS: GENERAL
 **********************************************************************/

// Get the next set of credentials, based on IMSI.
void UbloxATCellularInterface::get_next_credentials(const char ** config)
{
    if (*config) {
        _apn    = _APN_GET(*config);
        _uname  = _APN_GET(*config);
        _pwd    = _APN_GET(*config);
    }

    _apn    = _apn     ?  _apn    : "";
    _uname  = _uname   ?  _uname  : "";
    _pwd    = _pwd     ?  _pwd    : "";
}

// Active a connection profile on board the modem.
// Note: the AT interface should be locked before this is called.
bool UbloxATCellularInterface::activate_profile(const char* apn,
                                                const char* username,
                                                const char* password,
                                                nsapi_security_t auth)
{
    bool activated = false;
    bool success = false;
    int at_timeout = _at_timeout;
    SocketAddress address;

    // Set up the APN
    if (*apn) {
        success = _at->send("AT+UPSD=" PROFILE ",1,\"%s\"", apn) && _at->recv("OK");
    }
    if (success && *username) {
        success = _at->send("AT+UPSD=" PROFILE ",2,\"%s\"", username) && _at->recv("OK");
    }
    if (success && *password) {
        success = _at->send("AT+UPSD=" PROFILE ",3,\"%s\"", password) && _at->recv("OK");
    }

    if (success) {
        // Set up dynamic IP address assignment.
        success = _at->send("AT+UPSD=" PROFILE ",7,\"0.0.0.0\"") && _at->recv("OK");
        // Set up the authentication protocol
        // 0 = none
        // 1 = PAP (Password Authentication Protocol)
        // 2 = CHAP (Challenge Handshake Authentication Protocol)
        for (int protocol = nsapi_security_to_modem_security(NSAPI_SECURITY_NONE);
             success && (protocol <= nsapi_security_to_modem_security(NSAPI_SECURITY_CHAP)); protocol++) {
            if ((_auth == NSAPI_SECURITY_UNKNOWN) || (nsapi_security_to_modem_security(_auth) == protocol)) {
                if (_at->send("AT+UPSD=" PROFILE ",6,%d", protocol) && _at->recv("OK")) {
                    // Activate, waiting 30 seconds for the connection to be made
                    at_set_timeout(30000);
                    activated = _at->send("AT+UPSDA=" PROFILE ",3") && _at->recv("OK");
                    at_set_timeout(at_timeout);
                }
            }
        }
    }

    return activated;
}

// Activate a profile by reusing an external PDP context.
// Note: the AT interface should be locked before this is called.
bool UbloxATCellularInterface::activate_profile_reuse_external(void)
{
    bool success = false;
    int cid = -1;
    char ip[NSAPI_IP_SIZE];
    SocketAddress address;
    int t;
    int at_timeout = _at_timeout;

    //+CGDCONT: <cid>,"IP","<apn name>","<ip adr>",0,0,0,0,0,0
    if (_at->send("AT+CGDCONT?")) {
        if (_at->recv("+CGDCONT: %d,\"IP\",\"%*[^\"]\",\"%" u_stringify(NSAPI_IP_SIZE) "[^\"]\",%*d,%*d,%*d,%*d,%*d,%*d",
                      &t, ip) &&
            _at->recv("OK")) {
            // Check if the IP address is valid
            if (address.set_ip_address(ip)) {
                cid = t;
            }
        }
    }

    // If a context has been found, use it
    if ((cid != -1) && (_at->send("AT+UPSD=" PROFILE ",100,%d", cid) && _at->recv("OK"))) {
        // Activate, waiting 30 seconds for the connection to be made
        at_set_timeout(30000);
        success = _at->send("AT+UPSDA=" PROFILE ",3") && _at->recv("OK");
        at_set_timeout(at_timeout);
    }

    return success;
}

// Activate a profile by context ID.
// Note: the AT interface should be locked before this is called.
bool UbloxATCellularInterface::activate_profile_by_cid(int cid,
                                                       const char* apn,
                                                       const char* username,
                                                       const char* password,
                                                       nsapi_security_t auth)
{
    bool success = false;
    int at_timeout = _at_timeout;

    if (_at->send("AT+CGDCONT=%d,\"IP\",\"%s\"", cid, apn) && _at->recv("OK") &&
        _at->send("AT+UAUTHREQ=%d,%d,\"%s\",\"%s\"", cid, nsapi_security_to_modem_security(auth),
                  username, password) && _at->recv("OK") &&
        _at->send("AT+UPSD=" PROFILE ",100,%d", cid) && _at->recv("OK")) {

        // Wait 30 seconds for the connection to be made
        at_set_timeout(30000);
        // Activate the protocol
        success = _at->send("AT+UPSDA=" PROFILE ",3") && _at->recv("OK");
        at_set_timeout(at_timeout);
    }

    return success;
}

// Connect the on board IP stack of the modem.
bool UbloxATCellularInterface::connect_modem_stack()
{
    bool success = false;
    int active = 0;
    const char * config = NULL;
    LOCK();

    // Check the profile
    if (_at->send("AT+UPSND=" PROFILE ",8") && _at->recv("+UPSND: %*d,%*d,%d\n", &active) &&
        _at->recv("OK")) {
        if (active == 0) {
            // If the caller hasn't entered an APN, try to find it
            if (_apn == NULL) {
                config = apnconfig(_dev_info.imsi);
            }

            // Attempt to connect
            do {
                // Set up APN and IP protocol for PDP context
                get_next_credentials(&config);
                _auth = (*_uname && *_pwd) ? _auth : NSAPI_SECURITY_NONE;
                if ((_dev_info.dev != DEV_TOBY_L2) && (_dev_info.dev != DEV_MPCI_L2)) {
                    success = activate_profile(_apn, _uname, _pwd, _auth);
                } else {
                    success = activate_profile_reuse_external();
                    if (success) {
                        tr_debug("Reusing external context");
                    } else {
                        success = activate_profile_by_cid(1, _apn, _uname, _pwd, _auth);
                    }
                }
            } while (!success && config && *config);
        } else {
            // If the profile is already active, we're good
            success = true;
        }
    }

    if (!success) {
        tr_error("Failed to connect, check your APN/username/password");
    }

    UNLOCK();
    return success;
}

// Disconnect the on board IP stack of the modem.
bool UbloxATCellularInterface::disconnect_modem_stack()
{
    bool success = false;
    LOCK();

    if (get_ip_address() != NULL) {
        if (_at->send("AT+UPSDA=" PROFILE ",4") && _at->recv("OK")) {
            success = true;
            if (_connection_status_cb) {
                _connection_status_cb(NSAPI_ERROR_CONNECTION_LOST);
            }
        }
    }

    UNLOCK();
    return success;
}

/**********************************************************************
 * PROTECTED METHODS: NETWORK INTERFACE and SOCKETS
 **********************************************************************/

// Gain access to us.
NetworkStack *UbloxATCellularInterface::get_stack()
{
    return this;
}

// Create a socket.
nsapi_error_t UbloxATCellularInterface::socket_open(nsapi_socket_t *handle,
                                                    nsapi_protocol_t proto)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    bool success = false;
    int modem_handle;
    SockCtrl *socket;
    LOCK();

    // Find a free socket
    socket = find_socket();
    tr_debug("socket_open(%d)", proto);

    if (socket != NULL) {
        if (proto == NSAPI_UDP) {
            success = _at->send("AT+USOCR=17");
        } else if (proto == NSAPI_TCP) {
            success = _at->send("AT+USOCR=6");
        } else  {
            nsapi_error = NSAPI_ERROR_UNSUPPORTED;
        }

        if (success) {
            nsapi_error = NSAPI_ERROR_NO_SOCKET;
            if (_at->recv("+USOCR: %d\n", &modem_handle) && (modem_handle != SOCKET_UNUSED) &&
                _at->recv("OK")) {
                tr_debug("Socket 0x%8x: handle %d was created", (unsigned int) socket, modem_handle);
                clear_socket(socket);
                socket->modem_handle         = modem_handle;
                *handle = (nsapi_socket_t) socket;
                nsapi_error = NSAPI_ERROR_OK;
            }
        }
    } else {
        nsapi_error = NSAPI_ERROR_NO_MEMORY;
    }

    UNLOCK();
    return nsapi_error;
}

// Close a socket.
nsapi_error_t UbloxATCellularInterface::socket_close(nsapi_socket_t handle)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    SockCtrl *socket = (SockCtrl *) handle;
    LOCK();

    tr_debug("socket_close(0x%08x)", (unsigned int) handle);

    MBED_ASSERT (check_socket(socket));

    if (_at->send("AT+USOCL=%d", socket->modem_handle) &&
        _at->recv("OK")) {
        clear_socket(socket);
        nsapi_error = NSAPI_ERROR_OK;
    }

    UNLOCK();
    return nsapi_error;
}

// Bind a local port to a socket.
nsapi_error_t UbloxATCellularInterface::socket_bind(nsapi_socket_t handle,
                                                    const SocketAddress &address)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_NO_SOCKET;
    int proto;
    int modem_handle;
    SockCtrl savedSocket;
    SockCtrl *socket = (SockCtrl *) handle;
    LOCK();

    tr_debug("socket_bind(0x%08x, :%d)", (unsigned int) handle, address.get_port());

    MBED_ASSERT (check_socket(socket));

    // Query the socket type
    if (_at->send("AT+USOCTL=%d,0", socket->modem_handle) &&
        _at->recv("+USOCTL: %*d,0,%d\n", &proto) &&
        _at->recv("OK")) {
        savedSocket = *socket;
        nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
        // Now close the socket and re-open it with the binding given
        if (_at->send("AT+USOCL=%d", socket->modem_handle) &&
            _at->recv("OK")) {
            clear_socket(socket);
            nsapi_error = NSAPI_ERROR_CONNECTION_LOST;
            if (_at->send("AT+USOCR=%d,%d", proto, address.get_port()) &&
                _at->recv("+USOCR: %d\n", &modem_handle) && (modem_handle != SOCKET_UNUSED) &&
                _at->recv("OK")) {
                *socket = savedSocket;
                nsapi_error = NSAPI_ERROR_OK;
            }
        }
    }

    UNLOCK();
    return nsapi_error;
}

// Connect to a socket
nsapi_error_t UbloxATCellularInterface::socket_connect(nsapi_socket_t handle,
                                                       const SocketAddress &address)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    SockCtrl *socket = (SockCtrl *) handle;
    LOCK();

    tr_debug("socket_connect(0x%08x, %s(:%d))", (unsigned int) handle,
             address.get_ip_address(), address.get_port());

    MBED_ASSERT (check_socket(socket));

    if (_at->send("AT+USOCO=%d,\"%s\",%d", socket->modem_handle,
                  address.get_ip_address(), address.get_port()) &&
        _at->recv("OK")) {
        nsapi_error = NSAPI_ERROR_OK;
    }

    UNLOCK();
    return nsapi_error;
}

// Send to a socket.
nsapi_size_or_error_t UbloxATCellularInterface::socket_send(nsapi_socket_t handle,
                                                            const void *data,
                                                            nsapi_size_t size)
{
    nsapi_size_or_error_t nsapi_error_size = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    const char *buf = (const char *) data;
    nsapi_size_t blk = MAX_WRITE_SIZE;
    nsapi_size_t count = size;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_send(0x%08x, 0x%08x, %d)", (unsigned int) handle, (unsigned int) data, size);

    MBED_ASSERT (check_socket(socket));

    if (socket->modem_handle == SOCKET_UNUSED) {
        tr_debug("socket_send: socket closed");
        return NSAPI_ERROR_NO_SOCKET;
    }

    while ((count > 0) && success) {
        if (count < blk) {
            blk = count;
        }
        LOCK();

        if (_at->send("AT+USOWR=%d,%d", socket->modem_handle, blk) && _at->recv("@")) {
            wait_ms(50);
            if ((_at->write(buf, blk) < (int) blk) ||
                 !_at->recv("OK")) {
                success = false;
            }
        } else {
            success = false;
        }

        UNLOCK();
        buf += blk;
        count -= blk;
    }

    if (success) {
        nsapi_error_size = size - count;
        if (_debug_trace_on) {
            tr_debug("socket_send: %d \"%*.*s\"", size, size, size, (char *) data);
        }
    }

    return nsapi_error_size;
}

// Send to an IP address.
nsapi_size_or_error_t UbloxATCellularInterface::socket_sendto(nsapi_socket_t handle,
                                                              const SocketAddress &address,
                                                              const void *data,
                                                              nsapi_size_t size)
{
    nsapi_size_or_error_t nsapi_error_size = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    const char *buf = (const char *) data;
    nsapi_size_t blk = MAX_WRITE_SIZE;
    nsapi_size_t count = size;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_sendto(0x%8x, %s(:%d), 0x%08x, %d)", (unsigned int) handle,
             address.get_ip_address(), address.get_port(), (unsigned int) data, size);

    MBED_ASSERT (check_socket(socket));

    if (size > MAX_WRITE_SIZE) {
        tr_warn("WARNING: packet length %d is too big for one UDP packet (max %d), will be fragmented.", size, MAX_WRITE_SIZE);
    }

    while ((count > 0) && success) {
        if (count < blk) {
            blk = count;
        }
        LOCK();

        if (_at->send("AT+USOST=%d,\"%s\",%d,%d", socket->modem_handle,
                      address.get_ip_address(), address.get_port(), blk) &&
            _at->recv("@")) {
            wait_ms(50);
            if ((_at->write(buf, blk) >= (int) blk) &&
                 _at->recv("OK")) {
            } else {
                success = false;
            }
        } else {
            success = false;
        }

        UNLOCK();
        buf += blk;
        count -= blk;
    }

    if (success) {
        nsapi_error_size = size - count;
        if (_debug_trace_on) {
            tr_debug("socket_sendto: %d \"%*.*s\"", size, size, size, (char *) data);
        }
    }

    return nsapi_error_size;
}

// Receive from a socket, TCP style.
nsapi_size_or_error_t UbloxATCellularInterface::socket_recv(nsapi_socket_t handle,
                                                            void *data,
                                                            nsapi_size_t size)
{
    nsapi_size_or_error_t nsapi_error_size = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    char *buf = (char *) data;
    nsapi_size_t read_blk;
    nsapi_size_t count = 0;
    unsigned int usord_sz;
    int read_sz;
    Timer timer;
    SockCtrl *socket = (SockCtrl *) handle;
    int at_timeout;

    tr_debug("socket_recv(0x%08x, 0x%08x, %d)",
             (unsigned int) handle, (unsigned int) data, size);

    MBED_ASSERT (check_socket(socket));

    if (socket->modem_handle == SOCKET_UNUSED) {
        tr_debug("socket_recv: socket closed");
        return NSAPI_ERROR_NO_SOCKET;
    }

    timer.start();

    while (success && (size > 0)) {
        LOCK();
        at_timeout = _at_timeout;
        at_set_timeout(1000);

        read_blk = MAX_READ_SIZE;
        if (read_blk > size) {
            read_blk = size;
        }
        if (socket->pending > 0) {
            tr_debug("Socket 0x%08x: modem handle %d has %d byte(s) pending",
                     (unsigned int) socket, socket->modem_handle, socket->pending);
            _at->debug_on(false); // ABSOLUTELY no time for debug here if you want to
                                  // be able to read packets of any size without
                                  // losing characters in UARTSerial
            if (_at->send("AT+USORD=%d,%d", socket->modem_handle, read_blk) &&
                _at->recv("+USORD: %*d,%d,\"", &usord_sz)) {
                // Must use what +USORD returns here as it may be less or more than we asked for
                if (usord_sz > socket->pending) {
                    socket->pending = 0;
                } else {
                    socket->pending -= usord_sz; 
                }
                // Note: insert no debug between _at->recv() and _at->read(), no time...
                if (usord_sz > size) {
                    usord_sz = size;
                }
                read_sz = _at->read(buf, usord_sz);
                if (read_sz > 0) {
                    tr_debug("...read %d byte(s) from modem handle %d...", read_sz,
                             socket->modem_handle);
                    if (_debug_trace_on) {
                        tr_debug("Read returned %d,  |%*.*s|", read_sz, read_sz, read_sz, buf);
                    }
                    count += read_sz;
                    buf += read_sz;
                    size -= read_sz;
                } else {
                    // read() should not fail
                    success = false;
                }
                tr_debug("Socket 0x%08x: modem handle %d now has only %d byte(s) pending",
                         (unsigned int) socket, socket->modem_handle, socket->pending);
                // Wait for the "OK" before continuing
                _at->recv("OK");
            } else {
                // Should never fail to do _at->send()/_at->recv()
                success = false;
            }
            _at->debug_on(_debug_trace_on);
        } else if (timer.read_ms() < SOCKET_TIMEOUT) {
            // Wait for URCs
            _at->recv(UNNATURAL_STRING);
        } else {
            if (count == 0) {
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
        nsapi_error_size = count;
    }

    if (_debug_trace_on) {
        tr_debug("socket_recv: %d \"%*.*s\"", count, count, count, buf - count);
    } else {
        tr_debug("socket_recv: received %d byte(s)", count);
    }

    return nsapi_error_size;
}

// Receive a packet over a UDP socket.
nsapi_size_or_error_t UbloxATCellularInterface::socket_recvfrom(nsapi_socket_t handle,
                                                                SocketAddress *address,
                                                                void *data,
                                                                nsapi_size_t size)
{
    nsapi_size_or_error_t nsapi_error_size = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    char *buf = (char *) data;
    nsapi_size_t read_blk;
    nsapi_size_t count = 0;
    char ipAddress[NSAPI_IP_SIZE];
    int port;
    unsigned int usorf_sz;
    int read_sz;
    Timer timer;
    SockCtrl *socket = (SockCtrl *) handle;
    int at_timeout;

    tr_debug("socket_recvfrom(0x%08x, 0x%08x, %d)",
             (unsigned int) handle, (unsigned int) data, size);

    MBED_ASSERT (check_socket(socket));

    timer.start();

    while (success && (size > 0)) {
        LOCK();
        at_timeout = _at_timeout;
        at_set_timeout(1000);

        read_blk = MAX_READ_SIZE;
        if (read_blk > size) {
            read_blk = size;
        }
        if (socket->pending > 0) {
            tr_debug("Socket 0x%08x: modem handle %d has %d byte(s) pending",
                     (unsigned int) socket, socket->modem_handle, socket->pending);
            memset (ipAddress, 0, sizeof (ipAddress)); // Ensure terminator

            // Note: the maximum length of UDP packet we can receive comes from
            // fitting all of the following into one buffer:
            //
            // +USORF: xx,"max.len.ip.address.ipv4.or.ipv6",yyyyy,wwww,"the_data"\r\n
            //
            // where xx is the handle, max.len.ip.address.ipv4.or.ipv6 is NSAPI_IP_SIZE,
            // yyyyy is the port number (max 65536), wwww is the length of the data and
            // the_data is binary data. I make that 29 + 48 + len(the_data),
            // so the overhead is 77 bytes.

            _at->debug_on(false); // ABSOLUTELY no time for debug here if you want to
                                  // be able to read packets of any size without
                                  // losing characters in UARTSerial
            if (_at->send("AT+USORF=%d,%d", socket->modem_handle, read_blk) &&
                _at->recv("+USORF: %*d,\"%" u_stringify(NSAPI_IP_SIZE) "[^\"]\",%d,%d,\"",
                          ipAddress, &port, &usorf_sz)) {
                // Must use what +USORF returns here as it may be less or more than we asked for
                if (usorf_sz > socket->pending) {
                    socket->pending = 0;
                } else {
                    socket->pending -= usorf_sz; 
                }
                // Note: insert no debug between _at->recv() and _at->read(), no time...
                if (usorf_sz > size) {
                    usorf_sz = size;
                }
                read_sz = _at->read(buf, usorf_sz);
                if (read_sz > 0) {
                    address->set_ip_address(ipAddress);
                    address->set_port(port);
                    tr_debug("...read %d byte(s) from modem handle %d...", read_sz,
                             socket->modem_handle);
                    if (_debug_trace_on) {
                        tr_debug("Read returned %d,  |%*.*s|", read_sz, read_sz, read_sz, buf);
                    }
                    count += read_sz;
                    buf += read_sz;
                    size -= read_sz;
                    if ((usorf_sz < read_blk) || (usorf_sz == MAX_READ_SIZE)) {
                        size = 0; // If we've received less than we asked for, or
                                  // the max size, then a whole UDP packet has arrived and
                                  // this means DONE.
                    }
                } else {
                    // read() should not fail
                    success = false;
                }
                tr_debug("Socket 0x%08x: modem handle %d now has only %d byte(s) pending",
                         (unsigned int) socket, socket->modem_handle, socket->pending);
                // Wait for the "OK" before continuing
                _at->recv("OK");
            } else {
                // Should never fail to do _at->send()/_at->recv()
                success = false;
            }
            _at->debug_on(_debug_trace_on);
        } else if (timer.read_ms() < SOCKET_TIMEOUT) {
            // Wait for URCs
            _at->recv(UNNATURAL_STRING);
        } else {
            if (count == 0) {
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
        nsapi_error_size = count;
    }

    if (_debug_trace_on) {
        tr_debug("socket_recvfrom: %d \"%*.*s\"", count, count, count, buf - count);
    } else {
        tr_debug("socket_recvfrom: received %d byte(s)", count);
    }

    return nsapi_error_size;
}

// Attach an event callback to a socket, required for asynchronous
// data reception
void UbloxATCellularInterface::socket_attach(nsapi_socket_t handle,
                                             void (*callback)(void *),
                                             void *data)
{
    SockCtrl *socket = (SockCtrl *) handle;

    MBED_ASSERT (check_socket(socket));

    socket->callback = callback;
    socket->data = data;
}

// Unsupported TCP server functions.
nsapi_error_t UbloxATCellularInterface::socket_listen(nsapi_socket_t handle,
                                                      int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
nsapi_error_t UbloxATCellularInterface::socket_accept(nsapi_socket_t server,
                                                      nsapi_socket_t *handle,
                                                      SocketAddress *address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

// Unsupported option functions.
nsapi_error_t UbloxATCellularInterface::setsockopt(nsapi_socket_t handle,
                                                   int level, int optname,
                                                   const void *optval,
                                                   unsigned optlen)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
nsapi_error_t UbloxATCellularInterface::getsockopt(nsapi_socket_t handle,
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
UbloxATCellularInterface::UbloxATCellularInterface(PinName tx,
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
    _network_search_timeout_seconds = 180;

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
    event_thread.start(callback(this, &UbloxATCellularInterface::handle_event));

    // URC handlers for sockets
    _at->oob("+UUSORD", callback(this, &UbloxATCellularInterface::UUSORD_URC));
    _at->oob("+UUSORF", callback(this, &UbloxATCellularInterface::UUSORF_URC));
    _at->oob("+UUSOCL", callback(this, &UbloxATCellularInterface::UUSOCL_URC));
    _at->oob("+UUPSDD", callback(this, &UbloxATCellularInterface::UUPSDD_URC));
}

// Destructor.
UbloxATCellularInterface::~UbloxATCellularInterface()
{
    // Let the event thread shut down tidily
    _run_event_thread = false;
    event_thread.join();
    
    // Free _ip if it was ever allocated
    free(_ip);
}

// Set the authentication scheme.
void UbloxATCellularInterface::set_authentication(nsapi_security_t auth)
{
    _auth = auth;
}

// Set APN, user name and password.
void  UbloxATCellularInterface::set_credentials(const char *apn,
                                                const char *uname,
                                                const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
}

// Set PIN.
void UbloxATCellularInterface::set_sim_pin(const char *pin)
{
    set_pin(pin);
}

// Set the network search timeout.
void UbloxATCellularInterface::set_network_search_timeout(int timeout_seconds) {
    _network_search_timeout_seconds = timeout_seconds;
}

// Set release assistance.
void UbloxATCellularInterface::set_release_assistance(bool isOn) {
    // Not supported on 2G/3G
}

// Get the IP address of a host.
nsapi_error_t UbloxATCellularInterface::gethostbyname(const char *host,
                                                      SocketAddress *address,
                                                      nsapi_version_t version)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    int at_timeout;
    char ipAddress[NSAPI_IP_SIZE];

    if (address->set_ip_address(host)) {
        nsapi_error = NSAPI_ERROR_OK;
    } else {
        LOCK();
        // This interrogation can sometimes take longer than the usual 8 seconds
        at_timeout = _at_timeout;
        at_set_timeout(60000);
        memset (ipAddress, 0, sizeof (ipAddress)); // Ensure terminator
        if (_at->send("AT+UDNSRN=0,\"%s\"", host) &&
            _at->recv("+UDNSRN: \"%" u_stringify(NSAPI_IP_SIZE) "[^\"]\"", ipAddress) &&
            _at->recv("OK")) {
            if (address->set_ip_address(ipAddress)) {
                nsapi_error = NSAPI_ERROR_OK;
            }
        }
        at_set_timeout(at_timeout);
        UNLOCK();
    }

    return nsapi_error;
}

// Make a cellular connection
nsapi_error_t UbloxATCellularInterface::connect(const char *sim_pin,
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

    nsapi_error = connect();

    return nsapi_error;
}

// Make a cellular connection using the IP stack on board the cellular modem
nsapi_error_t UbloxATCellularInterface::connect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    bool registered = false;

    // Set up modem and then register with the network
    if (init()) {
        nsapi_error = NSAPI_ERROR_NO_CONNECTION;
        // Perform any pending SIM actions
        if (_sim_pin_check_change_pending) {
            if (!sim_pin_check_enable(_sim_pin_check_change_pending_enabled_value)) {
                nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
            }
            _sim_pin_check_change_pending = false;
        }
        if (_sim_pin_change_pending) {
            if (!change_sim_pin(_sim_pin_change_pending_new_pin_value)) {
                nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
            }
            _sim_pin_change_pending = false;
        }

        if (nsapi_error == NSAPI_ERROR_NO_CONNECTION) {
            //for (int retries = 0; !registered && (retries < 3); retries++) {
                if (nwk_registration(_network_search_timeout_seconds)) {
                    registered = true;;
                }
            //}
        }
    }

    // Attempt to establish a connection
#ifdef TARGET_UBLOX_C030_R410M
    if (registered) {
#else
    if (registered && connect_modem_stack()) {
#endif
        nsapi_error = NSAPI_ERROR_OK;
    }

    return nsapi_error;
}

// User initiated disconnect.
nsapi_error_t UbloxATCellularInterface::disconnect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;

    if (disconnect_modem_stack() && nwk_deregistration()) {
        nsapi_error = NSAPI_ERROR_OK;
    }

    return nsapi_error;
}

// Enable or disable SIM PIN check lock.
nsapi_error_t UbloxATCellularInterface::set_sim_pin_check(bool set,
                                                          bool immediate,
                                                          const char *sim_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;

    if (sim_pin != NULL) {
        _pin = sim_pin;
    }

    if (immediate) {
        if (init()) {
            if (sim_pin_check_enable(set)) {
                nsapi_error = NSAPI_ERROR_OK;
            }
        } else {
            nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
        }
    } else {
        nsapi_error = NSAPI_ERROR_OK;
        _sim_pin_check_change_pending = true;
        _sim_pin_check_change_pending_enabled_value = set;
    }

    return nsapi_error;
}

// Change the PIN code for the SIM card.
nsapi_error_t UbloxATCellularInterface::set_new_sim_pin(const char *new_pin,
                                                        bool immediate,
                                                        const char *old_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;

    if (old_pin != NULL) {
        _pin = old_pin;
    }

    if (immediate) {
        if (init()) {
            if (change_sim_pin(new_pin)) {
                nsapi_error = NSAPI_ERROR_OK;
            }
        } else {
            nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
        }
    } else {
        nsapi_error = NSAPI_ERROR_OK;
        _sim_pin_change_pending = true;
        _sim_pin_change_pending_new_pin_value = new_pin;
    }

    return nsapi_error;
}

// Determine if the connection is up.
bool UbloxATCellularInterface::is_connected()
{
    return get_ip_address() != NULL;
}

// Get the IP address of the on-board modem IP stack.
const char * UbloxATCellularInterface::get_ip_address()
{
    SocketAddress address;
    LOCK();

    if (_ip == NULL) {
        // Temporary storage for an IP address string with terminator
        _ip = (char *) malloc(NSAPI_IP_SIZE);
    }

    if (_ip != NULL) {
        memset(_ip, 0, NSAPI_IP_SIZE); // Ensure a terminator
        // +UPSND=<profile_id>,<param_tag>[,<dynamic_param_val>]
        // If we get back a quoted "w.x.y.z" then we have an IP address,
        // otherwise we don't.
        if (!_at->send("AT+UPSND=" PROFILE ",0") ||
            !_at->recv("+UPSND: " PROFILE ",0,\"%" u_stringify(NSAPI_IP_SIZE) "[^\"]\"", _ip) ||
            !_at->recv("OK") ||
            !address.set_ip_address(_ip) || // Return NULL if the address is not a valid one
            !address) { // Return null if the address is zero
            free (_ip);
            _ip = NULL;
        }
    }

    UNLOCK();
    return _ip;
}

// Get the local network mask.
const char *UbloxATCellularInterface::get_netmask()
{
    // Not implemented.
    return NULL;
}

// Get the local gateways.
const char *UbloxATCellularInterface::get_gateway()
{
    return get_ip_address();
}

// Callback in case the connection is lost.
void UbloxATCellularInterface::connection_status_cb(Callback<void(nsapi_error_t)> cb)
{
    _connection_status_cb = cb;
}

// End of file


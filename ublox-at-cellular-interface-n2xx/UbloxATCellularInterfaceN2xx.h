/* Copyright (c) 2017 ARM Limited
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

#ifndef _UBLOX_AT_CELLULAR_INTERFACE_N2XX_
#define _UBLOX_AT_CELLULAR_INTERFACE_N2XX_

#include "UbloxCellularBaseN2xx.h"
#include "CellularBase.h"
#include "NetworkStack.h"

/** UbloxATCellularInterface class.
 *
 *  This uses the cellular-tuned IP stack that
 *  is on-board the cellular modem instead of the
 *  LWIP stack on the mbed MCU.
 *
 *  There are three advantages to using this mechanism:
 *
 *  1.  Since the modem interface remains in AT mode
 *      throughout, it is possible to continue using
 *      any AT commands (e.g. send SMS, use the module's
 *      file system, etc.) while the connection is up.
 *
 *  2.  The UbloxATCellularInterfaceExt class can
 *      be used to perform very simple HTTP and FTP
 *      operations using the modem's on-board clients.
 *
 *  3.  LWIP is not required (and hence RAM is saved).
 *
 *  The disadvantage is that some additional parsing
 *  (at the AT interface) has to go on in order to exchange
 *  IP packets, so this is less efficient under heavy loads.
 *  Also TCP Server and getting/setting of socket options is
 *  currently not supported.
 */

// Forward declaration
class NetworkStack;

/*
 * NOTE: order is important in the inheritance below!  PAL takes this class
 * and casts it to CellularInterface and so CellularInterface has to be first
 * in the last for that to work.
 */

/** UbloxATCellularInterface class.
 *
 *  This class implements the network stack interface into the cellular
 *  modems on the C030 and C027 boards for 2G/3G/4G modules using
 *  the IP stack running on the cellular module.
 */
class UbloxATCellularInterfaceN2xx : public CellularBase, public NetworkStack, virtual public UbloxCellularBaseN2xx  {

public:

    /** Constructor.
     *
     * @param tx       the UART TX data pin to which the modem is attached.
     * @param rx       the UART RX data pin to which the modem is attached.
     * @param baud     the UART baud rate.
     * @param debug_on true to switch AT interface debug on, otherwise false.
     */
     UbloxATCellularInterfaceN2xx(PinName tx = MDMTXD,
                              PinName rx = MDMRXD,
                              int baud = MBED_CONF_UBLOX_CELL_N2XX_BAUD_RATE,
                              bool debug_on = false);

     /* Destructor.
      */
     virtual ~UbloxATCellularInterfaceN2xx();

    /** The amount of extra space needed in terms of AT interface
     * characters to get a chunk of user data (i.e. one UDP packet
     * or a portion of a TCP packet) across the AT interface.
     */
    #define AT_PACKET_OVERHEAD 77

    /** The profile to use (on board the modem).
     */
    #define PROFILE "0"

    /** Translates a host name to an IP address with specific IP version.
     *
     *  The host name may be either a domain name or an IP address. If the
     *  host name is an IP address, no network transactions will be performed.
     *
     *  If no stack-specific DNS resolution is provided, the host name
     *  will be resolved using a UDP socket on the stack.
     *
     *  @param host     Host name to resolve.
     *  @param address  Destination for the host SocketAddress.
     *  @param version  IP version of address to resolve, NSAPI_UNSPEC indicates
     *                  version is chosen by the stack (defaults to NSAPI_UNSPEC).
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t gethostbyname(const char *host,
                                        SocketAddress *address,
                                        nsapi_version_t version = NSAPI_UNSPEC);

    /** Set the authentication scheme.
     *
     *  @param auth      The authentication scheme, chose from
     *                   NSAPI_SECURITY_NONE, NSAPI_SECURITY_PAP,
     *                   NSAPI_SECURITY_CHAP or NSAPI_SECURITY_UNKNOWN;
     *                   use NSAPI_SECURITY_UNKNOWN to try all of none,
     *                   PAP and CHAP.
     */
    virtual void set_authentication(nsapi_security_t auth);

    /** Set the cellular network credentials.
     *
     *  Please check documentation of connect() for default behaviour of APN settings.
     *
     *  @param apn      Access point name.
     *  @param uname    Optionally, user name.
     *  @param pwd      Optionally, password.
     */
    virtual void set_credentials(const char *apn, const char *uname = 0,
                                 const char *pwd = 0);

    /** Set the PIN code for the SIM card.
     *
     *  @param sim_pin      PIN for the SIM card.
     */
    virtual void set_sim_pin(const char *sim_pin);
    
    /** Set the timeout for network search.
     *
     *  @param timeout_seconds    the network search timeout.
     */
    void set_network_search_timeout(int timeout_seconds);
    
    /** Set release assistance on or off.  When release
     * assistance is set the module will not wait any additional
     * period for network-originated traffic.  Set release 
     * assistance to on in situations where all traffic is mobile
     * originated and power saving is critical.
     *
     *  @param isOn set to true for release assistance on (default is off).
     */
    void set_release_assistance(bool isOn);
    
    /** Set the local listen port when opening a UDP socket
     * 
     */     
    void set_LocalListenPort(int port);
    
    /** 
     * Turns on the modem and reads the module information     
     */
    bool initialise();

    /** Connect to the cellular network and start the interface.
     *
     *  Attempts to connect to a cellular network.  Note: if initialise() has
     *  not been called beforehand, connect() will call it first.
     *
     *  @param sim_pin     PIN for the SIM card.
     *  @param apn         Optionally, access point name.
     *  @param uname       Optionally, user name.
     *  @param pwd         Optionally, password.
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure.
     */
    virtual nsapi_error_t connect(const char *sim_pin, const char *apn = 0,
                                  const char *uname = 0, const char *pwd = 0);

    /** Attempt to connect to the cellular network.
     *
     *  Brings up the network interface. Connects to the cellular radio
     *  network and then brings up IP stack on the cellular modem to be used
     *  indirectly via AT commands, rather than LWIP.  Note: if init() has
     *  not been called beforehand, connect() will call it first.
     *  NOTE: even a failed attempt to connect will cause the modem to remain
     *  powered up.  To power it down, call deinit().
     *
     *  For APN setup, default behaviour is to use 'internet' as APN string
     *  and assuming no authentication is required, i.e., user name and password
     *  are not set. Optionally, a database lookup can be requested by turning
     *  on the APN database lookup feature. The APN database is by no means
     *  exhaustive (feel free to submit a pull request with additional values).
     *  It contains a short list of some public APNs with publicly available
     *  user names and passwords (if required) in some particular countries only.
     *  Lookup is done using IMSI (International mobile subscriber identifier).
     *
     *  The preferred method is to setup APN using 'set_credentials()' API.
     *
     *  If you find that the AT interface returns "CONNECT" but shortly afterwards
     *  drops the connection then 99% of the time this will be because the APN
     *  is incorrect.
     *
     *  @return            0 on success, negative error code on failure.
     */
    virtual nsapi_error_t connect();

    /** Attempt to disconnect from the network.
     *
     *  Brings down the network interface.
     *  Does not bring down the Radio network.
     *
     *  @return            0 on success, negative error code on failure.
     */
    virtual nsapi_error_t disconnect();

    /** Adds or removes a SIM facility lock.
     *
     * Can be used to enable or disable SIM PIN check at device startup.
     *
     * @param set          Can be set to true if the SIM PIN check is supposed
     *                     to be enabled and vice versa.
     * @param immediate    If true, change the SIM PIN now, else set a flag
     *                     and make the change only when connect() is called.
     *                     If this is true and init() has not been called previously,
     *                     it will be called first.
     * @param sim_pin      The current SIM PIN, must be a const.  If this is not
     *                     provided, the SIM PIN must have previously been set by a
     *                     call to set_sim_pin().
     * @return             0 on success, negative error code on failure.
     */
    nsapi_error_t set_sim_pin_check(bool set, bool immediate = false,
                                    const char *sim_pin = NULL);

    /** Change the PIN for the SIM card.
     *
     * Provide the new PIN for your SIM card with this API.  It is ONLY possible to
     * change the SIM PIN when SIM PIN checking is ENABLED.
     *
     * @param new_pin    New PIN to be used in string format, must be a const.
     * @param immediate  If true, change the SIM PIN now, else set a flag
     *                   and make the change only when connect() is called.
     *                   If this is true and init() has not been called previously,
     *                   it will be called first.
     * @param old_pin    Old PIN, must be a const.  If this is not provided, the SIM PIN
     *                   must have previously been set by a call to set_sim_pin().
     * @return           0 on success, negative error code on failure.
     */
    nsapi_error_t set_new_sim_pin(const char *new_pin, bool immediate = false,
                                  const char *old_pin = NULL);

    /** Check if the connection is currently established or not.
     *
     * @return          True if connected to a data network, otherwise false.
     */
    virtual bool is_connected();

    /** Get the local IP address
     *
     *  @return         Null-terminated representation of the local IP address
     *                  or null if no IP address has been received.
     */
    virtual const char *get_ip_address();

    /** Get the local network mask.
     *
     *  @return         Null-terminated representation of the local network mask
     *                  or null if no network mask has been received.
     */
    virtual const char *get_netmask();

    /** Get the local gateways.
     *
     *  @return         Null-terminated representation of the local gateway
     *                  or null if no network mask has been received.
     */
    virtual const char *get_gateway();

    /** Call back in case connection is lost.
     *
     * @param cb     The function to call.
     */
    void connection_status_cb(Callback<void(nsapi_error_t)> cb);

protected:

    /** Socket "unused" value.
     */
    #define SOCKET_UNUSED -1

    /** Socket timeout value in milliseconds.
     * Note: the sockets layer above will retry the
     * call to the functions here when they return NSAPI_ERROR_WOULD_BLOCK
     * and the user has set a larger timeout or full blocking.
     */
    #define SOCKET_TIMEOUT 1000

    /** The maximum number of bytes in a packet that can be written
     * to the AT interface in one go.
     */
    #define MAX_WRITE_SIZE_N2XX 512

    /** The maximum number of bytes in a packet that can be read from
     * from the AT interface in one go.
     */
    #define MAX_READ_SIZE_N2XX 512

    /** Management structure for sockets.
     */
    typedef struct {
        int modem_handle;  //!< The modem's handle for the socket.
        volatile nsapi_size_t pending; //!< The number of received bytes pending.
        void (*callback)(void *); //!< A callback for events.
        void *data; //!< A data pointer that must be passed to the callback.
    } SockCtrl;

    /** Sockets storage.
     */
    SockCtrl _sockets[7];

    /** Storage for a single IP address.
     */
    char *_ip;

    /** The APN to use.
     */
    const char *_apn;

    /** The user name to use.
     */
    const char *_uname;

    /** The password to use.
     */
    const char *_pwd;
    
    /** The network search timeout.
     */
     int _network_search_timeout_seconds;

    /** The type of authentication to use.
     */
    nsapi_security_t _auth;

    /** Get the next set of credentials from the database.
     */
    virtual void get_next_credentials(const char * config);

    /** Provide access to the NetworkStack object
     *
     *  @return        The underlying NetworkStack object.
     */
    virtual NetworkStack *get_stack();

protected:

    /** Open a socket.
     *
     *  Creates a network socket and stores it in the specified handle.
     *  The handle must be passed to following calls on the socket.
     *
     *  @param handle   Destination for the handle to a newly created socket.
     *  @param proto    Protocol of socket to open, NSAPI_TCP or NSAPI_UDP.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_open(nsapi_socket_t *handle,
                                      nsapi_protocol_t proto);

    /** Close a socket.
     *
     *  Closes any open connection and deallocates any memory associated
     *  with the socket.
     *
     *  @param handle   Socket handle.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_close(nsapi_socket_t handle);

    /** Bind a specific port to a socket.
     *
     *  Binding a socket specifies port on which to receive
     *  data. The IP address is ignored.  Note that binding
     *  a socket involves closing it and reopening and so the
     *  bind operation should be carried out before any others.
     *
     *  @param handle   Socket handle.
     *  @param address  Local address to bind (of which only the port is used).
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_bind(nsapi_socket_t handle,
                                      const SocketAddress &address);

    /** Connects TCP socket to a remote host.
     *
     *  Initiates a connection to a remote server specified by the
     *  indicated address.
     *
     *  @param handle   Socket handle.
     *  @param address  The SocketAddress of the remote host.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_connect(nsapi_socket_t handle,
                                         const SocketAddress &address);

    /** Send data over a TCP socket.
     *
     *  The socket must be connected to a remote host. Returns the number of
     *  bytes sent from the buffer.  This class sets no upper buffer limit on
     *  buffer size and the maximum packet size is not connected with the
     *  platform.buffered-serial-txbuf-size/platform.buffered-serial-rxbuf-size
     *  definitions.
     *
     *  @param handle   Socket handle.
     *  @param data     Buffer of data to send to the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of sent bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_send(nsapi_socket_t handle,
                                              const void *data, nsapi_size_t size);

    /** Send a packet over a UDP socket.
     *
     *  Sends data to the specified address. Returns the number of bytes
     *  sent from the buffer.
     *
     *  PACKET SIZES: the maximum packet size that can be sent in a single
     *  UDP packet is limited by the configuration value
     *  platform.buffered-serial-txbuf-size (defaults to 256).
     *  The maximum UDP packet size is:
     *
     *  platform.buffered-serial-txbuf-size - AT_PACKET_OVERHEAD
     *
     *  ...with a limit of 1024 bytes (at the AT interface). So, to allow sending
     *  of a 1024 byte UDP packet, edit your mbed_app.json to add a target override
     *  setting platform.buffered-serial-txbuf-size to 1101.  However, for
     *  UDP packets, 508 bytes is considered a more realistic size, taking into
     *  account fragmentation sizes over the public internet, which leads to a
     *  platform.buffered-serial-txbuf-size/platform.buffered-serial-rxbuf-size
     *  setting of 585.
     *
     *  If size is larger than this limit, the data will be split across separate
     *  UDP packets.
     *
     *  @param handle   Socket handle.
     *  @param address  The SocketAddress of the remote host.
     *  @param data     Buffer of data to send to the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of sent bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_sendto(nsapi_socket_t handle,
                                                const SocketAddress &address,
                                                const void *data,
                                                nsapi_size_t size);

    /** Receive data over a TCP socket.
     *
     *  The socket must be connected to a remote host. Returns the number of
     *  bytes received into the buffer.  This class sets no upper limit on the
     *  buffer size and the maximum packet size is not connected with the
     *  platform.buffered-serial-txbuf-size/platform.buffered-serial-rxbuf-size
     *  definitions.
     *
     *  @param handle   Socket handle.
     *  @param data     Destination buffer for data received from the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of received bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_recv(nsapi_socket_t handle,
                                              void *data, nsapi_size_t size);

    /** Receive a packet over a UDP socket.
     *
     *  Receives data and stores the source address in address if address
     *  is not NULL. Returns the number of bytes received into the buffer.
     *
     *  PACKET SIZES: the maximum packet size that can be retrieved in a
     *  single call to this method is limited by the configuration value
     *  platform.buffered-serial-rxbuf-size (default 256).  The maximum
     *  UDP packet size is:
     *
     *  platform.buffered-serial-rxbuf-size - AT_PACKET_OVERHEAD
     *
     *  ...with a limit of 1024 (at the AT interface). So to allow reception of a
     *  1024 byte UDP packet in a single call, edit your mbed_app.json to add a
     *  target override setting platform.buffered-serial-rxbuf-size to 1101.
     *
     *  If the received packet is larger than this limit, any remainder will
     *  be returned in subsequent calls to this method.  Once a single UDP
     *  packet has been received, this method will return.
     *
     *  @param handle   Socket handle.
     *  @param address  Destination for the source address or NULL.
     *  @param data     Destination buffer for data received from the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of received bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_recvfrom(nsapi_socket_t handle,
                                                  SocketAddress *address,
                                                  void *data, nsapi_size_t size);

    /** Register a callback on state change of the socket.
     *
     *  The specified callback will be called on state changes such as when
     *  the socket can recv/send/accept successfully and on when an error
     *  occurs. The callback may also be called spuriously without reason.
     *
     *  The callback may be called in an interrupt context and should not
     *  perform expensive operations such as recv/send calls.
     *
     *  @param handle   Socket handle.
     *  @param callback Function to call on state change.
     *  @param data     Argument to pass to callback.
     */
    virtual void socket_attach(nsapi_socket_t handle, void (*callback)(void *),
                               void *data);

    /** Listen for connections on a TCP socket.
     *
     *  Marks the socket as a passive socket that can be used to accept
     *  incoming connections.
     *
     *  @param handle   Socket handle.
     *  @param backlog  Number of pending connections that can be queued
     *                  simultaneously, defaults to 1.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_listen(nsapi_socket_t handle, int backlog);

    /** Accepts a connection on a TCP socket.
     *
     *  The server socket must be bound and set to listen for connections.
     *  On a new connection, creates a network socket and stores it in the
     *  specified handle. The handle must be passed to following calls on
     *  the socket.
     *
     *  A stack may have a finite number of sockets, in this case
     *  NSAPI_ERROR_NO_SOCKET is returned if no socket is available.
     *
     *  This call is non-blocking. If accept would block,
     *  NSAPI_ERROR_WOULD_BLOCK is returned immediately.
     *
     *  @param server   Socket handle to server to accept from.
     *  @param handle   Destination for a handle to the newly created socket.
     *  @param address  Destination for the remote address or NULL.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_accept(nsapi_socket_t server,
                                        nsapi_socket_t *handle,
                                        SocketAddress *address = 0);

    /**  Set stack-specific socket options.
     *
     *  The setsockopt allow an application to pass stack-specific hints
     *  to the underlying stack. For unsupported options,
     *  NSAPI_ERROR_UNSUPPORTED is returned and the socket is unmodified.
     *
     *  @param handle   Socket handle.
     *  @param level    Stack-specific protocol level.
     *  @param optname  Stack-specific option identifier.
     *  @param optval   Option value.
     *  @param optlen   Length of the option value.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t setsockopt(nsapi_socket_t handle, int level,
                                     int optname, const void *optval,
                                     unsigned optlen);

    /**  Get stack-specific socket options.
     *
     *  The getstackopt allow an application to retrieve stack-specific hints
     *  from the underlying stack. For unsupported options,
     *  NSAPI_ERROR_UNSUPPORTED is returned and optval is unmodified.
     *
     *  @param handle   Socket handle.
     *  @param level    Stack-specific protocol level.
     *  @param optname  Stack-specific option identifier.
     *  @param optval   Destination for option value.
     *  @param optlen   Length of the option value.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t getsockopt(nsapi_socket_t handle, int level,
                                     int optname, void *optval,
                                     unsigned *optlen);

private:

    // u_ added to namespace us somewhat as this darned macro
    // is defined by everyone and their dog
    #define u_stringify(a) str(a)
    #define str(a) #a

    int _localListenPort;
    
    bool _sim_pin_check_change_pending;
    bool _sim_pin_check_change_pending_enabled_value;
    bool _sim_pin_change_pending;
    const char *_sim_pin_change_pending_new_pin_value;
    Thread event_thread;
    void handle_event();
    bool _run_event_thread;
    const char *_sendFlags;
    SockCtrl * find_socket(int modem_handle = SOCKET_UNUSED);
    void clear_socket(SockCtrl * socket);
    bool check_socket(SockCtrl * socket);
    
    nsapi_size_or_error_t receivefrom(int socketId, SocketAddress *address, int length, char *buf);
    nsapi_size_or_error_t sendto(SockCtrl *socket, const SocketAddress &address, const char *buf, int size);
    bool sendATChopped(const char *);
    
    char hex_char(char c);
    int hex_to_bin(const char* s, char * buff, int length);
    void bin_to_hex(const char *buff, unsigned int length, char *output);
    
    Callback<void(nsapi_error_t)> _connection_status_cb;
    void NSONMI_URC();    
};

#endif // _UBLOX_AT_CELLULAR_INTERFACE_


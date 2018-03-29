#include "UbloxATCellularInterface.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "UDPSocket.h"
#ifdef FEATURE_COMMON_PAL
#include "mbed_trace.h"
#define TRACE_GROUP "TEST"
#else
#define tr_debug(format, ...) debug(format "\n", ## __VA_ARGS__)
#define tr_info(format, ...)  debug(format "\n", ## __VA_ARGS__)
#define tr_warn(format, ...)  debug(format "\n", ## __VA_ARGS__)
#define tr_error(format, ...) debug(format "\n", ## __VA_ARGS__)
#endif

using namespace utest::v1;

// IMPORTANT!!! if you make a change to the tests here you should
// check whether the same change should be made to the tests under
// the PPP interface.

// NOTE: these test are only as reliable as UDP across the internet
// over a radio link.  The tests expect an NTP server to respond
// to UDP packets and, if configured, an echo server to respond
// to UDP packets.  This simply may not happen.  Please be patient.

// ----------------------------------------------------------------
// COMPILE-TIME MACROS
// ----------------------------------------------------------------

// These macros can be overridden with an mbed_app.json file and
// contents of the following form:
//
//{
//    "config": {
//        "default-pin": {
//            "value": "\"1234\""
//        }
//}
//
// See the template_mbed_app.txt in this directory for a fuller example.

// Whether debug trace is on
#ifndef MBED_CONF_APP_DEBUG_ON
# define MBED_CONF_APP_DEBUG_ON false
#endif

// Run the SIM change tests, which require the DEFAULT_PIN
// above to be correct for the board on which the test
// is being run (and the SIM PIN to be disabled before tests run).
#ifndef MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS
# define MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS 0
#endif

#if MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS
# ifndef MBED_CONF_APP_DEFAULT_PIN
#   error "MBED_CONF_APP_DEFAULT_PIN must be defined to run the SIM tests"
# endif
# ifndef MBED_CONF_APP_ALT_PIN
#   error "MBED_CONF_APP_ALT_PIN must be defined to run the SIM tests"
# endif
# ifndef MBED_CONF_APP_INCORRECT_PIN
#   error "MBED_CONF_APP_INCORRECT_PIN must be defined to run the SIM tests"
# endif
#endif

// The credentials of the SIM in the board.
#ifndef MBED_CONF_APP_DEFAULT_PIN
// Note: if PIN is enabled on your SIM, or you wish to run the SIM PIN change
// tests, you must define the PIN for your SIM (see note above on using
// mbed_app.json to do so).
# define MBED_CONF_APP_DEFAULT_PIN "0000"
#endif
#ifndef MBED_CONF_APP_APN
# define MBED_CONF_APP_APN         NULL
#endif
#ifndef MBED_CONF_APP_USERNAME
# define MBED_CONF_APP_USERNAME    NULL
#endif
#ifndef MBED_CONF_APP_PASSWORD
# define MBED_CONF_APP_PASSWORD    NULL
#endif

// Alternate PIN to use during pin change testing
#ifndef MBED_CONF_APP_ALT_PIN
# define MBED_CONF_APP_ALT_PIN    "9876"
#endif

// A PIN that is definitely incorrect
#ifndef MBED_CONF_APP_INCORRECT_PIN
# define MBED_CONF_APP_INCORRECT_PIN "1530"
#endif

// Servers and ports
#ifdef MBED_CONF_APP_ECHO_SERVER
# ifndef MBED_CONF_APP_ECHO_UDP_PORT
#  error "MBED_CONF_APP_ECHO_UDP_PORT (the port on which your echo server echoes UDP packets) must be defined"
# endif
# ifndef MBED_CONF_APP_ECHO_TCP_PORT
#  error "MBED_CONF_APP_ECHO_TCP_PORT (the port on which your echo server echoes TCP packets) must be defined"
# endif
#endif

#ifndef MBED_CONF_APP_NTP_SERVER
# define MBED_CONF_APP_NTP_SERVER "2.pool.ntp.org"
#else
# ifndef MBED_CONF_APP_NTP_PORT
#  error "MBED_CONF_APP_NTP_PORT must be defined if MBED_CONF_APP_NTP_SERVER is defined"
# endif
#endif
#ifndef MBED_CONF_APP_NTP_PORT
# define MBED_CONF_APP_NTP_PORT 123
#endif

#ifndef MBED_CONF_APP_LOCAL_PORT
# define MBED_CONF_APP_LOCAL_PORT 15
#endif

// UDP packet size limit for testing
#ifndef MBED_CONF_APP_UDP_MAX_PACKET_SIZE
#  define MBED_CONF_APP_UDP_MAX_PACKET_SIZE 1024
#endif

// The maximum size of UDP data fragmented across
// multiple packets
#ifndef MBED_CONF_APP_UDP_MAX_FRAG_PACKET_SIZE
# define MBED_CONF_APP_UDP_MAX_FRAG_PACKET_SIZE 1500
#endif

// TCP packet size limit for testing
#ifndef MBED_CONF_APP_MBED_CONF_APP_TCP_MAX_PACKET_SIZE
# define MBED_CONF_APP_TCP_MAX_PACKET_SIZE 1500
#endif

// The number of retries for UDP exchanges
#define NUM_UDP_RETRIES 5

// How long to wait for stuff to travel in the async echo tests
#define ASYNC_TEST_WAIT_TIME 10000

// The maximum number of sockets that can be open at one time
#define MAX_NUM_SOCKETS 7

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

#ifdef FEATURE_COMMON_PAL
// Lock for debug prints
static Mutex mtx;
#endif

// An instance of the cellular interface
static UbloxATCellularInterface *interface =
       new UbloxATCellularInterface(MDMTXD, MDMRXD,
                                    MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                    MBED_CONF_APP_DEBUG_ON);

// Connection flag
static bool connection_has_gone_down = false;

// Data to exchange
static const char send_data[] =  "_____0000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____2000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789";

// ----------------------------------------------------------------
// PRIVATE FUNCTIONS
// ----------------------------------------------------------------

#ifdef FEATURE_COMMON_PAL
// Locks for debug prints
static void lock()
{
    mtx.lock();
}

static void unlock()
{
    mtx.unlock();
}
#endif

// Callback in case the connection goes down
static void connection_down_cb(nsapi_error_t err)
{
    connection_has_gone_down = true;
}

#ifdef MBED_CONF_APP_ECHO_SERVER
// Make sure that size is greater than 0 and no more than limit,
// useful since, when moduloing a very large number number,
// compilers sometimes screw up and produce a small *negative*
// number.  Who knew?  For example, GCC decided that
// 492318453 (0x1d582ef5) modulo 508 was -47 (0xffffffd1).
static int fix (int size, int limit)
{
    if (size <= 0) {
        size = limit / 2; // better than 1
    } else if (size > limit) {
        size = limit;
    }

    return size;
}

// Do a UDP socket echo test to a given host of a given packet size
static void do_udp_echo(UDPSocket *sock, SocketAddress *host_address, int size)
{
    bool success = false;
    void * recv_data = malloc (size);
    SocketAddress sender_address;
    TEST_ASSERT(recv_data != NULL);

    // Retry this a few times, don't want to fail due to a flaky link
    for (int x = 0; !success && (x < NUM_UDP_RETRIES); x++) {
        tr_debug("Echo testing UDP packet size %d byte(s), try %d.", size, x + 1);
        if ((sock->sendto(*host_address, (void*) send_data, size) == size) &&
            (sock->recvfrom(&sender_address, recv_data, size) == size)) {
            TEST_ASSERT (memcmp(send_data, recv_data, size) == 0);
            TEST_ASSERT (strcmp(sender_address.get_ip_address(), host_address->get_ip_address()) == 0);
            TEST_ASSERT (sender_address.get_port() == host_address->get_port());
            success = true;
        }
    }
    TEST_ASSERT (success);
    TEST_ASSERT(!connection_has_gone_down);

    free (recv_data);
}

// The asynchronous callback
static void async_cb(bool *callback_triggered)
{

    TEST_ASSERT (callback_triggered != NULL);
    *callback_triggered = true;
}

// Do a UDP echo but using the asynchronous interface; we can exchange
// packets longer in size than one UDP packet this way
static void do_udp_echo_async(UDPSocket *sock, SocketAddress *host_address,
                              int size, bool *callback_triggered)
{
    void * recv_data = malloc (size);
    int recv_size = 0;
    SocketAddress sender_address;
    Timer timer;
    int x, y, z;
    TEST_ASSERT(recv_data != NULL);

    *callback_triggered = false;
    for (y = 0; (recv_size < size) && (y < NUM_UDP_RETRIES); y++) {
        tr_debug("Echo testing UDP packet size %d byte(s) async, try %d.", size, y + 1);
        recv_size = 0;
        // Retry this a few times, don't want to fail due to a flaky link
        if (sock->sendto(*host_address, (void *) send_data, size) == size) {
            // Wait for all the echoed data to arrive
            timer.start();
            while ((recv_size < size) && (timer.read_ms() < ASYNC_TEST_WAIT_TIME)) {
                if (*callback_triggered) {
                    *callback_triggered = false;
                    x = sock->recvfrom(&sender_address, (char *) recv_data + recv_size, size);
                    if (x > 0) {
                        recv_size += x;
                    }
                    tr_debug("%d byte(s) echoed back so far, %d to go.", recv_size, size - recv_size);
                    TEST_ASSERT(strcmp(sender_address.get_ip_address(), host_address->get_ip_address()) == 0);
                    TEST_ASSERT(sender_address.get_port() == host_address->get_port());
                }
                wait_ms(10);
            }
            timer.stop();
            timer.reset();

            // If everything arrived back, check it's the same as we sent
            if (recv_size == size) {
                z = memcmp(send_data, recv_data, size);
                if (z != 0) {
                    tr_debug("WARNING: mismatch, retrying");
                    tr_debug("Sent %d, |%*.*s|", size, size, size, send_data);
                    tr_debug("Rcvd %d, |%*.*s|", size, size, size, (char *) recv_data);
                    // If things don't match, it could be due to data loss (this is UDP
                    // you know...), so set recv_size to 0 to cause another try
                    recv_size = 0;
                }
            }
        }
    }

    TEST_ASSERT(recv_size == size);
    TEST_ASSERT(!connection_has_gone_down);

    free (recv_data);
}

// Send an entire TCP data buffer until done
static int sendAll(TCPSocket *sock,  const char *data, int size)
{
    int x;
    int count = 0;
    Timer timer;

    timer.start();
    while ((count < size) && (timer.read_ms() < 10000)) {
        x = sock->send(data + count, size - count);
        if (x > 0) {
            count += x;
            tr_debug("%d byte(s) sent, %d left to send.", count, size - count);
        }
        wait_ms(10);
    }
    timer.stop();

    return count;
}

// Do a TCP echo but using the asynchronous interface
static void do_tcp_echo_async(TCPSocket *sock, int size, bool *callback_triggered)
{
    void * recv_data = malloc (size);
    int recv_size = 0;
    int x, y;
    Timer timer;
    TEST_ASSERT(recv_data != NULL);

    *callback_triggered = false;
    tr_debug("Echo testing TCP packet size %d byte(s) async.", size);
    TEST_ASSERT (sendAll(sock, send_data, size) == size);

    // Wait for all the echoed data to arrive
    timer.start();
    while ((recv_size < size) && (timer.read_ms() < ASYNC_TEST_WAIT_TIME)) {
        if (*callback_triggered) {
            *callback_triggered = false;
            x = sock->recv((char *) recv_data + recv_size, size);
            TEST_ASSERT(x > 0);
            recv_size += x;
            tr_debug("%d byte(s) echoed back so far, %d to go.", recv_size, size - recv_size);
        }
        wait_ms(10);
    }
    TEST_ASSERT(recv_size == size);
    y = memcmp(send_data, recv_data, size);
    if (y != 0) {
        tr_debug("Sent %d, |%*.*s|", size, size, size, send_data);
        tr_debug("Rcvd %d, |%*.*s|", size, size, size, (char *) recv_data);
        TEST_ASSERT(false);
    }
    timer.stop();

    TEST_ASSERT(!connection_has_gone_down);

    free (recv_data);
}
#endif

// Get NTP time from a socket
static void do_ntp_sock (UDPSocket *sock, SocketAddress ntp_address)
{
    char ntp_values[48] = { 0 };
    time_t timestamp = 0;
    struct tm *localTime;
    char timeString[25];
    time_t TIME1970 = 2208988800U;
    int len;
    bool comms_done = false;

    ntp_values[0] = '\x1b';

    // Retry this a few times, don't want to fail due to a flaky link
    for (unsigned int x = 0; !comms_done && (x < NUM_UDP_RETRIES); x++) {
        sock->sendto(ntp_address, (void*) ntp_values, sizeof(ntp_values));
        len = sock->recvfrom(&ntp_address, (void*) ntp_values, sizeof(ntp_values));
        if (len > 0) {
            comms_done = true;
        }
    }
    TEST_ASSERT (comms_done);

    tr_debug("UDP: %d byte(s) returned by NTP server.", len);
    if (len >= 43) {
        timestamp |= ((int) *(ntp_values + 40)) << 24;
        timestamp |= ((int) *(ntp_values + 41)) << 16;
        timestamp |= ((int) *(ntp_values + 42)) << 8;
        timestamp |= ((int) *(ntp_values + 43));
        timestamp -= TIME1970;
        srand (timestamp);
        tr_debug("srand() called");
        localTime = localtime(&timestamp);
        if (localTime) {
            if (strftime(timeString, sizeof(timeString), "%a %b %d %H:%M:%S %Y", localTime) > 0) {
                printf("NTP timestamp is %s.\n", timeString);
            }
        }
    }
}

// Get NTP time
static void do_ntp(UbloxATCellularInterface *interface)
{
    UDPSocket sock;
    SocketAddress host_address;

    TEST_ASSERT(sock.open(interface) == 0)

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_NTP_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_NTP_PORT);

    tr_debug("UDP: NIST server %s address: %s on port %d.", MBED_CONF_APP_NTP_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    sock.set_timeout(10000);

    do_ntp_sock(&sock, host_address);

    sock.close();
}

// Use a connection, checking that it is good
static void use_connection(UbloxATCellularInterface *interface)
{
    const char * ip_address = interface->get_ip_address();
    const char * net_mask = interface->get_netmask();
    const char * gateway = interface->get_gateway();

    TEST_ASSERT(interface->is_connected());

    TEST_ASSERT(ip_address != NULL);
    tr_debug ("IP address %s.", ip_address);
    TEST_ASSERT(net_mask == NULL);
    tr_debug ("Net mask %s.", net_mask);
    TEST_ASSERT(gateway != NULL);
    tr_debug ("Gateway %s.", gateway);

    do_ntp(interface);
    TEST_ASSERT(!connection_has_gone_down);
}

// Drop a connection and check that it has dropped
static void drop_connection(UbloxATCellularInterface *interface)
{
    TEST_ASSERT(interface->disconnect() == 0);
    TEST_ASSERT(connection_has_gone_down);
    connection_has_gone_down = false;
    TEST_ASSERT(!interface->is_connected());
}

// ----------------------------------------------------------------
// TESTS
// ----------------------------------------------------------------

// Tests of stuff in the base class
void test_base_class() {
    const char *imei;
    const char *meid;
    const char *imsi;
    const char *iccid;
    int rssi;
    
    // Power-up the modem
    interface->init();
    
    // Check all of the IMEI, MEID, IMSI and ICCID calls
    imei = interface->imei();
    if (imei != NULL) {
        tr_debug("IMEI is %s.", imei);
    } else {
        TEST_ASSERT(false);
    }
    
    meid = interface->meid();
    if (meid != NULL) {
        tr_debug("MEID is %s.", meid);
    } else {
        TEST_ASSERT(false);
    }
    
    imsi = interface->imsi();
    if (imsi != NULL) {
        tr_debug("IMSI is %s.", imsi);
    } else {
        TEST_ASSERT(false);
    }
    
    iccid = interface->iccid();
    if (iccid != NULL) {
        tr_debug("ICCID is %s.", iccid);
    } else {
        TEST_ASSERT(false);
    }
    
    // Check the RSSI call at least doesn't assert
    rssi = interface->rssi();
    tr_debug("RSSI is %d dBm.", rssi);
    
    // Now connect and check that the answers for the
    // static fields are the same while connected
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    
    TEST_ASSERT(strcmp(imei, interface->imei()) == 0);
    TEST_ASSERT(strcmp(meid, interface->meid()) == 0);
    TEST_ASSERT(strcmp(imsi, interface->imsi()) == 0);
    TEST_ASSERT(strcmp(iccid, interface->iccid()) == 0);

    // Check that the RSSI call still doesn't assert
    rssi = interface->rssi();
    tr_debug("RSSI is %d dBm.", rssi);
}

// Call srand() using the NTP server
void test_set_randomise() {
    UDPSocket sock;
    SocketAddress host_address;

    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    do_ntp(interface);
    TEST_ASSERT(!connection_has_gone_down);
    drop_connection(interface);
}

#ifdef MBED_CONF_APP_ECHO_SERVER

// Test UDP data exchange
void test_udp_echo() {
    UDPSocket sock;
    SocketAddress host_address;
    SocketAddress local_address;
    int x;
    int size;

    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_UDP_PORT);

    tr_debug("UDP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    // Do a bind, just for the helluvit
    local_address.set_port(MBED_CONF_APP_LOCAL_PORT);
    TEST_ASSERT(sock.bind(local_address) == 0);

    sock.set_timeout(10000);

    // Test min, max, and some random sizes in-between
    do_udp_echo(&sock, &host_address, 1);
    do_udp_echo(&sock, &host_address, MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
    for (x = 0; x < 10; x++) {
        size = (rand() % MBED_CONF_APP_UDP_MAX_PACKET_SIZE) + 1;
        size = fix(size, MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
        do_udp_echo(&sock, &host_address, size);
    }

    sock.close();
    drop_connection(interface);
    tr_debug("%d UDP packets of size up to %d byte(s) echoed successfully.",
             x, MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
}

// Test many different sizes of UDP data arriving at once
void  test_udp_echo_recv_sizes() {
    UDPSocket sock;
    SocketAddress host_address;
    int x, y, z;
    int size;
    int tries = 0;
    unsigned int offset;
    char * recv_data;
    bool packetLoss;
    bool sendSuccess;
    Timer timer;

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_UDP_PORT);

    tr_debug("UDP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    do {
        tr_debug("--- UDP packet size test, test try %d, flushing input buffers", tries + 1);
        // First of all, clear any junk from the socket
        sock.set_timeout(1000);
        recv_data = (char *) malloc (MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
        TEST_ASSERT(recv_data != NULL);
        while (sock.recvfrom(&host_address, (void *) recv_data, MBED_CONF_APP_UDP_MAX_PACKET_SIZE) > 0) {
            // Throw it away
        }
        free (recv_data);

        sock.set_timeout(10000);

        // Throw random sized UDP packets up...
        x = 0;
        offset = 0;
        while (offset < sizeof (send_data)) {
            size = (rand() % (MBED_CONF_APP_UDP_MAX_PACKET_SIZE / 2)) + 1;
            size = fix(size, MBED_CONF_APP_UDP_MAX_PACKET_SIZE / 2);
            if (offset + size > sizeof (send_data)) {
                size = sizeof (send_data) - offset;
            }
            sendSuccess = false;
            for (y = 0; !sendSuccess && (y < NUM_UDP_RETRIES); y++) {
                tr_debug("Sending UDP packet number %d, size %d byte(s), send try %d.", x + 1, size, y + 1);
                if (sock.sendto(host_address, (void *) (send_data + offset), size) == size) {
                    sendSuccess = true;
                    offset += size;
                }
            }
            TEST_ASSERT(sendSuccess);
            x++;
        }
        tr_debug("--- All UDP packets sent");

        // ...and capture them all again afterwards
        recv_data = (char *) malloc (sizeof (send_data));
        TEST_ASSERT(recv_data != NULL);
        memset (recv_data, 0, sizeof (send_data));
        size = 0;
        y = 0;
        packetLoss = false;
        timer.start();
        while ((size < (int) sizeof (send_data)) && (timer.read_ms() < 10000)) {
            y = sock.recvfrom(&host_address, (void *) (recv_data + size), sizeof (send_data) - size);
            if (y > 0) {
                size += y;
            }
        }
        timer.stop();
        timer.reset();
        tr_debug(   "--- Either received everything back or timed out waiting");

        // Check that we reassembled everything correctly
        if (size == sizeof (send_data)) {
            for (x = 0; ((*(recv_data + x) == *(send_data + x))) && (x < (int) sizeof (send_data)); x++) {
            }
            if (x != sizeof (send_data)) {
                y = x - 5;
                if (y < 0) {
                    y = 0;
                }
                z = 10;
                if (y + z > (int) sizeof (send_data)) {
                    z = sizeof(send_data) - y;
                }
                tr_debug("   --- Difference at character %d (send \"%*.*s\", recv \"%*.*s\")",
                         x + 1, z, z, send_data + y, z, z, recv_data + y);
                packetLoss = true;
            }
        } else {
            tr_debug("   --- %d bytes missing (%d bytes received when %d were expected))",
                      sizeof (send_data) - size, size, sizeof (send_data));
            packetLoss = true;
        }
        free (recv_data);
        tries++;
    } while (packetLoss && (tries < NUM_UDP_RETRIES));

    TEST_ASSERT(!packetLoss);
    TEST_ASSERT(!connection_has_gone_down);
    sock.close();
    drop_connection(interface);
}

// Test UDP data exchange via the asynchronous sigio() mechanism
void test_udp_echo_async() {
    UDPSocket sock;
    SocketAddress host_address;
    SocketAddress local_address;
    bool callback_triggered = false;
    int x;
    int size;

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_UDP_PORT);

    tr_debug("UDP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    // Set up the async callback and set the timeout to zero
    sock.sigio(callback(async_cb, &callback_triggered));
    sock.set_timeout(0);

    // Test min, max, and some random sizes in-between
    // and this time allow the UDP packets to be fragmented
    do_udp_echo_async(&sock, &host_address, 1, &callback_triggered);
    do_udp_echo_async(&sock, &host_address, MBED_CONF_APP_UDP_MAX_FRAG_PACKET_SIZE,
                      &callback_triggered);
    for (x = 0; x < 10; x++) {
        size = (rand() % MBED_CONF_APP_UDP_MAX_FRAG_PACKET_SIZE) + 1;
        size = fix(size, MBED_CONF_APP_UDP_MAX_FRAG_PACKET_SIZE);
        do_udp_echo_async(&sock, &host_address, size, &callback_triggered);
    }

    sock.close();

    drop_connection(interface);

    tr_debug("%d UDP packets of size up to %d byte(s) echoed asynchronously and successfully.",
             x, MBED_CONF_APP_UDP_MAX_FRAG_PACKET_SIZE);
}

// Test many different sizes of TCP data arriving at once
void  test_tcp_echo_recv_sizes() {
    TCPSocket sock;
    SocketAddress host_address;
    int x, y, z;
    int size;
    unsigned int offset;
    char * recv_data;
    Timer timer;

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_TCP_PORT);

    tr_debug("TCP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    TEST_ASSERT(sock.connect(host_address) == 0);

    sock.set_timeout(10000);

    // Throw random sized TCP packets up...
    x = 0;
    offset = 0;
    while (offset < sizeof (send_data)) {
        size = (rand() % (MBED_CONF_APP_UDP_MAX_PACKET_SIZE / 2)) + 1;
        size = fix(size, MBED_CONF_APP_UDP_MAX_PACKET_SIZE / 2);
        if (offset + size > sizeof (send_data)) {
            size = sizeof (send_data) - offset;
        }
        tr_debug("Sending TCP packet number %d, size %d byte(s).", x + 1, size);
        TEST_ASSERT(sendAll(&sock, (send_data + offset), size) == size);
        offset += size;
        x++;
    }

    // ...and capture them all again afterwards
    recv_data = (char *) malloc (sizeof (send_data));
    TEST_ASSERT(recv_data != NULL);
    memset (recv_data, 0, sizeof (send_data));
    size = 0;
    x = 0;
    timer.start();
    while ((size < (int) sizeof (send_data)) && (timer.read_ms() < 30000)) {
        y = sock.recv((void *) (recv_data + size), sizeof (send_data) - size);
        tr_debug("Received TCP packet number %d, size %d byte(s).", x, y);
        size += y;
        x++;
    }
    timer.stop();
    timer.reset();

    // Check that we reassembled everything correctly
    for (x = 0; ((*(recv_data + x) == *(send_data + x))) && (x < (int) sizeof (send_data)); x++) {
    }
    if (x != sizeof (send_data)) {
        y = x - 5;
        if (y < 0) {
            y = 0;
        }
        z = 10;
        if (y + z > (int) sizeof (send_data)) {
            z = sizeof(send_data) - y;
        }
        tr_debug("Difference at character %d (send \"%*.*s\", recv \"%*.*s\")",
                 x + 1, z, z, send_data + y, z, z, recv_data + y);
        TEST_ASSERT(false);
    }
    free (recv_data);

    TEST_ASSERT(!connection_has_gone_down);
    sock.close();
    drop_connection(interface);
}

// Test TCP data exchange via the asynchronous sigio() mechanism
void test_tcp_echo_async() {
    TCPSocket sock;
    SocketAddress host_address;
    bool callback_triggered = false;
    int x;
    int size;

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_TCP_PORT);

    tr_debug("TCP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    // Set up the async callback and set the timeout to zero
    sock.sigio(callback(async_cb, &callback_triggered));
    sock.set_timeout(0);

    TEST_ASSERT(sock.connect(host_address) == 0);
    // Test min, max, and some random sizes in-between
    do_tcp_echo_async(&sock, 1, &callback_triggered);
    do_tcp_echo_async(&sock, MBED_CONF_APP_TCP_MAX_PACKET_SIZE, &callback_triggered);
    for (x = 0; x < 10; x++) {
        size = (rand() % MBED_CONF_APP_TCP_MAX_PACKET_SIZE) + 1;
        size = fix(size, MBED_CONF_APP_TCP_MAX_PACKET_SIZE);
        do_tcp_echo_async(&sock, size, &callback_triggered);
    }

    sock.close();

    drop_connection(interface);

    tr_debug("%d TCP packets of size up to %d byte(s) echoed asynchronously and successfully.",
             x, MBED_CONF_APP_TCP_MAX_PACKET_SIZE);
}
#endif

// Allocate max sockets
void test_max_sockets() {
    UDPSocket sock[MAX_NUM_SOCKETS];
    UDPSocket sockNone;
    SocketAddress host_address;

    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_NTP_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_NTP_PORT);

    // Open the first socket and use it
    TEST_ASSERT(sock[0].open(interface) == 0)
    sock[0].set_timeout(10000);
    do_ntp_sock(&sock[0], host_address);

    // Check that we stop being able to get sockets at the max number
    for (int x = 1; x < (int) (sizeof (sock) / sizeof (sock[0])); x++) {
        TEST_ASSERT(sock[x].open(interface) == 0)
    }
    TEST_ASSERT(sockNone.open(interface) < 0);

    // Now use the last one
    sock[sizeof (sock) / sizeof (sock[0]) - 1].set_timeout(10000);
    do_ntp_sock(&sock[sizeof (sock) / sizeof (sock[0]) - 1], host_address);

    // Close all of the sockets
    for (int x = 0; x < (int) (sizeof (sock) / sizeof (sock[0])); x++) {
        TEST_ASSERT(sock[x].close() == 0);
    }

    drop_connection(interface);
}

// Connect with credentials included in the connect request
void test_connect_credentials() {

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
}

// Test with credentials preset
void test_connect_preset_credentials() {

    interface->deinit();
    TEST_ASSERT(interface->init(MBED_CONF_APP_DEFAULT_PIN));
    interface->set_credentials(MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                               MBED_CONF_APP_PASSWORD);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN) == 0);
    use_connection(interface);
    drop_connection(interface);
}

// Test adding and using a SIM pin, then removing it, using the pending
// mechanism where the change doesn't occur until connect() is called
void test_check_sim_pin_pending() {

    interface->deinit();

    // Enable PIN checking (which will use the current PIN)
    // and also flag that the PIN should be changed to MBED_CONF_APP_ALT_PIN,
    // then try connecting
    interface->set_sim_pin_check(true);
    interface->set_new_sim_pin(MBED_CONF_APP_ALT_PIN);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();

    // Now change the PIN back to what it was before
    interface->set_new_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_ALT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();

    // Check that it was changed back, and this time
    // use the other way of entering the PIN
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    TEST_ASSERT(interface->connect(NULL, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                                   MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();

    // Remove PIN checking again and check that it no
    // longer matters what the PIN is
    interface->set_sim_pin_check(false);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();
    TEST_ASSERT(interface->init(NULL));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_INCORRECT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    // Put the SIM pin back to the correct value for any subsequent tests
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
}

// Test adding and using a SIM pin, then removing it, using the immediate
// mechanism
void test_check_sim_pin_immediate() {

    interface->deinit();
    interface->connection_status_cb(connection_down_cb);

    // Enable PIN checking (which will use the current PIN), change
    // the PIN to MBED_CONF_APP_ALT_PIN, then try connecting after powering on and
    // off the modem
    interface->set_sim_pin_check(true, true, MBED_CONF_APP_DEFAULT_PIN);
    interface->set_new_sim_pin(MBED_CONF_APP_ALT_PIN, true);
    interface->deinit();
    TEST_ASSERT(interface->init(NULL));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_ALT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    interface->connection_status_cb(connection_down_cb);

    // Now change the PIN back to what it was before
    interface->set_new_sim_pin(MBED_CONF_APP_DEFAULT_PIN, true);
    interface->deinit();
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    TEST_ASSERT(interface->init(NULL));
    TEST_ASSERT(interface->connect(NULL, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                                   MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    interface->connection_status_cb(connection_down_cb);

    // Remove PIN checking again and check that it no
    // longer matters what the PIN is
    interface->set_sim_pin_check(false, true);
    interface->deinit();
    TEST_ASSERT(interface->init(MBED_CONF_APP_INCORRECT_PIN));
    TEST_ASSERT(interface->connect(NULL, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                                   MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    // Put the SIM pin back to the correct value for any subsequent tests
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
}

// Test being able to connect with a local instance of the interface
// NOTE: since this local instance will fiddle with bits of HW that the
// static instance thought it owned, the static instance will no longer
// work afterwards, hence this must be run as the last test in the list
void test_connect_local_instance_last_test() {

    UbloxATCellularInterface *pLocalInterface = NULL;

    pLocalInterface = new UbloxATCellularInterface(MDMTXD, MDMRXD,
                                                   MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                                   MBED_CONF_APP_DEBUG_ON);
    pLocalInterface->connection_status_cb(connection_down_cb);

    TEST_ASSERT(pLocalInterface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                         MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;

    pLocalInterface = new UbloxATCellularInterface(MDMTXD, MDMRXD,
                                                   MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                                   MBED_CONF_APP_DEBUG_ON);
    pLocalInterface->connection_status_cb(connection_down_cb);

    TEST_ASSERT(pLocalInterface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                         MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;
}

// ----------------------------------------------------------------
// TEST ENVIRONMENT
// ----------------------------------------------------------------

// Setup the test environment
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    GREENTEA_SETUP(960, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// IMPORTANT!!! if you make a change to the tests here you should
// check whether the same change should be made to the tests under
// the PPP interface.

// Test cases
Case cases[] = {
    Case("Base class tests", test_base_class),
    Case("Set randomise", test_set_randomise),
#ifdef MBED_CONF_APP_ECHO_SERVER
    Case("UDP echo test", test_udp_echo),
# ifndef TARGET_UBLOX_C027 // Not enough RAM on little 'ole C027 to run this test
    Case("UDP recv sizes", test_udp_echo_recv_sizes),
# endif
    Case("UDP async echo test", test_udp_echo_async),
# ifndef TARGET_UBLOX_C027 // Not enough RAM on little 'ole C027 to run this test
    Case("TCP recv sizes", test_tcp_echo_recv_sizes),
# endif
    Case("TCP async echo test", test_tcp_echo_async),
#endif
#ifndef TARGET_UBLOX_C027 // Not enough RAM on little 'ole C027 to run this test
    Case("Alloc max sockets", test_max_sockets),
#endif
    Case("Connect with credentials", test_connect_credentials),
    Case("Connect with preset credentials", test_connect_preset_credentials),
#if MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS
    Case("Check SIM pin, pending", test_check_sim_pin_pending),
    Case("Check SIM pin, immediate", test_check_sim_pin_immediate),
#endif
#ifndef TARGET_UBLOX_C027 // Not enough RAM on little 'ole C027 for this
    Case("Connect using local instance, must be last test", test_connect_local_instance_last_test)
#endif
};

Specification specification(test_setup, cases);

// ----------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------

int main() {

#ifdef FEATURE_COMMON_PAL
    mbed_trace_init();

    mbed_trace_mutex_wait_function_set(lock);
    mbed_trace_mutex_release_function_set(unlock);
#endif

    interface->connection_status_cb(connection_down_cb);

    // Run tests
    return !Harness::run(specification);
}

// End Of File

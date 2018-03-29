#include "UbloxATCellularInterface.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "UDPSocket.h"
#include "mbed_stats.h"
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

// Whether we can do the mbed stats tests or not
#ifndef MBED_HEAP_STATS_ENABLED
# define MBED_HEAP_STATS_ENABLED 0
#endif

// Whether debug trace is on
#ifndef MBED_CONF_APP_DEBUG_ON
# define MBED_CONF_APP_DEBUG_ON false
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

// Servers and ports
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

// UDP packet size limit for testing
#ifndef MBED_CONF_APP_UDP_MAX_PACKET_SIZE
#  define MBED_CONF_APP_UDP_MAX_PACKET_SIZE 1024
#endif

// The number of retries for UDP exchanges
#define NUM_UDP_RETRIES 5

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

#ifdef FEATURE_COMMON_PAL
// Lock for debug prints
static Mutex mtx;
#endif

// Connection flag
static bool connection_has_gone_down = false;

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

// Test that sleep is possible both
// before and after running the driver.
void test_sleep() {
    
    TEST_ASSERT(sleep_manager_can_deep_sleep() == true);

    // Create an instance of the cellular interface
    UbloxATCellularInterface *interface =
       new UbloxATCellularInterface(MDMTXD, MDMRXD,
                                    MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                    MBED_CONF_APP_DEBUG_ON);
    interface->connection_status_cb(connection_down_cb);

    // Use it
    TEST_ASSERT(interface->init(MBED_CONF_APP_DEFAULT_PIN));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    TEST_ASSERT(sleep_manager_can_deep_sleep() == false);
    drop_connection(interface);
    
    // Destroy the instance
    delete interface;

    TEST_ASSERT(sleep_manager_can_deep_sleep() == true);
}

// Test that if communication with the modem
// fails for some reason that sleeping is possible
// afterwards (specific case found by Rostyslav Y.)
void test_sleep_failed_connection() {
    
    TEST_ASSERT(sleep_manager_can_deep_sleep() == true);

    // Create a bad instance of the cellular interface
    UbloxATCellularInterface *interface =
       new UbloxATCellularInterface(MDMTXD, MDMRXD,
                                    20, /* Silly baud rate */
                                    MBED_CONF_APP_DEBUG_ON);

    // [Fail to] use it
    TEST_ASSERT_FALSE(interface->init(MBED_CONF_APP_DEFAULT_PIN));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) != NSAPI_ERROR_OK);
    // Destroy the instance
    delete interface;

    TEST_ASSERT(sleep_manager_can_deep_sleep() == true);
}

#if MBED_HEAP_STATS_ENABLED
// Test for memory leaks.
void test_memory_leak() {
    
    mbed_stats_heap_t heap_stats_start;
    mbed_stats_heap_t heap_stats;

    mbed_stats_heap_get(&heap_stats_start);

    // Create an instance of the cellular interface
    UbloxATCellularInterface *interface =
       new UbloxATCellularInterface(MDMTXD, MDMRXD,
                                    MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                    MBED_CONF_APP_DEBUG_ON);
    interface->connection_status_cb(connection_down_cb);

    // Use it
    TEST_ASSERT(interface->init(MBED_CONF_APP_DEFAULT_PIN));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    mbed_stats_heap_get(&heap_stats);
    TEST_ASSERT(heap_stats.current_size > heap_stats_start.current_size);
    use_connection(interface);
    drop_connection(interface);
    
    // Destroy the instance
    delete interface;

    mbed_stats_heap_get(&heap_stats);
    TEST_ASSERT(heap_stats.current_size == heap_stats_start.current_size);
}
#endif

// ----------------------------------------------------------------
// TEST ENVIRONMENT
// ----------------------------------------------------------------

// Setup the test environment
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    GREENTEA_SETUP(300, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// IMPORTANT!!! if you make a change to the tests here you should
// check whether the same change should be made to the tests under
// the PPP interface.

// Test cases
Case cases[] = {
    Case("Sleep test", test_sleep),
    Case("Sleep test with failed modem comms", test_sleep_failed_connection)
#if MBED_HEAP_STATS_ENABLED
    , Case("Memory leak test", test_memory_leak)
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

    // Run tests
    return !Harness::run(specification);
}

// End Of File

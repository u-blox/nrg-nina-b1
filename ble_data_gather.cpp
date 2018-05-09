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

#include "ble_data_gather.h"
#include "utilities.h"

/**************************************************************************
 * MACROS
 *************************************************************************/

/** Debug BLE_DEBUG_PRINTF()s.
 */
#define BLE_DEBUG_PRINTF(...) if (gDebugOn) {printf(__VA_ARGS__);}

/** The maximum number of BLE addresses we can handle
 * Note that this should be big enough to hold the number
 * of discoverable BLE devices around us, not just the wanted
 * ones.  And there are loads of these things about.
 */
#define MAX_NUM_BLE_DEVICES 100

/** Storage required for a BLE address.
 */
#define BLE_ADDRESS_SIZE 6

/** Storage required for a BLE address as a string
 */
#define BLE_ADDRESS_STRING_SIZE 19

/** The maximum number of failed connection attempts before
 * we give up on a device.
 */
#define BLE_MAX_DISCOVERY_ATTEMPTS 3

/** The connection time-out.
 */
#define BLE_CONNECTION_TIMEOUT_SECONDS 3

/**************************************************************************
 * TYPES
 *************************************************************************/

/** The states that a BLE connection can be in.
 */
typedef enum {
    BLE_CONNECTION_STATE_DISCONNECTED,
    BLE_CONNECTION_STATE_CONNECTING,
    BLE_CONNECTION_STATE_CONNECTED,
    MAX_NUM_BLE_CONNECTION_STATES
} BleConnectionState;

/** The states that a BLE device can be in.
 */
typedef enum {
    BLE_DEVICE_STATE_UNKNOWN,
    BLE_DEVICE_STATE_DISCOVERED,
    BLE_DEVICE_STATE_NOT_WANTED,
    BLE_DEVICE_STATE_IS_WANTED,
    MAX_NUM_BLE_DEVICE_STATES
} BleDeviceState;

/** Structure to contain some data read from a BLE peer.
 */
typedef struct BleDataContainerTag {
    BleData dataStruct;
    BleDataContainerTag *pPrevious;
    BleDataContainerTag *pNext;
} BleDataContainer;

/** Structure defining a BLE device.
 */
typedef struct {
    char address[BLE_ADDRESS_SIZE];
    int addressType;
    BleDeviceState deviceState;
    BleConnectionState connectionState;
    Gap::Handle_t connectionHandle;
    int discoveryAttempts;
    int numCharacteristics;
    DiscoveredCharacteristic *pDeviceNameCharacteristic;
    DiscoveredCharacteristic *pWantedCharacteristic;
    char *pDeviceName;
    BleDataContainer *pDataContainer;
    BleDataContainer *pNextDataItemToRead;
} BleDevice;

/**************************************************************************
 * VARIABLES
 *************************************************************************/

/** The BLE event queue.
 */
EventQueue *gpBleEventQueue = NULL;

/** The list of BLE devices.
 */
BleDevice gBleDeviceList[MAX_NUM_BLE_DEVICES];

/** Mutex to protect the list.
 */
Mutex gMtx;

/** Helper to make sure that lock unlock pair is always balanced.
 */
#define LOCK()         { gMtx.lock()

/** Helper to make sure that lock unlock pair is always balanced.
 */
#define UNLOCK()       } gMtx.unlock()

/** The Device Name prefix to look for.
 */
static const char *gpDeviceNamePrefix = NULL;

/** The characteristic to read from a device.
 */
static int gWantedCharacteristicUuid = 0;

/** The maximum number of data items to store per device.
 */
static int gMaxNumDataItemsPerDevice = 0;

/** The number of devices in the list.
 */
static int gNumBleDevicesInList = 0;

/** The next device in the list to take a reading from.
 */
static int gNextBleDeviceToRead = 0;

/** Index into the device list for pBleGetNextDeviceName().
 */
static int gBleGetNextDeviceIndex = 0;

/** Whether to put out debug printf()s or not.
 */
static bool gDebugOn = false;

/** Gap connection parameters as recommended by ARM.
 */
static const Gap::ConnectionParams_t gConnectionParams = {50 /* minConnectionInterval */,
                                                          100 /* maxConnectionInterval */,
                                                          0 /* slaveLatency */,
                                                          600 /* connectionSupervisionTimeout (10 ms units) */};

/** Gap connection parameters as recommended by ARM.
 */
static const GapScanningParams gConnectionScanParams(100 /* interval */,
                                                     100 /* window */,
                                                     /* timeout - if this is zero the connection attempt will never time out */
                                                     BLE_CONNECTION_TIMEOUT_SECONDS,
                                                     false /* active scanning */);

/** Gap advertising types as strings, for debug only.
 * NOTE: not static to avoid compiler warnings when it is not used.
 */
const char *gpSapAdvertisingDataTypeString[] = {"VALUE_NOT_ALLOWED", "FLAGS", "INCOMPLETE_LIST_16BIT_SERVICE_IDS", "COMPLETE_LIST_16BIT_SERVICE_IDS",
                                                "INCOMPLETE_LIST_32BIT_SERVICE_IDS", "COMPLETE_LIST_32BIT_SERVICE_IDS",
                                                "INCOMPLETE_LIST_128BIT_SERVICE_ID", "COMPLETE_LIST_128BIT_SERVICE_IDS",
                                                "SHORTENED_LOCAL_NAME", "COMPLETE_LOCAL_NAME", "TX_POWER_LEVEL", "DEVICE_ID",
                                                "SLAVE_CONNECTION_INTERVAL_RANGE", "LIST_128BIT_SOLICITATION_IDS", "SERVICE_DATA",
                                                "APPEARANCE", "ADVERTISING_INTERVAL"};

/** Gap address types as strings, for debug only.
 */
static const char *gpAddressTypeString[] = {"PUBLIC", "RANDOM_STATIC", "RANDOM_PRIVATE_RESOLVABLE", "RANDOM_PRIVATE_NON_RESOLVABLE"};

/**************************************************************************
 * STATIC FUNCTION PROTOTYPES
 *************************************************************************/

/** Print the BLE device list.
 * Note that this does NOT lock the BLE list.
 * Not static to avoid compiler warnings when it is not used.
 */
void printBleDeviceList();

/** Print a BLE address out nicely as a null-terminated string.
 *
 * @param  pAddress a pointer to the BLE_ADDRESS_SIZE byte address.
 * @param  pBuf     a pointer to a place to put the nice string, must
 *                  be at least BLE_ADDRESS_STRING_LENGTH bytes long.
 * @return          a pointer to the start of the nice string.
 */
static char *pPrintBleAddress(const char *pAddress, char *pBuf);

/** Determine if two BLE address types match.
 * See https://github.com/ARMmbed/mbed-os/issues/6820.
 *
 * @param addressType1 the first address.
 * @param addressType2 the second address.
 * @return             true if the address types match,
 *                     otherwise false.
 */
static bool bleAddressTypesMatch(int addressType1, int addressType2);

/** Find a BLE device in the list by its address.
 * Note that this does NOT lock the BLE list.
 *
 * @param  pAddress    a pointer to a BLE_ADDRESS_SIZE byte address.
 * @param  addressType the address type.
 * @return             a pointer to the device entry or NULL if the
 *                     device is not found.
 */
static BleDevice *pFindBleDeviceInListByAddress(const char *pAddress, int addressType);

/** Find a BLE device in the list by the its Device Name pointer.
 * Note that this does NOT lock the BLE list.
 *
 * @param  pDeviceName the pointer to the Device Name to find.
 * @return             a pointer to the device entry or NULL if the
 *                     device is not found.
 */
static BleDevice *pFindBleDeviceInListByDeviceNamePtr(const char *pDeviceName);

/** Find a BLE device in the list by its connection handle.
 * Note that this does NOT lock the BLE list.
 *
 * @param connectionHandle the connection handle to search for.
 * @return                 a pointer to the device entry or NULL if
 *                         the device is not found.
 */
static BleDevice *pFindBleConnectionInList(Gap::Handle_t connectionHandle);

/** Find the first connected BLE device (there should be only one anyway).
 * Note that this does NOT lock the BLE list.
 *
 * @return                 a pointer to the device entry or NULL if
 *                         no connected device is found.
 */
static BleDevice *pFindBleNotDisconnectedInList();

/** Add a BLE device to the list.  If the device is already in
 * the list a pointer is returned to the (unmodified) existing
 * entry.
 * Note that this does NOT lock the BLE list.
 *
 * @param  pAddress    a pointer to the BLS address of the device.
 * @param  addressType the address type of the device.
 * @return             a pointer to the new entry or NULL if
 *                     the list is full and so no entry could be
 *                     added.
 */
static BleDevice *pAddBleDeviceToList(const char *pAddress, int addressType);

/** Remove a BLE device from the list, including its data.
 * Note that this does NOT lock the BLE list.
 *
 * @param  pAddress    a pointer to the BLS address of the device.
 * @param  addressType the address type of the device.
 * @return             the number of items in the list after the
 *                     removal has occurred.
 */
static int freeBleDevice(const char *pAddress, int addressType);

/** Clear the BLE device list.
 */
static void clearBleDeviceList();

/** Callback to obtain a reading from a BLE peer.
 */
static void getBleReadingsCallback();

/** Add a data entry for a BLE device.
 *
 * @param  pAddress    a pointer to the BLE address of the device.
 * @param  addressType the address type of the device.
 * @param  pData       a pointer to the data to add (which will be
 *                     copied into the list).
 * @param  dataLen     the length of the data pointed to by pData.
 * @return             the number of items now in the data list for the
 *                     device.
 */
static int addBleData(const char *pAddress, int addressType, const char *pData, int dataLen);

/** Return the BleData for a given device and increment its
 * pNextDataItemToRead pointer.
 * Note that this does NOT lock the BLE list.
 *
 * @param  pBleDevice  a pointer to the BLE device in the device list.
 * @return             a pointer to a copy of the BleData (malloc()ed
 *                     for the purpose, as is also the pData item inside
 *                     it).
 */
static BleData *pGetNextDataItemCopy(BleDevice *pBleDevice);

/** Remove a single item of data from a BLE device.  This
 * may result in pNextDataItemToRead being invalid: it is up
 * to the caller to sort this out.
 * Note that this does NOT lock the BLE list.
 *
 * @param pDataContainer a pointer to the entry in the list
 *                       to remove.
 */
static void freeBleDataItem(BleDataContainer *pDataContainer);

/** Clear the data for a BLE device from the given entry
 * in its data list onwards.
 * Note that this does NOT lock the BLE list.
 *
 * @param pDataContainer a pointer to the entry in the list
 *                       to start deleting from.
 */
static void clearBleDeviceData(BleDataContainer *pDataContainer);

/** Callback to process a BLE advertisement.  This method will
 * connect to the device if it is one that we want and don't already
 * know about.
 *
 * @param pParams the advertisement parameters.
 */
static void advertisementCallback(const Gap::AdvertisementCallbackParams_t *pParams);

/** Callback to act on the discovery of a service.
 * @param pService a pointer to the discovered service.
 */
static void serviceDiscoveryCallback(const DiscoveredService *pService);

/** Callback to act on the discovery of a characteristic.
 *
 * @param pCharacteristic a pointer to the discovered characteristic.
 */
static void characteristicDiscoveryCallback(const DiscoveredCharacteristic *pCharacteristic);

/** Callback to act on the end of service/characteristic discovery.
 *
 * @param connectionHandle the connection handle.
 */
static void discoveryTerminationCallback(Gap::Handle_t connectionHandle);

/** Callback to act on a connection being successfully made
 * to a BLE peer.
 *
 * @param pParams a oitner to the connection parameters.
 */
static void connectionCallback(const Gap::ConnectionCallbackParams_t *pParams);

/** Act on a connection having ended; may be called as a result
 * of a disconnection or a time-out.
 *
 * @param pBleDevice a pointer to the BLE device concerned.
 */
static void actOnDisconnect(BleDevice *pBleDevice);

/** Callback to act on a time-out in BLE.
 *
 * @param reason the reason for the time-out.
 */
static void timeoutCallback(Gap::TimeoutSource_t reason);

/** Callback to handle BLE peer disconnection.
 *
 * @param pParams a pointer to the disconnection parameters.
 */
static void disconnectionCallback(const Gap::DisconnectionCallbackParams_t *pParams);

/** Check if the device name is one we want.
 *
 * @param pResponse  a pointer to the GATT read callback parameters
 *                   for the Device Name characteristic.
 */
static void checkDeviceNameCallback(const GattReadCallbackParams *pResponse);

/** Take a reading from the wanted characteristic of a BLE peer.
 *
 * @param pResponse  a pointer to the GATT read callback parameters
 *                   for the wanted characteristic.
 */
static void readWantedValueCallback(const GattReadCallbackParams *pResponse);

/** Callback to handle a BLE initialisation error.
 *
 * @param ble   the BLE instance.
 * @param error the error.
 */
static void onBleInitError(BLE &ble, ble_error_t error);

/** Callback to handle the successful completion of BLE
 * initialisation.
 *
 * @param pParams pointer to the callback initialisation context.
 */
static void bleInitComplete(BLE::InitializationCompleteCallbackContext *pParams);

/** Throw a BLE event onto the BLE event queue (used by the BLE stack).
 *
 * @param pContext point to the BLE event processing context.
 */
static void scheduleBleEventsProcessing(BLE::OnEventsToProcessCallbackContext* pContext);

/**************************************************************************
 * STATIC FUNCTIONS
 *************************************************************************/

// Print the BLE device list.
// Note that this does NOT lock the BLE list.
void printBleDeviceList()
{
    char addressString[BLE_ADDRESS_STRING_SIZE];
    BleDevice *pBleDevice = NULL;

    for (int x = 0; (x < gNumBleDevicesInList); x++) {
        pBleDevice = &(gBleDeviceList[x]);
        BLE_DEBUG_PRINTF("%d: %s", x, pPrintBleAddress(pBleDevice->address, addressString));
        if (pBleDevice->pDeviceName != NULL) {
            BLE_DEBUG_PRINTF(" \"%s\"", pBleDevice->pDeviceName);
        }
        BLE_DEBUG_PRINTF(", device state %d, connect state %d", pBleDevice->deviceState, pBleDevice->connectionState);
        BLE_DEBUG_PRINTF(" (handle 0x%02x), connection attempt(s) %d", pBleDevice->connectionHandle,
                         pBleDevice->discoveryAttempts);
        BLE_DEBUG_PRINTF(", DeviceName* 0x%08x, Wanted* 0x%08x", (int) pBleDevice->pDeviceNameCharacteristic,
                         (int) pBleDevice->pWantedCharacteristic);
        if (pBleDevice->pDataContainer != NULL) {
            BLE_DEBUG_PRINTF(", has data");
        }
        BLE_DEBUG_PRINTF(".\n");
    }
}

// Print a BLE_ADDRESS_SIZE digit binary BLE address out nicely as a
// string; pBuf must point to storage for a string at least
// BLE_ADDRESS_STRING_LENGTH bytes long.
static char *pPrintBleAddress(const char *pAddress, char *pBuf)
{
    int x = 0;

    for (int i = (BLE_ADDRESS_SIZE - 1); i >= 0; i--) {
        sprintf(pBuf + x, "%02x:", *(pAddress + i));
        x += 3;
    }
    *(pBuf + x - 1) = 0; // Remove the final ':'

    return pBuf;
}

// Determine if two BLE address types match.
static bool bleAddressTypesMatch(int addressType1, int addressType2)
{
    bool addressType1IsRandom = (addressType1 == BLEProtocol::AddressType::RANDOM_STATIC) ||
                                (addressType1 == BLEProtocol::AddressType::RANDOM_PRIVATE_RESOLVABLE) ||
                                (addressType1 == BLEProtocol::AddressType::RANDOM_PRIVATE_NON_RESOLVABLE);
    bool addressType2IsRandom = (addressType2 == BLEProtocol::AddressType::RANDOM_STATIC) ||
                                (addressType2 == BLEProtocol::AddressType::RANDOM_PRIVATE_RESOLVABLE) ||
                                (addressType2 == BLEProtocol::AddressType::RANDOM_PRIVATE_NON_RESOLVABLE);

    return addressType1IsRandom == addressType2IsRandom;
}

// Find a BLE device in the list by its address.
// Note that this does NOT lock the BLE list.
static BleDevice *pFindBleDeviceInListByAddress(const char *pAddress, int addressType)
{
    BleDevice *pBleDevice = NULL;

    for (int x = 0; (x < gNumBleDevicesInList) && (pBleDevice == NULL); x++) {
        if (bleAddressTypesMatch(gBleDeviceList[x].addressType, addressType) &&
           (memcmp (pAddress, gBleDeviceList[x].address, sizeof (gBleDeviceList[x].address)) == 0)) {
            pBleDevice = &(gBleDeviceList[x]);
        }
    }

    return pBleDevice;
}

// Find a BLE device in the list by its device name pointer.
// Note that this does NOT lock the BLE list.
static BleDevice *pFindBleDeviceInListByDeviceNamePtr(const char *pDeviceName)
{
    BleDevice *pBleDevice = NULL;

    for (int x = 0; (x < gNumBleDevicesInList) && (pBleDevice == NULL); x++) {
        if (pDeviceName == gBleDeviceList[x].pDeviceName) {
            pBleDevice = &(gBleDeviceList[x]);
        }
    }

    return pBleDevice;
}

// Find a BLE device in the list by its connection.
// Note that this does NOT lock the BLE list.
static BleDevice *pFindBleConnectionInList(Gap::Handle_t connectionHandle)
{
    BleDevice *pBleDevice = NULL;

    for (int x = 0; (x < gNumBleDevicesInList) && (pBleDevice == NULL); x++) {
        if ((gBleDeviceList[x].connectionState == BLE_CONNECTION_STATE_CONNECTED) &&
            (gBleDeviceList[x].connectionHandle == connectionHandle)) {
            pBleDevice = &(gBleDeviceList[x]);
        }
    }

    return pBleDevice;
}

// Find the first connected device (there should be only one anyway).
// Note that this does NOT lock the BLE list.
static BleDevice *pFindBleNotDisconnectedInList()
{
    BleDevice *pBleDevice = NULL;

    for (int x = 0; (x < gNumBleDevicesInList) && (pBleDevice == NULL); x++) {
        if (gBleDeviceList[x].connectionState != BLE_CONNECTION_STATE_DISCONNECTED) {
            pBleDevice = &(gBleDeviceList[x]);
        }
    }

    return pBleDevice;
}

// Add a BLE device to the list, returning a pointer
// to the new entry.  If the device is already in the list
// a pointer is returned to the (unmodified) existing entry.
// If the list is already full then NULL is returned.
// Note that this does NOT lock the BLE list.
static BleDevice *pAddBleDeviceToList(const char *pAddress, int addressType)
{
    BleDevice *pBleDevice;

    pBleDevice = pFindBleDeviceInListByAddress(pAddress, addressType);
    if (pBleDevice == NULL) {
        if (gNumBleDevicesInList < (int) (sizeof(gBleDeviceList) / sizeof (gBleDeviceList[0]))) {
            pBleDevice = &(gBleDeviceList[gNumBleDevicesInList]);
            memcpy (pBleDevice->address, pAddress, sizeof (pBleDevice->address));
            pBleDevice->addressType = addressType;
            pBleDevice->discoveryAttempts = 0;
            pBleDevice->numCharacteristics = 0;
            pBleDevice->deviceState = BLE_DEVICE_STATE_UNKNOWN;
            pBleDevice->connectionState = BLE_CONNECTION_STATE_DISCONNECTED;
            pBleDevice->pDeviceNameCharacteristic = NULL;
            pBleDevice->pWantedCharacteristic = NULL;
            pBleDevice->pDeviceName = NULL;
            pBleDevice->pDataContainer = NULL;
            pBleDevice->pNextDataItemToRead = pBleDevice->pDataContainer;
            gNumBleDevicesInList++;
        }
    }

    return pBleDevice;
}

// Remove a BLE device from the list, returning the number
// of devices in the list afterwards.
// Note that this does NOT lock the BLE list.
static int freeBleDevice(const char *pAddress, int addressType)
{
    BleDevice *pBleDevice;

    pBleDevice = pFindBleDeviceInListByAddress(pAddress, addressType);
    if (pBleDevice != NULL) {
        pBleDevice->deviceState = BLE_DEVICE_STATE_UNKNOWN;
        if (pBleDevice->connectionState != BLE_CONNECTION_STATE_DISCONNECTED) {
            // No point in trapping any errors here as there's nothing we can do about them
            BLE::Instance().gap().disconnect(pBleDevice->connectionHandle, Gap::LOCAL_HOST_TERMINATED_CONNECTION);
        }
        pBleDevice->connectionState = BLE_CONNECTION_STATE_DISCONNECTED;
        pBleDevice->discoveryAttempts = 0;
        if (pBleDevice->pDeviceNameCharacteristic != NULL) {
            free (pBleDevice->pDeviceNameCharacteristic);
        }
        if (pBleDevice->pWantedCharacteristic != NULL) {
            free (pBleDevice->pWantedCharacteristic);
        }
        if (pBleDevice->pDeviceName != NULL) {
            free (pBleDevice->pDeviceName);
        }
        clearBleDeviceData(pBleDevice->pDataContainer);
        pBleDevice->pDataContainer = NULL;
        pBleDevice->pNextDataItemToRead = pBleDevice->pDataContainer;
        gNumBleDevicesInList--;
    }

    return gNumBleDevicesInList;
}

// Clear the BLE device list.
static void clearBleDeviceList()
{
    LOCK();
    while (freeBleDevice(gBleDeviceList[gNumBleDevicesInList - 1].address,
                         gBleDeviceList[gNumBleDevicesInList - 1].addressType) > 0) {}
    UNLOCK();
}

// Callback to get BLE readings.
static void getBleReadingsCallback()
{
    BleDevice *pBleDevice = NULL;
    char addressString[BLE_ADDRESS_STRING_SIZE];
    bool deviceRead = false;
    ble_error_t bleError;

    LOCK();
    for (int x = 0; (x < gNumBleDevicesInList) && !deviceRead; x++) {
        pBleDevice = &(gBleDeviceList[(gNextBleDeviceToRead + x) % gNumBleDevicesInList]);
        if (pBleDevice->deviceState == BLE_DEVICE_STATE_IS_WANTED) {
            bleError = BLE::Instance().gap().connect((const uint8_t *) pBleDevice->address, (BLEProtocol::AddressType_t) pBleDevice->addressType, &gConnectionParams, &gConnectionScanParams);
            if (bleError == BLE_ERROR_NONE) {
                BLE_DEBUG_PRINTF("Connecting to BLE device %s for a reading...\n", pPrintBleAddress(pBleDevice->address, addressString));
                pBleDevice->connectionState = BLE_CONNECTION_STATE_CONNECTING;
                deviceRead = true;
            }
            gNextBleDeviceToRead++;
            if (gNextBleDeviceToRead > gNumBleDevicesInList) {
                gNextBleDeviceToRead = 0;
            }
        }
    }
    UNLOCK();
}

// Add a data entry for a BLE device
static int addBleData(const char *pAddress, int addressType, const char *pData, int dataLen)
{
    BleDevice *pBleDevice = NULL;
    BleDataContainer **ppThis;
    BleDataContainer *pPrevious = NULL;
    //char addressString[BLE_ADDRESS_STRING_SIZE];
    int numItems = 0;

    LOCK();
    // Find the device
    pBleDevice = pFindBleDeviceInListByAddress(pAddress, addressType);
    if (pBleDevice != NULL) {
        //BLE_DEBUG_PRINTF("Adding data of length %d to device %s.\n", dataLen, pPrintBleAddress(pAddress, addressString));
        // Find the end of the data list
        ppThis = &(pBleDevice->pDataContainer);
        while (*ppThis != NULL) {
            //BLE_DEBUG_PRINTF("%d  pThis 0x%08x, pThis->pPrevious 0x%08x, pThis->pNext 0x%08x.\n", numItems, (int) *ppThis, (int) (*ppThis)->pPrevious, (int) (*ppThis)->pNext);
            pPrevious = *ppThis;
            ppThis = &((*ppThis)->pNext);
            numItems++;
        }

        //BLE_DEBUG_PRINTF("  %d data item(s) already in the list.\n", numItems);
        // Add the new container
        *ppThis = (BleDataContainer *) malloc(sizeof(BleDataContainer));
        if (*ppThis != NULL) {
            //BLE_DEBUG_PRINTF("  allocated %d byte(s) for the data container at time %d.\n", sizeof(BleDataContainer), (int) time(NULL));
            (*ppThis)->dataStruct.timestamp = time(NULL);
            (*ppThis)->pPrevious = pPrevious;
            (*ppThis)->pNext = NULL;
            //BLE_DEBUG_PRINTF("New pThis 0x%08x, pThis->pPrevious 0x%08x, pThis->pNext 0x%08x.\n", (int) *ppThis, (int) (*ppThis)->pPrevious, (int) (*ppThis)->pNext);
            // Add the data to the container
            (*ppThis)->dataStruct.pData = (char *) malloc(dataLen);
            if ((*ppThis)->dataStruct.pData != NULL) {
                //BLE_DEBUG_PRINTF("  allocated %d byte(s) for the data.\n", dataLen);
                memcpy ((*ppThis)->dataStruct.pData, pData, dataLen);
                (*ppThis)->dataStruct.dataLen = dataLen;
                // Connect this item into the rest of the list
                if (pPrevious != NULL) {
                    pPrevious->pNext = *ppThis;
                    //BLE_DEBUG_PRINTF("  pPrevious 0x%08x", (int) pPrevious);
                    //if (pPrevious != NULL) {
                    //    BLE_DEBUG_PRINTF(", pPrevious->pPrevious 0x%08x, pPrevious->pNext 0x%08x.\n", (int) pPrevious->pPrevious, (int) pPrevious->pNext);
                    //}
                    //BLE_DEBUG_PRINTF(".\n");
                }
                numItems++;
                //BLE_DEBUG_PRINTF("  data addition complete.\n");
            } else {
                //BLE_DEBUG_PRINTF("  unable to allocate %d byte(s) for the data.\n", dataLen);
                // If we can't allocate space for the data, go back
                // and delete the container
                free (*ppThis);
                *ppThis = NULL;
            }
        } else {
            //BLE_DEBUG_PRINTF("  unable to allocate %d byte(s) for the data container.\n", sizeof(BleDataContainer));
        }
    }
    UNLOCK();

    return numItems;
}

// Get the given data item from the given BLE device
// and increment the pNextDataItemToRead for that device.
// Note that this does NOT lock the BLE list.
static BleData *pGetNextDataItemCopy(BleDevice *pBleDevice)
{
    BleData *pDataStruct = NULL;
    const char *pTmp;
    BleDataContainer *pThis = pBleDevice->pNextDataItemToRead;

    if (pThis != NULL) {
        pDataStruct = (BleData *) malloc(sizeof(BleData));
        if (pDataStruct != NULL) {
            memcpy(pDataStruct, &(pThis->dataStruct), sizeof (*pDataStruct));
            // Also make a copy of the malloc()ed pData
            // in the copied structure pDataStruct to give it complete
            // safety
            if (pDataStruct->dataLen > 0) {
                pTmp = pDataStruct->pData;
                pDataStruct->pData = (char *) malloc(pDataStruct->dataLen);
                if (pDataStruct->pData != NULL) {
                    memcpy (pDataStruct->pData, pTmp, pDataStruct->dataLen);
                } else {
                    // If that malloc() failed, reverse the first one
                    free(pDataStruct);
                    pDataStruct = NULL;
                }
            }
        }
        pBleDevice->pNextDataItemToRead = pThis->pNext;
    }

    return pDataStruct;
}

// Remove a BLE data item from the list
// Note that this does NOT lock the BLE list.
static void freeBleDataItem(BleDataContainer *pDataContainer)
{
    // Free the data for this entry
    if (pDataContainer->dataStruct.pData != NULL) {
        free (pDataContainer->dataStruct.pData);
    }
    // Seal up the list
    if (pDataContainer->pPrevious != NULL) {
        pDataContainer->pPrevious->pNext = pDataContainer->pNext;
    }
    if (pDataContainer->pNext != NULL) {
        pDataContainer->pNext->pPrevious = pDataContainer->pPrevious;
    }
    // Free this container
    free (pDataContainer);
}

// Clear the data for a BLE device from the given entry onwards.
// Note that this does NOT lock the BLE list.
static void clearBleDeviceData(BleDataContainer *pDataContainer)
{
    BleDataContainer *pTmp;
    
    while (pDataContainer != NULL) {
        pTmp = pDataContainer->pNext;
        freeBleDataItem(pDataContainer);
        pDataContainer = pTmp;
    }
}

// Process an advertisement and connect to the device if
// it is one that we want.
static void advertisementCallback(const Gap::AdvertisementCallbackParams_t *pParams)
{
    char buf[BLE_ADDRESS_STRING_SIZE + 32];
    BleDevice *pBleDevice;
    ble_error_t bleError;
    int recordLength;
    int x = 0;
    bool discoverable = false;

    BLE_DEBUG_PRINTF("BLE device %s is visible, has a %s address",
                     pPrintBleAddress((char *) pParams->peerAddr, buf), gpAddressTypeString[pParams->addressType]);
    // Check if the device is discoverable
    while ((x < pParams->advertisingDataLen) && !discoverable) {
        /* The advertising payload is a collection of key/value records where
         * byte 0: length of the record excluding this byte but including the "type" byte
         * byte 1: The key, it is the type of the data
         * byte [2..N] The value. N is equal to byte0 - 1 */
        recordLength = pParams->advertisingData[x];
        if ((recordLength > 0) && (pParams->advertisingData[x + 1] != 0)) {  // Type of 0 is not allowed
            const int type = pParams->advertisingData[x + 1];
            const char *pValue = (const char *) pParams->advertisingData + x + 2;
            //BLE_DEBUG_PRINTF("  Advertising payload type 0x%02x", type);
            //if (type < (int) (sizeof(gapAdvertisingDataTypeString) / sizeof(gapAdvertisingDataTypeString[0]))) {
            //    BLE_DEBUG_PRINTF(" (%s)", gapAdvertisingDataTypeString[type]);
            //}
            //BLE_DEBUG_PRINTF(" (%d byte(s)): 0x%.*s.\n", recordLength - 1,
            //         bytesToHexString(pValue, recordLength - 1, buf, sizeof(buf)), buf);
            if ((type == GapAdvertisingData::FLAGS) &&
                (*pValue & (GapAdvertisingData::LE_GENERAL_DISCOVERABLE | GapAdvertisingData::LE_LIMITED_DISCOVERABLE))) {
                discoverable = true;
            }
            x += recordLength + 1;
        } else {
            x++;
        }
    }

    if (discoverable) {
        BLE_DEBUG_PRINTF(" and is discoverable");
        LOCK();
        pBleDevice = pAddBleDeviceToList((const char *) pParams->peerAddr, (int) pParams->addressType);
        if (pBleDevice != NULL) {
            if (pBleDevice->deviceState == BLE_DEVICE_STATE_UNKNOWN) {
                if ((pBleDevice->connectionState != BLE_CONNECTION_STATE_CONNECTED) ||
                    (pBleDevice->connectionState != BLE_CONNECTION_STATE_CONNECTING)) {
                    BLE_DEBUG_PRINTF(", attempting to connect to it");
                    bleError = BLE::Instance().gap().connect(pParams->peerAddr, pParams->addressType, &gConnectionParams, &gConnectionScanParams);
                    if (bleError == BLE_ERROR_NONE) {
                        pBleDevice->connectionState = BLE_CONNECTION_STATE_CONNECTING;
                        pBleDevice->discoveryAttempts++;
                       BLE_DEBUG_PRINTF(", connect() successfully issued.\n");
                    } else if (bleError == BLE_ERROR_INVALID_STATE) {
                        BLE_DEBUG_PRINTF(" but CAN'T as BLE is in an invalid state (may already be connecting?).\n");
                    } else {
                        BLE_DEBUG_PRINTF(" but unable to issue connect (error %d \"%s\").\n", bleError, BLE::Instance().errorToString(bleError));
                    }
                } else {
                    BLE_DEBUG_PRINTF(" but we are already connected to it (or attempting to do so).\n");
                }
            } else {
                BLE_DEBUG_PRINTF(" but we already know about it so there is nothing to do.\n");
            }
        } else {
            BLE_DEBUG_PRINTF(" but the BLE device list is full (%d device(s))!\n", gNumBleDevicesInList);
        }
        UNLOCK();
    } else {
        BLE_DEBUG_PRINTF(" but is not discoverable.\n");
    }
}

// Act on the discovery of a service.
static void serviceDiscoveryCallback(const DiscoveredService *pService)
{
    if (pService->getUUID().shortOrLong() == UUID::UUID_TYPE_SHORT) {
        BLE_DEBUG_PRINTF("Service 0x%x attrs[%u %u].\n", pService->getUUID().getShortUUID(),
                         pService->getStartHandle(), pService->getEndHandle());
    } else {
        BLE_DEBUG_PRINTF("Service 0x");
        const char *pLongUUIDBytes = (char *) pService->getUUID().getBaseUUID();
        for (unsigned int i = 0; i < UUID::LENGTH_OF_LONG_UUID; i++) {
            BLE_DEBUG_PRINTF("%02x", *(pLongUUIDBytes + i));
        }
        BLE_DEBUG_PRINTF(" attrs[%u %u].\n", pService->getStartHandle(), pService->getEndHandle());
    }
}

// Act on the discovery of a characteristic.
static void characteristicDiscoveryCallback(const DiscoveredCharacteristic *pCharacteristic)
{
    char addressString[BLE_ADDRESS_STRING_SIZE];
    UUID::ShortUUIDBytes_t uuid = pCharacteristic->getUUID().getShortUUID();
    DiscoveredCharacteristic **ppStoredCharacteristic = NULL;
    BleDevice *pBleDevice;

    BLE_DEBUG_PRINTF("  Characteristic 0x%x valueAttr[%u] props[0x%x].\n", pCharacteristic->getUUID().getShortUUID(),
                     pCharacteristic->getValueHandle(), (uint8_t) pCharacteristic->getProperties().broadcast());

    LOCK();
    pBleDevice = pFindBleConnectionInList(pCharacteristic->getConnectionHandle());
    if (pBleDevice != NULL) {
        pBleDevice->numCharacteristics++;
        // If this device isn't marked as "not wanted" and if we're not already
        // reading from it...
        if (uuid == BLE_UUID_GAP_CHARACTERISTIC_DEVICE_NAME) {
            ppStoredCharacteristic = &(pBleDevice->pDeviceNameCharacteristic);
        } else if (uuid == gWantedCharacteristicUuid) {
            ppStoredCharacteristic = &(pBleDevice->pWantedCharacteristic);
        }

        if (ppStoredCharacteristic != NULL) {
            // Take a copy of the characteristic so that we can read it once service discovery has ended
            BLE_DEBUG_PRINTF("  BLE device %s has a characteristic we want to read (0x%04x).\n", pPrintBleAddress(pBleDevice->address, addressString), uuid);
            if (*ppStoredCharacteristic != NULL) {
                free(*ppStoredCharacteristic);
            }
            *ppStoredCharacteristic = (DiscoveredCharacteristic *) malloc(sizeof (*pCharacteristic));
            if (*ppStoredCharacteristic != NULL) {
                memcpy(*ppStoredCharacteristic, pCharacteristic, sizeof (*pCharacteristic));
            }
        }
    }
    UNLOCK();
}

// Handle end of service discovery.
static void discoveryTerminationCallback(Gap::Handle_t connectionHandle)
{
    char addressString[BLE_ADDRESS_STRING_SIZE];
    BleDevice *pBleDevice;
    ble_error_t bleError;

    LOCK();
    pBleDevice = pFindBleConnectionInList(connectionHandle);
    BLE_DEBUG_PRINTF("Terminated service discovery for handle %u", connectionHandle);
    if (pBleDevice != NULL) {
        BLE_DEBUG_PRINTF(", BLE device %s, %d characteristic(s) found",
                         pPrintBleAddress(pBleDevice->address, addressString),
                         pBleDevice->numCharacteristics);
        if (pBleDevice->numCharacteristics == 0) {
            BLE_DEBUG_PRINTF(", dropping it");
            pBleDevice->deviceState = BLE_DEVICE_STATE_NOT_WANTED;
        } else {
            pBleDevice->deviceState = BLE_DEVICE_STATE_DISCOVERED;
            if (pBleDevice->pDeviceNameCharacteristic != NULL) {
                if (pBleDevice->pWantedCharacteristic != NULL) {
                    // Read the device's name characteristic to see if we want it
                    BLE_DEBUG_PRINTF(", reading the DeviceName characteristic");
                    bleError = pBleDevice->pDeviceNameCharacteristic->read(0, checkDeviceNameCallback);
                    if (bleError != BLE_ERROR_NONE) {
                        BLE_DEBUG_PRINTF(" but unable to do so (error %d)", bleError);
                    }
                } else {
                    BLE_DEBUG_PRINTF(" but dropping it as the wanted characteristic (0x%04x) was not found",
                                     gWantedCharacteristicUuid);
                    pBleDevice->deviceState = BLE_DEVICE_STATE_NOT_WANTED;
                    // Free up the Device Name characteristic to save RAM
                    free(pBleDevice->pDeviceNameCharacteristic);
                    pBleDevice->pDeviceNameCharacteristic = NULL;
                }
            } else {
                BLE_DEBUG_PRINTF(" but dropping it as no DeviceName characteristic was found");
                pBleDevice->deviceState = BLE_DEVICE_STATE_NOT_WANTED;
                // Free up the wanted characteristic if it was there to save RAM
                if (pBleDevice->pWantedCharacteristic != NULL) {
                    free(pBleDevice->pWantedCharacteristic);
                    pBleDevice->pWantedCharacteristic = NULL;
                }
            }
        }
    }
    BLE_DEBUG_PRINTF(".\n");
    UNLOCK();

    // Disconnect immediately to save time if we can, noting that
    // this might fail if we're already disconnecting anyway
    BLE::Instance().gap().disconnect(connectionHandle, Gap::LOCAL_HOST_TERMINATED_CONNECTION);
}

// When a connection has been made, find out what services are available
// and their characteristics.
static void connectionCallback(const Gap::ConnectionCallbackParams_t *pParams)
{
    char addressString[BLE_ADDRESS_STRING_SIZE];
    BleDevice *pBleDevice;
    ble_error_t bleError;

    LOCK();
    pBleDevice = pFindBleDeviceInListByAddress((char *) pParams->peerAddr, (int) pParams->peerAddrType);
    BLE_DEBUG_PRINTF("BLE device %s (address type %s) is connected (handle %u).\n",
                     pPrintBleAddress((char *) pParams->peerAddr, addressString), gpAddressTypeString[pParams->peerAddrType],
                     pParams->handle);
    if (pBleDevice != NULL) {
        pBleDevice->connectionHandle = pParams->handle;
        pBleDevice->connectionState = BLE_CONNECTION_STATE_CONNECTED;
        if (pParams->role == Gap::CENTRAL) {
            // If we're not reading the device already, find out about it first,
            // otherwise just read it straight away
            if (pBleDevice->deviceState != BLE_DEVICE_STATE_IS_WANTED) {
                pBleDevice->numCharacteristics = 0;
                BLE_DEBUG_PRINTF("  Attempting to discover its services and characteristics...\n");
                BLE &ble = BLE::Instance();
                ble.gattClient().onServiceDiscoveryTermination(discoveryTerminationCallback);
                bleError = ble.gattClient().launchServiceDiscovery(pParams->handle, serviceDiscoveryCallback,
                                                                   characteristicDiscoveryCallback,
                                                                   BLE_UUID_UNKNOWN, BLE_UUID_UNKNOWN);
                if (bleError != BLE_ERROR_NONE) {
                    BLE_DEBUG_PRINTF("  !!! Unable to launch service discovery (error %d, \"%s\") !!!!\n", bleError, ble.errorToString(bleError));
                }
            } else {
                MBED_ASSERT(pBleDevice->pWantedCharacteristic != NULL);
                BLE_DEBUG_PRINTF("  Reading the wanted characteristic (0x%04x) of BLE device %s.\n",
                                 pBleDevice->pWantedCharacteristic->getUUID().getShortUUID(),
                                 pPrintBleAddress(pBleDevice->address, addressString));
                bleError = pBleDevice->pWantedCharacteristic->read(0, readWantedValueCallback);
                if (bleError != BLE_ERROR_NONE) {
                    BLE_DEBUG_PRINTF("  Unable to start read of wanted characteristic (error %d).\n", bleError);
                }
            }
        }
    }
    UNLOCK();
}

// Do disconnection actions, may be called as a result
// of a disconnection or a time-out.
static void actOnDisconnect(BleDevice *pBleDevice)
{
    char addressString[BLE_ADDRESS_STRING_SIZE];

    BLE_DEBUG_PRINTF("Disconnected from device %s", pPrintBleAddress(pBleDevice->address, addressString));
    if ((pBleDevice->connectionState == BLE_CONNECTION_STATE_CONNECTING) &&
        (pBleDevice->deviceState == BLE_DEVICE_STATE_DISCOVERED)) {
        if (pBleDevice->discoveryAttempts >= BLE_MAX_DISCOVERY_ATTEMPTS) {
            // If we were discovering this device and it's rudely bounced us too
            // many times then it probably doesn't want to know about us so cross it
            // off our Christmas list
            pBleDevice->deviceState = BLE_DEVICE_STATE_NOT_WANTED;
            BLE_DEBUG_PRINTF(" too many times while attempting discovery, so dropping it");
        } else {
            BLE_DEBUG_PRINTF(" on discovery attempt %d", pBleDevice->discoveryAttempts);
        }
    }
    pBleDevice->connectionState = BLE_CONNECTION_STATE_DISCONNECTED;
    BLE_DEBUG_PRINTF(".\n");

    /* Start scanning again */
    BLE::Instance().gap().startScan(advertisementCallback);
}

// When a time-out has occurred, determine what to do.
static void timeoutCallback(Gap::TimeoutSource_t reason)
{
    BleDevice *pBleDevice;

    switch (reason) {
        case Gap::TIMEOUT_SRC_ADVERTISING:
            BLE_DEBUG_PRINTF("Time-out while advertising.\n");
        break;
        case Gap::TIMEOUT_SRC_SECURITY_REQUEST:
            BLE_DEBUG_PRINTF("Time-out on a security request.\n");
        break;
        case Gap::TIMEOUT_SRC_SCAN:
            // Connection timeouts can appear as scan timeouts
            // because of the way they are done
            BLE_DEBUG_PRINTF("Time-out while scanning or connecting.\n");
            LOCK();
            pBleDevice = pFindBleNotDisconnectedInList();
            if (pBleDevice != NULL) {
                actOnDisconnect(pBleDevice);
            }
            UNLOCK();
        break;
        case Gap::TIMEOUT_SRC_CONN:
            BLE_DEBUG_PRINTF("Time-out of connection [attempt].\n");
            LOCK();
            pBleDevice = pFindBleNotDisconnectedInList();
            if (pBleDevice != NULL) {
                actOnDisconnect(pBleDevice);
            }
            UNLOCK();
        break;
        default:
            BLE_DEBUG_PRINTF("Time-out, type unknown (%d).\n", reason);
        break;
    }
}

// Handle BLE peer disconnection.
static void disconnectionCallback(const Gap::DisconnectionCallbackParams_t *pParams)
{
    BleDevice *pBleDevice;

    LOCK();
    pBleDevice = pFindBleConnectionInList(pParams->handle);
    BLE_DEBUG_PRINTF("Disconnected (handle %d).\n", pParams->handle);
    if (pBleDevice != NULL) {
        actOnDisconnect(pBleDevice);
    }
    UNLOCK();
}


// Check if the device is an interesting one.
static void checkDeviceNameCallback(const GattReadCallbackParams *pResponse)
{
    BleDevice *pBleDevice;
    char addressString[BLE_ADDRESS_STRING_SIZE];

    // See if the prefix on the data (which will be the device name), is
    // not one we are interested in then don't bother with this device again
    LOCK();
    pBleDevice = pFindBleConnectionInList(pResponse->connHandle);
    if (pBleDevice != NULL) {
        if ((pResponse->len < strlen(gpDeviceNamePrefix)) ||
            (memcmp(pResponse->data, gpDeviceNamePrefix, strlen(gpDeviceNamePrefix)) != 0)) {
            pBleDevice->deviceState = BLE_DEVICE_STATE_NOT_WANTED;
            BLE_DEBUG_PRINTF("BLE device %s (with name \"%.*s\") is not one of ours, dropping it.\n",
                             pPrintBleAddress(pBleDevice->address, addressString),
                             pResponse->len, pResponse->data);
        } else {
            BLE_DEBUG_PRINTF("Found one of our BLE devices: %s, with name \"%.*s\".\n",
                             pPrintBleAddress(pBleDevice->address, addressString),
                             pResponse->len, pResponse->data);
            // Save the device name
            pBleDevice->pDeviceName = (char *) malloc(pResponse->len + 1);
            if (pBleDevice->pDeviceName != NULL) {
                memcpy (pBleDevice->pDeviceName, pResponse->data, pResponse->len);
                *(pBleDevice->pDeviceName + pResponse->len) = 0;  // Add terminator
            }
            // Free up the Device Name characteristic to save RAM
            if (pBleDevice->pDeviceNameCharacteristic != NULL) {
                free(pBleDevice->pDeviceNameCharacteristic);
                pBleDevice->pDeviceNameCharacteristic = NULL;
            }
            pBleDevice->deviceState = BLE_DEVICE_STATE_IS_WANTED;
        }
        // Disconnect immediately to save time if we can, noting that
        // this might fail if we're already disconnecting anyway
        BLE::Instance().gap().disconnect(pResponse->connHandle, Gap::LOCAL_HOST_TERMINATED_CONNECTION);
    }
    UNLOCK();
}

// Take a reading from the wanted characteristic.
static void readWantedValueCallback(const GattReadCallbackParams *pResponse)
{
    BleDevice *pBleDevice;
    char buf[32];

    LOCK();
    pBleDevice = pFindBleConnectionInList(pResponse->connHandle);
    if (pBleDevice != NULL) {
        BLE_DEBUG_PRINTF("Read from BLE device %s of characteristic 0x%04x",
               pPrintBleAddress(pBleDevice->address, buf), gWantedCharacteristicUuid);
        if (pResponse->len > 0) {
            BLE_DEBUG_PRINTF(" returned %d byte(s): 0x%.*s", pResponse->len,
                             bytesToHexString((const char *) pResponse->data, pResponse->len, buf, sizeof(buf)), buf);
            BLE_DEBUG_PRINTF(", %d data item(s) now in its list.\n", addBleData(pBleDevice->address, pBleDevice->addressType,
                                                           (const char *) pResponse->data, pResponse->len));
        } else {
            BLE_DEBUG_PRINTF(" returned 0 byte(s) of data.\n");
        }

        // Disconnect immediately to save time if we can, noting that
        // this might fail if we're already disconnecting anyway
        BLE::Instance().gap().disconnect(pResponse->connHandle, Gap::LOCAL_HOST_TERMINATED_CONNECTION);
    }
    UNLOCK();
}

// Handle BLE initialisation error.
static void onBleInitError(BLE &ble, ble_error_t error)
{
   BLE_DEBUG_PRINTF("!!! BLE Error %d !!!\n", error);
}

// Handle BLE being initialised, finish configuration here.
static void bleInitComplete(BLE::InitializationCompleteCallbackContext *pParams)
{
    BLE& ble = pParams->ble;
    ble_error_t error = pParams->error;
    Gap::AddressType_t addr_type;
    Gap::Address_t address;
    BLE::Instance().gap().getAddress(&addr_type, address);
    char addressString[BLE_ADDRESS_STRING_SIZE];

    if (error != BLE_ERROR_NONE) {
        /* In case of error, forward the error handling to onBleInitError */
        onBleInitError(ble, error);
        return;
    }

    /* Ensure that it is the default instance of BLE */
    if (ble.getInstanceID() != BLE::DEFAULT_INSTANCE) {
        return;
    }

    BLE_DEBUG_PRINTF("This device's BLE address is %s.\n", pPrintBleAddress((char *) address, addressString));

    ble.gap().onDisconnection(disconnectionCallback);
    ble.gap().onConnection(connectionCallback);
    ble.gap().onTimeout(timeoutCallback);

    // scan interval: 1000 ms and scan window: 500 ms.
    // Every 1000 ms the device will scan for 500 ms
    // This means that the device will scan continuously.
    ble.gap().setScanParams(1000, 500);
    ble.gap().startScan(advertisementCallback);

    // Try to get readings every second.
    MBED_ASSERT(gpBleEventQueue != NULL);
    gpBleEventQueue->call_every(1000, getBleReadingsCallback);
}

// Throw a BLE event onto the BLE event queue.
static void scheduleBleEventsProcessing(BLE::OnEventsToProcessCallbackContext* pContext)
{
    BLE &ble = BLE::Instance();
    MBED_ASSERT(gpBleEventQueue != NULL);
    gpBleEventQueue->call(Callback<void()>(&ble, &BLE::processEvents));
}

/**************************************************************************
 * PUBLIC FUNCTIONS
 *************************************************************************/

// Initialise.
void bleInit(const char *gpDeviceNamePrefix, int wantedCharacteristicUuid,
             int maxNumDataItemsPerDevice, bool debugOn)
{
    gpDeviceNamePrefix = gpDeviceNamePrefix;
    gWantedCharacteristicUuid = wantedCharacteristicUuid;
    gMaxNumDataItemsPerDevice = maxNumDataItemsPerDevice;
    gDebugOn = debugOn;
    gNumBleDevicesInList = 0;
    gBleGetNextDeviceIndex = 0;

    gpBleEventQueue = new EventQueue (/* event count */ 16 * EVENTS_EVENT_SIZE);
    // TODO treat gMaxNumDataItemsPerDevice
}

// Shutdown.
void bleDeinit()
{
    clearBleDeviceList();
    BLE::Instance().shutdown();
    if (gpBleEventQueue != NULL) {
        delete gpBleEventQueue;
    }
    gpBleEventQueue = NULL;
}

// Run BLE for a given time; use -1 for infinity, in which
// case this function will never return.
bool bleRun(int durationMs)
{
    bool success = false;

    if (gpBleEventQueue != NULL) {
        gNextBleDeviceToRead = 0;
        BLE::Instance().onEventsToProcess(scheduleBleEventsProcessing);
        BLE::Instance().init(bleInitComplete);
        gpBleEventQueue->dispatch(durationMs);
        success = true;
    }

    return success;
}

// Get the number of devices in the list.
int bleGetNumDevices()
{
    return gNumBleDevicesInList;
}

// Get the first device name in the list.
const char *pBleGetFirstDeviceName()
{
    gBleGetNextDeviceIndex = 0;

    return pBleGetNextDeviceName();
}

// Get the next device name in the list.
const char *pBleGetNextDeviceName()
{
    const char *pDeviceName = NULL;

    LOCK();
    if (gNumBleDevicesInList > gBleGetNextDeviceIndex) {
        pDeviceName = gBleDeviceList[gBleGetNextDeviceIndex].pDeviceName;
        gBleGetNextDeviceIndex++;
    }
    UNLOCK();

    return pDeviceName;
}

// Get the number of data items that have been
// read from a given device.
int bleGetNumDataItems(const char *pDeviceName)
{
    int numDataItems = -1;
    BleDevice *pBleDevice;
    BleDataContainer *pThis;

    LOCK();
    pBleDevice = pFindBleDeviceInListByDeviceNamePtr(pDeviceName);
    if (pBleDevice != NULL) {
        numDataItems = 0;
        pThis = pBleDevice->pDataContainer;
        while (pThis != NULL) {
            pThis = pThis->pNext;
            numDataItems++;
        }
    }
    UNLOCK();

    return numDataItems;
}

// Get the first data item for the given device name.
BleData *pBleGetFirstDataItem(const char *pDeviceName, bool andDelete)
{
    BleData *pDataItem = NULL;
    BleDevice *pBleDevice;
    BleDataContainer *pTmp;

    LOCK();
    pBleDevice = pFindBleDeviceInListByDeviceNamePtr(pDeviceName);
    if (pBleDevice != NULL) {
        pBleDevice->pNextDataItemToRead = pBleDevice->pDataContainer;
        pDataItem = pGetNextDataItemCopy(pBleDevice);
        if ((pDataItem != NULL) && andDelete) {
            pTmp = pBleDevice->pDataContainer->pNext;
            freeBleDataItem(pBleDevice->pDataContainer);
            pBleDevice->pDataContainer = pTmp;
            pBleDevice->pNextDataItemToRead = pBleDevice->pDataContainer;

        }
    }
    UNLOCK();

    return pDataItem;
}

// Get the next data item for the given device name.
BleData *pBleGetNextDataItem(const char *pDeviceName)
{
    BleData *pDataItem = NULL;
    BleDevice *pBleDevice;

    LOCK();
    pBleDevice = pFindBleDeviceInListByDeviceNamePtr(pDeviceName);
    if (pBleDevice != NULL) {
        pDataItem = pGetNextDataItemCopy(pBleDevice);
    }
    UNLOCK();

    return pDataItem;
}

// End of file
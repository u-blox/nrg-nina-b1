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
#ifndef _BLE_DATA_GATHER_
#define _BLE_DATA_GATHER_

#include <events/mbed_events.h>
#include <mbed.h>
#include "ble/BLE.h"
#include "ble/DiscoveredCharacteristic.h"
#include "ble/DiscoveredService.h"

/**********************************************************************
 * TYPES
 **********************************************************************/

/** Structure to contain a reading from a BLE peer.
 */
typedef struct {
    int timestamp; /// Unix timestamp.
    char *pData; /// This will be malloc()ed; it is up to the caller to free()
    int dataLen;
} BleData;

/**********************************************************************
 * FUNCTIONS
 **********************************************************************/

/** Initialise.
 *
 * @param pDeviceNamePrefix        a pointer to a NULL terminated string
 *                                 defining the initial characters of the
 *                                 Device Name to look for, e.g. to find
 *                                 devices "BLAH1234" and "BLAHxy", the prefix
 *                                 would be "BLAH".  The Device Name is a
 *                                 standard characteristic for BLE devices:
 *                                 BLE_UUID_GAP_CHARACTERISTIC_DEVICE_NAME.
 * @param wantedCharacteristicUuid the UUID of the characteristic to read
 *                                 from the wanted devices.
 * @param maxNumDataItemsPerDevice the maximum number of data items to collect,
 *                                 older items will be lost when the maximum
 *                                 number of data items is reached.
 * @param debugOn                  true to switch on debug printf()s.
 */
 void bleInit(const char *pDeviceNamePrefix, int wantedCharacteristicUuid,
              int maxNumDataItemsPerDevice, bool debugOn);

 /* Shutdown.
  */
 void bleDeinit();

/** Run BLE for a given time.
 *
 * @param durationMs the duration to run for in ms;
 *                   use -1 for infinity, in which
 *                   case this method will never
 *                   return.
 * @return           true on success, otherwise false.
 */
bool bleRun(int durationMs);

/** Get the number of devices in the list.
 *
 * @return the number of devices in the list.
 */
int bleGetNumDevices();

/** Get the first device name in the list.
 *
 * @return  pointer to the device name or NULL
 *          if there are none.
 */
const char *pBleGetFirstDeviceName();

/** Get the next device name in the list.
 *
 * @return  pointer to the next device name or
 *          NULL if the end of the list has been
 *          reached.
 */
const char *pBleGetNextDeviceName();

/** Get the number of data items that have been
 * read from a given device.
 *
 * @param  pDeviceName a pointer to the device name
 *                     string that the data items
 *                     are from, as returned by
 *                     pGetFirstDeviceName() or
 *                     pGetNextDeviceName() (not a
 *                     copy, the address is the
 *                     important thing).
 * @return             the number of data items in
 *                     the list for that device
 *                     name or -1 if pDeviceName
 *                     cannot be found.
 */
int bleGetNumDataItems(const char *pDeviceName);

/** Get the first data item for the given device
 *  name.
 *
 * @param  pDeviceName a pointer to the device name
 *                     string that the data items
 *                     are from, as returned by
 *                     pGetFirstDeviceName() or
 *                     pGetNextDeviceName() (not a
 *                     copy, the address is the
 *                     important thing).
 * @param  andDelete   if true, then the first data item
 *                     is deleted from the data store
 *                     after it has been returned; a
 *                     subsequent call to this function
 *                     will then effectively return the
 *                     next data item.
 * @return             pointer to the first data item
 *                     or NULL if there are none.
 *                     This is a COPY of the data
 *                     item, malloc()ed for the purpose
 *                     by this function, and it is up
 *                     to the caller to free() it when
 *                     done.  Note also that the pData
 *                     item inside the BleData structure
 *                     is ALSO malloc()ed by this
 *                     function and should be free()ed
 *                     before the BleData structure is
 *                     free()ed to avoid memory leaks.
 */
BleData *pBleGetFirstDataItem(const char *pDeviceName,
                              bool andDelete);

/** Get the next data item for the given device
 *  name.
 *
 * @param  pDeviceName a pointer to the device name
 *                     string that the data items are
 *                     from, as returned by
 *                     pGetFirstDeviceName() or
 *                     pGetNextDeviceName() (not a copy,
 *                     the address is the important thing).
 * @return             pointer to the next data item
 *                     or NULL if there are no more.
 *                     This is a COPY of the data
 *                     item, malloc()ed for the purpose
 *                     by this function, and it is up
 *                     to the caller to free() it when
 *                     done.  Note also that the pData
 *                     item inside the BleData structure
 *                     is ALSO malloc()ed by this
 *                     function and should be free()ed
 *                     before the BleData structure is
 *                     free()ed to avoid memory leaks.
 */
BleData *pBleGetNextDataItem(const char *pDeviceName);

#endif // _BLE_DATA_GATHER_

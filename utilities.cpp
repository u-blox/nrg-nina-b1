/*
 * Copyright (C) u-blox Melbourn Ltd
 * u-blox Melbourn Ltd, Melbourn, UK
 *
 * All rights reserved.
 *
 * This source file is the sole property of u-blox Melbourn Ltd.
 * Reproduction or utilisation of this source in whole or part is
 * forbidden without the written consent of u-blox Melbourn Ltd.
 */

#include "stdint.h"
#include "stdlib.h"
#include "string.h"

// ----------------------------------------------------------------
// COMPILE-TIME CONSTANTS
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// TYPES
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

static const char hexTable[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

// ----------------------------------------------------------------
// STATIC FUNCTIONS
// ----------------------------------------------------------------

// Reverse an array working in stepSize chunks
// e.g., if stepSize is 1 then 123456 becomes 654321
// while if stepSize is 2 then 123456 becomes 563412.
// lenBuf must always be a multiple of stepSize.
static void reverseArray(char *pBuf, int lenBuf, int stepSize)
{
    char *pStore;
    char *pDst;
    char *pSrc = pBuf + lenBuf;
    
    pStore = (char *) malloc(lenBuf);
    if (pStore != NULL) {
        pDst = pStore;
        for (int x = 0; x < lenBuf; x += stepSize) {
            pSrc -= stepSize;
            for (int y = 0; y < stepSize; y++) {
                *(pDst + y) = *(pSrc + y);
            }
            pDst += stepSize;
        }
        memcpy(pBuf, pStore, lenBuf);
        free(pStore);
    }
}

// ----------------------------------------------------------------
// PUBLIC FUNCTIONS
// ----------------------------------------------------------------

// Convert a hex string of a given length into a sequence of bytes, returning the
// number of bytes written.
int hexStringToBytes(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf)
{
    int y = 0;
    int z;
    int a = 0;

    for (int x = 0; (x < lenInBuf) && (y < lenOutBuf); x++) {
        z = *(pInBuf + x);
        if ((z >= '0') && (z <= '9')) {
            z = z - '0';
        } else {
            z &= ~0x20;
            if ((z >= 'A') && (z <= 'F')) {
                z = z - 'A' + 10;
            } else {
                z = -1;
            }
        }

        if (z >= 0) {
            if (a % 2 == 0) {
                *(pOutBuf + y) = (z << 4) & 0xF0;
            } else {
                *(pOutBuf + y) += z;
                y++;
            }
            a++;
        }
    }

    return y;
}

// Convert a sequence of bytes into a hex string, returning the number
// of characters written. The hex string is NOT null terminated.
int bytesToHexString(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf)
{
    int y = 0;

    for (int x = 0; (x < lenInBuf) && (y < lenOutBuf); x++) {
        pOutBuf[y] = hexTable[(pInBuf[x] >> 4) & 0x0f]; // upper nibble
        y++;
        if (y < lenOutBuf) {
            pOutBuf[y] = hexTable[pInBuf[x] & 0x0f]; // lower nibble
            y++;
        }
    }

    return y;
}

// Convert a string of a given length representing a BLE address into a byte array
// in network or big-endian byte order.
int hexStringToBleAddress(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf)
{
    int y = hexStringToBytes(pInBuf, lenInBuf, pOutBuf, lenOutBuf);
    reverseArray(pOutBuf, lenOutBuf, 1);
    return y;
}

// Convert a BLE address (which is in network or big-endian byte order)
// into a string which is NOT null terminated.
int bleAddressToHexString(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf)
{
    int y = bytesToHexString(pInBuf, lenInBuf, pOutBuf, lenOutBuf);
    reverseArray(pOutBuf, lenOutBuf, 2);
    return y;
}

// End Of File
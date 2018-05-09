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

#ifndef _UTILITIES_H_
#define _UTILITIES_H_

// ----------------------------------------------------------------
// COMPILE-TIME CONSTANTS
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// FUNCTIONS
// ----------------------------------------------------------------

/** Convert a hex string of a given length into a sequence of bytes, returning the
 * number of bytes written.
 *
 * @param pInBuf    pointer to the input string.
 * @param lenInBuf  length of the input string (not including any terminator).
 * @param pOutBuf   pointer to the output buffer.
 * @param lenOutBuf length of the output buffer.
 * @return          the number of bytes written.
 */
int hexStringToBytes(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf);

/** Convert an array of bytes into a hex string, returning the number of bytes
 * written.  The hex string is NOT null terminated.
 *
 * @param pInBuf    pointer to the input buffer.
 * @param lenInBuf  length of the input buffer.
 * @param pOutBuf   pointer to the output buffer.
 * @param lenOutBuf length of the output buffer.
 * @return          the number of bytes in the output hex string.
 */
int bytesToHexString(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf);

/** Convert a string of a given length representing a BLE address into a byte array
 * in network or big-endian byte order.
 
 * @param pInBuf    pointer to the input string.
 * @param lenInBuf  length of the input string (not including any terminator).
 * @param pOutBuf   pointer to the output buffer.
 * @param lenOutBuf length of the output buffer.
 * @return          the number of bytes written.
 */
int hexStringToBleAddress(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf);

/** Convert an array containing a BLE address (which is in network or big-endian byte order)
 * into a hex string.  The hex string is NOT null terminated.
 *
 * @param pInBuf    pointer to the input buffer.
 * @param lenInBuf  length of the input buffer.
 * @param pOutBuf   pointer to the output buffer.
 * @param lenOutBuf length of the output buffer.
 * @return          the number of bytes in the output hex string.
 */
int bleAddressToHexString(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf);

#endif

// End Of File
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

#ifndef _MORSE_H_
#define _MORSE_H_

// ----------------------------------------------------------------
// COMPILE-TIME CONSTANTS
// ----------------------------------------------------------------

/** The short Morse pulse, used for rapid flashes at the beginning
 * and end of a Morse sequence (in milliseconds).
 */
#define MORSE_VERY_SHORT_PULSE 35 // Don't set this any smaller as this is the smallest
                                  // value where individual flashes are visible on a mobile
                                  // phone video

/** Morse dot duration in milliseconds.
 */
#define MORSE_DOT              100

/** Morse dash duration in milliseconds.
 */
#define MORSE_DASH             500

/** The gap between each dot or dash in milliseconds.
 */
#define MORSE_GAP              250

/** The gap between Morse letters in milliseconds.
 */
#define MORSE_LETTER_GAP       1250

/** The gap between Morse words in milliseconds.
 */
#define MORSE_WORD_GAP         1500

/** The gap at the start and end of a Morse sequence
 * in milliseconds.
 * Note: must be at least as large as the letter gap.
 */
#define MORSE_START_END_GAP    1500

// ----------------------------------------------------------------
// FUNCTIONS
// ----------------------------------------------------------------

/** Initialise Morse.
 *
 * @param pMorseLedBar  pointer to an LED to flash, where high
 *                      is off and low is on.
 */
void initMorse(DigitalOut *pMorseLedBar);

/** printf(), Morse style.
 *
 * @param pFormat  the printf() formatter.
 * @param ...      the printf() variable argument list.
 */
void printfMorse(const char *pFormat, ...);

/** printf() Morse but will run in its own task, returning
 * immediately.
 *
 * @param pFormat  the printf() formatter.
 * @param ...      the printf() variable argument list.
 */
void tPrintfMorse(const char *pFormat, ...);

/** Determine if Morse is currently active.
 *
 * @return true if Morse is active, otherwise false.
 */
bool morseIsActive();

# ifdef ENABLE_ASSERTS_IN_MORSE
/** printf() Morse an Mbed error vfprintf().
 * Note: requires you to edit mbed_error_vfprintf()
 * in mbed-os/platform/mbed_board.c so that it is WEAK and
 * hence can be overridden here.
 *
 * @param pFormat  the printf() formatter.
 * @param args     the printf() arguments.
 */
void mbed_error_vfprintf(const char *pFormat, va_list args);

/** printf() Morse an Mbed assert.
 * Note: requires you to edit mbed_assert_internal()
 * in mbed-os/platform/mbed_assert.c so that it is WEAK and
 * hence can be overridden here.
 *
 * @param expr  a pointer to the assert Boolean expression.
 * @param file  a pointer to the name of the file where the
 *              assert occurred.
 * @param line  the line number where the assert occurred.
 */
void mbed_assert_internal(const char *expr, const char *file, int line);
# endif

#endif

// End Of File
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
#include <mbed.h>
#include <ctype.h>      // For toupper()
#include <morse.h>

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

/**************************************************************************
 * LOCAL VARIABLES
 *************************************************************************/

// POitner to a Morse LED.
static DigitalOut *gpMorseLedBar = NULL;

// A pointer to a thread to display Morse on the LED.
static Thread *gpMorseThread = NULL;

// Flag to indicate that Morse output is active.
static bool volatile gMorseActive = false;

// Buffer for Morse printf()s.
static char gMorseBuf[64];

// Morse codes.
static const char gMorseLetters[] = {'?', '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                                     'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                                     'W', 'X', 'Y', 'Z', '.', ',', '/', '1', '2', '3', '4', '5',
                                     '6', '7', '8', '9', '0'};
static const char *gpMorseCodes[] = {"..--.." /* ? */, ".--.-." /* @ */, ".-" /* A */, "-..." /* B */, "-.-." /* C */, "-.." /* D */,
                                     "." /* E */, "..-." /* F */, "--." /* G */, "...." /* H */, ".." /* I */, ".---" /* J */,
                                     "-.-" /* K */, ".-.." /* L */, "--" /* M */, "-." /* N */, "---" /* O */, ".--." /* P */,
                                     "--.-" /* Q */, ".-." /* R */, "..." /* S */, "-" /* T */, "..-" /* U */, "...-" /* V */,
                                     ".--" /* W */, "-..-" /* Z */, "-.--" /* Y */, "--.." /* Z */, ".-.-.-" /* . */, "--..--" /* , */,
                                     "-..-." /* / */, ".----" /* 1 */, "..---" /* 2 */, "...--" /* 3 */, "....-" /* 4 */, "....." /* 5 */,
                                     "-...." /* 6 */, "--..." /* 7 */, "---.." /* 8 */, "----." /* 9 */, "-----" /* 0 */};

/**************************************************************************
 * STATIC FUNCTIONS
 *************************************************************************/

// Flag the start or end of a Morse sequence.
static void morseStartEndFlag()
{
    if (gpMorseLedBar != NULL) {
        for (int x = 0; x < 5; x++) {
            *gpMorseLedBar = 1;
            wait_ms(MORSE_VERY_SHORT_PULSE);
            *gpMorseLedBar = 0;
            wait_ms(MORSE_VERY_SHORT_PULSE);
        }
    }
}

// Flash out a buffer of characters in Morse.
// Please call printfMorse() or tPrintfMorse(),
// see public functions below.
static void morseFlash(const char *pBuf)
{
    char letter;
    const char *pMorseString;
    int morseLen;
    int len = strlen(pBuf);
    
    if (gpMorseLedBar != NULL) {
        gMorseActive = true;
       // Begin with the opening sequence
        *gpMorseLedBar = 0;
        wait_ms(MORSE_START_END_GAP);
        morseStartEndFlag();
        wait_ms(MORSE_START_END_GAP);
        // Flash each character
        for (int x = 0; x < len; x++) {
            letter = toupper(*(pBuf + x));
            if ((letter == ' ') || (letter == '\n')) {
                // A gap between words, but ignoring a last '\n' or ' '
                if (x != len - 1) {
                    wait_ms(MORSE_WORD_GAP);
                }
            } else {
                // A real letter
                pMorseString = NULL;
                for (unsigned int y = 0; (pMorseString == NULL) && (y < sizeof(gMorseLetters)); y++) {
                    if (letter == gMorseLetters[y]) {
                        pMorseString = gpMorseCodes[y];
                    }
                }
                
                // If the letter is not found, put in '?'
                if (pMorseString == NULL) {
                    pMorseString = gpMorseCodes[0];
                }
                
                // Now flash the LED
                morseLen = strlen(pMorseString);
                for (int y = 0; y < morseLen; y++) {
                    *gpMorseLedBar = 1;
                    if (*(pMorseString + y) == '.') {
                        wait_ms(MORSE_DOT);
                    } else if (*(pMorseString + y) == '-') {
                        wait_ms(MORSE_DASH);
                    } else {
                        // Must be some mishtake
                    }
                    *gpMorseLedBar = 0;
                    wait_ms(MORSE_GAP);
                }
                
                // Wait between letters
                wait_ms(MORSE_LETTER_GAP);
            }
        }
        wait_ms(MORSE_START_END_GAP - MORSE_LETTER_GAP);
        morseStartEndFlag();
        wait_ms(MORSE_START_END_GAP);
        gMorseActive = false;
    }
}

// Flash a message in Morse on the LED; please call printfMorse() 
// or tPrintfMorse(), see public functions below.
static void vPrintfMorse(bool async, const char *pFormat, va_list args)
{
    unsigned int len;
    
    // Get the string into a buffer
    len = vsnprintf(gMorseBuf, sizeof(gMorseBuf), pFormat, args);
    if (len > sizeof(gMorseBuf) - 1) {
        gMorseBuf[sizeof(gMorseBuf) - 1] = 0; // Ensure terminator
    }

    if (async) {
        // Only have one outstanding at a time
        if (gpMorseThread != NULL) {
            gpMorseThread->terminate();
            gpMorseThread->join();
            delete gpMorseThread;
            gpMorseThread = NULL;
        }
        gpMorseThread = new Thread();
        gpMorseThread->start(callback(morseFlash, gMorseBuf));
    } else {
        morseFlash(gMorseBuf);
    }
}

/**************************************************************************
 * PUBLIC FUNCTIONS
 *************************************************************************/

// Initialise Morse.
void initMorse(DigitalOut *pMorseLedBar)
{
    gpMorseLedBar = pMorseLedBar;
}

// Printf() a message in Morse.
void printfMorse(const char *pFormat, ...)
{
    va_list args;

    va_start(args, pFormat);
    vPrintfMorse(false, pFormat, args);
    va_end(args);
}

// Printf() a message in Morse in its own thread.
// If the thread is already running it will be terminated
// and the new message will replace it.
void tPrintfMorse(const char *pFormat, ...)
{
    va_list args;

    va_start(args, pFormat);
    vPrintfMorse(true, pFormat, args);
    va_end(args);
}

// Return whether Morse is currently active.
bool morseIsActive()
{
    return gMorseActive;
}

#ifdef ENABLE_ASSERTS_IN_MORSE
// Override the Mbed error vPrintf.
void mbed_error_vfprintf(const char *pFormat, va_list args)
{
    vPrintfMorse(false, pFormat, args);
}

// Capture Mbed asserts.
void mbed_assert_internal(const char *expr, const char *file, int line)
{
    while (1) {
        printfMorse("ASRT %s %s %d", expr, file, line);
    }
}
#endif

// End of file

#include "print.h"
#include "string_helpers.h"
#include "IO.h"
#include "sleep.h"

#define MOUSE_CHECK 0x64
#define MOUSE_AUXILIARY_PORT 0x60
#define USED 0xACE
#define ON 0x1
#define OFF 0x0

short status;

/* NOTE: MOUSE USES THE SAME PORT AS KEYBOARD, 0x64 PORT IS USED TO DIFFERENTIATE! */

void mouse(void) {
    uint8_t b1, b2, b3;

    // Enable mouse only once
    if (!status) {
        // Wait for input buffer empty
        while (inb(MOUSE_CHECK) & 2);
        outb(0x64, 0xD4);          // Tell mouse next byte is a command
        outb(MOUSE_AUXILIARY_PORT, 0xF4); // Enable data reporting
        // Wait for ACK
        while (!(inb(MOUSE_CHECK) & 1));
        inb(MOUSE_AUXILIARY_PORT); // read 0xFA
        status = USED;
    }

    // Wait for 3 bytes from mouse
    while (!(inb(MOUSE_CHECK) & 1)); b1 = inb(MOUSE_AUXILIARY_PORT);
    while (!(inb(MOUSE_CHECK) & 1)); b2 = inb(MOUSE_AUXILIARY_PORT);
    while (!(inb(MOUSE_CHECK) & 1)); b3 = inb(MOUSE_AUXILIARY_PORT);

    // Print the packet
    print_unsigned((unsigned long long)b1, 16); PRINT(0xFFFFFF,0x000000," ");
    print_unsigned((unsigned long long)b2, 16); PRINT(0xFFFFFF,0x000000," ");
    print_unsigned((unsigned long long)b3, 16); PRINT(0xFFFFFF,0x000000," ");
}


/*

TODO: INTERPRET BYTES.

*/

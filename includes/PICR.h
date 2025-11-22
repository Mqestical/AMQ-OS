#ifndef PICR_H
#define PICR_H

// PIC ports
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

// PIC commands
#define PIC_EOI      0x20

// Function prototypes
void pic_remap(void);

#endif // PICR_H
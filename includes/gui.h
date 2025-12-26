#ifndef GUI_H
#define GUI_H

#include <stdint.h>

// GUI thread entry point
void gui_thread_entry(void);

// Main GUI function
void gui_main(void);

// GUI state
extern int isgui;

#endif // GUI_H
#ifndef AUTO_SCROLL_H
#define AUTO_SCROLL_H

#include <stdint.h>

// Initialize auto-scroll system
void auto_scroll_init(void);

// Check if cursor is at bottom and scroll if needed
void auto_scroll_check(void);

// Get the scroll offset (how many lines we've scrolled)
int auto_scroll_get_offset(void);

#endif // AUTO_SCROLL_H
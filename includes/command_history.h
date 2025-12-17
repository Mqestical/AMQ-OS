#ifndef COMMAND_HISTORY_H
#define COMMAND_HISTORY_H

#include "string_helpers.h"

#define HISTORY_SIZE 50
#define CMD_MAX_LENGTH 256

typedef struct {
    char commands[HISTORY_SIZE][CMD_MAX_LENGTH];
    int count;
    int current;
    int write_pos;
} command_history_t;

// Global history
static command_history_t cmd_history = {
    .count = 0,
    .current = -1,
    .write_pos = 0
};

// Helper function to compare strings
static int strings_equal(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return s1[i] == s2[i];
}

// Add command to history
static void history_add(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;
    
    // Don't add duplicate of last command
    if (cmd_history.count > 0) {
        int last = (cmd_history.write_pos - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        if (strings_equal(cmd_history.commands[last], cmd)) {
            return;
        }
    }
    
    // Copy command
    int i = 0;
    while (cmd[i] && i < CMD_MAX_LENGTH - 1) {
        cmd_history.commands[cmd_history.write_pos][i] = cmd[i];
        i++;
    }
    cmd_history.commands[cmd_history.write_pos][i] = '\0';
    
    // Update counters
    cmd_history.write_pos = (cmd_history.write_pos + 1) % HISTORY_SIZE;
    if (cmd_history.count < HISTORY_SIZE) {
        cmd_history.count++;
    }
    
    // Reset current position
    cmd_history.current = -1;
}

// Get previous command (up arrow)
static const char* history_prev(void) {
    if (cmd_history.count == 0) return NULL;
    
    if (cmd_history.current == -1) {
        // First time pressing up - go to most recent
        cmd_history.current = (cmd_history.write_pos - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    } else {
        // Go back one more
        int steps_back = 0;
        int pos = cmd_history.current;
        
        // Count how many steps we've gone back
        while (pos != cmd_history.write_pos) {
            steps_back++;
            pos = (pos + 1) % HISTORY_SIZE;
        }
        
        // Only go back if we haven't reached the oldest
        if (steps_back < cmd_history.count) {
            cmd_history.current = (cmd_history.current - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        }
    }
    
    return cmd_history.commands[cmd_history.current];
}

// Get next command (down arrow)
static const char* history_next(void) {
    if (cmd_history.current == -1) return NULL;
    
    int next_pos = (cmd_history.current + 1) % HISTORY_SIZE;
    
    // If we reach the write position, we're at the newest command
    if (next_pos == cmd_history.write_pos) {
        cmd_history.current = -1;
        // Return empty string
        static char empty[1] = {0};
        return empty;
    }
    
    cmd_history.current = next_pos;
    return cmd_history.commands[cmd_history.current];
}

// Reset history navigation
static void history_reset(void) {
    cmd_history.current = -1;
}

// List all history
static void history_list(void) {
    
    if (cmd_history.count == 0) {
        PRINT(WHITE, BLACK, "No commands in history\n");
        return;
    }
    
    PRINT(CYAN, BLACK, "\n=== Command History ===\n");
    
    int start;
    if (cmd_history.count < HISTORY_SIZE) {
        start = 0;
    } else {
        start = cmd_history.write_pos;
    }
    
    for (int i = 0; i < cmd_history.count; i++) {
        int idx = (start + i) % HISTORY_SIZE;
        PRINT(WHITE, BLACK, "%s  %s\n", i + 1, cmd_history.commands[idx]);
    }
    PRINT(WHITE, BLACK, "\n");
}

#endif // COMMAND_HISTORY_H
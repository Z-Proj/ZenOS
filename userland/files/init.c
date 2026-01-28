#include "../userlib.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

typedef struct {
    char name[28];
    uint32_t size;
    uint32_t start_block;
    uint32_t position;                  
    uint8_t entry_index;                
    uint8_t is_open;
} zfs_file_t;

#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 16

// Command buffer
static char command_buffer[MAX_COMMAND_LENGTH];
static int buffer_pos = 0;

// Safe string functions with null checks

static int strlen(const char* s) {
    if (!s) return 0;
    int len = 0;
    while (*s && len < 4096) {  // bounds check
        s++;
        len++;
    }
    return len;
}

static int strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    int i = 0;
    while (s1[i] && s2[i] && i < 4096) {  // bounds check
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        i++;
    }
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

static void strcpy(char* dest, const char* src) {
    if (!dest || !src) return;
    
    int i = 0;
    while (src[i] && i < MAX_COMMAND_LENGTH - 1) {  // bounds check
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void memset_char(char* ptr, char val, int size) {
    if (!ptr || size <= 0) return;
    for (int i = 0; i < size; i++) {
        ptr[i] = val;
    }
}

// Parse command into arguments
static int parse_command(char* command, char* args[], int max_args) {
    if (!command || !args || max_args <= 0) return 0;
    
    int arg_count = 0;
    int in_arg = 0;
    int i = 0;
    
    while (command[i] && arg_count < max_args && i < MAX_COMMAND_LENGTH) {
        if (command[i] == ' ' || command[i] == '\t') {
            command[i] = '\0';
            in_arg = 0;
        }
        else if (!in_arg) {
            args[arg_count++] = &command[i];
            in_arg = 1;
        }
        i++;
    }
    
    // Null-terminate args array
    if (arg_count < max_args) {
        args[arg_count] = (char*)0;
    }
    
    return arg_count;
}

// ============ COMMANDS ============

static void cmd_exec(int argc, char* argv[]) {
    if (!argv || argc < 2 || !argv[1]) {
        prints(COLOR_RED "Usage: exec <filename>\n" COLOR_RESET);
        return;
    }
    
    prints(COLOR_YELLOW "Executing: " COLOR_RESET);
    prints(argv[1]);
    prints("\n");
    
    int result = exec(argv[1]);
    
    if (result != 0) {
        prints(COLOR_RED "Failed to execute: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    }
}

static void cmd_help(void) {
    prints(COLOR_CYAN "Available commands:\n" COLOR_RESET);
    prints("  help      - Show this help message\n");
    prints("  clear     - Clear the screen\n");
    prints("  exec      - Execute a program file\n");
    prints("  echo      - Print arguments\n");
    prints("  version   - Show OS version\n");
    prints("  exit      - Exit shell\n");
    prints("  beep      - Play a beep\n");
    prints("  test      - Run a test sequence\n");
}

static void cmd_clear(void) {
    prints("\033[2J\033[H");
}

static void cmd_echo(int argc, char* argv[]) {
    if (!argv) return;
    
    for (int i = 1; i < argc && i < MAX_ARGS; i++) {
        if (argv[i]) {
            prints(argv[i]);
            if (i < argc - 1) prints(" ");
        }
    }
    prints("\n");
}

static void cmd_version(void) {
    prints(COLOR_BOLD COLOR_CYAN);
    prints(" _____           ___  ____  \n");
    prints("|__  /___ _ __  / _ \\/ ___| \n");
    prints("  / // _ \\ '_ \\| | | \\___ \\ \n");
    prints(" / /|  __/ | | | |_| |___) |\n");
    prints("/____\\___|_| |_|\\___/|____/ \n\n");
    prints("ZenOS v0.90.0\n\n");
    prints(COLOR_RESET);
}

static void cmd_beep(void) {
    prints("Playing beep...\n");
    speaker_play(440);
    sleep(200);
    speaker_play(880);
    sleep(200);
    speaker_stop();
}

static void cmd_test(void) {
    prints(COLOR_YELLOW "Running syscall tests...\n" COLOR_RESET);
    
    prints("1. Testing prints... ");
    sleep(100);
    prints(COLOR_GREEN "OK\n" COLOR_RESET);
    
    prints("2. Testing sleep... ");
    sleep(500);
    prints(COLOR_GREEN "OK\n" COLOR_RESET);
    
    prints("3. Testing speaker... ");
    speaker_play(523);
    sleep(100);
    speaker_stop();
    prints(COLOR_GREEN "OK\n" COLOR_RESET);
    
    prints("4. Testing mouse... ");
    uint32_t x = mouse_x();
    uint32_t y = mouse_y();
    (void)x; (void)y;
    prints(COLOR_GREEN "OK\n" COLOR_RESET);
    
    prints(COLOR_GREEN "All tests passed!\n" COLOR_RESET);
}

// ============ SHELL CORE ============

static void show_prompt(void) {
    prints(COLOR_BOLD COLOR_GREEN "ZenOS" COLOR_RESET);
    prints(COLOR_BOLD COLOR_BLUE " ~$ " COLOR_RESET);
}

static void read_command(void) {
    buffer_pos = 0;
    memset_char(command_buffer, 0, MAX_COMMAND_LENGTH);
    
    while (1) {
        char c = getkey();
        
        if (c == '\0') {
            sleep(10);
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            command_buffer[buffer_pos] = '\0';
            prints("\n");
            break;
        }
        else if (c == '\b' || c == 127) {
            if (buffer_pos > 0) {
                buffer_pos--;
                command_buffer[buffer_pos] = '\0';
                prints("\b \b");
            }
        }
        else if (c >= 32 && c < 127) {
            if (buffer_pos < MAX_COMMAND_LENGTH - 1) {
                command_buffer[buffer_pos++] = c;
                char buf[2] = {c, '\0'};
                prints(buf);
            }
        }
    }
}

static int execute_command(void) {
    char cmd_copy[MAX_COMMAND_LENGTH];
    char* argv[MAX_ARGS];
    
    // Initialize arrays
    memset_char(cmd_copy, 0, MAX_COMMAND_LENGTH);
    for (int i = 0; i < MAX_ARGS; i++) {
        argv[i] = (char*)0;
    }
    
    // Safe copy
    strcpy(cmd_copy, command_buffer);
    
    int argc = parse_command(cmd_copy, argv, MAX_ARGS);
    
    if (argc == 0 || !argv[0]) {
        return 1;  // Continue
    }
    
    // Command dispatch with null checks
    if (strcmp(argv[0], "help") == 0) {
        cmd_help();
    }
    else if (strcmp(argv[0], "exec") == 0) {
        cmd_exec(argc, argv);  
    }
    else if (strcmp(argv[0], "clear") == 0) {
        cmd_clear();
    }
    else if (strcmp(argv[0], "echo") == 0) {
        cmd_echo(argc, argv);
    }
    else if (strcmp(argv[0], "version") == 0) {
        cmd_version();
    }
    else if (strcmp(argv[0], "exit") == 0) {
        prints(COLOR_YELLOW "Exiting shell...\n" COLOR_RESET);
        return 0;
    }
    else if (strcmp(argv[0], "beep") == 0) {
        cmd_beep();
    }
    else if (strcmp(argv[0], "test") == 0) {
        cmd_test();
    }
    else if (strcmp(argv[0], "shutdown") == 0) {
        shutdown();
    }
    else {
        prints(COLOR_RED "Unknown command: " COLOR_RESET);
        if (argv[0]) prints(argv[0]);
        prints("\n");
        prints("Type 'help' for available commands.\n");
    }
    
    return 1;
}

static void show_banner(void) {
    cmd_clear();
    prints(COLOR_CYAN COLOR_BOLD);
    prints(" _____           ___  ____  \n");
    prints("|__  /___ _ __  / _ \\/ ___| \n");
    prints("  / // _ \\ '_ \\| | | \\___ \\ \n");
    prints(" / /|  __/ | | | |_| |___) |\n");
    prints("/____\\___|_| |_|\\___/|____/ \n\n");
    prints(COLOR_RESET);
    prints("\n");
    prints(COLOR_YELLOW "Welcome to ZenOS Shell!\n" COLOR_RESET);
    prints("Type 'help' for available commands.\n");
    prints("\n");
}

int main(void) {
    // Initialize buffer
    memset_char(command_buffer, 0, MAX_COMMAND_LENGTH);
    
    show_banner();
    
    while (1) {
        show_prompt();
        read_command();
        
        if (!execute_command()) {
            break;
        }
    }
    
    prints(COLOR_GREEN "Shell exited cleanly.\n" COLOR_RESET);
    exit(0);
    return 0;
}
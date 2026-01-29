#include "../userlib.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 16

// Command buffer
static char command_buffer[MAX_COMMAND_LENGTH];
static char lastcmd[MAX_COMMAND_LENGTH];
static int buffer_pos = 0;

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
    
    if (arg_count < max_args) {
        args[arg_count] = NULL;
    }
    
    return arg_count;
}

// ============ FILE COMMANDS ============

static void cmd_touch(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: touch <filename> [size]\n" COLOR_RESET);
        return;
    }
    
    uint32_t size = 1024;
    if (argc >= 3) {
        size = atoi(argv[2]);
    }
    
    int ret = create(argv[1], size);
    if (ret == 0) {
        prints(COLOR_GREEN "Created: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to create file\n" COLOR_RESET);
    }
}

static void cmd_rm(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: rm <filename>\n" COLOR_RESET);
        return;
    }
    
    int ret = delete(argv[1]);
    if (ret == 0) {
        prints(COLOR_GREEN "Deleted: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to delete file\n" COLOR_RESET);
    }
}

static void cmd_cat(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: cat <filename>\n" COLOR_RESET);
        return;
    }
    
    zfs_file_t file;
    if (open(argv[1], &file) != 0) {
        prints(COLOR_RED "Failed to open file\n" COLOR_RESET);
        return;
    }
    
    char buffer[256];
    ssize_t bytes;
    
    while ((bytes = read(&file, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        prints(buffer);
    }
    
    close(&file);
    prints("\n");
}

static void cmd_write(int argc, char* argv[]) {
    if (argc < 3) {
        prints(COLOR_RED "Usage: write <filename> <text>\n" COLOR_RESET);
        return;
    }
    
    zfs_file_t file;
    if (open(argv[1], &file) != 0) {
        prints(COLOR_RED "Failed to open file\n" COLOR_RESET);
        return;
    }
    
    // Concatenate all args after filename
    char text[256];
    int pos = 0;
    for (int i = 2; i < argc && pos < 255; i++) {
        int j = 0;
        while (argv[i][j] && pos < 255) {
            text[pos++] = argv[i][j++];
        }
        if (i < argc - 1 && pos < 255) text[pos++] = ' ';
    }
    text[pos] = '\0';
    
    ssize_t written = write(&file, text, strlen(text));
    if (written > 0) {
        prints(COLOR_GREEN "Wrote " COLOR_RESET);
        char num[16];
        // Simple itoa
        int n = written;
        int i = 0;
        do {
            num[i++] = '0' + (n % 10);
            n /= 10;
        } while (n > 0);
        num[i] = '\0';
        // Reverse
        for (int j = 0; j < i/2; j++) {
            char tmp = num[j];
            num[j] = num[i-1-j];
            num[i-1-j] = tmp;
        }
        prints(num);
        prints(" bytes\n");
    } else {
        prints(COLOR_RED "Write failed\n" COLOR_RESET);
    }
    
    close(&file);
}

static void cmd_stat(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: stat <filename>\n" COLOR_RESET);
        return;
    }
    
    stat_t st;
    if (stat(argv[1], &st) != 0) {
        prints(COLOR_RED "Failed to stat file\n" COLOR_RESET);
        return;
    }
    
    prints(COLOR_CYAN "File: " COLOR_RESET);
    prints(argv[1]);
    prints("\n");
    
    prints("  Size: ");
    char num[32];
    int n = st.st_size;
    int i = 0;
    if (n == 0) {
        num[i++] = '0';
    } else {
        while (n > 0) {
            num[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints(" bytes\n");
    
    prints("  Blocks: ");
    n = st.st_blocks;
    i = 0;
    if (n == 0) {
        num[i++] = '0';
    } else {
        while (n > 0) {
            num[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints("\n");
}

// ============ DIRECTORY COMMANDS ============

static void cmd_pwd(void) {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        prints(COLOR_CYAN);
        prints(cwd);
        prints(COLOR_RESET "\n");
    } else {
        prints(COLOR_RED "Failed to get current directory\n" COLOR_RESET);
    }
}

static void cmd_cd(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: cd <directory>\n" COLOR_RESET);
        return;
    }
    
    if (chdir(argv[1]) == 0) {
        prints(COLOR_GREEN "Changed to: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to change directory\n" COLOR_RESET);
    }
}

static void cmd_mkdir(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: mkdir <directory>\n" COLOR_RESET);
        return;
    }
    
    if (mkdir(argv[1]) == 0) {
        prints(COLOR_GREEN "Created directory: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to create directory\n" COLOR_RESET);
    }
}

static void cmd_rmdir(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: rmdir <directory>\n" COLOR_RESET);
        return;
    }
    
    if (rmdir(argv[1]) == 0) {
        prints(COLOR_GREEN "Removed directory: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to remove directory\n" COLOR_RESET);
    }
}

// ============ MEMORY COMMANDS ============

static void cmd_malloc_test(int argc, char* argv[]) {
    uint32_t size = 1024;
    if (argc >= 2) {
        size = atoi(argv[1]);
    }
    
    prints(COLOR_YELLOW "Allocating " COLOR_RESET);
    char num[16];
    int n = size;
    int i = 0;
    do {
        num[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints(" bytes via sbrk...\n");
    
    void* ptr = sbrk(size);
    if (ptr != (void*)-1) {
        prints(COLOR_GREEN "Allocated at: 0x" COLOR_RESET);
        
        // Print hex address
        uint64_t addr = (uint64_t)ptr;
        char hex[20];
        int idx = 0;
        for (int shift = 60; shift >= 0; shift -= 4) {
            int digit = (addr >> shift) & 0xF;
            hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
        }
        hex[idx] = '\0';
        prints(hex);
        prints("\n");
        
        // Write to memory to test
        memset(ptr, 0x42, size > 16 ? 16 : size);
        prints(COLOR_GREEN "Memory test successful\n" COLOR_RESET);
    } else {
        prints(COLOR_RED "Allocation failed\n" COLOR_RESET);
    }
}

static void cmd_mmap_test(int argc, char* argv[]) {
    uint32_t size = 4096;
    if (argc >= 2) {
        size = atoi(argv[1]);
    }
    
    prints(COLOR_YELLOW "Mapping " COLOR_RESET);
    char num[16];
    int n = size;
    int i = 0;
    do {
        num[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints(" bytes via mmap...\n");
    
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr != (void*)-1) {
        prints(COLOR_GREEN "Mapped at: 0x" COLOR_RESET);
        
        uint64_t addr = (uint64_t)ptr;
        char hex[20];
        int idx = 0;
        for (int shift = 60; shift >= 0; shift -= 4) {
            int digit = (addr >> shift) & 0xF;
            hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
        }
        hex[idx] = '\0';
        prints(hex);
        prints("\n");
        
        memset(ptr, 0xAA, size > 16 ? 16 : size);
        prints(COLOR_GREEN "Memory test successful\n" COLOR_RESET);
        
        // Unmap it
        if (munmap(ptr, size) == 0) {
            prints(COLOR_GREEN "Unmapped successfully\n" COLOR_RESET);
        }
    } else {
        prints(COLOR_RED "mmap failed\n" COLOR_RESET);
    }
}

// ============ IPC COMMANDS ============

static void cmd_socket_create(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: sockcreate <name>\n" COLOR_RESET);
        return;
    }
    
    if (socket_create(argv[1]) == 0) {
        prints(COLOR_GREEN "Created socket: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to create socket\n" COLOR_RESET);
    }
}

static void cmd_socket_write(int argc, char* argv[]) {
    if (argc < 3) {
        prints(COLOR_RED "Usage: sockwrite <name> <message>\n" COLOR_RESET);
        return;
    }
    
    socket_file_t *sock;
    if (socket_open(argv[1], &sock) != 0) {
        prints(COLOR_RED "Failed to open socket\n" COLOR_RESET);
        return;
    }
    
    // Concatenate message
    char msg[256];
    int pos = 0;
    for (int i = 2; i < argc && pos < 255; i++) {
        int j = 0;
        while (argv[i][j] && pos < 255) {
            msg[pos++] = argv[i][j++];
        }
        if (i < argc - 1 && pos < 255) msg[pos++] = ' ';
    }
    msg[pos] = '\0';
    
    if (socket_write(sock, msg, strlen(msg)) >= 0) {
        prints(COLOR_GREEN "Wrote to socket\n" COLOR_RESET);
    } else {
        prints(COLOR_RED "Failed to write\n" COLOR_RESET);
    }
    
    socket_close(sock);
}

static void cmd_socket_read(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: sockread <name>\n" COLOR_RESET);
        return;
    }
    
    socket_file_t *sock;
    if (socket_open(argv[1], &sock) != 0) {
        prints(COLOR_RED "Failed to open socket\n" COLOR_RESET);
        return;
    }
    
    char buffer[256];
    uint32_t bytes_read = 0;
    
    if (socket_read(sock, buffer, sizeof(buffer) - 1, &bytes_read) >= 0 && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        prints(COLOR_CYAN "Read: " COLOR_RESET);
        prints(buffer);
        prints("\n");
    } else {
        prints(COLOR_YELLOW "No data available\n" COLOR_RESET);
    }
    
    socket_close(sock);
}

static void cmd_socket_delete(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: sockdel <name>\n" COLOR_RESET);
        return;
    }
    
    if (socket_delete(argv[1]) == 0) {
        prints(COLOR_GREEN "Deleted socket: " COLOR_RESET);
        prints(argv[1]);
        prints("\n");
    } else {
        prints(COLOR_RED "Failed to delete socket\n" COLOR_RESET);
    }
}

// ============ PROCESS COMMANDS ============

static void cmd_ps(void) {
    pid_t pid = getpid();
    prints(COLOR_CYAN "Current PID: " COLOR_RESET);
    
    char num[16];
    int n = pid;
    int i = 0;
    if (n == 0) {
        num[i++] = '0';
    } else {
        while (n > 0) {
            num[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints("\n");
}

static void cmd_exec(int argc, char* argv[]) {
    if (argc < 2) {
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

// ============ TIME COMMANDS ============

static void cmd_time(void) {
    timeval_t tv;
    if (gettimeofday(&tv, NULL) == 0) {
        prints(COLOR_CYAN "Time: " COLOR_RESET);
        
        char num[32];
        int64_t sec = tv.tv_sec;
        int i = 0;
        if (sec == 0) {
            num[i++] = '0';
        } else {
            while (sec > 0) {
                num[i++] = '0' + (sec % 10);
                sec /= 10;
            }
        }
        num[i] = '\0';
        for (int j = 0; j < i/2; j++) {
            char tmp = num[j];
            num[j] = num[i-1-j];
            num[i-1-j] = tmp;
        }
        prints(num);
        prints(".");
        
        int64_t usec = tv.tv_usec;
        i = 0;
        if (usec == 0) {
            num[i++] = '0';
        } else {
            while (usec > 0) {
                num[i++] = '0' + (usec % 10);
                usec /= 10;
            }
        }
        num[i] = '\0';
        for (int j = 0; j < i/2; j++) {
            char tmp = num[j];
            num[j] = num[i-1-j];
            num[i-1-j] = tmp;
        }
        prints(num);
        prints(" seconds\n");
    }
}

static void cmd_sleep_cmd(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: sleep <milliseconds>\n" COLOR_RESET);
        return;
    }
    
    uint32_t ms = atoi(argv[1]);
    prints(COLOR_YELLOW "Sleeping for " COLOR_RESET);
    char num[16];
    int n = ms;
    int i = 0;
    do {
        num[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints("ms...\n");
    
    sleep(ms);
    prints(COLOR_GREEN "Done!\n" COLOR_RESET);
}

// ============ SYSTEM COMMANDS ============

static void cmd_uname(void) {
    utsname_t uts;
    if (uname(&uts) == 0) {
        prints(COLOR_CYAN);
        prints(uts.sysname);
        prints(" ");
        prints(uts.release);
        prints(" ");
        prints(uts.version);
        prints(" ");
        prints(uts.machine);
        prints(COLOR_RESET "\n");
    }
}

static void cmd_mouse(void) {
    prints(COLOR_CYAN "Mouse Position: " COLOR_RESET);
    
    uint32_t x = mouse_x();
    uint32_t y = mouse_y();
    uint8_t btn = mouse_button();
    
    prints("X=");
    char num[16];
    int n = x;
    int i = 0;
    if (n == 0) {
        num[i++] = '0';
    } else {
        while (n > 0) {
            num[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    
    prints(" Y=");
    n = y;
    i = 0;
    if (n == 0) {
        num[i++] = '0';
    } else {
        while (n > 0) {
            num[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    
    prints(" Button=");
    n = btn;
    i = 0;
    if (n == 0) {
        num[i++] = '0';
    } else {
        while (n > 0) {
            num[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints("\n");
}

static void cmd_beep(int argc, char* argv[]) {
    uint32_t freq = 440;
    uint32_t duration = 200;
    
    if (argc >= 2) freq = atoi(argv[1]);
    if (argc >= 3) duration = atoi(argv[2]);
    
    prints("Beep ");
    char num[16];
    int n = freq;
    int i = 0;
    do {
        num[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    num[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = num[j];
        num[j] = num[i-1-j];
        num[i-1-j] = tmp;
    }
    prints(num);
    prints("Hz...\n");
    
    speaker_play(freq);
    sleep(duration);
    speaker_stop();
}

static void cmd_yield_cmd(void) {
    prints(COLOR_YELLOW "Yielding CPU...\n" COLOR_RESET);
    yield();
    prints(COLOR_GREEN "Back from yield\n" COLOR_RESET);
}

static void cmd_help(void) {
    prints("  touch <file> [size]  - Create file\n");
    prints("  rm <file>            - Delete file\n");
    prints("  cat <file>           - Display file contents\n");
    prints("  write <file> <text>  - Write to file\n");
    prints("  stat <file>          - Show file info\n");
    prints("  pwd                  - Print working directory\n");
    prints("  cd <dir>             - Change directory\n");
    prints("  mkdir <dir>          - Create directory\n");
    prints("  rmdir <dir>          - Remove directory\n");
    prints("  malloc <size>        - Test sbrk allocation\n");
    prints("  mmap <size>          - Test mmap allocation\n");
    prints("  sockcreate <name>    - Create IPC socket\n");
    prints("  sockwrite <name> <msg> - Write to socket\n");
    prints("  sockread <name>      - Read from socket\n");
    prints("  sockdel <name>       - Delete socket\n");
    prints("  exec <file>          - Execute program\n");
    prints("  ps                   - Show process info\n");
    prints("  yield                - Yield CPU\n");
    prints("  uname                - System information\n");
    prints("  time                 - Show current time\n");
    prints("  sleep <ms>           - Sleep for milliseconds\n");
    prints("  mouse                - Show mouse position\n");
    prints("  beep [freq] [dur]    - Play sound\n");
    prints("  help                 - Show this help\n");
    prints("  clear                - Clear screen\n");
    prints("  echo <text>          - Print text\n");
    prints("  version              - Show OS version\n");
    prints("  z                    - Repeat last command\n");
    prints("  exit                 - Exit shell\n");
    prints("  shutdown <sec>       - Shutdown system\n");
    prints("  reboot               - Reboot system\n");
}

static void cmd_clear(void) {
    prints("\033[2J\033[H");
}

static void cmd_echo(int argc, char* argv[]) {
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
    cmd_uname();
    prints(COLOR_RESET);
}

static int shutdown_accept = 0;
static void cmd_shutdown(int argc, char* argv[]) {
    if (argc < 2) {
        prints(COLOR_RED "Usage: shutdown <delay_seconds>\n" COLOR_RESET);
        return;
    }
    
    int time = atoi(argv[1]) * 1000;
    if (time < 0) {
        prints(COLOR_RED "Invalid delay\n" COLOR_RESET);
        return;
    }
    
    if (time > 50000 && !shutdown_accept) {
        prints(COLOR_YELLOW "Large delay. Run again to confirm.\n" COLOR_RESET);
        shutdown_accept = 1;
        return;
    }
    
    if (time == 0) {
        log("Shutting down", 2, 1);
        shutdown();
    }
    
    sleep(time - 1000);
    log("Shutting down", 2, 1);
    sleep(1000);
    shutdown();
}

static void cmd_reboot(void) {
    prints(COLOR_YELLOW "Rebooting system in 2 seconds...\n" COLOR_RESET);
    sleep(2000);
    log("Rebooting", 2, 1);
    reboot();
}

// ============ SHELL CORE ============

static void show_prompt(void) {
    prints(COLOR_BOLD COLOR_GREEN "Zen" COLOR_RESET);
    prints(COLOR_BOLD COLOR_BLUE "~$ " COLOR_RESET);
}

static void read_command(void) {
    buffer_pos = 0;
    memset(command_buffer, 0, MAX_COMMAND_LENGTH);
    
    while (1) {
        char c = getkey();
        
        if (c == '\0') {
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
    
    memset(cmd_copy, 0, MAX_COMMAND_LENGTH);
    for (int i = 0; i < MAX_ARGS; i++) {
        argv[i] = NULL;
    }
    
    strcpy(cmd_copy, command_buffer);
    int argc = parse_command(cmd_copy, argv, MAX_ARGS);
    
    if (argc == 0 || !argv[0]) return 1;
    
    if (strcmp(argv[0], "z") != 0) {
        strcpy(lastcmd, command_buffer);
    }
    
    // Command dispatch
    if (strcmp(argv[0], "help") == 0) cmd_help();
    else if (strcmp(argv[0], "clear") == 0) cmd_clear();
    else if (strcmp(argv[0], "echo") == 0) cmd_echo(argc, argv);
    else if (strcmp(argv[0], "version") == 0) cmd_version();
    else if (strcmp(argv[0], "exit") == 0) {
        prints(COLOR_YELLOW "Exiting...\n" COLOR_RESET);
        return 0;
    }
    // File ops
    else if (strcmp(argv[0], "touch") == 0) cmd_touch(argc, argv);
    else if (strcmp(argv[0], "rm") == 0) cmd_rm(argc, argv);
    else if (strcmp(argv[0], "cat") == 0) cmd_cat(argc, argv);
    else if (strcmp(argv[0], "write") == 0) cmd_write(argc, argv);
    else if (strcmp(argv[0], "stat") == 0) cmd_stat(argc, argv);
    // Directory ops
    else if (strcmp(argv[0], "pwd") == 0) cmd_pwd();
    else if (strcmp(argv[0], "cd") == 0) cmd_cd(argc, argv);
    else if (strcmp(argv[0], "mkdir") == 0) cmd_mkdir(argc, argv);
    else if (strcmp(argv[0], "rmdir") == 0) cmd_rmdir(argc, argv);
    // Memory
    else if (strcmp(argv[0], "malloc") == 0) cmd_malloc_test(argc, argv);
    else if (strcmp(argv[0], "mmap") == 0) cmd_mmap_test(argc, argv);
    // IPC
    else if (strcmp(argv[0], "sockcreate") == 0) cmd_socket_create(argc, argv);
    else if (strcmp(argv[0], "sockwrite") == 0) cmd_socket_write(argc, argv);
    else if (strcmp(argv[0], "sockread") == 0) cmd_socket_read(argc, argv);
    else if (strcmp(argv[0], "sockdel") == 0) cmd_socket_delete(argc, argv);
    // Process
    else if (strcmp(argv[0], "exec") == 0) cmd_exec(argc, argv);
    else if (strcmp(argv[0], "ps") == 0) cmd_ps();
    else if (strcmp(argv[0], "yield") == 0) cmd_yield_cmd();
    // System
    else if (strcmp(argv[0], "uname") == 0) cmd_uname();
    else if (strcmp(argv[0], "time") == 0) cmd_time();
    else if (strcmp(argv[0], "sleep") == 0) cmd_sleep_cmd(argc, argv);
    else if (strcmp(argv[0], "mouse") == 0) cmd_mouse();
    else if (strcmp(argv[0], "beep") == 0) cmd_beep(argc, argv);
    else if (strcmp(argv[0], "shutdown") == 0) cmd_shutdown(argc, argv);
    else if (strcmp(argv[0], "reboot") == 0) cmd_reboot();
    // Special
    else if (strcmp(argv[0], "z") == 0) {
        if (lastcmd[0] == '\0') {
            prints(COLOR_RED "No previous command\n" COLOR_RESET);
            return 1;
        }
        strcpy(command_buffer, lastcmd);
        execute_command();
    }
    else {
        prints(COLOR_RED "Unknown: " COLOR_RESET);
        prints(argv[0]);
        prints("\nType 'help' for commands.\n");
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
    prints(COLOR_YELLOW "ZenOS Shell\n" COLOR_RESET);
    prints("Type 'help' for commands.\n\n");
}

int main(void) {
    memset(command_buffer, 0, MAX_COMMAND_LENGTH);
    memset(lastcmd, 0, MAX_COMMAND_LENGTH);
    
    show_banner();
    
    while (1) {
        show_prompt();
        read_command();
        
        if (!execute_command()) {
            break;
        }
    }
    
    prints(COLOR_GREEN "Shell exited\n" COLOR_RESET);
    exit(0);
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
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
        fputs(COLOR_RED "Usage: touch <filename>\n" COLOR_RESET, stdout);
        return;
    }
    
    int ret = zen_create(argv[1]);
    if (ret == 0) {
        fputs(COLOR_GREEN "Created: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to create file\n" COLOR_RESET, stdout);
    }
}

static void cmd_rm(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: rm <filename>\n" COLOR_RESET, stdout);
        return;
    }
    
    int ret = unlink(argv[1]);
    if (ret == 0) {
        fputs(COLOR_GREEN "Deleted: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to delete file\n" COLOR_RESET, stdout);
    }
}

static void cmd_cat(int argc, char* argv[]) {
    if (argc < 2) {
        fputs("Usage: cat <filename>\n", stdout);
        return;
    }

    int fd = open(argv[1], 0);
    if (fd < 0) {
        fputs("Failed to open file\n", stdout);
        return;
    }

    char buffer[512];
    ssize_t bytes;

    while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
        write(1, buffer, bytes);
    }

    close(fd);
}

static void cmd_write(int argc, char* argv[]) {
    if (argc < 3) {
        fputs(COLOR_RED "Usage: write <filename> <text>\n" COLOR_RESET, stdout);
        return;
    }
    
    
    int file = open(argv[1], 1);
    if (file < 0) {
        fputs(COLOR_RED "Failed to open file\n" COLOR_RESET, stdout);
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
    
    int success = write(file, text, strlen(text));
    if (!success) {
        fputs(COLOR_GREEN "Write successful.\n" COLOR_RESET, stdout);
    } else {
        fputs(COLOR_RED "Write failed\n" COLOR_RESET, stdout);
    }
    close(file);
}

static void cmd_stat(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: stat <filename>\n" COLOR_RESET, stdout);
        return;
    }
    
    struct stat st;
    if (stat(argv[1], &st) != 0) {
        fputs(COLOR_RED "Failed to stat file\n" COLOR_RESET, stdout);
        return;
    }
    
    fputs(COLOR_CYAN "File: " COLOR_RESET, stdout);
    fputs(argv[1], stdout);
    fputs("\n", stdout);
    
    fputs("  Size: ", stdout);
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
    fputs(num, stdout);
    fputs(" bytes\n", stdout);
    
    fputs("  Blocks: ", stdout);
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
    fputs(num, stdout);
    fputs("\n", stdout);
}

// ============ DIRECTORY COMMANDS ============

static void cmd_pwd(void) {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        fputs(COLOR_CYAN, stdout);
        fputs(cwd, stdout);
        fputs(COLOR_RESET "\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to get current directory\n" COLOR_RESET, stdout);
    }
}

static void cmd_cd(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: cd <directory>\n" COLOR_RESET, stdout);
        return;
    }
    
    if (chdir(argv[1]) == 0) {
        fputs(COLOR_GREEN "Changed to: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to change directory\n" COLOR_RESET, stdout);
    }
}

static void cmd_mkdir(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: mkdir <directory>\n" COLOR_RESET, stdout);
        return;
    }
    
    if (mkdir(argv[1], 0755) == 0) {
        fputs(COLOR_GREEN "Created directory: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to create directory\n" COLOR_RESET, stdout);
    }
}

static void cmd_rmdir(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: rmdir <directory>\n" COLOR_RESET, stdout);
        return;
    }
    
    if (rmdir(argv[1]) == 0) {
        fputs(COLOR_GREEN "Removed directory: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to remove directory\n" COLOR_RESET, stdout);
    }
}

static void cmd_ls(int argc, char* argv[]) {
    size_t ls_buf_size = 4096;
    char *ls_buf = (char*)malloc(ls_buf_size);
    if (!ls_buf) {
        fputs(COLOR_RED "Out of memory\n" COLOR_RESET, stdout);
        return;
    }

    if (argc < 2) {
        if (zen_ls(ls_buf, ls_buf_size) != 0) {
            fputs(COLOR_RED "Failed to list directory\n" COLOR_RESET, stdout);
            free(ls_buf);
            return;
        }
        fputs(ls_buf, stdout);
        free(ls_buf);
        return;
    }

    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        fputs(COLOR_RED "Failed to get current directory\n" COLOR_RESET, stdout);
        free(ls_buf);
        return;
    }

    if (chdir(argv[1]) != 0) {
        fputs(COLOR_RED "Failed to change directory\n" COLOR_RESET, stdout);
        free(ls_buf);
        return;
    }

    if (zen_ls(ls_buf, ls_buf_size) != 0) {
        fputs(COLOR_RED "Failed to list directory\n" COLOR_RESET, stdout);
    } else {
        fputs(ls_buf, stdout);
    }

    free(ls_buf);

    if (chdir(cwd) != 0) {
        fputs(COLOR_RED "Failed to restore current directory\n" COLOR_RESET, stdout);
    }
}

// ============ MEMORY COMMANDS ============

static void cmd_malloc_test(int argc, char* argv[]) {
    uint32_t size = 1024;
    if (argc >= 2) {
        size = atoi(argv[1]);
    }
    
    fputs(COLOR_YELLOW "Allocating " COLOR_RESET, stdout);
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
    fputs(num, stdout);
    fputs(" bytes via sbrk...\n", stdout);
    
    void* ptr = sbrk(size);
    if (ptr != (void*)-1) {
        fputs(COLOR_GREEN "Allocated at: 0x" COLOR_RESET, stdout);
        
        // Print hex address
        uint64_t addr = (uint64_t)ptr;
        char hex[20];
        int idx = 0;
        for (int shift = 60; shift >= 0; shift -= 4) {
            int digit = (addr >> shift) & 0xF;
            hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
        }
        hex[idx] = '\0';
        fputs(hex, stdout);
        fputs("\n", stdout);
        
        // Write to memory to test
        memset(ptr, 0x42, size > 16 ? 16 : size);
        fputs(COLOR_GREEN "Memory test successful\n" COLOR_RESET, stdout);
    } else {
        fputs(COLOR_RED "Allocation failed\n" COLOR_RESET, stdout);
    }
}

static void cmd_mmap_test(int argc, char* argv[]) {
    uint32_t size = 4096;
    if (argc >= 2) {
        size = atoi(argv[1]);
    }
    
    fputs(COLOR_YELLOW "Mapping " COLOR_RESET, stdout);
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
    fputs(num, stdout);
    fputs(" bytes via mmap...\n", stdout);
    
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr != (void*)-1) {
        fputs(COLOR_GREEN "Mapped at: 0x" COLOR_RESET, stdout);
        
        uint64_t addr = (uint64_t)ptr;
        char hex[20];
        int idx = 0;
        for (int shift = 60; shift >= 0; shift -= 4) {
            int digit = (addr >> shift) & 0xF;
            hex[idx++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
        }
        hex[idx] = '\0';
        fputs(hex, stdout);
        fputs("\n", stdout);
        
        memset(ptr, 0xAA, size > 16 ? 16 : size);
        fputs(COLOR_GREEN "Memory test successful\n" COLOR_RESET, stdout);
        
        // Unmap it
        if (munmap(ptr, size) == 0) {
            fputs(COLOR_GREEN "Unmapped successfully\n" COLOR_RESET, stdout);
        }
    } else {
        fputs(COLOR_RED "mmap failed\n" COLOR_RESET, stdout);
    }
}

// ============ IPC COMMANDS ============

static void cmd_socket_create(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: sockcreate <name>\n" COLOR_RESET, stdout);
        return;
    }
    
    if (socket_create(argv[1]) == 0) {
        fputs(COLOR_GREEN "Created socket: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to create socket\n" COLOR_RESET, stdout);
    }
}

static void cmd_socket_write(int argc, char* argv[]) {
    if (argc < 3) {
        fputs(COLOR_RED "Usage: sockwrite <name> <message>\n" COLOR_RESET, stdout);
        return;
    }
    
    socket_file_t *sock;
    if (socket_open(argv[1], &sock) != 0) {
        fputs(COLOR_RED "Failed to open socket\n" COLOR_RESET, stdout);
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
        fputs(COLOR_GREEN "Wrote to socket\n" COLOR_RESET, stdout);
    } else {
        fputs(COLOR_RED "Failed to write\n" COLOR_RESET, stdout);
    }
    
    socket_close(sock);
}

static void cmd_socket_read(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: sockread <name>\n" COLOR_RESET, stdout);
        return;
    }
    
    socket_file_t *sock;
    if (socket_open(argv[1], &sock) != 0) {
        fputs(COLOR_RED "Failed to open socket\n" COLOR_RESET, stdout);
        return;
    }
    
    char buffer[256];
    uint32_t bytes_read = 0;
    
    if (socket_read(sock, buffer, sizeof(buffer) - 1, &bytes_read) >= 0 && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        fputs(COLOR_CYAN "Read: " COLOR_RESET, stdout);
        fputs(buffer, stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_YELLOW "No data available\n" COLOR_RESET, stdout);
    }
    
    socket_close(sock);
}

static void cmd_socket_delete(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: sockdel <name>\n" COLOR_RESET, stdout);
        return;
    }
    
    if (socket_delete(argv[1]) == 0) {
        fputs(COLOR_GREEN "Deleted socket: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed to delete socket\n" COLOR_RESET, stdout);
    }
}

// ============ PROCESS COMMANDS ============

static void cmd_ps(void) {
    task_info_t tasks[32];
    int count = zen_list_tasks(tasks, 32);

    fputs(COLOR_CYAN "PID  NAME\n" COLOR_RESET, stdout);
    fputs("---- ----\n", stdout);

    for (int i = 0; i < count; i++) {
        char num[16];
        int n = (int)tasks[i].pid;
        int idx = 0;
        if (n == 0) {
            num[idx++] = '0';
        } else {
            while (n > 0) {
                num[idx++] = '0' + (n % 10);
                n /= 10;
            }
        }
        num[idx] = '\0';
        for (int j = 0; j < idx/2; j++) {
            char tmp = num[j];
            num[j] = num[idx-1-j];
            num[idx-1-j] = tmp;
        }
        fputs(num, stdout);
        fputs("  ", stdout);
        fputs(tasks[i].name, stdout);
        fputs("\n", stdout);
    }
}

static void cmd_exec(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: exec <filename> [args...]\n" COLOR_RESET, stdout);
        return;
    }
    
    fputs(COLOR_YELLOW "Executing: " COLOR_RESET, stdout);
    fputs(argv[1], stdout);
    fputs("\n", stdout);
    
    int result = execv(argv[1], &argv[1]);
    
    if (result != 0) {
        fputs(COLOR_RED "Failed to execute: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
    }
}

static void cmd_execwait(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: execwait <filename> [args...]\n" COLOR_RESET, stdout);
        return;
    }

    task_info_t before[32];
    int before_count = zen_list_tasks(before, 32);

    fputs(COLOR_YELLOW "Executing: " COLOR_RESET, stdout);
    fputs(argv[1], stdout);
    fputs("\n", stdout);

    int result = execv(argv[1], &argv[1]);
    if (result != 0) {
        fputs(COLOR_RED "Failed to execute: " COLOR_RESET, stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
        return;
    }

    task_info_t after[32];
    int after_count = zen_list_tasks(after, 32);

    pid_t new_pid = -1;
    for (int a = 0; a < after_count; a++) {
        int found = 0;
        for (int b = 0; b < before_count; b++) {
            if (after[a].pid == before[b].pid) { found = 1; break; }
        }
        if (!found) { new_pid = (pid_t)after[a].pid; break; }
    }

    if (new_pid == -1) {
        fputs(COLOR_RED "Could not find spawned task\n" COLOR_RESET, stdout);
        return;
    }

    waitpid(new_pid, NULL, 0);
}

static void cmd_kill(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: kill <pid>\n" COLOR_RESET, stdout);
        return;
    }

    pid_t pid = atoi(argv[1]);
    if (pid <= 0) {
        fputs(COLOR_RED "Invalid PID\n" COLOR_RESET, stdout);
        return;
    }

    if (kill(pid, SIGKILL) == 0) {
        fputs(COLOR_GREEN "Killed PID " COLOR_RESET, stdout);
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
        fputs(num, stdout);
        fputs("\n", stdout);
    } else {
        fputs(COLOR_RED "Failed — no such PID or already dead\n" COLOR_RESET, stdout);
    }
}

// ============ TIME COMMANDS ============

static void cmd_time(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        fputs(COLOR_CYAN "Time: " COLOR_RESET, stdout);
        
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
        fputs(num, stdout);
        fputs(".", stdout);
        
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
        fputs(num, stdout);
        fputs(" seconds\n", stdout);
    }
}

static void cmd_sleep_cmd(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: sleep <milliseconds>\n" COLOR_RESET, stdout);
        return;
    }
    
    uint32_t ms = atoi(argv[1]);
    fputs(COLOR_YELLOW "Sleeping for " COLOR_RESET, stdout);
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
    fputs(num, stdout);
    fputs("ms...\n", stdout);
    
    zen_sleep_ms(ms);
    fputs(COLOR_GREEN "Done!\n" COLOR_RESET, stdout);
}

// ============ SYSTEM COMMANDS ============

static void cmd_uname(void) {
    utsname_t uts;
    if (uname(&uts) == 0) {
        fputs(COLOR_CYAN, stdout);
        fputs(uts.sysname, stdout);
        fputs(" ", stdout);
        fputs(uts.release, stdout);
        fputs(" ", stdout);
        fputs(uts.version, stdout);
        fputs(" ", stdout);
        fputs(uts.machine, stdout);
        fputs(COLOR_RESET "\n", stdout);
    }
}

static void cmd_mouse(void) {
    fputs(COLOR_CYAN "Mouse Position: " COLOR_RESET, stdout);
    
    uint32_t x = zen_mouse_x();
    uint32_t y = zen_mouse_y();
    uint8_t btn = zen_mouse_btn();
    
    fputs("X=", stdout);
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
    fputs(num, stdout);
    
    fputs(" Y=", stdout);
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
    fputs(num, stdout);
    
    fputs(" Button=", stdout);
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
    fputs(num, stdout);
    fputs("\n", stdout);
}

static void cmd_beep(int argc, char* argv[]) {
    uint32_t freq = 440;
    uint32_t duration = 200;
    
    if (argc >= 2) freq = atoi(argv[1]);
    if (argc >= 3) duration = atoi(argv[2]);
    
    fputs("Beep ", stdout);
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
    fputs(num, stdout);
    fputs("Hz...\n", stdout);
    
    zen_speaker(freq);
    zen_sleep_ms(duration);
    zen_speaker_off();
}

static void cmd_yield_cmd(void) {
    fputs(COLOR_YELLOW "Yielding CPU...\n" COLOR_RESET, stdout);
    sched_yield();
    fputs(COLOR_GREEN "Back from yield\n" COLOR_RESET, stdout);
}

static void cmd_help(void) {
    fputs("  touch <file> [size]  - Create file\n", stdout);
    fputs("  rm <file>            - Delete file\n", stdout);
    fputs("  cat <file>           - Display file contents\n", stdout);
    fputs("  write <file> <text>  - Write to file\n", stdout);
    fputs("  stat <file>          - Show file info\n", stdout);
    fputs("  pwd                  - Print working directory\n", stdout);
    fputs("  ls [dir]             - List directory contents\n", stdout);
    fputs("  cd <dir>             - Change directory\n", stdout);
    fputs("  mkdir <dir>          - Create directory\n", stdout);
    fputs("  rmdir <dir>          - Remove directory\n", stdout);
    fputs("  malloc <size>        - Test sbrk allocation\n", stdout);
    fputs("  mmap <size>          - Test mmap allocation\n", stdout);
    fputs("  sockcreate <name>    - Create IPC socket\n", stdout);
    fputs("  sockwrite <name><msg>- Write to socket\n", stdout);
    fputs("  sockread <name>      - Read from socket\n", stdout);
    fputs("  sockdel <name>       - Delete socket\n", stdout);
    fputs("  exec <file>          - Execute program (or just type progname)\n", stdout);
    fputs("  execwait <file>      - Execute and wait for exit\n", stdout);
    fputs("  kill <pid>           - Kill a process by PID\n", stdout);
    fputs("  ps                   - Show process info\n", stdout);
    fputs("  yield                - Yield CPU\n", stdout);
    fputs("  uname                - System information\n", stdout);
    fputs("  time                 - Show current time\n", stdout);
    fputs("  sleep <ms>           - Sleep for milliseconds\n", stdout);
    fputs("  mouse                - Show mouse position\n", stdout);
    fputs("  beep [freq] [dur]    - Play sound\n", stdout);
    fputs("  help                 - Show this help\n", stdout);
    fputs("  clear                - Clear screen\n", stdout);
    fputs("  echo <text>          - Print text\n", stdout);
    fputs("  version              - Show OS version\n", stdout);
    fputs("  z                    - Repeat last command\n", stdout);
    fputs("  exit                 - Exit shell\n", stdout);
    fputs("  shutdown <sec>       - Shutdown system\n", stdout);
    fputs("  reboot               - Reboot system\n", stdout);
}

static void cmd_clear(void) {
    fputs("\033[2J\033[H", stdout);
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc && i < MAX_ARGS; i++) {
        if (argv[i]) {
            fputs(argv[i], stdout);
            if (i < argc - 1) fputs(" ", stdout);
        }
    }
    fputs("\n", stdout);
}

static void cmd_version(void) {
    fputs(COLOR_BOLD COLOR_CYAN, stdout);
    fputs(" _____           ___  ____  \n", stdout);
    fputs("|__  /___ _ __  / _ \\/ ___| \n", stdout);
    fputs("  / // _ \\ '_ \\| | | \\___ \\ \n", stdout);
    fputs(" / /|  __/ | | | |_| |___) |\n", stdout);
    fputs("/____\\___|_| |_|\\___/|____/ \n\n", stdout);
    cmd_uname();
    fputs(COLOR_RESET, stdout);
}

static int shutdown_accept = 0;
static void cmd_shutdown(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: shutdown <delay_seconds>\n" COLOR_RESET, stdout);
        return;
    }
    
    int time = atoi(argv[1]) * 1000;
    if (time < 0) {
        fputs(COLOR_RED "Invalid delay\n" COLOR_RESET, stdout);
        return;
    }
    
    if (time > 50000 && !shutdown_accept) {
        fputs(COLOR_YELLOW "Large delay. Run again to confirm.\n" COLOR_RESET, stdout);
        shutdown_accept = 1;
        return;
    }
    
    if (time == 0) {
        zen_log("Shutting down", 2, 1);
        zen_shutdown();
    }
    
    zen_sleep_ms(time - 1000);
    zen_log("Shutting down", 2, 1);
    zen_sleep_ms(1000);
    zen_shutdown();
}

static void cmd_reboot(void) {
    fputs(COLOR_YELLOW "Rebooting system in 2 seconds...\n" COLOR_RESET, stdout);
    zen_sleep_ms(2000);
    zen_log("Rebooting", 2, 1);
    zen_reboot();
}

// ============ SHELL CORE ============

static void show_prompt(void) {
    utsname_t uts;
    char hostname[65];
    if (uname(&uts) == 0) {
        strncpy(hostname, uts.nodename, 64);
        hostname[64] = '\0';
    } else {
        strcpy(hostname, "zen");
    }

    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "~");
    }

    fputs(COLOR_BOLD COLOR_GREEN "root@", stdout);
    fputs(hostname, stdout);
    fputs(COLOR_RESET COLOR_BOLD ":", stdout);
    fputs(COLOR_BOLD COLOR_BLUE, stdout);
    fputs(cwd, stdout);
    fputs(COLOR_RESET "$ ", stdout);
    fflush(stdout);
}

static void read_command(void) {
    buffer_pos = 0;
    memset(command_buffer, 0, MAX_COMMAND_LENGTH);
    
    while (1) {
        zen_halt();
        char c = zen_getkey();
        
        if (c == '\0') {
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            command_buffer[buffer_pos] = '\0';
            fputs("\n", stdout);
            break;
        }
        else if (c == '\b' || c == 127) {
            if (buffer_pos > 0) {
                buffer_pos--;
                command_buffer[buffer_pos] = '\0';
                fputs("\b \b", stdout);
            }
        }
        else if (c >= 32 && c < 127) {
            if (buffer_pos < MAX_COMMAND_LENGTH - 1) {
                command_buffer[buffer_pos++] = c;
                char buf[2] = {c, '\0'};
                fputs(buf, stdout);
            }
        }
    }
}

static int is_builtin(const char *name) {
    const char *builtins[] = {
        "help","clear","echo","version","exit",
        "touch","rm","cat","write","stat",
        "pwd","ls","cd","mkdir","rmdir",
        "malloc","mmap",
        "sockcreate","sockwrite","sockread","sockdel",
        "exec","execwait","kill","ps","yield",
        "uname","time","sleep","mouse","beep",
        "shutdown","reboot","z",
    };
    for (int i = 0; i < (int)(sizeof(builtins)/sizeof(builtins[0])); i++) {
        if (strcmp(name, builtins[i]) == 0) return 1;
    }
    return 0;
}

static int file_exists(const char *name) {
    char *path;
    int fd;
    int result = 0;
    path = malloc(strlen("/bin/") + strlen(name) + 1);
    if (!path) {
        return 0;
    }
    strcpy(path, "/bin/");
    strcat(path, name);
    fd = open(path, O_RDONLY);
    if (fd >= 0) { 
        close(fd); 
        result = 1; 
    }
    free(path);
    return result;
}

static void run_file(const char *path, int argc, char *argv[]) {
    int result = execv(path, argv);
    if (result != 0) {
        fputs(COLOR_RED "Failed to execute: " COLOR_RESET, stdout);
        fputs(path, stdout);
        fputs("\n", stdout);
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

    if (is_builtin(argv[0]) && file_exists(argv[0])) {
        fputs(COLOR_YELLOW "'" COLOR_RESET, stdout);
        fputs(argv[0], stdout);
        fputs(COLOR_YELLOW "' is both a builtin and a file.\n" COLOR_RESET, stdout);
        fputs("  [b] Run builtin   [f] Run file\n> ", stdout);
        char choice = '\0';
        while (choice != 'b' && choice != 'f') {
            choice = zen_getkey();
        }
        if (choice == 'b') {
            fputs("builtin\n", stdout);
        } else {
            fputs("file\n", stdout);
            char *path = malloc(strlen("/bin/") + strlen(argv[0]) + 1);
            if (path) {
                strcpy(path, "/bin/");
                strcat(path, argv[0]);
                run_file(path, argc, argv);
                free(path);
            }
            return 1;
        }
    }

    if (strcmp(argv[0], "help") == 0) cmd_help();
    else if (strcmp(argv[0], "clear") == 0) cmd_clear();
    else if (strcmp(argv[0], "echo") == 0) cmd_echo(argc, argv);
    else if (strcmp(argv[0], "version") == 0) cmd_version();
    else if (strcmp(argv[0], "exit") == 0) {
        fputs(COLOR_YELLOW "Exiting...\n" COLOR_RESET, stdout);
        return 0;
    }
    else if (strcmp(argv[0], "touch") == 0) cmd_touch(argc, argv);
    else if (strcmp(argv[0], "rm") == 0) cmd_rm(argc, argv);
    else if (strcmp(argv[0], "cat") == 0) cmd_cat(argc, argv);
    else if (strcmp(argv[0], "write") == 0) cmd_write(argc, argv);
    else if (strcmp(argv[0], "stat") == 0) cmd_stat(argc, argv);
    else if (strcmp(argv[0], "pwd") == 0) cmd_pwd();
    else if (strcmp(argv[0], "ls") == 0) cmd_ls(argc, argv);
    else if (strcmp(argv[0], "cd") == 0) cmd_cd(argc, argv);
    else if (strcmp(argv[0], "mkdir") == 0) cmd_mkdir(argc, argv);
    else if (strcmp(argv[0], "rmdir") == 0) cmd_rmdir(argc, argv);
    else if (strcmp(argv[0], "malloc") == 0) cmd_malloc_test(argc, argv);
    else if (strcmp(argv[0], "mmap") == 0) cmd_mmap_test(argc, argv);
    else if (strcmp(argv[0], "sockcreate") == 0) cmd_socket_create(argc, argv);
    else if (strcmp(argv[0], "sockwrite") == 0) cmd_socket_write(argc, argv);
    else if (strcmp(argv[0], "sockread") == 0) cmd_socket_read(argc, argv);
    else if (strcmp(argv[0], "sockdel") == 0) cmd_socket_delete(argc, argv);
    else if (strcmp(argv[0], "exec") == 0) cmd_exec(argc, argv);
    else if (strcmp(argv[0], "execwait") == 0) cmd_execwait(argc, argv);
    else if (strcmp(argv[0], "kill") == 0) cmd_kill(argc, argv);
    else if (strcmp(argv[0], "ps") == 0) cmd_ps();
    else if (strcmp(argv[0], "yield") == 0) cmd_yield_cmd();
    else if (strcmp(argv[0], "uname") == 0) cmd_uname();
    else if (strcmp(argv[0], "time") == 0) cmd_time();
    else if (strcmp(argv[0], "sleep") == 0) cmd_sleep_cmd(argc, argv);
    else if (strcmp(argv[0], "mouse") == 0) cmd_mouse();
    else if (strcmp(argv[0], "beep") == 0) cmd_beep(argc, argv);
    else if (strcmp(argv[0], "shutdown") == 0) cmd_shutdown(argc, argv);
    else if (strcmp(argv[0], "reboot") == 0) cmd_reboot();
    else if (strcmp(argv[0], "z") == 0) {
        if (lastcmd[0] == '\0') {
            fputs(COLOR_RED "No previous command\n" COLOR_RESET, stdout);
            return 1;
        }
        strcpy(command_buffer, lastcmd);
        execute_command();
    }
    else {
        if (file_exists(argv[0])) {
            char *path = malloc(strlen("/bin/") + strlen(argv[0]) + 1);
            if (path) {
                strcpy(path, "/bin/");
                strcat(path, argv[0]);
                run_file(path, argc, argv);
                free(path);
            }
        } else {
            fputs(COLOR_RED "Unknown: " COLOR_RESET, stdout);
            fputs(argv[0], stdout);
            fputs("\nType 'help' for commands.\n", stdout);
        }
    }
    
    return 1;
}

static void show_banner(void) {
    cmd_clear();
    fputs(COLOR_CYAN COLOR_BOLD, stdout);
    fputs(" _____           ___  ____  \n", stdout);
    fputs("|__  /___ _ __  / _ \\/ ___| \n", stdout);
    fputs("  / // _ \\ '_ \\| | | \\___ \\ \n", stdout);
    fputs(" / /|  __/ | | | |_| |___) |\n", stdout);
    fputs("/____\\___|_| |_|\\___/|____/ \n\n", stdout);
    fputs(COLOR_RESET, stdout);
    fputs(COLOR_YELLOW "ZenOS Shell\n" COLOR_RESET, stdout);
    fputs("Type 'help' for commands.\n", stdout);
    fputs(COLOR_CYAN "F1 key" COLOR_RESET " to shift keyboard focus.\n\n", stdout);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
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
    
    fputs(COLOR_GREEN "Shell exited\n" COLOR_RESET, stdout);
    exit(0);
    return 0;
}

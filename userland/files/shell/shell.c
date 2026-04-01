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
#include "../../userlib.h"

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
    if (success) {
        fputs(COLOR_GREEN "Write successful.\n" COLOR_RESET, stdout);
    } else {
        fputs(COLOR_RED "Write failed\n" COLOR_RESET, stdout);
    }
    close(file);
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


static void run_foreground(const char *path, char *argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        fputs(COLOR_RED "Fork failed\n" COLOR_RESET, stdout);
        return;
    }

    if (pid == 0) {
        execv(path, argv);
        fputs(COLOR_RED "Failed to execute: " COLOR_RESET, stdout);
        fputs(path, stdout);
        fputs("\n", stdout);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        fputs(COLOR_RED "waitpid failed\n" COLOR_RESET, stdout);
}

static void cmd_exec(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: exec <filename> [args...]\n" COLOR_RESET, stdout);
        return;
    }

    run_foreground(argv[1], &argv[1]);
}

static void cmd_execwait(int argc, char* argv[]) {
    if (argc < 2) {
        fputs(COLOR_RED "Usage: execwait <filename> [args...]\n" COLOR_RESET, stdout);
        return;
    }

    run_foreground(argv[1], &argv[1]);
}

static void run_file(const char *path, int argc, char *argv[]) {
    (void)argc;
    run_foreground(path, argv);
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

    fputs(COLOR_BOLD COLOR_GREEN "\nroot@", stdout);
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

    if (!fgets(command_buffer, sizeof(command_buffer), stdin)) {
        command_buffer[0] = '\0';
        return;
    }

    while (command_buffer[buffer_pos] &&
           command_buffer[buffer_pos] != '\n' &&
           command_buffer[buffer_pos] != '\r') {

        putchar(command_buffer[buffer_pos]);
        buffer_pos++;
    }

    if (command_buffer[buffer_pos] == '\n' || command_buffer[buffer_pos] == '\r') {
        command_buffer[buffer_pos] = '\0';
    } else {
        int c = 0;
        while ((c = getchar()) != '\n' && c != EOF) {
            putchar(c);
        }
    }
}

static int is_builtin(const char *name) {
    const char *builtins[] = {
        "help","clear","version","exit",
        "write","cd",
        "sockcreate","sockwrite","sockread","sockdel",
        "exec","execwait","yield",
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
    path = malloc(strlen("/mnt/drv0/bin/") + strlen(name) + 1);
    if (!path) {
        return 0;
    }
    strcpy(path, "/mnt/drv0/bin/");
    strcat(path, name);
    fd = open(path, O_RDONLY);
    if (fd >= 0) { 
        close(fd); 
        result = 1; 
    }
    free(path);
    return result;
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
        char line[8];
        while (choice != 'b' && choice != 'f') {
            if (!fgets(line, sizeof(line), stdin))
                return 1;
            choice = line[0];
        }
        if (choice == 'b') {
            fputs("builtin\n", stdout);
        } else {
            fputs("file\n", stdout);
            char *path = malloc(strlen("/mnt/drv0/bin/") + strlen(argv[0]) + 1);
            if (path) {
                strcpy(path, "/mnt/drv0/bin/");
                strcat(path, argv[0]);
                run_file(path, argc, argv);
                free(path);
            }
            return 1;
        }
    }

    if (strcmp(argv[0], "help") == 0) cmd_help();
    else if (strcmp(argv[0], "clear") == 0) cmd_clear();
    else if (strcmp(argv[0], "version") == 0) cmd_version();
    else if (strcmp(argv[0], "exit") == 0) {
        fputs(COLOR_YELLOW "Exiting...\n" COLOR_RESET, stdout);
        return 0;
    }
    else if (strcmp(argv[0], "write") == 0) cmd_write(argc, argv);
    else if (strcmp(argv[0], "cd") == 0) cmd_cd(argc, argv);
    else if (strcmp(argv[0], "sockcreate") == 0) cmd_socket_create(argc, argv);
    else if (strcmp(argv[0], "sockwrite") == 0) cmd_socket_write(argc, argv);
    else if (strcmp(argv[0], "sockread") == 0) cmd_socket_read(argc, argv);
    else if (strcmp(argv[0], "sockdel") == 0) cmd_socket_delete(argc, argv);
    else if (strcmp(argv[0], "exec") == 0) cmd_exec(argc, argv);
    else if (strcmp(argv[0], "execwait") == 0) cmd_execwait(argc, argv);
    else if (strcmp(argv[0], "yield") == 0) cmd_yield_cmd();
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
            char *path = malloc(strlen("/mnt/drv0/bin/") + strlen(argv[0]) + 1);
            if (path) {
                strcpy(path, "/mnt/drv0/bin/");
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
    fputs("\n", stdout);
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

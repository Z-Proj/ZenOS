#include "../userlib.h"

int main(void) {
    pid_t pid = getpid();
    
    // Create unique filename based on PID
    char filename[32] = "/testfile_";
    int pos = 10;
    
    char pidbuf[16];
    int n = pid;
    int i = 0;
    while (n > 0 || i == 0) {
        pidbuf[i++] = '0' + (n % 10);
        n /= 10;
    }
    pidbuf[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char tmp = pidbuf[j];
        pidbuf[j] = pidbuf[i-1-j];
        pidbuf[i-1-j] = tmp;
    }
    
    for (int j = 0; pidbuf[j] && pos < 30; j++) {
        filename[pos++] = pidbuf[j];
    }
    filename[pos] = '\0';
    
    prints("\033[33m[FileIO ");
    prints(pidbuf);
    prints("] Testing file operations...\033[0m\n");
    
    // Create file
    if (create(filename, 1024) != 0) {
        prints("\033[31m[FileIO ");
        prints(pidbuf);
        prints("] Failed to create file\033[0m\n");
        exit(1);
        return 1;
    }
    
    prints("\033[32m[FileIO ");
    prints(pidbuf);
    prints("] Created: ");
    prints(filename);
    prints("\033[0m\n");
    
    yield();
    
    // Write to file
    zfs_file_t file;
    if (open(filename, &file) != 0) {
        prints("\033[31m[FileIO ");
        prints(pidbuf);
        prints("] Failed to open file\033[0m\n");
        exit(1);
        return 1;
    }
    
    char data[64] = "Hello from process ";
    pos = 19;
    for (int j = 0; pidbuf[j] && pos < 60; j++) {
        data[pos++] = pidbuf[j];
    }
    data[pos++] = '!';
    data[pos] = '\0';
    
    if (write(&file, data, strlen(data)) <= 0) {
        prints("\033[31m[FileIO ");
        prints(pidbuf);
        prints("] Failed to write\033[0m\n");
        close(&file);
        exit(1);
        return 1;
    }
    
    close(&file);
    
    prints("\033[32m[FileIO ");
    prints(pidbuf);
    prints("] Wrote data\033[0m\n");
    
    yield();
    
    // Read back
    if (open(filename, &file) != 0) {
        prints("\033[31m[FileIO ");
        prints(pidbuf);
        prints("] Failed to reopen\033[0m\n");
        exit(1);
        return 1;
    }
    
    char readbuf[128];
    ssize_t bytes = read(&file, readbuf, sizeof(readbuf) - 1);
    if (bytes > 0) {
        readbuf[bytes] = '\0';
        prints("\033[36m[FileIO ");
        prints(pidbuf);
        prints("] Read: ");
        prints(readbuf);
        prints("\033[0m\n");
    }
    
    close(&file);
    
    yield();
    
    // Delete file
    if (delete(filename) != 0) {
        prints("\033[31m[FileIO ");
        prints(pidbuf);
        prints("] Failed to delete\033[0m\n");
    } else {
        prints("\033[32m[FileIO ");
        prints(pidbuf);
        prints("] Deleted file\033[0m\n");
    }
    
    prints("\033[32m[FileIO ");
    prints(pidbuf);
    prints("] Test complete!\033[0m\n");
    
    exit(0);
    return 0;
}
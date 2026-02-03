#include "../userlib.h"

int main(void) {
    pid_t pid = getpid();
    
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
    
    prints("\033[36m[IPCPing ");
    prints(pidbuf);
    prints("] Starting IPC test...\033[0m\n");
    
    // Create socket with unique name
    char sockname[32] = "sock_";
    int pos = 5;
    for (int j = 0; pidbuf[j] && pos < 30; j++) {
        sockname[pos++] = pidbuf[j];
    }
    sockname[pos] = '\0';
    
    if (socket_create(sockname) != 0) {
        prints("\033[31m[IPCPing ");
        prints(pidbuf);
        prints("] Failed to create socket\033[0m\n");
        exit(1);
        return 1;
    }
    
    prints("\033[32m[IPCPing ");
    prints(pidbuf);
    prints("] Created socket: ");
    prints(sockname);
    prints("\033[0m\n");
    
    yield();
    
    // Open socket for writing
    socket_file_t *sock;
    if (socket_open(sockname, &sock) != 0) {
        prints("\033[31m[IPCPing ");
        prints(pidbuf);
        prints("] Failed to open socket\033[0m\n");
        socket_delete(sockname);
        exit(1);
        return 1;
    }
    
    // Send ping messages
    for (int ping = 0; ping < 5; ping++) {
        char msg[64] = "PING from ";
        pos = 10;
        for (int j = 0; pidbuf[j] && pos < 50; j++) {
            msg[pos++] = pidbuf[j];
        }
        msg[pos++] = ' ';
        msg[pos++] = '#';
        msg[pos++] = '0' + ping;
        msg[pos] = '\0';
        
        if (socket_write(sock, msg, strlen(msg)) < 0) {
            prints("\033[31m[IPCPing ");
            prints(pidbuf);
            prints("] Write failed\033[0m\n");
        } else {
            prints("\033[33m[IPCPing ");
            prints(pidbuf);
            prints("] Sent: ");
            prints(msg);
            prints("\033[0m\n");
        }
        
        sleep(100);
        yield();
        
        // Try to read response
        char readbuf[128];
        uint32_t bytes_read = 0;
        if (socket_read(sock, readbuf, sizeof(readbuf) - 1, &bytes_read) >= 0 && bytes_read > 0) {
            readbuf[bytes_read] = '\0';
            prints("\033[36m[IPCPing ");
            prints(pidbuf);
            prints("] Received: ");
            prints(readbuf);
            prints("\033[0m\n");
        }
        
        sleep(50);
        yield();
    }
    
    socket_close(sock);
    socket_delete(sockname);
    
    prints("\033[32m[IPCPing ");
    prints(pidbuf);
    prints("] Test complete!\033[0m\n");
    
    exit(0);
    return 0;
}
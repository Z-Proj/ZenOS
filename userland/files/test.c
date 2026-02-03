#include "../userlib.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

int main(void) {
    log(COLOR_CYAN "\nThis is a bare program for you to build upon.\nCheck userland/files/test.c for the source.\n", 2, 1);
    while(1)exec("test");
    return 0;
}
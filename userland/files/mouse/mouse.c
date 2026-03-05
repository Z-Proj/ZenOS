#include <stdio.h>
#include "../../userlib.h"

int main(void) {
    printf("x=%u y=%u btn=%u\n",
           zen_mouse_x(), zen_mouse_y(), zen_mouse_btn());
    return 0;
}

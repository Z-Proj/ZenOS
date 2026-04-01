#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../userlib.h"

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

int main(void) {
    uint64_t tls_words[2];
    uint64_t fs_value = 0;
    uint64_t fs_word = 0;
    char msg[160];

    tls_words[0] = 0x1122334455667788ULL;
    tls_words[1] = 0x99AABBCCDDEEFF00ULL;

    if(_sc_ret(_syscall2(74, ARCH_SET_FS, (uint64_t)tls_words)) < 0) {
        zen_log("tlstest: ARCH_SET_FS failed", 2, 0);
        return 1;
    }

    if(_sc_ret(_syscall2(74, ARCH_GET_FS, (uint64_t)&fs_value)) < 0) {
        zen_log("tlstest: ARCH_GET_FS failed", 2, 0);
        return 1;
    }

    __asm__ volatile("movq %%fs:0, %0" : "=r"(fs_word));

    snprintf(msg, sizeof(msg),
            "tlstest: fs=%#llx fs0=%#llx expected=%#llx",
            (unsigned long long)fs_value,
            (unsigned long long)fs_word,
            (unsigned long long)tls_words[0]);
    zen_log(msg, 4, 0);
    printf("%s\n", msg);

    if(fs_value != (uint64_t)tls_words || fs_word != tls_words[0]) {
        zen_log("tlstest: mismatch", 2, 0);
        return 2;
    }

    zen_log("tlstest: ok", 4, 0);
    return 0;
}

#include "../../libk/debug/log.h"
#include <stdint.h>
#include "../../libk/string.h"
#include "cpuid.h"

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static void get_vendor_string(char *vendor) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    
    memcpy(vendor, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';
}

static void get_brand_string(char *brand) {
    uint32_t eax, ebx, ecx, edx;
    char *p = brand;
    
    for (uint32_t i = 0x80000002; i <= 0x80000004; i++) {
        cpuid(i, 0, &eax, &ebx, &ecx, &edx);
        memcpy(p, &eax, 4); p += 4;
        memcpy(p, &ebx, 4); p += 4;
        memcpy(p, &ecx, 4); p += 4;
        memcpy(p, &edx, 4); p += 4;
    }
    brand[48] = '\0';
    
    while (*brand == ' ') brand++;
    memmove(brand, brand, strlen(brand) + 1);
}

static void get_cache_info(int vis) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(4, 0, &eax, &ebx, &ecx, &edx);
    
    if (eax & 0x1F) {
        uint32_t ways = ((ebx >> 22) & 0x3FF) + 1;
        uint32_t partitions = ((ebx >> 12) & 0x3FF) + 1;
        uint32_t line_size = (ebx & 0xFFF) + 1;
        uint32_t sets = ecx + 1;
        uint32_t cache_size = ways * partitions * line_size * sets;
        
        const char* cache_type[] = {"Null", "Data", "Instruction", "Unified"};
        uint32_t type = eax & 0x1F;
        uint32_t level = (eax >> 5) & 0x7;
        
        log("L%d %s Cache: %d KB", 1, vis, level, cache_type[type > 3 ? 0 : type], cache_size / 1024);
    }
}

void detect_cpu_info(int vis) {
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];
    char brand[49];
    
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;
    
    get_vendor_string(vendor);
    log("CPU Vendor: %s", 1, vis, vendor);
    
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        get_brand_string(brand);
        log("CPU Brand: %s", 1, vis, brand);
    }
    
    if (max_leaf >= 1) {
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        
        uint32_t stepping = eax & 0xF;
        uint32_t model = (eax >> 4) & 0xF;
        uint32_t family = (eax >> 8) & 0xF;
        uint32_t ext_model = (eax >> 16) & 0xF;
        uint32_t ext_family = (eax >> 20) & 0xFF;
        
        if (family == 0xF) family += ext_family;
        if (family == 0x6 || family == 0xF) model |= (ext_model << 4);
        
        log("Family: %d, Model: %d, Stepping: %d", 1, vis, family, model, stepping);
        
        uint32_t logical_cores = (ebx >> 16) & 0xFF;
        log("Logical processors: %d", 1, vis, logical_cores);
        
        if (edx & (1 << 28)) {
            log("Hyper-Threading: Supported", 1, vis);
        }
        
        if (edx & (1 << 23)) {
            log("MMX: Supported", 1, vis);
        }
        if (edx & (1 << 25)) {
            log("SSE: Supported", 1, vis);
        }
        if (edx & (1 << 26)) {
            log("SSE2: Supported", 1, vis);
        }
        if (ecx & (1 << 0)) {
            log("SSE3: Supported", 1, vis);
        }
        if (ecx & (1 << 9)) {
            log("SSSE3: Supported", 1, vis);
        }
        if (ecx & (1 << 19)) {
            log("SSE4.1: Supported", 1, vis);
        }
        if (ecx & (1 << 20)) {
            log("SSE4.2: Supported", 1, vis);
        }
        if (ecx & (1 << 28)) {
            log("AVX: Supported", 1, vis);
        }
    }
    
    if (max_leaf >= 4) {
        uint32_t cores = 0;
        for (int i = 0; i < 8; i++) {
            cpuid(4, i, &eax, &ebx, &ecx, &edx);
            if ((eax & 0x1F) == 0) break;
            
            if (i == 0) {
                cores = ((eax >> 26) & 0x3F) + 1;
                log("Physical cores: %d", 1, vis, cores);
            }
        }
        get_cache_info(vis);
    }
    
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000006) {
        cpuid(0x80000006, 0, &eax, &ebx, &ecx, &edx);
        uint32_t l3_cache = ((ecx >> 18) & 0x3FFF) * 512;
        if (l3_cache > 0) {
            log("L3 Cache: %d KB", 1, vis, l3_cache / 1024);
        }
    }
    
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000001) {
        cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
        if (edx & (1 << 29)) {
            log("x86-64: Supported", 1, vis);
        }
        if (edx & (1 << 20)) {
            log("NX Bit: Supported", 1, vis);
        }
    }
    
    log("CPU detection complete", 4, vis);
}
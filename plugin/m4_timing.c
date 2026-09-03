#include "m4_timing.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void get_mnemonic(const char *disas, char out[32]) {
    size_t n = 0;

    while (isspace((unsigned char)*disas)) {
        disas++;
    }
    while (*disas && !isspace((unsigned char)*disas) && n + 1 < 32) {
        out[n++] = (char)tolower((unsigned char)*disas++);
    }
    out[n] = '\0';

    /* Instruction width suffixes do not affect the timing class. */
    if (n > 2 && out[n - 2] == '.' && (out[n - 1] == 'w' || out[n - 1] == 'n')) {
        out[n - 2] = '\0';
    }
}

static bool mnemonic_is(const char *mnemonic, const char *const *names,
                        size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(mnemonic, names[i]) == 0) {
            return true;
        }
    }
    return false;
}

static unsigned register_number(const char *name) {
    if (name[0] == 'r' && isdigit((unsigned char)name[1])) {
        unsigned value = 0;
        for (size_t i = 1; isdigit((unsigned char)name[i]); i++) {
            value = value * 10u + (unsigned)(name[i] - '0');
        }
        return value;
    }
    if (strncmp(name, "sp", 2) == 0) return 13;
    if (strncmp(name, "lr", 2) == 0) return 14;
    if (strncmp(name, "pc", 2) == 0) return 15;
    return 32;
}

static unsigned count_core_registers(const char *disas, bool *has_pc) {
    const char *p = strchr(disas, '{');
    unsigned count = 0;

    *has_pc = false;
    if (!p) return 0;
    p++;
    while (*p && *p != '}') {
        char first[8] = {0};
        char last[8] = {0};
        size_t n = 0;

        while (isspace((unsigned char)*p) || *p == ',') p++;
        while ((isalnum((unsigned char)*p) || *p == '_') && n + 1 < sizeof(first)) {
            first[n++] = (char)tolower((unsigned char)*p++);
        }
        if (n == 0) {
            if (*p) p++;
            continue;
        }

        unsigned lo = register_number(first);
        unsigned hi = lo;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '-') {
            p++;
            while (isspace((unsigned char)*p)) p++;
            n = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && n + 1 < sizeof(last)) {
                last[n++] = (char)tolower((unsigned char)*p++);
            }
            hi = register_number(last);
        }

        if (lo <= 15 && hi <= 15 && hi >= lo) {
            count += hi - lo + 1;
            if (lo <= 15 && hi >= 15) *has_pc = true;
        }
    }
    return count;
}

static unsigned count_fp_register_words(const char *disas) {
    const char *p = strchr(disas, '{');
    unsigned words = 0;

    if (!p) return 0;
    p++;
    while (*p && *p != '}') {
        char kind;
        unsigned lo = 0, hi;

        while (isspace((unsigned char)*p) || *p == ',') p++;
        kind = (char)tolower((unsigned char)*p);
        if ((kind != 's' && kind != 'd') || !isdigit((unsigned char)p[1])) {
            if (*p) p++;
            continue;
        }
        p++;
        while (isdigit((unsigned char)*p)) {
            lo = lo * 10u + (unsigned)(*p++ - '0');
        }
        hi = lo;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '-') {
            p++;
            while (isspace((unsigned char)*p)) p++;
            if ((char)tolower((unsigned char)*p) == kind) p++;
            hi = 0;
            while (isdigit((unsigned char)*p)) {
                hi = hi * 10u + (unsigned)(*p++ - '0');
            }
        }
        if (hi >= lo) words += (hi - lo + 1) * (kind == 'd' ? 2u : 1u);
    }
    return words;
}

static bool is_conditional_branch(const char *m) {
    static const char *const names[] = {
        "beq", "bne", "bcs", "bhs", "bcc", "blo", "bmi", "bpl",
        "bvs", "bvc", "bhi", "bls", "bge", "blt", "bgt", "ble"
    };
    return mnemonic_is(m, names, sizeof(names) / sizeof(names[0]));
}

static bool writes_pc(const char *disas) {
    const char *p = disas;

    while (isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p)) p++;
    while (isspace((unsigned char)*p)) p++;
    return tolower((unsigned char)p[0]) == 'p' &&
           tolower((unsigned char)p[1]) == 'c' &&
           !isalnum((unsigned char)p[2]);
}

static bool fp_destination_is_double(const char *disas) {
    const char *p = disas;

    while (isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p)) p++;
    while (isspace((unsigned char)*p)) p++;
    return tolower((unsigned char)*p) == 'd' && isdigit((unsigned char)p[1]);
}

struct m4_timing m4_timing_for_disas(const char *disas) {
    char m[32];
    struct m4_timing result = {.cycles = 1};
    bool has_pc = false;

    get_mnemonic(disas, m);

    if (m[0] == '\0' || !isalpha((unsigned char)m[0])) {
        result.undecoded = 1;
        return result;
    }

    if (strcmp(m, "sdiv") == 0 || strcmp(m, "udiv") == 0) {
        result.cycles = 12;
        result.divide_extra = 11;
        return result;
    }

    if (is_conditional_branch(m) || strcmp(m, "cbz") == 0 ||
        strcmp(m, "cbnz") == 0) {
        /* The execution plugin adds worst-case P=3 only when it is taken. */
        result.conditional_branch = 1;
        return result;
    }

    if (strcmp(m, "b") == 0 || strcmp(m, "bl") == 0 || strcmp(m, "bx") == 0 ||
        strcmp(m, "blx") == 0) {
        /* Worst-case P=3. */
        result.cycles = 4;
        result.branch_extra = 3;
        return result;
    }

    if (strcmp(m, "tbb") == 0 || strcmp(m, "tbh") == 0) {
        /* 2 + worst-case pipeline refill P=3. */
        result.cycles = 5;
        result.branch_extra = 4;
        return result;
    }

    if (strncmp(m, "ldm", 3) == 0 || strncmp(m, "stm", 3) == 0 ||
        strcmp(m, "push") == 0 || strcmp(m, "pop") == 0) {
        unsigned n = count_core_registers(disas, &has_pc);
        result.cycles = 1 + n + (has_pc ? 3 : 0);
        result.memory_extra = n;
        result.branch_extra = has_pc ? 3 : 0;
        return result;
    }

    if (strcmp(m, "ldrd") == 0 || strcmp(m, "strd") == 0) {
        result.cycles = 3;
        result.memory_extra = 2;
        return result;
    }

    if (strncmp(m, "ldr", 3) == 0 || strncmp(m, "str", 3) == 0 ||
        strncmp(m, "ldrex", 5) == 0 || strncmp(m, "strex", 5) == 0) {
        result.cycles = writes_pc(disas) && strncmp(m, "ldr", 3) == 0 ? 5 : 2;
        result.memory_extra = 1;
        if (result.cycles == 5) result.branch_extra = 3;
        return result;
    }

    if (m[0] == 'v') {
        unsigned words;

        if (strncmp(m, "vdiv", 4) == 0 || strncmp(m, "vsqrt", 5) == 0) {
            /* Includes the documented worst-case following-consumer penalty. */
            result.cycles = 15;
            result.fp_extra = 14;
        } else if (strncmp(m, "vmla", 4) == 0 || strncmp(m, "vmls", 4) == 0 ||
                   strncmp(m, "vnmla", 5) == 0 || strncmp(m, "vnmls", 5) == 0 ||
                   strncmp(m, "vfma", 4) == 0 || strncmp(m, "vfms", 4) == 0 ||
                   strncmp(m, "vfnma", 5) == 0 || strncmp(m, "vfnms", 5) == 0) {
            result.cycles = 4;
            result.fp_extra = 3;
        } else if (strncmp(m, "vldr", 4) == 0 || strncmp(m, "vstr", 4) == 0) {
            result.cycles = strstr(m, ".64") || fp_destination_is_double(disas) ? 3 : 2;
            result.memory_extra = result.cycles - 1;
        } else if (strncmp(m, "vldm", 4) == 0 || strncmp(m, "vstm", 4) == 0 ||
                   strcmp(m, "vpush") == 0 || strcmp(m, "vpop") == 0) {
            words = count_fp_register_words(disas);
            result.cycles = 1 + words;
            result.memory_extra = words;
        } else if (strncmp(m, "vmov", 4) == 0) {
            /* The register-pair form is two cycles; choose it pessimistically. */
            result.cycles = 2;
            result.fp_extra = 1;
        } else if (strncmp(m, "vadd", 4) == 0 || strncmp(m, "vsub", 4) == 0 ||
                   strncmp(m, "vmul", 4) == 0 || strncmp(m, "vnmul", 5) == 0 ||
                   strncmp(m, "vcvt", 4) == 0) {
            /* One execution cycle plus worst-case immediate result consumption. */
            result.cycles = 2;
            result.fp_extra = 1;
        }
        return result;
    }

    if ((strcmp(m, "mov") == 0 || strcmp(m, "movs") == 0 ||
         strcmp(m, "add") == 0 || strcmp(m, "adds") == 0) &&
        writes_pc(disas)) {
        result.cycles = 4;
        result.branch_extra = 3;
        return result;
    }

    if (strcmp(m, "isb") == 0) {
        result.cycles = 4;
        result.system_extra = 3;
        return result;
    }

    if (strcmp(m, "cpsid") == 0 || strcmp(m, "cpsie") == 0 ||
        strcmp(m, "mrs") == 0 || strcmp(m, "msr") == 0) {
        result.cycles = 2;
        result.system_extra = 1;
        return result;
    }

    if (strcmp(m, "wfe") == 0 || strcmp(m, "wfi") == 0 ||
        strcmp(m, "dmb") == 0 || strcmp(m, "dsb") == 0 ||
        strcmp(m, "svc") == 0 || strcmp(m, "bkpt") == 0) {
        /* No finite instruction-only upper bound: report, but retain base cost. */
        result.unbounded = 1;
    }

    return result;
}

#ifndef DOLRECOMP_ORACLE_DEDICATED_CASES_H
#define DOLRECOMP_ORACLE_DEDICATED_CASES_H

#include "common/types.h"

typedef struct {
    const char* name;
    u32 raw;
    u32 address;
} DedicatedCase;

static const DedicatedCase dedicated_cases[] = {
    {"bl",      0x48000069u, 0x81010000u},
    {"beq",     0x41820064u, 0x81010004u},
    {"blr",     0x4E800020u, 0x81010008u},
    {"bctr",    0x4E800420u, 0x8101000Cu},
    {"tw",      0x7FE32008u, 0x81010010u},
    {"twi",     0x0FE3FFFFu, 0x81010014u},
    {"sc",      0x44000002u, 0x81010018u},
    {"rfi",     0x4C000064u, 0x8101001Cu},
    {"mfmsr",   0x7D2000A6u, 0x81010020u},
    {"mtmsr",   0x7D400124u, 0x81010024u},
    {"mfsr",    0x7D6304A6u, 0x81010028u},
    {"mfsrin",  0x7D806D26u, 0x8101002Cu},
    {"mtsr",    0x7DC401A4u, 0x81010030u},
    {"mtsrin",  0x7DE081E4u, 0x81010034u},
    {"mftb",    0x7E2C42E6u, 0x81010038u},
    {"mftbu",   0x7E4D42E6u, 0x8101003Cu},
    {"tlbie",   0x7C009A64u, 0x81010040u},
    {"tlbsync", 0x7C00046Cu, 0x81010044u},
    {"dcbz_l",  0x1014AFECu, 0x81010048u},
    {"dcbst",   0x7C14A86Cu, 0x8101004Cu},
    {"dcbf",    0x7C14A8ACu, 0x81010050u},
    {"dcbt",    0x7C14AA2Cu, 0x81010054u},
    {"dcbi",    0x7C14ABACu, 0x81010058u},
    {"icbi",    0x7C14AFACu, 0x8101005Cu},
    {"eciwx",   0x7ED4AA6Cu, 0x81010060u},
    {"ecowx",   0x7EF4AB6Cu, 0x81010064u},
};

static const unsigned dedicated_case_count =
    (unsigned)(sizeof(dedicated_cases) / sizeof(dedicated_cases[0]));

#endif

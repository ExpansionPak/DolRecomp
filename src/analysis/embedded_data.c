#include "analysis/embedded_data.h"
#include <string.h>

int rpx_embedded_data_word(u32 raw) {
    return raw == 0x00400000u || raw == 0x00600000u;
}

int word_bytes_are_text(u32 raw) {
    u8 bytes[4] = {
        (u8)(raw >> 24),
        (u8)(raw >> 16),
        (u8)(raw >> 8),
        (u8)raw,
    };

    int printable = 0;
    for (u32 i = 0; i < 4; i++) {
        if (bytes[i] >= 0x20 && bytes[i] <= 0x7Eu) {
            printable++;
        } else if (bytes[i] != 0) {
            return 0;
        }
    }

    return printable >= 3;
}

int dol_embedded_data_word(u32 raw) {
    if (raw == 0)
        return 1;
    if ((raw >> 26) == 0)
        return 1;
    if (word_bytes_are_text(raw))
        return 1;
    return 0;
}

int embedded_data_word(EmbeddedDataMode mode, u32 raw) {
    switch (mode) {
    case EMBEDDED_DATA_DOL:
        return dol_embedded_data_word(raw);
    case EMBEDDED_DATA_RPX:
        return rpx_embedded_data_word(raw);
    default:
        return 0;
    }
}


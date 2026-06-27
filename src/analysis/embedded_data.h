#ifndef DOLRECOMP_ANALYSIS_EMBEDDED_DATA_H
#define DOLRECOMP_ANALYSIS_EMBEDDED_DATA_H

#include "common/types.h"

typedef enum {
    EMBEDDED_DATA_NONE = 0,
    EMBEDDED_DATA_DOL,
    EMBEDDED_DATA_RPX
} EmbeddedDataMode;

int rpx_embedded_data_word(u32 raw);
int word_bytes_are_text(u32 raw);
int dol_embedded_data_word(u32 raw);
int embedded_data_word(EmbeddedDataMode mode, u32 raw);

#endif

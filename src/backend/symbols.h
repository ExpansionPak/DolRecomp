#ifndef DOLRECOMP_BACKEND_SYMBOLS_H
#define DOLRECOMP_BACKEND_SYMBOLS_H

#include "analysis/code_section.h"
#include "analysis/symbol_map.h"
#include "common/types.h"
#include <stdio.h>

u32 count_code_symbols(const DolRecompSymbolMap* map,
                       const LoadedCodeSection* sections, u32 section_count);
int emit_symbol_definitions(FILE* output, const DolRecompSymbolMap* map,
                            const LoadedCodeSection* sections, u32 section_count);

#endif

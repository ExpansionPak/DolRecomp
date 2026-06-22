#include <stdio.h>
#include <stdlib.h>

#include "../src/backend/emitter.h"
#include "../src/core/types.h"
#include "../src/frontend/decoder.h"

#define BASE 0x80003000u

static const u32 opcode_raws[] = {
    0x1C64FFF9, 0x20850001, 0x38610010, 0x3084FFFF,
    0x34A5FFFF, 0x3CA01234, 0x2C03FFFF, 0x28038000,
    0x6064FF00, 0x64851234, 0x68A6FFFF, 0x6CC78000,
    0x70E800FF, 0x74E900FF, 0x80610000, 0x84810004,
    0x88A10008, 0x8CC1000C, 0xA0E10010, 0xA5010014,
    0xA921FFFC, 0xAD410018, 0x9061001C, 0x94810020,
    0x98A10024, 0x9CC10028, 0xB0E1002C, 0xB5010030,
    0xBA810034, 0xBE810064, 0x480000C8, 0x418200C4,
    0x4E800020, 0x4E800420, 0x4C432202, 0x4C432102,
    0x4C432242, 0x4C4321C2, 0x4C432042, 0x4C432382,
    0x4C432342, 0x4C432182, 0x4D0C0000, 0x7D400026,
    0x7D4FF120, 0x7D4802A6, 0x7D4803A6, 0x7C832000,
    0x7D032040, 0x7D4B6214, 0x7D6C6814, 0x7D8D7114,
    0x7DAE0194, 0x7DCF8050, 0x7DF08810, 0x7E119110,
    0x7E320190, 0x7E5300D0, 0x7E93A838, 0x7EB4B078,
    0x7ED5BB78, 0x7EF6C338, 0x7F17CA78, 0x7F38D3B8,
    0x7F59D8F8, 0x7F7AE238, 0x7F9B0034, 0x7FBC0774,
    0x7FDD0734, 0x7FFE1830, 0x7C7F2430, 0x7C832E30,
    0x7CA43E70, 0x54C52A2E, 0x5CE64136, 0x5107421E,
    0x7C64282E, 0x7CC4286E, 0x7CE428AE, 0x7D0428EE,
    0x7D242A2E, 0x7D442A6E, 0x7D642AAE, 0x7D842AEE,
    0x7C642C2C, 0x7CC7462C, 0x7C64292E, 0x7CC4296E,
    0x7CE429AE, 0x7D0429EE, 0x7D242B2E, 0x7D442B6E,
    0x7D2A5D2C, 0x7D8D772C, 0x7C0F87EC,
    0xC0240000, 0xC4440004, 0xC8640008, 0xCC840010,
    0xD0A40014, 0xD4C40018, 0xD8E40020, 0xDD040028,
    0x7D242C2E, 0x7D442C6E, 0x7D642CAE, 0x7D842CEE,
    0x7DA42D2E, 0x7DC42D6E, 0x7DE42DAE, 0x7E042DEE,
    0xEC22182A, 0xEC853028, 0xECE80272, 0xED4B6024,
    0xFDAE782A, 0xFE119028, 0xFE740572, 0xFED7C024,
    0xFF20D090, 0xFF60E050, 0xFFA0F210, 0xFFE00110,
    0xFC201018, 0xFD032000, 0xFD853040,
    0xE0240000, 0xE4640008, 0xF0A40010, 0xF4E40018,
    0x1124280C, 0x1164284C, 0x11A4280E, 0x11E4284E,
    0x7C6429D6, 0x7CC74096, 0x7D2A5816, 0x7D8D73D6,
    0x7DF08B96,
};

int main(int argc, char** argv) {
    const int count = (int)(sizeof(opcode_raws) / sizeof(opcode_raws[0]));
    FILE* out = stdout;

    if ((PPC_OP_COUNT - 1) != count) {
        fprintf(stderr, "opcode count mismatch: enum has %d, table has %d\n",
                PPC_OP_COUNT - 1, count);
        return 1;
    }

    if (argc > 1) {
        out = fopen(argv[1], "w");
        if (!out) {
            perror(argv[1]);
            return 1;
        }
    }

    PPCInst* insts = (PPCInst*)calloc((size_t)count, sizeof(PPCInst));
    if (!insts) {
        if (out != stdout) fclose(out);
        return 1;
    }

    for (int i = 0; i < count; i++) {
        insts[i] = ppc_decode(opcode_raws[i], BASE + (u32)i * 4u);
        if (insts[i].op == PPC_OP_UNKNOWN) {
            fprintf(stderr, "raw 0x%08X decoded as unknown\n", opcode_raws[i]);
            free(insts);
            if (out != stdout) fclose(out);
            return 1;
        }
    }

    emit_header(out);
    emit_function(out, insts, (u32)count, BASE);

    PPCInst external_branch[3];
    external_branch[0] = ppc_decode(0x48001000, BASE + 0x1000);
    external_branch[1] = ppc_decode(0x41821000, BASE + 0x1004);
    external_branch[2] = ppc_decode(0x4E800020, BASE + 0x1008);
    emit_function(out, external_branch, 3, BASE + 0x1000);
    emit_footer(out);

    free(insts);
    if (out != stdout) fclose(out);
    return 0;
}

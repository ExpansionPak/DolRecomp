#include <stdio.h>
#include <stdlib.h>

#include "../src/backend/dispatch.h"
#include "../src/backend/emitter.h"
#include "../src/common/types.h"
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
    0xFC201018, 0xFC2220EE, 0xFD032000, 0xFD853040,
    0xFFE0008C, 0xFFE0004C,
    0xE0240000, 0xE4640008, 0xF0A40010, 0xF4E40018,
    0x1124280C, 0x1164284C, 0x11A4280E, 0x11E4284E,
    0x1022182A, 0x10853028, 0x10E80272, 0x114B6024,
    0x11AE83FA, 0x1232A4F8, 0x12B6C5FE, 0x133AE6FC,
    0x10201050, 0x10602210, 0x10A03110, 0x10E04090,
    0x112A62D4, 0x11AE83D6, 0x123204D8, 0x1295059A,
    0x12F8D65C, 0x137CF75E, 0x10221C20, 0x10853460,
    0x10E84CA0, 0x114B64E0, 0x110D7000, 0x118F8040,
    0x12119080, 0x1293A0C0, 0x12B6C5EE,
    0x7C6429D6, 0x7CC74096, 0x7D2A5816, 0x7D8D73D6,
    0x7DF08B96,
    0x7C6401D4, 0x7CA601D0, 0x7CEC6CAA, 0x7D34AC2A,
    0x7D8D8DAA, 0x7DCF852A, 0x7E329828, 0x7E95B12D,
    0x7EF8CFAE, 0xEC201030, 0xFC602034, 0x10A03030,
    0x10E04034, 0xFD20501C, 0xFD60601E, 0xFDAE83FA,
    0xEE32A4FA, 0xFEB6C5F8, 0xEF3AE6F8, 0xFFBE07FE,
    0xEC2220FE, 0xFCA641FC, 0xED2A62FC, 0xFDA0048E,
    0xFD0C0080, 0xFE00A10C, 0xFCB4758E, 0x7C0004AC,
    0x7C0006AC, 0x4C00012C,
    0x7D4B6614, 0x7D6C6C14, 0x7D8D7514, 0x7DAE05D4,
    0x7DCF0594, 0x7DF08C50, 0x7E119410, 0x7E329D10,
    0x7E5305D0, 0x7E740590, 0x7E9504D0, 0x7EB6BDD6,
    0x7ED7C7D6, 0x7EF8CF96, 0x0C85FFFE, 0x7CC74008,
    0x7D000400, 0x7D2000A6, 0x7D400124, 0x7D6304A6,
    0x7D806D26, 0x7DC401A4, 0x7DE081E4, 0x7C11906C,
    0x7C13A0AC, 0x7C15B1EC, 0x7C17C22C, 0x7C19D3AC,
    0x7C1BE7AC, 0x7C00046C,
    0x44000002, 0x4C000064, 0x7C6C42E6, 0x100537EC,
    0x7C003A64, 0x7D09526C, 0x7D6C6B6C,
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

    PPCInst adjacent_branch[3];
    adjacent_branch[0] = ppc_decode(0x48001000, BASE + 0x100C);
    adjacent_branch[1] = ppc_decode(0x41821000, BASE + 0x1010);
    adjacent_branch[2] = ppc_decode(0x4E800020, BASE + 0x1014);
    emit_function(out, adjacent_branch, 3, BASE + 0x100C);

    PPCInst linked_lr_branch = ppc_decode(0x4E800021, BASE + 0x1018);
    emit_function(out, &linked_lr_branch, 1, BASE + 0x1018);

    FunctionList funcs = {0};
    if (!function_list_add(&funcs, BASE, BASE + (u32)count * 4u) ||
        !function_list_add(&funcs, BASE + 0x1000, BASE + 0x100C) ||
        !function_list_add(&funcs, BASE + 0x100C, BASE + 0x1018) ||
        !function_list_add(&funcs, BASE + 0x1018, BASE + 0x101C)) {
        function_list_free(&funcs);
        free(insts);
        if (out != stdout) fclose(out);
        return 1;
    }
    emit_dispatch_helpers(out, &funcs, BASE);
    function_list_free(&funcs);

    emit_footer(out);

    free(insts);
    if (out != stdout) fclose(out);
    return 0;
}

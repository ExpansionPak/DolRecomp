#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

extern u32 dolphin_test_blr_fn(void);
extern u32 dolphin_test_bctr_fn(void);

asm(
    ".text\n"
    ".globl dolphin_test_blr_fn\n"
    "dolphin_test_blr_fn:\n"
    "    li 3,0x55\n"
    "    blr\n"
    ".globl dolphin_test_bctr_fn\n"
    "dolphin_test_bctr_fn:\n"
    "    lis 4,dolphin_test_bctr_target@ha\n"
    "    addi 4,4,dolphin_test_bctr_target@l\n"
    "    mtctr 4\n"
    "    bctr\n"
    "    li 3,0\n"
    "dolphin_test_bctr_target:\n"
    "    li 3,0x66\n"
    "    blr\n"
);

static int pass_count = 0;
static int fail_count = 0;

static void check(int cond, const char* name) {
    if (cond) {
        pass_count++;
        kprintf("  PASS: %s\n", name);
        printf("  PASS: %s\n", name);
    } else {
        fail_count++;
        kprintf("  FAIL: %s\n", name);
        printf("  FAIL: %s\n", name);
    }
}

static void check_eq(u32 got, u32 want, const char* name) {
    if (got == want) {
        pass_count++;
        kprintf("  PASS: %s (0x%08X)\n", name, got);
        printf("  PASS: %s (0x%08X)\n", name, got);
    } else {
        fail_count++;
        kprintf("  FAIL: %s - got 0x%08X, want 0x%08X\n", name, got, want);
        printf("  FAIL: %s - got 0x%08X, want 0x%08X\n", name, got, want);
    }
}

static void test_immediate_arithmetic(void) {
    kprintf("--- immediate arithmetic ---\n");
    printf("--- immediate arithmetic ---\n");

    u32 result;
    u32 xer;
    u32 saved_xer;

    asm volatile("li %0,42" : "=r"(result));
    check_eq(result, 42, "addi/li literal");

    u32 base = 100;
    asm volatile("addi %0,%1,-10" : "=r"(result) : "r"(base));
    check_eq(result, 90, "addi negative SIMM");

    asm volatile("lis %0,0x1234" : "=r"(result));
    check_eq(result, 0x12340000, "addis/lis shifted immediate");

    base = 0x00020000;
    asm volatile("addis %0,%1,-1" : "=r"(result) : "r"(base));
    check_eq(result, 0xFFFF0000u + base, "addis negative shifted immediate");

    asm volatile("mfxer %0" : "=r"(saved_xer));

    base = 0;
    asm volatile(
        "addic %0,%2,-1\n"
        "mfxer %1\n"
        : "=&r"(result), "=r"(xer)
        : "r"(base)
    );
    check_eq(result, 0xFFFFFFFF, "addic 0 + -1 result");
    check_eq(xer & 0x20000000u, 0, "addic 0 + -1 clears CA");

    base = 1;
    asm volatile(
        "addic %0,%2,-1\n"
        "mfxer %1\n"
        : "=&r"(result), "=r"(xer)
        : "r"(base)
    );
    check_eq(result, 0, "addic 1 + -1 result");
    check_eq(xer & 0x20000000u, 0x20000000u, "addic 1 + -1 sets CA");

    asm volatile("mtxer %0" : : "r"(saved_xer));
}

static void test_compare_and_bc(void) {
    kprintf("--- compare / bc ---\n");
    printf("--- compare / bc ---\n");

    u32 flag = 0;
    u32 value = 7;
    asm volatile(
        "cmpwi %1,7\n"
        "bne 1f\n"
        "li %0,1\n"
        "1:\n"
        : "+r"(flag)
        : "r"(value)
        : "cr0"
    );
    check_eq(flag, 1, "cmpwi equal then bne not taken");

    flag = 0;
    value = 0xFFFFFFFFu;
    asm volatile(
        "cmplwi %1,0x8000\n"
        "bgt 1f\n"
        "b 2f\n"
        "1:\n"
        "li %0,1\n"
        "2:\n"
        : "+r"(flag)
        : "r"(value)
        : "cr0"
    );
    check_eq(flag, 1, "cmplwi unsigned greater then bc taken");
}

static void test_immediate_logical(void) {
    kprintf("--- immediate logical ---\n");
    printf("--- immediate logical ---\n");

    u32 result;
    u32 flag;
    u32 src = 0x12340000;

    asm volatile("ori %0,%1,0x5678" : "=r"(result) : "r"(src));
    check_eq(result, 0x12345678, "ori low half");

    src = 0x00005678;
    asm volatile("oris %0,%1,0x1234" : "=r"(result) : "r"(src));
    check_eq(result, 0x12345678, "oris high half");

    src = 0xAAAA0000;
    asm volatile("xori %0,%1,0xFFFF" : "=r"(result) : "r"(src));
    check_eq(result, 0xAAAAFFFF, "xori low half");

    src = 0x2AAA5555;
    asm volatile("xoris %0,%1,0x8000" : "=r"(result) : "r"(src));
    check_eq(result, 0xAAAA5555, "xoris high half");

    src = 0x123400F0;
    flag = 0;
    asm volatile(
        "andi. %0,%2,0x00FF\n"
        "beq 1f\n"
        "li %1,1\n"
        "1:\n"
        : "=&r"(result), "+r"(flag)
        : "r"(src)
        : "cr0"
    );
    check_eq(result, 0xF0, "andi. result");
    check_eq(flag, 1, "andi. updates CR0 nonzero");

    src = 0x12FF0000;
    flag = 0;
    asm volatile(
        "andis. %0,%2,0x00FF\n"
        "beq 1f\n"
        "li %1,1\n"
        "1:\n"
        : "=&r"(result), "+r"(flag)
        : "r"(src)
        : "cr0"
    );
    check_eq(result, 0x00FF0000, "andis. result");
    check_eq(flag, 1, "andis. updates CR0 nonzero");
}

static void test_loads(void) {
    kprintf("--- loads ---\n");
    printf("--- loads ---\n");

    u32 result;
    volatile u32 words[8];
    words[0] = 0xDEADBEEF;
    words[1] = 0xCAFEBABE;
    words[2] = 0x12345678;

    asm volatile("lwz %0,0(%1)" : "=r"(result) : "r"(words) : "memory");
    check_eq(result, 0xDEADBEEF, "lwz offset 0");

    volatile u32* wp = &words[0];
    asm volatile("lwzu %0,4(%1)" : "=r"(result), "+r"(wp) : : "memory");
    check_eq(result, 0xCAFEBABE, "lwzu loads updated address");
    check((u32)wp == (u32)&words[1], "lwzu updates rA");

    volatile u8 bytes[8];
    bytes[0] = 0x11;
    bytes[1] = 0xA5;
    bytes[2] = 0x7F;

    asm volatile("lbz %0,1(%1)" : "=r"(result) : "r"(bytes) : "memory");
    check_eq(result, 0xA5, "lbz zero-extends byte");

    volatile u8* bp = &bytes[0];
    asm volatile("lbzu %0,2(%1)" : "=r"(result), "+r"(bp) : : "memory");
    check_eq(result, 0x7F, "lbzu loads updated address");
    check((u32)bp == (u32)&bytes[2], "lbzu updates rA");

    volatile u16 halves[4];
    halves[0] = 0x1234;
    halves[1] = 0x8001;
    halves[2] = 0x7FFF;

    asm volatile("lhz %0,0(%1)" : "=r"(result) : "r"(halves) : "memory");
    check_eq(result, 0x1234, "lhz zero-extends halfword");

    volatile u16* hp = &halves[0];
    asm volatile("lhzu %0,4(%1)" : "=r"(result), "+r"(hp) : : "memory");
    check_eq(result, 0x7FFF, "lhzu loads updated address");
    check((u32)hp == (u32)&halves[2], "lhzu updates rA");

    asm volatile("lha %0,2(%1)" : "=r"(result) : "r"(halves) : "memory");
    check_eq(result, 0xFFFF8001, "lha sign-extends halfword");
}

static void test_stores(void) {
    kprintf("--- stores ---\n");
    printf("--- stores ---\n");

    volatile u32 words[8];
    memset((void*)words, 0, sizeof(words));

    u32 value = 0xAABBCCDD;
    asm volatile("stw %0,4(%1)" : : "r"(value), "r"(words) : "memory");
    check_eq(words[1], 0xAABBCCDD, "stw offset 4");

    volatile u32* wp = &words[1];
    value = 0x11223344;
    asm volatile("stwu %1,4(%0)" : "+r"(wp) : "r"(value) : "memory");
    check_eq(words[2], 0x11223344, "stwu stores updated address");
    check((u32)wp == (u32)&words[2], "stwu updates rA");

    volatile u8 bytes[8];
    memset((void*)bytes, 0, sizeof(bytes));

    value = 0x123456A5;
    asm volatile("stb %0,1(%1)" : : "r"(value), "r"(bytes) : "memory");
    check_eq(bytes[1], 0xA5, "stb stores low byte");

    volatile u8* bp = &bytes[1];
    value = 0x0000007E;
    asm volatile("stbu %1,2(%0)" : "+r"(bp) : "r"(value) : "memory");
    check_eq(bytes[3], 0x7E, "stbu stores updated address");
    check((u32)bp == (u32)&bytes[3], "stbu updates rA");

    volatile u16 halves[8];
    memset((void*)halves, 0, sizeof(halves));

    value = 0xFFFFABCD;
    asm volatile("sth %0,2(%1)" : : "r"(value), "r"(halves) : "memory");
    check_eq(halves[1], 0xABCD, "sth stores low halfword");

    volatile u16* hp = &halves[1];
    value = 0x00001234;
    asm volatile("sthu %1,4(%0)" : "+r"(hp) : "r"(value) : "memory");
    check_eq(halves[3], 0x1234, "sthu stores updated address");
    check((u32)hp == (u32)&halves[3], "sthu updates rA");
}

static void test_branches_and_spr(void) {
    kprintf("--- branches / SPR ---\n");
    printf("--- branches / SPR ---\n");

    u32 flag = 0;
    asm volatile(
        "b 1f\n"
        "li %0,1\n"
        "1:\n"
        : "+r"(flag)
    );
    check_eq(flag, 0, "b skips instruction");

    u32 saved_lr;
    u32 new_lr;
    asm volatile(
        "mflr %0\n"
        "bl 1f\n"
        "1:\n"
        "mflr %1\n"
        "mtlr %0\n"
        : "=&r"(saved_lr), "=r"(new_lr)
        :
        : "lr"
    );
    check(new_lr != 0, "bl sets LR nonzero");
    check(new_lr != saved_lr, "bl changes LR");

    check_eq(dolphin_test_blr_fn(), 0x55, "bclr/blr returns from asm function");
    check_eq(dolphin_test_bctr_fn(), 0x66, "bcctr/bctr jumps through CTR");

    u32 ctr_value = 5;
    u32 ctr_readback;
    asm volatile(
        "mtctr %1\n"
        "mfctr %0\n"
        : "=r"(ctr_readback)
        : "r"(ctr_value)
        : "ctr"
    );
    check_eq(ctr_readback, 5, "mtspr/mfspr CTR roundtrip");

    u32 lr_readback;
    asm volatile(
        "mflr %0\n"
        "mtlr %0\n"
        "mflr %1\n"
        : "=&r"(saved_lr), "=r"(lr_readback)
        :
        : "lr"
    );
    check_eq(lr_readback, saved_lr, "mtspr/mfspr LR roundtrip");
}

int main(void) {
    VIDEO_Init();

    GXRModeObj* rmode = VIDEO_GetPreferredMode(NULL);
    void* xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    CON_Init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
             rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    kprintf("\n=== DolRecomp opcode test (real PPC) ===\n\n");
    printf("\n=== DolRecomp opcode test (real PPC) ===\n\n");

    test_immediate_arithmetic();
    test_compare_and_bc();
    test_immediate_logical();
    test_loads();
    test_stores();
    test_branches_and_spr();

    kprintf("\n%d passed, %d failed\n", pass_count, fail_count);
    printf("\n%d passed, %d failed\n", pass_count, fail_count);

    if (fail_count == 0) {
        kprintf("ALL TESTS PASSED\n");
        printf("\nALL TESTS PASSED\n");
    } else {
        kprintf("SOME TESTS FAILED\n");
        printf("\nSOME TESTS FAILED\n");
    }

    while (1) {
        VIDEO_WaitVSync();
    }

    return 0;
}

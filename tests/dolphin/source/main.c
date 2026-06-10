#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

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
        kprintf("  FAIL: %s — got 0x%08X, want 0x%08X\n", name, got, want);
        printf("  FAIL: %s — got 0x%08X, want 0x%08X\n", name, got, want);
    }
}

static void test_addi(void) {
    kprintf("--- addi ---\n");
    printf("--- addi ---\n");
    u32 result;

    asm volatile("li %0, 42" : "=r"(result));
    check_eq(result, 42, "li r, 42");

    asm volatile("li %0, -1" : "=r"(result));
    check_eq(result, 0xFFFFFFFF, "li r, -1");

    u32 base = 100;
    asm volatile("addi %0, %1, 16" : "=r"(result) : "r"(base));
    check_eq(result, 116, "addi 100 + 16");

    base = 100;
    asm volatile("addi %0, %1, -10" : "=r"(result) : "r"(base));
    check_eq(result, 90, "addi 100 + (-10)");

    base = 0xFFFFFFFF;
    asm volatile("addi %0, %1, 1" : "=r"(result) : "r"(base));
    check_eq(result, 0, "addi wrap 0xFFFFFFFF + 1");

    base = 0;
    asm volatile("addi %0, %1, -1" : "=r"(result) : "r"(base));
    check_eq(result, 0xFFFFFFFF, "addi wrap 0 + (-1)");
}

static void test_ori(void) {
    kprintf("--- ori ---\n");
    printf("--- ori ---\n");
    u32 result;

    u32 src = 0x12340000;
    asm volatile("ori %0, %1, 0x5678" : "=r"(result) : "r"(src));
    check_eq(result, 0x12345678, "ori 0x12340000 | 0x5678");

    src = 0;
    asm volatile("ori %0, %1, 0" : "=r"(result) : "r"(src));
    check_eq(result, 0, "ori 0 | 0");

    src = 0xFFFF0000;
    asm volatile("ori %0, %1, 0xFFFF" : "=r"(result) : "r"(src));
    check_eq(result, 0xFFFFFFFF, "ori 0xFFFF0000 | 0xFFFF");

    src = 0xAAAAAAAA;
    asm volatile("ori %0, %1, 0x5555" : "=r"(result) : "r"(src));
    check_eq(result, 0xAAAAFFFF, "ori 0xAAAAAAAA | 0x5555");
}

static void test_lwz_stw(void) {
    kprintf("--- lwz / stw ---\n");
    printf("--- lwz / stw ---\n");
    u32 result;

    volatile u32 buf[4];
    buf[0] = 0xDEADBEEF;
    buf[1] = 0xCAFEBABE;
    buf[2] = 0x12345678;
    buf[3] = 0;

    asm volatile("lwz %0, 0(%1)" : "=r"(result) : "r"(buf));
    check_eq(result, 0xDEADBEEF, "lwz offset 0");

    asm volatile("lwz %0, 4(%1)" : "=r"(result) : "r"(buf));
    check_eq(result, 0xCAFEBABE, "lwz offset 4");

    asm volatile("lwz %0, 8(%1)" : "=r"(result) : "r"(buf));
    check_eq(result, 0x12345678, "lwz offset 8");

    volatile u32* mid = &buf[2];
    asm volatile("lwz %0, -4(%1)" : "=r"(result) : "r"(mid));
    check_eq(result, 0xCAFEBABE, "lwz negative offset -4");

    u32 store_val = 0xABCD1234;
    asm volatile("stw %0, 12(%1)" : : "r"(store_val), "r"(buf) : "memory");
    check_eq(buf[3], 0xABCD1234, "stw offset 12 readback");

    store_val = 0x99887766;
    volatile u32* end = &buf[3];
    asm volatile("stw %0, -4(%1)" : : "r"(store_val), "r"(end) : "memory");
    check_eq(buf[2], 0x99887766, "stw negative offset -4 readback");

    store_val = 0x11111111;
    asm volatile("stw %0, 0(%1)" : : "r"(store_val), "r"(buf) : "memory");
    check_eq(buf[0], 0x11111111, "stw then lwz roundtrip");
    asm volatile("lwz %0, 0(%1)" : "=r"(result) : "r"(buf));
    check_eq(result, 0x11111111, "lwz after stw roundtrip");
}

static void test_branch(void) {
    kprintf("--- branch ---\n");
    printf("--- branch ---\n");

    // bl should set LR
    u32 saved_lr, new_lr;
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
    check(new_lr != 0, "bl sets LR non-zero");
    check(new_lr != saved_lr, "bl changes LR");

    // b should skip code
    volatile u32 flag = 0;
    asm volatile(
        "b 1f\n"
        "li %0, 1\n"
        "1:\n"
        : "=r"(flag)
    );
    check(flag == 0, "b skips instruction");

    // bl forward should also skip + set LR
    u32 lr_before, lr_after;
    asm volatile(
        "mflr %0\n"
        "bl 1f\n"
        "b 2f\n"
        "1:\n"
        "mflr %1\n"
        "mtlr %0\n"
        "b 3f\n"
        "2:\n"
        "li %1, 0\n"
        "3:\n"
        : "=&r"(lr_before), "=r"(lr_after)
        :
        : "lr"
    );
    check(lr_after != 0, "bl forward sets LR");
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

    test_addi();
    test_ori();
    test_lwz_stw();
    test_branch();

    kprintf("\n%d passed, %d failed\n", pass_count, fail_count);
    printf("\n%d passed, %d failed\n", pass_count, fail_count);

    if (fail_count == 0) {
        kprintf("ALL TESTS PASSED\n");
        printf("\nALL TESTS PASSED\n");
    } else {
        kprintf("SOME TESTS FAILED\n");
        printf("\nSOME TESTS FAILED\n");
    }

    while(1) { VIDEO_WaitVSync(); }
    return 0;
}

#ifndef DOLRECOMP_TYPES_H
#define DOLRECOMP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

// sign-extend from N bits to s32
static inline s32 sign_extend(u32 value, int bits) {
    u32 mask = 1u << (bits - 1);
    return (s32)((value ^ mask) - mask);
}

// byte swap
static inline u16 bswap16(u16 v) {
    return (u16)((v >> 8) | (v << 8));
}

static inline u32 bswap32(u32 v) {
    return ((v >> 24) & 0x000000FF) |
           ((v >>  8) & 0x0000FF00) |
           ((v <<  8) & 0x00FF0000) |
           ((v << 24) & 0xFF000000);
}

// big-endian read/write
static inline u16 read_be16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

static inline u32 read_be32(const u8* p) {
    return (u32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static inline u64 read_be64(const u8* p) {
    return ((u64)read_be32(p) << 32) | read_be32(p + 4);
}

static inline void write_be16(u8* p, u16 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)(v);
}

static inline void write_be32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)(v);
}

static inline void write_be64(u8* p, u64 v) {
    write_be32(p, (u32)(v >> 32));
    write_be32(p + 4, (u32)v);
}

#endif /* DOLRECOMP_TYPES_H */

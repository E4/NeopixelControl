#include <stdint.h>

static inline uint32_t pack(uint32_t r, uint32_t g, uint32_t b) { return (r << 16) | (g << 8) | b; }

static inline void unpack(uint32_t c, uint32_t *r, uint32_t *g, uint32_t *b) { *r = (c >> 16) & 0xFFu; *g = (c >> 8) & 0xFFu; *b = c & 0xFFu; }

/* Helpers for common math */
static inline uint32_t mul8(uint32_t a, uint32_t b) { return (a * b + 127u) / 255u; } // round
static inline uint32_t sat_add8(uint32_t a, uint32_t b) { a += b; return a > 255u ? 255u : a; }
static inline uint32_t sat_sub8(uint32_t a, uint32_t b) { return (a > b) ? (a - b) : 0u; }

/* A = base, B = blend. Some modes are ORDERED: rgb_mode(A,B) != rgb_mode(B,A). */


static inline uint32_t rgb_add(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(sat_add8(ar,br), sat_add8(ag,bg), sat_add8(ab,bb));
}

/* 1) Subtract (clamped at 0): R = max(A - B, 0) */
static inline uint32_t rgb_subtract(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(sat_sub8(ar,br), sat_sub8(ag,bg), sat_sub8(ab,bb));
}

/* 2) Difference: R = |A - B| (order-independent) */
static inline uint32_t rgb_difference(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    uint32_t r = (ar>br)? (ar-br):(br-ar);
    uint32_t g = (ag>bg)? (ag-bg):(bg-ag);
    uint32_t b = (ab>bb)? (ab-bb):(bb-ab);
    return pack(r,g,b);
}

/* 3) Multiply: R = A * B / 255 (darkens) */
static inline uint32_t rgb_multiply(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(mul8(ar,br), mul8(ag,bg), mul8(ab,bb));
}

/* 4) Screen: R = 255 - ((255-A)*(255-B))/255 (lightens) */
static inline uint32_t rgb_screen(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    uint32_t r = 255u - mul8(255u - ar, 255u - br);
    uint32_t g = 255u - mul8(255u - ag, 255u - bg);
    uint32_t b = 255u - mul8(255u - ab, 255u - bb);
    return pack(r,g,b);
}

/* 5) Overlay: if A<128: 2*A*B/255 else: 255 - 2*(255-A)*(255-B)/255 */
static inline uint32_t overlay_ch(uint32_t A, uint32_t B) {
    return (A < 128u) ? (2u * mul8(A,B)) : (255u - 2u * mul8(255u - A, 255u - B));
}
static inline uint32_t rgb_overlay(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(overlay_ch(ar,br), overlay_ch(ag,bg), overlay_ch(ab,bb));
}

/* 6) Color Dodge (brighten A by B): R = A==255 ? 255 : min(255, (B*255)/(255-A)) */
static inline uint32_t dodge_ch(uint32_t A, uint32_t B) {
    if (A >= 255u) return 255u;
    uint32_t denom = 255u - A;                // in [1..255]
    uint32_t val = (B * 255u + (denom - 1u)) / denom; // ceil division-ish
    return (val > 255u) ? 255u : val;
}
static inline uint32_t rgb_color_dodge(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(dodge_ch(ar,br), dodge_ch(ag,bg), dodge_ch(ab,bb));
}

/* 7) Color Burn (darken A by B): R = A==0 ? 0 : max(0, 255 - ((255-B)*255)/A) */
static inline uint32_t burn_ch(uint32_t A, uint32_t B) {
    if (A == 0u) return 0u;
    uint32_t t = ((255u - B) * 255u) / A;
    return (t >= 255u) ? 0u : (255u - t);
}
static inline uint32_t rgb_color_burn(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(burn_ch(ar,br), burn_ch(ag,bg), burn_ch(ab,bb));
}

/* 8) Average: (A + B)/2 */
static inline uint32_t rgb_average(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack((ar + br) >> 1, (ag + bg) >> 1, (ab + bb) >> 1);
}

/* 9) Lighten / Darken (per-channel max/min) */
static inline uint32_t rgb_lighten(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(ar>br?ar:br, ag>bg?ag:bg, ab>bb?ab:bb);
}
static inline uint32_t rgb_darken(uint32_t A, uint32_t B) {
    uint32_t ar,ag,ab, br,bg,bb; unpack(A,&ar,&ag,&ab); unpack(B,&br,&bg,&bb);
    return pack(ar<br?ar:br, ag<bg?ag:bg, ab<bb?ab:bb);
}

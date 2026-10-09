/* x87 and SSE for src/interp/x86_interp.c (included there: it shares Cpu,
 * the ModRM decoder and the memory helpers).
 *
 * Same representation as the converter: x87 registers are doubles on
 * g_fp_stack with g_fp_top, compare results in g_fp_cmp/g_fp_cc, rounding by
 * g_fp_control_word through recomp_fist/recomp_frndint; xmm registers are
 * g_xmm0..7. fxsave/fnsave images use the converter's private layout
 * (control word at +0, top at +8, doubles at +32 + 8*i, xmm at +160), so a
 * save made by one side restores on the other. */

static uint32_t s_mxcsr = 0x1F80;

static inline double *st(int i) { return &g_fp_stack[(g_fp_top + i) & 7]; }
static inline void fpush(double v) { g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = v; }
static inline void fpop(void) { g_fp_top = (g_fp_top + 1) & 7; }

static double rd_f80(uint32_t a)
{
    uint64_t mant = ((uint64_t)rd32(a + 4) << 32) | rd32(a);
    uint16_t se = rd16(a + 8);
    int exp = se & 0x7FFF, neg = (se >> 15) & 1;
    double v;
    if (!exp && !mant) return neg ? -0.0 : 0.0;
    if (exp == 0x7FFF) return (mant << 1) ? NAN : (neg ? -INFINITY : INFINITY);
    v = ldexp((double)mant, exp - 16383 - 63);
    return neg ? -v : v;
}

static void wr_f80(uint32_t a, double v)
{
    uint64_t mant = 0;
    int exp = 0, neg = signbit(v) != 0, e;
    double m;
    if (isnan(v)) { exp = 0x7FFF; mant = 0xC000000000000000ull; }
    else if (isinf(v)) { exp = 0x7FFF; mant = 0x8000000000000000ull; }
    else if (v != 0.0) {
        m = frexp(fabs(v), &e);                   /* v = m * 2^e, m in [0.5, 1) */
        mant = (uint64_t)ldexp(m, 64);
        exp = e - 1 + 16383;
    }
    wr32(a, (uint32_t)mant); wr32(a + 4, (uint32_t)(mant >> 32));
    wr16(a + 8, (uint16_t)(exp | (neg << 15)));
}

static void fcmp_set(double a, double b)
{
    g_fp_cmp = RECOMP_FCMP(a, b);
    g_fp_cc = RECOMP_FCMP_CC(g_fp_cmp);
}

static void fcomi_set(Cpu *c, double a, double b)
{
    int r = RECOMP_FCMP(a, b);
    c->zf = r == 0 || r == 2; c->pf = r == 2; c->cf = r < 0 || r == 2;
    c->of = c->sf = c->af = 0;
}

static double farith(int k, double a, double b)     /* k: 0 add 1 mul 4 sub 5 subr 6 div 7 divr */
{
    switch (k) {
    case 0: return a + b;
    case 1: return a * b;
    case 4: return a - b;
    case 5: return b - a;
    case 6: return a / b;
    default: return b / a;
    }
}

static void fsave_image(uint32_t a, int with_xmm)
{
    union { double d; uint32_t u[2]; } t;
    wr16(a, g_fp_control_word);
    wr32(a + 8, (uint32_t)g_fp_top);
    for (int k = 0; k < 8; k++) { t.d = g_fp_stack[k]; wr32(a + 32 + 8 * k, t.u[0]); wr32(a + 36 + 8 * k, t.u[1]); }
    if (with_xmm) {
        RecompXmm *x[8] = { &g_xmm0, &g_xmm1, &g_xmm2, &g_xmm3, &g_xmm4, &g_xmm5, &g_xmm6, &g_xmm7 };
        for (int k = 0; k < 8; k++) XMM_STORE(a + 160 + 16 * k, *x[k]);
        wr32(a + 24, s_mxcsr);
    }
}

static void frestore_image(uint32_t a, int with_xmm)
{
    union { double d; uint32_t u[2]; } t;
    g_fp_control_word = rd16(a);
    g_fp_top = (int)(rd32(a + 8) & 7);
    for (int k = 0; k < 8; k++) { t.u[0] = rd32(a + 32 + 8 * k); t.u[1] = rd32(a + 36 + 8 * k); g_fp_stack[k] = t.d; }
    if (with_xmm) {
        RecompXmm *x[8] = { &g_xmm0, &g_xmm1, &g_xmm2, &g_xmm3, &g_xmm4, &g_xmm5, &g_xmm6, &g_xmm7 };
        for (int k = 0; k < 8; k++) *x[k] = XMM_MEM(a + 160 + 16 * k);
    }
}

int interp_x87(Cpu *c, uint8_t op)
{
    modrm(c);
    int r = c->reg, i = c->rm;
    if (c->mod != 3) {
        uint32_t a = c->ea;
        switch (op) {
        case 0xD8: case 0xDC: case 0xDA: case 0xDE: {
            double v = op == 0xD8 ? (double)MEMF(a) : op == 0xDC ? MEMD(a)
                     : op == 0xDA ? (double)(int32_t)rd32(a) : (double)(int16_t)rd16(a);
            if (r == 2 || r == 3) { fcmp_set(*st(0), v); if (r == 3) fpop(); }
            else *st(0) = farith(r, *st(0), v);
            return 1;
        }
        case 0xD9:
            switch (r) {
            case 0: fpush((double)MEMF(a)); return 1;
            case 2: MEMF(a) = (float)*st(0); return 1;
            case 3: MEMF(a) = (float)*st(0); fpop(); return 1;
            case 4: g_fp_control_word = rd16(a); g_fp_top = (int)(rd32(a + 8) & 7); return 1;   /* fldenv */
            case 5: g_fp_control_word = rd16(a); return 1;
            case 6: wr16(a, g_fp_control_word); wr32(a + 8, (uint32_t)g_fp_top); return 1;       /* fnstenv */
            case 7: wr16(a, g_fp_control_word); return 1;
            }
            return 0;
        case 0xDB:
            switch (r) {
            case 0: fpush((double)(int32_t)rd32(a)); return 1;
            case 1: wr32(a, (uint32_t)(int32_t)recomp_fist(*st(0), (uint16_t)(g_fp_control_word | 0x0C00), 32)); fpop(); return 1;
            case 2: wr32(a, (uint32_t)(int32_t)recomp_fist(*st(0), g_fp_control_word, 32)); return 1;
            case 3: wr32(a, (uint32_t)(int32_t)recomp_fist(*st(0), g_fp_control_word, 32)); fpop(); return 1;
            case 5: fpush(rd_f80(a)); return 1;
            case 7: wr_f80(a, *st(0)); fpop(); return 1;
            }
            return 0;
        case 0xDD:
            switch (r) {
            case 0: fpush(MEMD(a)); return 1;
            case 1: { int64_t v = recomp_fist(*st(0), (uint16_t)(g_fp_control_word | 0x0C00), 64); wr32(a, (uint32_t)v); wr32(a + 4, (uint32_t)((uint64_t)v >> 32)); fpop(); return 1; }
            case 2: MEMD(a) = *st(0); return 1;
            case 3: MEMD(a) = *st(0); fpop(); return 1;
            case 4: frestore_image(a, 0); return 1;                      /* frstor */
            case 6: fsave_image(a, 0); g_fp_top = 0; g_fp_control_word = 0x037F; return 1;   /* fnsave */
            case 7: wr16(a, (uint16_t)(((g_fp_top & 7) << 11) | g_fp_cc)); return 1;
            }
            return 0;
        case 0xDF:
            switch (r) {
            case 0: fpush((double)(int16_t)rd16(a)); return 1;
            case 1: wr16(a, (uint16_t)recomp_fist(*st(0), (uint16_t)(g_fp_control_word | 0x0C00), 16)); fpop(); return 1;
            case 2: wr16(a, (uint16_t)recomp_fist(*st(0), g_fp_control_word, 16)); return 1;
            case 3: wr16(a, (uint16_t)recomp_fist(*st(0), g_fp_control_word, 16)); fpop(); return 1;
            case 5: fpush((double)(int64_t)(((uint64_t)rd32(a + 4) << 32) | rd32(a))); return 1;
            case 7: { int64_t v = recomp_fist(*st(0), g_fp_control_word, 64); wr32(a, (uint32_t)v); wr32(a + 4, (uint32_t)((uint64_t)v >> 32)); fpop(); return 1; }
            }
            return 0;
        }
        return 0;
    }
    /* register forms */
    switch (op) {
    case 0xD8:
        if (r == 2 || r == 3) { fcmp_set(*st(0), *st(i)); if (r == 3) fpop(); }
        else *st(0) = farith(r, *st(0), *st(i));
        return 1;
    case 0xDC:                                    /* st(i) = st(i) op st(0) */
        switch (r) {
        case 0: *st(i) += *st(0); return 1;
        case 1: *st(i) *= *st(0); return 1;
        case 2: fcmp_set(*st(0), *st(i)); return 1;
        case 3: fcmp_set(*st(0), *st(i)); fpop(); return 1;
        case 4: *st(i) = *st(0) - *st(i); return 1;   /* fsubr st(i),st */
        case 5: *st(i) = *st(i) - *st(0); return 1;   /* fsub  st(i),st */
        case 6: *st(i) = *st(0) / *st(i); return 1;   /* fdivr st(i),st */
        default: *st(i) = *st(i) / *st(0); return 1;  /* fdiv  st(i),st */
        }
    case 0xDE:
        switch (r) {
        case 0: *st(i) += *st(0); fpop(); return 1;
        case 1: *st(i) *= *st(0); fpop(); return 1;
        case 3: if (i == 1) { fcmp_set(*st(0), *st(1)); fpop(); fpop(); return 1; } return 0;
        case 4: *st(i) = *st(0) - *st(i); fpop(); return 1;   /* fsubrp */
        case 5: *st(i) = *st(i) - *st(0); fpop(); return 1;   /* fsubp */
        case 6: *st(i) = *st(0) / *st(i); fpop(); return 1;   /* fdivrp */
        case 7: *st(i) = *st(i) / *st(0); fpop(); return 1;   /* fdivp */
        }
        return 0;
    case 0xD9:
        if (r == 0) { double v = *st(i); fpush(v); return 1; }
        if (r == 1) { double t = *st(0); *st(0) = *st(i); *st(i) = t; return 1; }
        switch (0xC0 | (r << 3) | i) {
        case 0xD0: return 1;                          /* fnop */
        case 0xE0: *st(0) = -*st(0); return 1;
        case 0xE1: *st(0) = fabs(*st(0)); return 1;
        case 0xE4: fcmp_set(*st(0), 0.0); return 1;
        case 0xE5: g_fp_cc = recomp_fxam(*st(0)); return 1;
        case 0xE8: fpush(1.0); return 1;
        case 0xE9: fpush(3.321928094887362347870); return 1;
        case 0xEA: fpush(1.442695040888963407360); return 1;
        case 0xEB: fpush(3.141592653589793238463); return 1;
        case 0xEC: fpush(0.301029995663981195214); return 1;
        case 0xED: fpush(0.693147180559945309417); return 1;
        case 0xEE: fpush(0.0); return 1;
        case 0xF0: *st(0) = pow(2.0, *st(0)) - 1.0; return 1;
        case 0xF1: *st(1) = *st(1) * log2(*st(0)); fpop(); return 1;
        case 0xF2: *st(0) = tan(*st(0)); fpush(1.0); g_fp_cc &= ~0x400u; return 1;
        case 0xF3: *st(1) = atan2(*st(1), *st(0)); fpop(); return 1;
        case 0xF4: { int e; double m = frexp(*st(0), &e); *st(0) = (double)(e - 1); fpush(m * 2.0); return 1; }
        case 0xF5: *st(0) = remainder(*st(0), *st(1)); g_fp_cc &= ~0x400u; return 1;
        case 0xF6: g_fp_top = (g_fp_top + 7) & 7; return 1;
        case 0xF7: g_fp_top = (g_fp_top + 1) & 7; return 1;
        case 0xF8: *st(0) = fmod(*st(0), *st(1)); g_fp_cc &= ~0x400u; return 1;
        case 0xF9: *st(1) = *st(1) * log2(*st(0) + 1.0); fpop(); return 1;
        case 0xFA: *st(0) = sqrt(*st(0)); return 1;
        case 0xFB: { double v = *st(0); *st(0) = sin(v); fpush(cos(v)); g_fp_cc &= ~0x400u; return 1; }
        case 0xFC: *st(0) = recomp_frndint(*st(0), g_fp_control_word); return 1;
        case 0xFD: *st(0) = ldexp(*st(0), (int)trunc(*st(1))); return 1;
        case 0xFE: *st(0) = sin(*st(0)); g_fp_cc &= ~0x400u; return 1;
        case 0xFF: *st(0) = cos(*st(0)); g_fp_cc &= ~0x400u; return 1;
        }
        return 0;
    case 0xDA:
        if (r == 5 && i == 1) { fcmp_set(*st(0), *st(1)); fpop(); fpop(); return 1; }   /* fucompp */
        if (r <= 3) {
            int take = r == 0 ? c->cf : r == 1 ? c->zf : r == 2 ? (c->cf || c->zf) : c->pf;
            if (take) *st(0) = *st(i);
            return 1;
        }
        return 0;
    case 0xDB:
        if (r <= 3) {
            int take = r == 0 ? !c->cf : r == 1 ? !c->zf : r == 2 ? !(c->cf || c->zf) : !c->pf;
            if (take) *st(0) = *st(i);
            return 1;
        }
        if (r == 4 && i == 2) return 1;               /* fnclex */
        if (r == 4 && i == 3) { g_fp_top = 0; g_fp_control_word = 0x037F; g_fp_cc = 0; return 1; }   /* fninit */
        if (r == 5 || r == 6) { fcomi_set(c, *st(0), *st(i)); return 1; }
        return 0;
    case 0xDD:
        switch (r) {
        case 0: return 1;                             /* ffree */
        case 2: *st(i) = *st(0); return 1;
        case 3: *st(i) = *st(0); fpop(); return 1;
        case 4: fcmp_set(*st(0), *st(i)); return 1;
        case 5: fcmp_set(*st(0), *st(i)); fpop(); return 1;
        }
        return 0;
    case 0xDF:
        if (r == 4 && i == 0) { setreg(c, 2, EAX, (uint32_t)(((g_fp_top & 7) << 11) | g_fp_cc)); return 1; }
        if (r == 5 || r == 6) { fcomi_set(c, *st(0), *st(i)); fpop(); return 1; }
        return 0;
    }
    return 0;
}

/* ---- SSE ---------------------------------------------------------------------- */

static RecompXmm *xr(int i)
{
    switch (i & 7) {
    case 0: return &g_xmm0; case 1: return &g_xmm1; case 2: return &g_xmm2; case 3: return &g_xmm3;
    case 4: return &g_xmm4; case 5: return &g_xmm5; case 6: return &g_xmm6; default: return &g_xmm7;
    }
}

/* The source operand of an SSE instruction: the rm register, or memory. */
static RecompXmm xsrc(Cpu *c, int bytes)
{
    RecompXmm v;
    if (c->mod == 3) return *xr(c->rm);
    memset(&v, 0, sizeof v);
    if (bytes == 4) v.u[0] = rd32(c->ea);
    else if (bytes == 8) { v.u[0] = rd32(c->ea); v.u[1] = rd32(c->ea + 4); }
    else v = XMM_MEM(c->ea);
    return v;
}

static float fminf_x86(float a, float b) { return a < b ? a : b; }
static float fmaxf_x86(float a, float b) { return a > b ? a : b; }

static int32_t cvt_round(double v, int truncate)
{
    double r;
    if (isnan(v) || v >= 2147483648.0 || v < -2147483648.0) return (int32_t)0x80000000;
    if (truncate) return (int32_t)v;
    switch ((s_mxcsr >> 13) & 3) {
    case 1: r = floor(v); break;
    case 2: r = ceil(v); break;
    case 3: r = trunc(v); break;
    default: r = nearbyint(v); break;
    }
    return (int32_t)r;
}

static int cmp_pred(double a, double b, int p)
{
    int un = isnan(a) || isnan(b);
    switch (p & 7) {
    case 0: return !un && a == b;
    case 1: return !un && a < b;
    case 2: return !un && a <= b;
    case 3: return un;
    case 4: return un || a != b;
    case 5: return un || !(a < b);
    case 6: return un || !(a <= b);
    default: return !un;
    }
}

int interp_0f_sse(Cpu *c, uint8_t op2)
{
    int p = c->sse_prefix;           /* 0, 0x66, 0xF3, 0xF2 */
    switch (op2) {
    case 0x10: case 0x11: {
        modrm(c);
        RecompXmm *d = xr(c->reg);
        if (op2 == 0x10) {
            if (p == 0xF3) { if (c->mod == 3) d->u[0] = xr(c->rm)->u[0]; else { d->q[0] = rd32(c->ea); d->q[1] = 0; } }
            else if (p == 0xF2) { if (c->mod == 3) d->q[0] = xr(c->rm)->q[0]; else { d->u[0] = rd32(c->ea); d->u[1] = rd32(c->ea + 4); d->q[1] = 0; } }
            else *d = xsrc(c, 16);
        } else {
            if (p == 0xF3) { if (c->mod == 3) xr(c->rm)->u[0] = d->u[0]; else wr32(c->ea, d->u[0]); }
            else if (p == 0xF2) { if (c->mod == 3) xr(c->rm)->q[0] = d->q[0]; else { wr32(c->ea, d->u[0]); wr32(c->ea + 4, d->u[1]); } }
            else { if (c->mod == 3) *xr(c->rm) = *d; else XMM_STORE(c->ea, *d); }
        }
        return 1;
    }
    case 0x12: case 0x13: case 0x16: case 0x17: {
        modrm(c);
        RecompXmm *d = xr(c->reg);
        int high = op2 >= 0x16;
        if (op2 == 0x12 || op2 == 0x16) {
            if (c->mod == 3) {                    /* movhlps / movlhps */
                if (op2 == 0x12) d->q[0] = xr(c->rm)->q[1]; else d->q[1] = xr(c->rm)->q[0];
            } else {
                uint64_t v = ((uint64_t)rd32(c->ea + 4) << 32) | rd32(c->ea);
                d->q[high] = v;
            }
        } else {
            uint64_t v = d->q[high];
            wr32(c->ea, (uint32_t)v); wr32(c->ea + 4, (uint32_t)(v >> 32));
        }
        return 1;
    }
    case 0x14: case 0x15: {                       /* unpcklps / unpckhps (pd) */
        modrm(c);
        RecompXmm a = *xr(c->reg), b = xsrc(c, 16), r;
        if (p == 0x66) { r.q[0] = op2 == 0x14 ? a.q[0] : a.q[1]; r.q[1] = op2 == 0x14 ? b.q[0] : b.q[1]; }
        else if (op2 == 0x14) { r.u[0] = a.u[0]; r.u[1] = b.u[0]; r.u[2] = a.u[1]; r.u[3] = b.u[1]; }
        else { r.u[0] = a.u[2]; r.u[1] = b.u[2]; r.u[2] = a.u[3]; r.u[3] = b.u[3]; }
        *xr(c->reg) = r;
        return 1;
    }
    case 0x28: modrm(c); *xr(c->reg) = xsrc(c, 16); return 1;
    case 0x29: modrm(c); if (c->mod == 3) *xr(c->rm) = *xr(c->reg); else XMM_STORE(c->ea, *xr(c->reg)); return 1;
    case 0x2A: {
        modrm(c);
        if (p == 0xF3) xr(c->reg)->f[0] = (float)(int32_t)getrm(c, 4);
        else if (p == 0xF2) xr(c->reg)->d[0] = (double)(int32_t)getrm(c, 4);
        else return 0;
        return 1;
    }
    case 0x2C: case 0x2D: {
        modrm(c);
        RecompXmm s = xsrc(c, p == 0xF2 ? 8 : 4);
        double v = p == 0xF2 ? s.d[0] : (double)s.f[0];
        if (p != 0xF3 && p != 0xF2) return 0;
        setreg(c, 4, c->reg, (uint32_t)cvt_round(v, op2 == 0x2C));
        return 1;
    }
    case 0x2E: case 0x2F: {
        modrm(c);
        RecompXmm s = xsrc(c, p == 0x66 ? 8 : 4);
        if (p == 0x66) fcomi_set(c, xr(c->reg)->d[0], s.d[0]);
        else fcomi_set(c, (double)xr(c->reg)->f[0], (double)s.f[0]);
        return 1;
    }
    case 0x50: {
        modrm(c);
        RecompXmm *s = xr(c->rm);
        uint32_t m = 0;
        if (p == 0x66) m = (uint32_t)((s->q[0] >> 63) | ((s->q[1] >> 63) << 1));
        else for (int k = 0; k < 4; k++) m |= (s->u[k] >> 31) << k;
        setreg(c, 4, c->reg, m);
        return 1;
    }
    case 0x51: case 0x52: case 0x53: case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        modrm(c);
        int scalar = p == 0xF3 || p == 0xF2, dbl = p == 0x66 || p == 0xF2;
        RecompXmm a = *xr(c->reg), b = xsrc(c, scalar ? (dbl ? 8 : 4) : 16);
        int n = scalar ? 1 : (dbl ? 2 : 4);
        for (int k = 0; k < n; k++) {
            if (dbl) {
                double x = a.d[k], y = b.d[k], r;
                switch (op2) {
                case 0x51: r = sqrt(y); break;
                case 0x58: r = x + y; break; case 0x59: r = x * y; break;
                case 0x5C: r = x - y; break; case 0x5E: r = x / y; break;
                case 0x5D: r = x < y ? x : y; break; case 0x5F: r = x > y ? x : y; break;
                default: return 0;
                }
                a.d[k] = r;
            } else {
                float x = a.f[k], y = b.f[k], r;
                switch (op2) {
                case 0x51: r = sqrtf(y); break;
                case 0x52: r = 1.0f / sqrtf(y); break;
                case 0x53: r = 1.0f / y; break;
                case 0x58: r = x + y; break; case 0x59: r = x * y; break;
                case 0x5C: r = x - y; break; case 0x5E: r = x / y; break;
                case 0x5D: r = fminf_x86(x, y); break; case 0x5F: r = fmaxf_x86(x, y); break;
                default: return 0;
                }
                a.f[k] = r;
            }
        }
        *xr(c->reg) = a;
        return 1;
    }
    case 0x54: case 0x55: case 0x56: case 0x57: {
        modrm(c);
        RecompXmm a = *xr(c->reg), b = xsrc(c, 16);
        for (int k = 0; k < 4; k++)
            a.u[k] = op2 == 0x54 ? (a.u[k] & b.u[k]) : op2 == 0x55 ? (~a.u[k] & b.u[k]) : op2 == 0x56 ? (a.u[k] | b.u[k]) : (a.u[k] ^ b.u[k]);
        *xr(c->reg) = a;
        return 1;
    }
    case 0x5A: {
        modrm(c);
        RecompXmm *d = xr(c->reg);
        if (p == 0xF3) { RecompXmm s = xsrc(c, 4); d->d[0] = (double)s.f[0]; }
        else if (p == 0xF2) { RecompXmm s = xsrc(c, 8); d->f[0] = (float)s.d[0]; }
        else if (p == 0x66) { RecompXmm s = xsrc(c, 16); RecompXmm r; memset(&r, 0, sizeof r); r.f[0] = (float)s.d[0]; r.f[1] = (float)s.d[1]; *d = r; }
        else { RecompXmm s = xsrc(c, 8); RecompXmm r; r.d[0] = s.f[0]; r.d[1] = s.f[1]; *d = r; }
        return 1;
    }
    case 0x5B: {
        modrm(c);
        RecompXmm s = xsrc(c, 16), r;
        for (int k = 0; k < 4; k++) {
            if (p == 0) r.f[k] = (float)s.i[k];
            else r.i[k] = cvt_round((double)s.f[k], p == 0xF3);
        }
        *xr(c->reg) = r;
        return 1;
    }
    case 0xC2: {
        modrm(c);
        int scalar = p == 0xF3 || p == 0xF2, dbl = p == 0x66 || p == 0xF2;
        RecompXmm a = *xr(c->reg), b = xsrc(c, scalar ? (dbl ? 8 : 4) : 16);
        int pred = fetch8(c);
        int n = scalar ? 1 : (dbl ? 2 : 4);
        for (int k = 0; k < n; k++) {
            if (dbl) a.q[k] = cmp_pred(a.d[k], b.d[k], pred) ? ~0ull : 0;
            else a.u[k] = cmp_pred(a.f[k], b.f[k], pred) ? ~0u : 0;
        }
        *xr(c->reg) = a;
        return 1;
    }
    case 0xC6: {
        modrm(c);
        RecompXmm a = *xr(c->reg), b = xsrc(c, 16), r;
        int imm = fetch8(c);
        if (p == 0x66) { r.q[0] = a.q[imm & 1]; r.q[1] = b.q[(imm >> 1) & 1]; }
        else { r.u[0] = a.u[imm & 3]; r.u[1] = a.u[(imm >> 2) & 3]; r.u[2] = b.u[(imm >> 4) & 3]; r.u[3] = b.u[(imm >> 6) & 3]; }
        *xr(c->reg) = r;
        return 1;
    }
    case 0xAE: {
        modrm(c);
        if (c->mod == 3) return 1;                /* lfence / mfence / sfence */
        switch (c->reg) {
        case 0: fsave_image(c->ea, 1); return 1;
        case 1: frestore_image(c->ea, 1); return 1;
        case 2: s_mxcsr = rd32(c->ea); return 1;
        case 3: wr32(c->ea, s_mxcsr); return 1;
        case 7: return 1;                         /* clflush */
        }
        return 0;
    }
    /* SSE2 integer forms used by compiled code (66 prefix: xmm registers) */
    case 0x6E: modrm(c); if (p != 0x66) return 0; { RecompXmm v; memset(&v, 0, sizeof v); v.u[0] = getrm(c, 4); *xr(c->reg) = v; } return 1;
    case 0x7E:
        modrm(c);
        if (p == 0x66) { setrm(c, 4, xr(c->reg)->u[0]); return 1; }
        if (p == 0xF3) { RecompXmm v = xsrc(c, 8); v.q[1] = 0; *xr(c->reg) = v; return 1; }
        return 0;
    case 0xD6: modrm(c); if (p != 0x66) return 0;
        if (c->mod == 3) { xr(c->rm)->q[0] = xr(c->reg)->q[0]; xr(c->rm)->q[1] = 0; }
        else { wr32(c->ea, xr(c->reg)->u[0]); wr32(c->ea + 4, xr(c->reg)->u[1]); }
        return 1;
    case 0x6F: modrm(c); if (p != 0x66 && p != 0xF3) return 0; *xr(c->reg) = xsrc(c, 16); return 1;
    case 0x7F: modrm(c); if (p != 0x66 && p != 0xF3) return 0;
        if (c->mod == 3) *xr(c->rm) = *xr(c->reg); else XMM_STORE(c->ea, *xr(c->reg));
        return 1;
    case 0xEF: case 0xDB: case 0xEB: case 0xDF: case 0xFE: case 0xFA: case 0xD4: case 0xFB:
    case 0x62: case 0x6C: case 0x66: case 0x76: {
        modrm(c);
        if (p != 0x66) return 0;
        RecompXmm a = *xr(c->reg), b = xsrc(c, 16), r = a;
        switch (op2) {
        case 0xEF: for (int k = 0; k < 4; k++) r.u[k] = a.u[k] ^ b.u[k]; break;
        case 0xDB: for (int k = 0; k < 4; k++) r.u[k] = a.u[k] & b.u[k]; break;
        case 0xEB: for (int k = 0; k < 4; k++) r.u[k] = a.u[k] | b.u[k]; break;
        case 0xDF: for (int k = 0; k < 4; k++) r.u[k] = ~a.u[k] & b.u[k]; break;
        case 0xFE: for (int k = 0; k < 4; k++) r.u[k] = a.u[k] + b.u[k]; break;
        case 0xFA: for (int k = 0; k < 4; k++) r.u[k] = a.u[k] - b.u[k]; break;
        case 0xD4: r.q[0] = a.q[0] + b.q[0]; r.q[1] = a.q[1] + b.q[1]; break;
        case 0xFB: r.q[0] = a.q[0] - b.q[0]; r.q[1] = a.q[1] - b.q[1]; break;
        case 0x62: r.u[0] = a.u[0]; r.u[1] = b.u[0]; r.u[2] = a.u[1]; r.u[3] = b.u[1]; break;
        case 0x6C: r.q[0] = a.q[0]; r.q[1] = b.q[0]; break;
        case 0x66: for (int k = 0; k < 4; k++) r.u[k] = a.i[k] > b.i[k] ? ~0u : 0; break;
        case 0x76: for (int k = 0; k < 4; k++) r.u[k] = a.u[k] == b.u[k] ? ~0u : 0; break;
        }
        *xr(c->reg) = r;
        return 1;
    }
    case 0x70: {
        modrm(c);
        RecompXmm s = xsrc(c, 16), r = s;
        int imm = fetch8(c);
        if (p == 0x66) for (int k = 0; k < 4; k++) r.u[k] = s.u[(imm >> (2 * k)) & 3];
        else return 0;
        *xr(c->reg) = r;
        return 1;
    }
    case 0x72: case 0x73: {                       /* shifts by immediate on xmm */
        modrm(c);
        if (p != 0x66) return 0;
        RecompXmm *d = xr(c->rm);
        int n = fetch8(c);
        if (op2 == 0x72) {
            for (int k = 0; k < 4; k++) {
                if (c->reg == 2) d->u[k] = n > 31 ? 0 : d->u[k] >> n;
                else if (c->reg == 4) d->i[k] = d->i[k] >> (n > 31 ? 31 : n);
                else if (c->reg == 6) d->u[k] = n > 31 ? 0 : d->u[k] << n;
                else return 0;
            }
        } else {
            if (c->reg == 2) { for (int k = 0; k < 2; k++) d->q[k] = n > 63 ? 0 : d->q[k] >> n; }
            else if (c->reg == 6) { for (int k = 0; k < 2; k++) d->q[k] = n > 63 ? 0 : d->q[k] << n; }
            else return 0;
        }
        return 1;
    }
    }
    return 0;
}

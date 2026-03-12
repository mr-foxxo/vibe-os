static unsigned long long vibe_lua_udivmod(unsigned long long dividend,
                                           unsigned long long divisor,
                                           unsigned long long *remainder) {
    unsigned long long quotient = 0u;
    unsigned long long rem = 0u;

    if (divisor == 0u) {
        if (remainder) {
            *remainder = 0u;
        }
        return 0u;
    }

    for (int i = 63; i >= 0; --i) {
        rem = (rem << 1) | ((dividend >> i) & 1u);
        if (rem >= divisor) {
            rem -= divisor;
            quotient |= 1uLL << i;
        }
    }

    if (remainder) {
        *remainder = rem;
    }
    return quotient;
}

static unsigned long long vibe_lua_uabs(long long value) {
    if (value < 0) {
        return (unsigned long long)(-(value + 1)) + 1u;
    }
    return (unsigned long long)value;
}

unsigned long long __udivdi3(unsigned long long dividend, unsigned long long divisor) {
    return vibe_lua_udivmod(dividend, divisor, 0);
}

unsigned long long __umoddi3(unsigned long long dividend, unsigned long long divisor) {
    unsigned long long remainder = 0u;

    (void)vibe_lua_udivmod(dividend, divisor, &remainder);
    return remainder;
}

unsigned long long __udivmoddi4(unsigned long long dividend,
                                unsigned long long divisor,
                                unsigned long long *remainder) {
    return vibe_lua_udivmod(dividend, divisor, remainder);
}

long long __divdi3(long long dividend, long long divisor) {
    unsigned long long quotient;
    int negative = ((dividend < 0) != (divisor < 0));

    quotient = vibe_lua_udivmod(vibe_lua_uabs(dividend), vibe_lua_uabs(divisor), 0);
    return negative ? -(long long)quotient : (long long)quotient;
}

long long __moddi3(long long dividend, long long divisor) {
    unsigned long long remainder = 0u;

    (void)vibe_lua_udivmod(vibe_lua_uabs(dividend), vibe_lua_uabs(divisor), &remainder);
    return dividend < 0 ? -(long long)remainder : (long long)remainder;
}

long long __divmoddi4(long long dividend, long long divisor, long long *remainder) {
    unsigned long long urem = 0u;
    unsigned long long quotient;
    int negative_q = ((dividend < 0) != (divisor < 0));

    quotient = vibe_lua_udivmod(vibe_lua_uabs(dividend), vibe_lua_uabs(divisor), &urem);
    if (remainder) {
        *remainder = dividend < 0 ? -(long long)urem : (long long)urem;
    }
    return negative_q ? -(long long)quotient : (long long)quotient;
}

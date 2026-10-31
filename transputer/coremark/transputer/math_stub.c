/*
 * Minimal modf for bare-metal CoreMark.
 * Uses only int (32-bit signed) — no long long, no unsigned float conversion.
 */

double modf(double value, double *iptr)
{
    int int_part;

    /* Large values have no fractional part */
    if (value >= 2147483647.0 || value <= -2147483648.0) {
        *iptr = value;
        return 0.0;
    }

    /* Truncate towards zero */
    int_part = (int)value;
    *iptr = (double)int_part;
    return value - (double)int_part;
}

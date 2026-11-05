/* Host test: cc test_prefix_decode.c -o test_prefix_decode && ./test_prefix_decode */
#include "../../../transputer-enc.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    static const struct {
        uint8_t bytes[4];
        int len, op, operand;
    } cases[] = {
        { { 0x4f },                   1, TP_ENC_LDC, 15 },
        { { 0x60, 0x0f },             2, TP_ENC_J,   -1 },
        { { 0x61, 0x2e, 0x4f },       3, TP_ENC_LDC, -273 },
        { { 0x21, 0x27, 0xfc },       3, TP_ENC_OPR, 0x17c },
        { { 0x61, 0xf0 },             2, TP_ENC_OPR, -32 },
        { { 0x20, 0x20, 0x21, 0xf0 }, 4, TP_ENC_OPR, 16 },
    };
    static const int extremes[] = { INT32_MIN, INT32_MAX };
    uint8_t buf[12];
    size_t i;
    int n, op, operand, end;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        /* Start at a nonzero offset and leave a following instruction. */
        memcpy(buf + 1, cases[i].bytes, cases[i].len);
        buf[1 + cases[i].len] = 0x40;
        n = tp_decode_instr_at(buf, cases[i].len + 2, 1, &op, &operand);
        assert(n == cases[i].len && op == cases[i].op && operand == cases[i].operand);

        /* Cut the same stream at every boundary before its opcode. */
        for (end = 1; end <= cases[i].len; end++) {
            op = operand = 99;
            assert(tp_decode_instr_at(buf, end, 1, &op, &operand) == 0);
            assert(op == 99 && operand == 99);
        }
    }
    for (i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++) {
        n = tp_encode_instr(buf, TP_ENC_LDC, extremes[i]);
        assert(tp_decode_instr_at(buf, n, 0, &op, &operand) == n);
        assert(op == TP_ENC_LDC && operand == extremes[i]);
    }
    return 0;
}

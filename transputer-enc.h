/*
 * transputer-enc.h - Unified Transputer Instruction Encoding
 *
 * This header provides the canonical implementation of Transputer prefix
 * encoding (PFIX/NFIX) used by the compiler, assembler, and linker.
 *
 * Prefix Encoding Rules (TCWG):
 * positive: PFIX ... PFIX OP
 * negative: NFIX PFIX ... PFIX OP (NFIX is the FIRST prefix executed)
 *
 * Decoding:
 * Before each byte, add its low nibble into Oreg.
 * PFIX v: Oreg = (Oreg + v) << 4
 * NFIX v: Oreg = ~(Oreg + v) << 4
 * OP v:   Operand = Oreg + v; Oreg = 0
 */

#ifndef TRANSPUTER_ENC_H
#define TRANSPUTER_ENC_H

#include <stddef.h>
#include <stdint.h>

/* Opcode nibble values */
#define TP_ENC_J      0x0
#define TP_ENC_LDLP   0x1
#define TP_ENC_PFIX   0x2
#define TP_ENC_LDNL   0x3
#define TP_ENC_LDC    0x4
#define TP_ENC_LDNLP  0x5
#define TP_ENC_NFIX   0x6
#define TP_ENC_LDL    0x7
#define TP_ENC_ADC    0x8
#define TP_ENC_CALL   0x9
#define TP_ENC_CJ     0xA
#define TP_ENC_AJW    0xB
#define TP_ENC_EQC    0xC
#define TP_ENC_STL    0xD
#define TP_ENC_STNL   0xE
#define TP_ENC_OPR    0xF

/*
 * tp_encode_instr - Encode a Transputer instruction with proper prefix sequence
 *
 * @buf:     Output buffer (NULL to just calculate length)
 * @opcode:  Opcode nibble (0x0-0xF)
 * @operand: Operand value
 *
 * Returns: Number of bytes written/required
 *
 * Encoding algorithm:
 * - Positive values: extract nibbles directly, use PFIX prefixes
 * - Negative values: final nibble from operand, prefix nibbles encode (operand >> 4)
 *   using NFIX for the first prefix, then PFIX for subsequent prefixes
 *
 * NFIX decoding: oreg = (~oreg << 4) | nibble
 * For first NFIX from oreg=0: oreg = -16 + nibble
 * So to produce target oreg, use nibble = (target & 0xF)
 */
static inline int tp_encode_instr(uint8_t *buf, int opcode, int operand)
{
    int stack[12];  /* Nibble stack (LSB first) */
    int n = 0;
    int pos = 0;
    int val;
    int need_nfix = 0;
    int i;

    if (operand < 0) {
        need_nfix = 1;

        /* TCWG negative operand encoding:
         * - Final nibble comes from the original operand.
         * - NFIX contributes the first negative prefix.
         * - Higher-order negative digits are emitted as PFIX prefixes
         *   between the leading NFIX and the opcode.
         *
         * This follows the classic transputer encoder used by the toolchain
         * loaders/assemblers and matches the emulator's fetch logic:
         *   OReg += nibble;
         *   PFIX => OReg <<= 4;
         *   NFIX => OReg = ~OReg << 4;
         */
        stack[n++] = operand & 0xF;  /* Final nibble from original operand */
        val = ~operand;
        while ((val = (unsigned int)val >> 4) >= 16)
            stack[n++] = ~val & 0xF;
        stack[n++] = val & 0xF;
    } else {
        /* Positive operands: extract nibbles directly */
        val = operand;
        stack[n++] = val & 0xF;
        val >>= 4;

        /* Direct-function operands are unsigned unless introduced by NFIX.
         * A final nibble in the range 8..15 is therefore already a valid
         * positive operand and does not need a leading PFIX 0.  This matches
         * the original INMOS/TTOOLS encoder. */
        while (val > 0) {
            stack[n++] = val & 0xF;
            val >>= 4;
        }
    }

    /* Emit bytes: Prefixes MSB -> LSB, then opcode.
     * For negative numbers, the first prefix executed is NFIX, so when we emit
     * the byte stream from most-significant prefix to least-significant prefix,
     * NFIX corresponds to the last stacked prefix (i == n - 1).
     */
    for (i = n - 1; i >= 1; i--) {
        uint8_t pfx;
        if (need_nfix && i == n - 1) {
            pfx = (TP_ENC_NFIX << 4);
        } else {
            pfx = (TP_ENC_PFIX << 4);
        }

        if (buf)
            buf[pos] = pfx | (stack[i] & 0xF);
        pos++;
    }

    /* Emit final instruction */
    if (buf)
        buf[pos] = (opcode << 4) | (stack[0] & 0xF);
    pos++;

    return pos;
}

/* Helper to get length */
static inline int tp_encode_len(int opcode, int operand)
{
    return tp_encode_instr(NULL, opcode, operand);
}

/* Full-width assembler relocation slot. Left padding keeps NextInst fixed. */
#define TP_ASM_SLOT 9

static inline void tp_encode_asm_slot(uint8_t *buf, int opcode, int32_t operand)
{
    int pad = TP_ASM_SLOT - tp_encode_len(opcode, operand);
    int i;
    for (i = 0; i < pad; i++)
        buf[i] = TP_ENC_PFIX << 4;
    tp_encode_instr(buf + pad, opcode, operand);
}

/* Check if byte is a prefix */
static inline int tp_is_prefix_byte(uint8_t b)
{
    return (b & 0xF0) == (TP_ENC_PFIX << 4) || (b & 0xF0) == (TP_ENC_NFIX << 4);
}

/* Decode one instruction at pos. Return its byte length, or zero if the
 * buffer ends before the opcode. Write the outputs only on success. */
static inline int tp_decode_instr_at(const uint8_t *data, size_t len, size_t pos,
                                     int *opcode, int *operand)
{
    size_t start = pos;
    uint32_t oreg = 0;

    while (pos < len) {
        uint8_t byte = data[pos++];
        int op = byte >> 4;
        oreg |= byte & 15;
        if (op == TP_ENC_PFIX)
            oreg <<= 4;
        else if (op == TP_ENC_NFIX)
            oreg = ~oreg << 4;
        else {
            *opcode = op;
            *operand = (int32_t)oreg;
            return (int)(pos - start);
        }
    }
    return 0;
}

#endif /* TRANSPUTER_ENC_H */

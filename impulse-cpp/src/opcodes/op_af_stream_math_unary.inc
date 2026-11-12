#ifndef OP_AF_CONTRACT_DEFINED
#define OP_AF_CONTRACT_DEFINED
/**
 * @file op_af_stream_math_unary.inc
 * @brief Opcode 0xAF: OP_STREAM_MATH_UNARY — Per-edge unary transcendental/activation math function.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xAF (OP_STREAM_MATH_UNARY)
 *  - Execution Scope: Edge Stream Shader Sub-Opcode (NOP in top-level VM dispatch)
 *  - Stream Layout: sDst (stream reg 0..15), low (stream input reg 0..15), high (math func id)
 *  - Allowed Types: None (Stream sub-opcode)
 *  - Output: None (Top-level VM registers untouched)
 *
 * ARCHITECTURAL NOTE (Edge Stream Shader Sub-Opcode vs. Top-Level VM Opcode):
 * ---------------------------------------------------------------------------
 * Unlike standard top-level ImpulseVM opcodes (which execute at the outer
 * `vm_state->pc` loop over the 64 general-purpose registers `R0..R63`), the
 * Edge Stream Shader sub-opcodes (`0xA1`..`0xBF`, excluding the outer walk
 * initiators `OP_CSR_WALK_STREAM` `0xA9` and `OP_CSC_WALK_STREAM` `0xAA`)
 * are evaluated PER-EDGE inside the fused inner loop of:
 *   - `OP_COO_WALK_STREAM` (`0xA0`)
 *   - `OP_CSR_WALK_STREAM` (`0xA9`)
 *   - `OP_CSC_WALK_STREAM` (`0xAA`)
 *
 * Execution Model:
 *   1. Operates on the 16 per-edge float shader registers `s_regs[0..15]` and
 *      current edge context (`actualSource`, `actualTarget`, `eIdx`, `sInst`,
 *      `sDst`, `sPayloadLow`, `sPayloadHigh`).
 *   2. Included directly into the per-edge shader `switch (op)` in
 *      `opcodes/shared/walk_stream_common.hpp`.
 *   3. When the enclosing `OP_*_WALK_STREAM` instruction finishes iterating
 *      edges, `vm_state->pc` jumps past `OP_STREAM_FUNC_END`. If encountered
 *      standalone in the outer VM dispatch loop, this opcode acts as `OP_NOP`.
 */
#define OP_AF_DEFINED_FLAGS        IMP_FLAG_NONE
#define OP_AF_UNARY_ABS            0x01u
#define OP_AF_UNARY_SQRT           0x02u
#define OP_AF_UNARY_RSQRT          0x03u
#define OP_AF_UNARY_CBRT           0x04u
#define OP_AF_UNARY_EXP            0x08u
#define OP_AF_UNARY_EXP2           0x09u
#define OP_AF_UNARY_EXP10          0x0Au
#define OP_AF_UNARY_EXPM1          0x0Bu
#define OP_AF_UNARY_LOG            0x0Cu
#define OP_AF_UNARY_LOG2           0x0Du
#define OP_AF_UNARY_LOG10          0x0Eu
#define OP_AF_UNARY_LOG1P          0x0Fu
#define OP_AF_UNARY_SIN            0x10u
#define OP_AF_UNARY_COS            0x11u
#define OP_AF_UNARY_TAN            0x12u
#define OP_AF_UNARY_ASIN           0x13u
#define OP_AF_UNARY_ACOS           0x14u
#define OP_AF_UNARY_ATAN           0x15u
#define OP_AF_UNARY_SINC           0x17u
#define OP_AF_UNARY_SINH           0x18u
#define OP_AF_UNARY_COSH           0x19u
#define OP_AF_UNARY_TANH           0x1Au
#define OP_AF_UNARY_FLOOR          0x1Eu
#define OP_AF_UNARY_CEIL           0x1Fu
#define OP_AF_UNARY_ROUND_HALF_UP  0x21u
#define OP_AF_UNARY_RELU           0x25u
#define OP_AF_UNARY_LEAKY_RELU     0x26u
#define OP_AF_UNARY_SIGMOID        0x27u
#define OP_AF_UNARY_GELU           0x28u
#define OP_AF_UNARY_SILU           0x29u
#define OP_AF_UNARY_SOFTPLUS       0x2Au
#define OP_AF_UNARY_ISNAN          0x34u
#define OP_AF_UNARY_ISINF          0x35u
#define OP_AF_UNARY_ISFINITE       0x36u
#define OP_AF_SINC_EPSILON         1e-15f
#define OP_AF_LEAKY_RELU_ALPHA     0.01f
#define OP_AF_GELU_SQRT_2_OVER_PI  0.7978845608028654
#define OP_AF_GELU_CUBIC_COEFF     0.044715
#endif
                    case OP_STREAM_MATH_UNARY: {
                        IMPULSE_ASSERT(sDst < IMP_VM_STREAM_REG_COUNT &&
                                       sPayloadLow < IMP_VM_STREAM_REG_COUNT);
                        float v = s_regs[sPayloadLow];
                        IMPULSE_TESTCASE(v <= 0.0f);
                        IMPULSE_TESTCASE(std::isnan(v));
                        IMPULSE_TESTCASE(sPayloadHigh == OP_AF_UNARY_ISNAN);
                        s_regs[sDst] = impulse_math_unary_f32(static_cast<uint8_t>(sPayloadHigh), v);
                        break;
                    }

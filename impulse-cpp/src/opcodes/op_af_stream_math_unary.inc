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
#endif
                    case OP_STREAM_MATH_UNARY: {
                        float v = s_regs[sPayloadLow];
                        float res = v;
                        switch (sPayloadHigh) {
                            case 0x01: res = std::abs(v); break;
                            case 0x02: res = std::sqrt(v); break;
                            case 0x03: res = 1.0f / std::sqrt(v); break;
                            case 0x04: res = std::copysign(std::pow(std::abs(v), 1.0f / 3.0f), v); break;
                            case 0x08: res = std::exp(v); break;
                            case 0x09: res = std::exp2(v); break;
                            case 0x0A: res = std::pow(10.0f, v); break;
                            case 0x0B: res = std::expm1(v); break;
                            case 0x0C: res = std::log(v); break;
                            case 0x0D: res = std::log2(v); break;
                            case 0x0E: res = std::log10(v); break;
                            case 0x0F: res = std::log1p(v); break;
                            case 0x10: res = std::sin(v); break;
                            case 0x11: res = std::cos(v); break;
                            case 0x12: res = std::tan(v); break;
                            case 0x13: res = std::asin(v); break;
                            case 0x14: res = std::acos(v); break;
                            case 0x15: res = std::atan(v); break;
                            case 0x17: res = (std::abs(v) < 1e-15f) ? 1.0f : (std::sin(v) / v); break;
                            case 0x18: res = std::sinh(v); break;
                            case 0x19: res = std::cosh(v); break;
                            case 0x1A: res = std::tanh(v); break;
                            case 0x1E: res = std::floor(v); break;
                            case 0x1F: res = std::ceil(v); break;
                            case 0x21: res = std::floor(v + 0.5f); break;
                            case 0x25: res = (v > 0.0f) ? v : 0.0f; break;
                            case 0x26: res = (v > 0.0f) ? v : 0.01f * v; break;
                            case 0x27: res = 1.0f / (1.0f + std::exp(-v)); break;
                            case 0x28: res = 0.5f * v * (1.0f + std::tanh(0.7978845608028654 * (v + 0.044715 * v * v * v))); break;
                            case 0x29: res = v / (1.0f + std::exp(-v)); break;
                            case 0x2A: res = std::log(1.0f + std::exp(v)); break;
                            case 0x34: res = std::isnan(v) ? 1.0f : 0.0f; break;
                            case 0x35: res = std::isinf(v) ? 1.0f : 0.0f; break;
                            case 0x36: res = std::isfinite(v) ? 1.0f : 0.0f; break;
                        }
                        s_regs[sDst] = res;
                        break;
                    }

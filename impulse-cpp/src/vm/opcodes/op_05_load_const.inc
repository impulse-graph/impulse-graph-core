// Opcode 0x05: OP_LOAD_CONST
VM_OP_CASE(LOAD_CONST) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint32_t imm32_u = IMP_IMM32(w);
    const int32_t imm32_s = static_cast<int32_t>(imm32_u);
    switch (subop) {
        case 0: { // .i32
            uint64_t u = static_cast<uint64_t>(static_cast<int64_t>(imm32_s));
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), u);
            p1_set_scalar_flags(ws, true, u);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .u32
            uint64_t u = static_cast<uint64_t>(imm32_u);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), u);
            p1_set_scalar_flags(ws, true, u);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .f32 -> f64
            double f = static_cast<double>(p1_bits_to_f32(imm32_u));
            if (std::isnan(f)) {
                ws.write_r_null(dst);
                p1_set_f64_flags(ws, false, 0.0);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), p1_f64_to_bits(f));
                p1_set_f64_flags(ws, true, f);
            }
            ws.state.pc += 1u;
            break;
        }
        case 3: { // .f32_raw
            float f = p1_bits_to_f32(imm32_u);
            if (std::isnan(f)) {
                ws.write_r_null(dst);
                p1_set_scalar_flags(ws, false, 0ULL);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F32_RAW), static_cast<uint64_t>(imm32_u));
                ws.set_zf(f == 0.0f);
                ws.set_nf(false);
            }
            ws.state.pc += 1u;
            break;
        }
        case 4: { // .i64.ext
            if (static_cast<size_t>(ws.state.pc) + 1u >= word_count) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                break;
            }
            uint64_t w2 = words[ws.state.pc + 1u];
            uint64_t v = (static_cast<uint64_t>(IMP_IMM32(w2)) << 32) | static_cast<uint64_t>(imm32_u);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), v);
            p1_set_scalar_flags(ws, true, v);
            ws.state.pc += 2u;
            break;
        }
        case 5: { // .u64.ext
            if (static_cast<size_t>(ws.state.pc) + 1u >= word_count) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                break;
            }
            uint64_t w2 = words[ws.state.pc + 1u];
            uint64_t v = (static_cast<uint64_t>(IMP_IMM32(w2)) << 32) | static_cast<uint64_t>(imm32_u);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), v);
            p1_set_scalar_flags(ws, true, v);
            ws.state.pc += 2u;
            break;
        }
        case 6: { // .f64.ext
            if (static_cast<size_t>(ws.state.pc) + 1u >= word_count) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                break;
            }
            uint64_t w2 = words[ws.state.pc + 1u];
            uint64_t bits = (static_cast<uint64_t>(IMP_IMM32(w2)) << 32) | static_cast<uint64_t>(imm32_u);
            double f = p1_bits_to_f64(bits);
            if (std::isnan(f)) {
                ws.write_r_null(dst);
                p1_set_f64_flags(ws, false, 0.0);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), bits);
                p1_set_f64_flags(ws, true, f);
            }
            ws.state.pc += 2u;
            break;
        }
        case 7: { // .null
            ws.write_r_null(dst);
            p1_set_scalar_flags(ws, false, 0ULL);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}

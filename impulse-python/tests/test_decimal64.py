"""Unit tests for DECIMAL64 FFI constants, conversion helpers, Writer roundtrip, and ImpulseVM 0x15/0x58."""

from decimal import Decimal
import os
import struct
import tempfile

import numpy as np
import pytest

import impulse_graph
from impulse_graph import (
    ATTR_TYPE_BOOL,
    ATTR_TYPE_DECIMAL64,
    IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW,
    IMPULSE_VM_ERR_DIV_BY_ZERO,
    OP_SCALAR_DECIMAL_ALU,
    OP_VEC_DECIMAL_ALU,
    Decimal64,
    Snapshot,
    Writer,
    compile_cypher,
    compile_impscm,
    decimal_to_scaled_i64,
    scaled_i64_to_decimal,
)
from impulse_graph import vm as impulse_vm

try:
    from impulse_graph import _impulse_native
except ImportError:
    _impulse_native = None


def _build_impb(words, required_domains=0):
    hdr = struct.pack(
        "<IHBBIIIHHQ",
        0x494D5042,  # magic "IMPB"
        0x000B,      # isa_version (11)
        0,           # header_flags
        1,           # max_call_depth
        len(words),  # code_word_count
        0,           # const_pool_bytes
        0,           # entry_pc
        required_domains,
        0,           # required_relations / reserved
        0,           # program_xxh3_64
    )
    return hdr + b"".join(struct.pack("<Q", w) for w in words)


def _enc(op, flags=0, subop=0, dst=0, s1=0, s2=0, s3=0, imm32=0):
    b0 = op & 0xFF
    b1 = ((flags & 0xF) << 4) | (subop & 0xF)
    b2 = ((dst & 0xF) << 4) | (s1 & 0xF)
    b3 = ((s2 & 0xF) << 4) | (s3 & 0xF)
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | ((imm32 & 0xFFFFFFFF) << 32)


def test_decimal64_constants():
    assert ATTR_TYPE_BOOL == 0x0F
    assert ATTR_TYPE_DECIMAL64 == 0x15
    assert IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW == 16
    assert IMPULSE_VM_ERR_DIV_BY_ZERO == 17
    assert OP_SCALAR_DECIMAL_ALU == 0x15
    assert OP_VEC_DECIMAL_ALU == 0x58
    assert impulse_vm.IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW == 16
    assert impulse_vm.IMPULSE_VM_ERR_DIV_BY_ZERO == 17
    assert impulse_vm.OP_SCALAR_DECIMAL_ALU == 0x15
    assert impulse_vm.OP_VEC_DECIMAL_ALU == 0x58


def test_decimal64_helper_conversions():
    d = Decimal64.from_decimal(Decimal("123.45"), scale=2, precision=10)
    assert d.mantissa == 12345
    assert d.to_int64() == 12345
    assert d.to_decimal() == Decimal("123.45")

    neg = Decimal64.from_decimal("-0.0050", scale=4, precision=18)
    assert neg.mantissa == -50
    assert neg.to_decimal() == Decimal("-0.0050")

    assert decimal_to_scaled_i64("99.995", scale=2) == 10000
    assert scaled_i64_to_decimal(10000, scale=2) == Decimal("100.00")

    with pytest.raises(ValueError):
        Decimal64(1, scale=2, precision=0)
    with pytest.raises(ValueError):
        Decimal64(1, scale=2, precision=19)
    with pytest.raises(ValueError):
        Decimal64(1, scale=5, precision=4)
    with pytest.raises(OverflowError):
        Decimal64(1000, scale=0, precision=3)


def test_writer_decimal64_roundtrip():
    if _impulse_native is None or not hasattr(_impulse_native.Writer, "add_attribute_decimal64"):
        pytest.skip("_impulse_native decimal64 writer bindings not built")

    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "decimal64.imps")
        balances = np.array(
            [decimal_to_scaled_i64("10.50", scale=2), decimal_to_scaled_i64("25.75", scale=2)],
            dtype=np.int64,
        )
        edge_amounts = np.array(
            [decimal_to_scaled_i64("4.25", scale=2)],
            dtype=np.int64,
        )

        with Writer(snap_path) as writer:
            writer.add_domain(0, 1, "Account")
            writer.set_domain_node_count(0, 2)
            writer.add_domain_attribute_decimal64(
                domain_index=0,
                name="balance",
                precision=18,
                scale=2,
                data=balances,
            )
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=2,
                edge_count=1,
                section_features=0,
                row_offsets=[0, 1, 1],
                col_indices=[1],
            )
            writer.add_attribute_decimal64(
                relation_index=0,
                name="amount",
                precision=18,
                scale=2,
                data=edge_amounts,
            )
            writer.finalize()

        with Snapshot(snap_path) as snap:
            info = snap.get_attribute_info(0, 0)
            assert info["type_code"] == ATTR_TYPE_DECIMAL64
            arr = snap.get_attribute_array(0, 0, dtype=np.int64)
            assert arr.tolist() == [425]
            assert scaled_i64_to_decimal(int(arr[0]), scale=2) == Decimal("4.25")


def test_vm_decimal64_scalar_and_vector_execution():
    if _impulse_native is None or not hasattr(_impulse_native.opcodes, "OP_SCALAR_DECIMAL_ALU"):
        pytest.skip("_impulse_native v0.11 DECIMAL64 VM opcodes not built")

    VmWorkspace = _impulse_native.VmWorkspace
    VmStatus = _impulse_native.VmStatus
    opcodes = _impulse_native.opcodes
    validate_bytecode = _impulse_native.validate_bytecode

    assert opcodes.OP_SCALAR_DECIMAL_ALU == OP_SCALAR_DECIMAL_ALU
    assert opcodes.OP_VEC_DECIMAL_ALU == OP_VEC_DECIMAL_ALU
    assert int(VmStatus.ERR_ARITHMETIC_OVERFLOW) == IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW
    assert int(VmStatus.ERR_DIV_BY_ZERO) == IMPULSE_VM_ERR_DIV_BY_ZERO

    # 1) OP_SCALAR_DECIMAL_ALU ADD_CHK (subop=0): 1250 + 3750 = 5000
    scalar_words = [
        _enc(opcodes.OP_LOAD_CONST, subop=0, dst=0, imm32=1250),
        _enc(opcodes.OP_LOAD_CONST, subop=0, dst=1, imm32=3750),
        _enc(OP_SCALAR_DECIMAL_ALU, subop=0, dst=2, s1=0, s2=1),
        _enc(opcodes.OP_COLLECT, subop=3, s1=2, imm32=0),
        _enc(opcodes.OP_HALT),
    ]
    impb_scalar = _build_impb(scalar_words)
    st, bad_pc = validate_bytecode(impb_scalar)
    assert st == int(VmStatus.OK), f"validation failed at pc={bad_pc} status={st}"

    with VmWorkspace(1024 * 1024) as ws:
        rc = ws.execute(impb_scalar, None)
        assert rc == int(VmStatus.OK)
        assert ws.get_output(0)["data"] == [5000]

        # 2) OP_SCALAR_DECIMAL_ALU DIV_ROUND (subop=4) by zero -> ERR_DIV_BY_ZERO (17)
        div0_words = [
            _enc(opcodes.OP_LOAD_CONST, subop=0, dst=0, imm32=100),
            _enc(opcodes.OP_LOAD_CONST, subop=0, dst=1, imm32=0),
            _enc(OP_SCALAR_DECIMAL_ALU, subop=4, dst=2, s1=0, s2=1),
            _enc(opcodes.OP_HALT),
        ]
        rc_div0 = ws.execute(_build_impb(div0_words), None)
        assert rc_div0 == IMPULSE_VM_ERR_DIV_BY_ZERO

        # 3) OP_VEC_DECIMAL_ALU ADD_CHK (subop=0) over a 4-element vector
        vec_words = [
            _enc(opcodes.OP_LOAD_CONST, subop=0, dst=0, imm32=100),
            _enc(opcodes.OP_LOAD_CONST, subop=0, dst=1, imm32=250),
            _enc(opcodes.OP_VEC_INIT, flags=0x2, subop=0, dst=0, s2=0, imm32=4),
            _enc(opcodes.OP_VEC_INIT, flags=0x2, subop=0, dst=1, s2=1, imm32=4),
            _enc(OP_VEC_DECIMAL_ALU, subop=0, dst=2, s1=0, s2=1),
            _enc(opcodes.OP_COLLECT, subop=2, s1=2, imm32=0),
            _enc(opcodes.OP_HALT),
        ]
        impb_vec = _build_impb(vec_words)
        st_v, bad_pc_v = validate_bytecode(impb_vec)
        assert st_v == int(VmStatus.OK), f"vec validation failed at pc={bad_pc_v} status={st_v}"
        rc_v = ws.execute(impb_vec, None)
        assert rc_v == int(VmStatus.OK)
        assert ws.get_output(0)["data"] == [350, 350, 350, 350]


def test_e2e_python_cypher_and_impscm_decimal64_param():
    if _impulse_native is None or not hasattr(_impulse_native, "compile_cypher"):
        pytest.skip("_impulse_native compile_cypher / compile_impscm not built")

    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "ledger.imps")
        balances = np.array(
            [
                decimal_to_scaled_i64("10.00", scale=2),
                decimal_to_scaled_i64("19.99", scale=2),
                decimal_to_scaled_i64("45.25", scale=2),
            ],
            dtype=np.int64,
        )

        with Writer(snap_path) as writer:
            writer.add_domain(0, 1, "Account")
            writer.set_domain_node_count(0, 3)
            writer.add_domain_attribute_decimal64(
                domain_index=0,
                name="balance",
                precision=18,
                scale=2,
                data=balances,
            )
            writer.finalize()

        with Snapshot(snap_path) as snap:
            # 1. openCypher with Decimal("19.99") parameter
            prog = compile_cypher(
                "MATCH (a:Account) WHERE a.balance >= $min_bal RETURN sum(a.balance)",
                snapshot=snap,
                param_types={"min_bal": "DECIMAL(18, 2)"},
            )
            assert prog.output_schema[0]["is_decimal"] is True
            assert prog.output_schema[0]["scale"] == 2
            res = prog.execute(snap, params={"min_bal": Decimal("19.99")})
            assert res["result"] == Decimal("65.24")

            # 2. openCypher with 19.99d literal + CAST(... AS DECIMAL(18, 2))
            prog_lit = compile_cypher(
                "MATCH (a:Account) WHERE a.balance >= 19.99d AND a.balance <= CAST(25.00 AS DECIMAL(18, 2)) RETURN count(a)",
                snapshot=snap,
            )
            assert prog_lit.execute(snap)["count"] == 1

            # 3. ImpScheme v0.11 with Decimal("19.99") parameter
            scm_prog = compile_impscm(
                '(query (param $min_bal (decimal 18 2)) '
                '(let ((f (node-filter (domain-all "Account") "balance" >= $min_bal)) '
                '      (b (load-node-attr "Account" "balance"))) '
                '  (collect :scalar "total" (reduce :sum b :mask f))))',
                snapshot=snap,
            )
            scm_res = scm_prog.execute(snap, params={"min_bal": Decimal("19.99")})
            assert scm_res["total"] == Decimal("65.24")

            # 4. Strict TYPE_MISMATCH rejection (DECIMAL + FLOAT forbidden)
            with pytest.raises(RuntimeError, match="TYPE_MISMATCH"):
                compile_impscm('(query (+ (dec "19.99" 10 2) 1.5))', snapshot=snap)

            # 5. Audit regression #1: NULL decimal output -> None (never Decimal("0.00")),
            #    BOOL_3VL -> True/False/None, STR -> str
            null_div = compile_impscm(
                '(query (collect :scalar "q" (safe-div (dec "10.00" 10 2) (dec "0.00" 10 2))))',
                snapshot=snap,
            )
            assert null_div.execute(snap)["q"] is None

            bool_prog = compile_impscm(
                '(query (collect :scalar "b" (> (dec "20.00" 10 2) (dec "10.00" 10 2))))',
                snapshot=snap,
            )
            assert bool_prog.execute(snap)["b"] is True

            bool_vec_prog = compile_impscm(
                '(query (let ((b (load-node-attr "Account" "balance"))) '
                '  (collect :vec "flags" (>= b (dec "19.99" 10 2)))))',
                snapshot=snap,
            )
            assert bool_vec_prog.execute(snap)["flags"] == [False, True, True]

            str_prog = compile_impscm(
                '(query (collect :scalar "s" "hello"))',
                snapshot=snap,
            )
            assert str_prog.execute(snap)["s"] == "hello"

            # 6. Audit regression #3: None param -> SCALAR_NULL; scale-4 Decimal64 rescaled to scale-2 param
            null_param_prog = compile_impscm(
                '(query (param $d (decimal 18 2)) '
                '  (collect :scalar "q" (safe-div (dec "10.00" 18 2) $d)))',
                snapshot=snap,
            )
            assert null_param_prog.execute(snap, params={"d": None})["q"] is None

            d4 = Decimal64.from_decimal(Decimal("19.9900"), scale=4, precision=18)
            assert prog.execute(snap, params={"min_bal": d4})["result"] == Decimal("65.24")

            # 7. Audit regression #9: param_types precision/scale validation & lowercase tuple
            with pytest.raises(ValueError):
                compile_cypher(
                    "MATCH (a:Account) WHERE a.balance >= $min_bal RETURN count(a)",
                    snapshot=snap,
                    param_types={"min_bal": "DECIMAL(19, 2)"},
                )
            with pytest.raises(ValueError):
                compile_cypher(
                    "MATCH (a:Account) WHERE a.balance >= $min_bal RETURN count(a)",
                    snapshot=snap,
                    param_types={"min_bal": ("decimal", 5, 6)},
                )
            prog_lower = compile_cypher(
                "MATCH (a:Account) WHERE a.balance >= $min_bal RETURN count(a)",
                snapshot=snap,
                param_types={"min_bal": ("decimal", 18, 2)},
            )
            assert prog_lower.execute(snap, params={"min_bal": Decimal("19.99")})["count"] == 2

            # 8. Audit regression #10: 1-element frontier-ids & vec stay list, never unwrapped
            one_node = compile_cypher(
                "MATCH (a:Account) WHERE a.balance >= 40.00d RETURN a",
                snapshot=snap,
            )
            assert one_node.execute(snap)["result"] == [2]

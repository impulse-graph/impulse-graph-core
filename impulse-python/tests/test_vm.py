"""ImpulseVM v0.11 Python native binding tests (VmWorkspace, Statement, validate_bytecode, plan_memory)."""

import struct
import pytest
import impulse_graph
from impulse_graph._impulse_native import (
    Statement,
    VmStatus,
    VmWorkspace,
    opcodes,
    plan_memory,
    validate_bytecode,
)


def _build_impb(words):
    hdr = struct.pack(
        "<IHBBIIIHHQ",
        0x494D5042,  # magic "IMPB"
        0x000B,      # isa_version (11)
        0,           # header_flags
        1,           # max_call_depth
        len(words),  # code_word_count
        0,           # const_pool_bytes
        0,           # entry_pc
        0,           # required_domains
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


def test_vm_v11_opcodes_and_status():
    assert opcodes.OP_HALT == 0x00
    assert opcodes.OP_WALK == 0x30
    assert opcodes.OP_WALK_MORSEL == 0x35
    assert int(VmStatus.OK) == 0


def test_vm_v11_validate_plan_and_execute():
    # LOAD_CONST.i64 R0, 42; COLLECT.scalar R0, slot=0; HALT
    words = [
        _enc(opcodes.OP_LOAD_CONST, subop=0, dst=0, imm32=42),
        _enc(opcodes.OP_COLLECT, subop=3, s1=0, imm32=0),
        _enc(opcodes.OP_HALT),
    ]
    impb = _build_impb(words)
    status, bad_pc = validate_bytecode(impb)
    assert status == int(VmStatus.OK)
    assert bad_pc == 0

    plan = plan_memory(impb, None, 1)
    assert plan["min_scratch_bytes"] > 0

    with VmWorkspace(1024 * 1024) as ws:
        rc = ws.execute(impb, None)
        assert rc == int(VmStatus.OK)
        assert ws.output_count() == 1
        out = ws.get_output(0)
        assert out["length"] == 1
        assert out["data"] == [42]
        logical, physical = ws.compute_ictx_v2()
        assert logical != 0
        assert physical != 0

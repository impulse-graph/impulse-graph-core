using System;
using System.Collections.Generic;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Fluent C# QueryBuilder for assembling impOps bytecode instruction sequences.
/// Compiles zero-allocation impulse_instruction_t streams for Impulse VM execution.
/// </summary>
public sealed class QueryBuilder
{
    private readonly List<ImpulseInstruction> _instructions = new();
    private ushort _currentReg = 0;

    public QueryBuilder InputNode(ushort dstReg = 0)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x02, 0x00, dstReg, 0));
        return this;
    }

    public QueryBuilder InputSet(ushort dstReg = 0)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x03, 0x00, dstReg, 0));
        return this;
    }

    public QueryBuilder LoadConstInt(uint val, ushort dstReg = 0)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x04, 0x00, dstReg, val));
        return this;
    }

    public QueryBuilder WalkEdge(ushort relationId, ushort dstReg = 1)
    {
        return WalkEdge(relationId, _currentReg, dstReg);
    }

    public QueryBuilder WalkEdge(ushort relationId, ushort srcReg, ushort dstReg)
    {
        uint payload = ((uint)relationId << 16) | srcReg;
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x10, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder WalkEdgeFiltered(ushort relationId, ushort filterReg, ushort dstReg = 1)
    {
        return WalkEdgeFiltered(relationId, filterReg, _currentReg, dstReg);
    }

    public QueryBuilder WalkEdgeFiltered(ushort relationId, ushort filterReg, ushort srcReg, ushort dstReg)
    {
        uint payload = ((uint)relationId << 16) | ((filterReg & 0xFFu) << 8) | (srcReg & 0xFFu);
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x11, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder Degree(ushort relationId, ushort dstReg = 1)
    {
        return Degree(relationId, _currentReg, dstReg);
    }

    public QueryBuilder Degree(ushort relationId, ushort srcReg, ushort dstReg)
    {
        uint payload = ((uint)relationId << 16) | srcReg;
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x12, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder CscWalk(ushort relationId, ushort dstReg = 1)
    {
        return CscWalk(relationId, _currentReg, dstReg);
    }

    public QueryBuilder CscWalk(ushort relationId, ushort srcReg, ushort dstReg)
    {
        uint payload = ((uint)relationId << 16) | srcReg;
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x18, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder SetUnion(ushort srcReg1, ushort srcReg2, ushort dstReg)
    {
        uint payload = ((uint)srcReg1 << 16) | srcReg2;
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x30, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder SetIntersect(ushort srcReg1, ushort srcReg2, ushort dstReg)
    {
        uint payload = ((uint)srcReg1 << 16) | srcReg2;
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x31, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder SetDifference(ushort srcReg1, ushort srcReg2, ushort dstReg)
    {
        uint payload = ((uint)srcReg2 << 16) | srcReg1;
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x32, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder SetCardinality(ushort srcReg, ushort dstReg)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x33, 0x00, dstReg, srcReg));
        return this;
    }

    public QueryBuilder AfforestCC(ushort relationId, ushort dstReg = 0)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x40, 0x00, dstReg, relationId));
        return this;
    }

    public QueryBuilder MxV(ushort relationId, ushort vectorReg, ushort dstReg, ushort semiringId = 0)
    {
        uint payload = ((uint)semiringId << 16) | (((uint)relationId & 0xFFu) << 8) | (vectorReg & 0xFFu);
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x41, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder BrandesForward(ushort relationId, ushort dstReg = 0)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x48, 0x00, dstReg, relationId));
        return this;
    }

    public QueryBuilder SampleNeighbors(ushort relationId, uint kSamples, ushort dstReg = 1)
    {
        uint payload = ((uint)relationId << 16) | (kSamples & 0xFFFF);
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x60, 0x00, dstReg, payload));
        return this;
    }

    public QueryBuilder RebacCheck(uint permissionId, ushort dstReg = 0)
    {
        _currentReg = dstReg;
        _instructions.Add(new ImpulseInstruction(0x63, 0x00, dstReg, permissionId));
        return this;
    }

    public QueryBuilder CollectBitset(ushort reg = 0)
    {
        _instructions.Add(new ImpulseInstruction(0x90, 0x00, reg, reg));
        _currentReg = reg;
        return this;
    }

    public QueryBuilder CollectArray(ushort reg = 0)
    {
        _instructions.Add(new ImpulseInstruction(0x91, 0x00, reg, reg));
        _currentReg = reg;
        return this;
    }

    public QueryBuilder Halt()
    {
        _instructions.Add(new ImpulseInstruction(0x00, 0x00, 0, 0));
        return this;
    }

    public Query Compile()
    {
        if (_instructions.Count == 0 || _instructions[^1].Opcode != 0x00)
        {
            Halt();
        }
        return new Query(_instructions.ToArray());
    }
}

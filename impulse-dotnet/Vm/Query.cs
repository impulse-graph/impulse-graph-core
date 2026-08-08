using System;
using System.Collections.Generic;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Compiled bytecode query container ready for execution against an Impulse Graph Snapshot.
/// </summary>
public sealed class Query
{
    public ImpulseInstruction[] Bytecode { get; }
    public int InstructionCount => Bytecode.Length;

    public Query(IEnumerable<ImpulseInstruction> instructions)
    {
        ArgumentNullException.ThrowIfNull(instructions);
        Bytecode = [.. instructions];
    }

    public Query(ImpulseInstruction[] bytecode)
    {
        ArgumentNullException.ThrowIfNull(bytecode);
        Bytecode = bytecode;
    }

    public ReadOnlySpan<ImpulseInstruction> AsSpan() => Bytecode.AsSpan();
}

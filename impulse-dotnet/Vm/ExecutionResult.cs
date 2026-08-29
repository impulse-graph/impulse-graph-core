using System;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Execution result encapsulating VM status, register states, and status flags.
/// </summary>

public sealed class ExecutionResult
{
    public ImpulseVmStatus Status { get; }
    public ulong Flags { get; }
    public ulong[] Registers { get; }
    
    public int ResultRegister { get; }
    public ulong RawValue { get; }
    public int ResultType { get; }

    public bool IsOk => Status == ImpulseVmStatus.Ok;

    public ExecutionResult(ImpulseVmStatus status, ulong flags, ulong[] registers, int resultRegister = 0, ulong rawValue = 0, int resultType = 0)
    {
        Status = status;
        Flags = flags;
        Registers = registers ?? Array.Empty<ulong>();
        ResultRegister = resultRegister;
        RawValue = rawValue;
        ResultType = resultType;
    }

    public ulong GetRegister(int index)
    {
        if (index < 0 || index >= Registers.Length)
            throw new ArgumentOutOfRangeException(nameof(index), "Register index must be between 0 and 63");
        return Registers[index];
    }

    public override string ToString() =>
        $"ExecutionResult(Status={Status}, IsOk={IsOk}, R{ResultRegister}={RawValue}, Flags=0x{Flags:X})";
}


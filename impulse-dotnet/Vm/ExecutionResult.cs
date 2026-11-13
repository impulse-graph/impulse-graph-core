using System;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Execution result encapsulating VM status, register states, and status flags.
/// </summary>
public sealed class ExecutionResult : IDisposable
{
    private VmContext? _ownedContext;

    public ImpulseVmStatus Status { get; }
    public ulong Flags { get; }
    public ulong[] Registers { get; }
    public VmContext? Context => _ownedContext;

    public bool IsOk => Status == ImpulseVmStatus.Ok;
    public ulong ResultRegister => Registers.Length > 0 ? Registers[0] : 0UL;

    public ExecutionResult(ImpulseVmStatus status, ulong flags, ulong[] registers, VmContext? ownedContext = null)
    {
        Status = status;
        Flags = flags;
        Registers = registers ?? Array.Empty<ulong>();
        _ownedContext = ownedContext;
    }

    public ulong GetRegister(int index)
    {
        if (index < 0 || index >= Registers.Length)
            throw new ArgumentOutOfRangeException(nameof(index), "Register index must be between 0 and 63");
        return Registers[index];
    }

    public void Dispose()
    {
        _ownedContext?.Dispose();
        _ownedContext = null;
    }

    public override string ToString() =>
        $"ExecutionResult(Status={Status}, IsOk={IsOk}, R0={ResultRegister}, Flags=0x{Flags:X})";
}

using System.Runtime.InteropServices;

namespace ImpulseGraph.Native;

/// <summary>
/// Fixed 64-bit instruction structure layout (8 bytes).
/// Maps to impulse_instruction_t in impulse_vm.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, Size = 8)]
public struct ImpulseInstruction
{
    public byte Opcode;
    public byte Flags;
    public ushort DstReg;
    public uint Payload;

    public ImpulseInstruction(byte opcode, byte flags, ushort dstReg, uint payload)
    {
        Opcode = opcode;
        Flags = flags;
        DstReg = dstReg;
        Payload = payload;
    }
}

/// <summary>
/// Relation Directory Entry Descriptor (Fixed 128 Bytes).
/// Maps to impulse_relation_directory_entry_v0_9_t in impulse_format_v0_9.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, Size = 128)]
public unsafe struct ImpulseRelationEntry
{
    public ushort RelationId;
    public ushort SrcDomainId;
    public ushort TgtDomainId;
    public byte EncodingId;
    public byte NodeIdWidth;
    public byte EdgeIndexWidth;
    public byte Reserved1_0;
    public byte Reserved1_1;
    public byte Reserved1_2;
    public uint NameOffset;
    public ulong NodeCount;
    public ulong EdgeCount;
    public ulong SectionFeatures;
    public ulong CsrRowOffOffset;
    public ulong CsrRowOffBytes;
    public ulong CsrColIdxOffset;
    public ulong CsrColIdxBytes;
    public ulong CscRowOffOffset;
    public ulong CscRowOffBytes;
    public ulong CscColIdxOffset;
    public ulong CscColIdxBytes;
    public ushort AttrCount;
    public fixed byte Reserved2[22];
}

/// <summary>
/// VM Execution State Frame (640 bytes, 64-byte aligned).
/// Maps to impulse_vm_state_t in impulse_vm.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Size = 640)]
public unsafe struct ImpulseVmState
{
    public uint Pc;
    public uint Reserved;
    public ulong Flags;
    public fixed ulong Registers[64];
    public fixed byte RegisterTypes[64];
    public nint QueryContext;
    public fixed uint CallStack[8];
    public uint CallStackDepth;
    public uint ReservedPadding2;
}

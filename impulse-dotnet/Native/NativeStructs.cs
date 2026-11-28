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
/// Naturally aligned summary of active Page 0 snapshot header fields.
/// Maps to impulse_snapshot_header_info_t in impulse_graph.h.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ImpulseSnapshotHeaderInfo
{
    public uint Magic;
    public uint DataOffset;
    public ulong TimestampMs;
    public ulong RequiredFeatures;
    public ulong FooterDirectoryOffset;
    public ulong FooterDirectoryBytes;
    public ulong FileSize;
    public ushort Version;
    public ushort DomainCount;
    public ushort RelationCount;
    public ushort IndexCount;
    public ushort HeaderChecksum;
    public fixed byte Reserved[6];
    public fixed byte SnapshotUuid[16];
}

/// <summary>
/// Attribute Descriptor Structure (Fixed 44 Bytes).
/// Maps to impulse_attribute_descriptor_v0_11_t in impulse_format_v0_11.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, Size = 44)]
public struct ImpulseAttributeDescriptor
{
    public uint NameOffset;
    public byte TypeCode;
    public byte Reserved1;
    public ushort Reserved2;
    public uint Dimension;
    public ulong DataOffset;
    public ulong DataBytes;
    public ulong OffsetsOffset;
    public ulong OffsetsBytes;
}

/// <summary>
/// Secondary Index Directory Entry (Fixed 64 Bytes).
/// Maps to impulse_index_directory_entry_v0_11_t in impulse_format_v0_11.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, Size = 64)]
public unsafe struct ImpulseIndexEntry
{
    public uint IndexId;
    public ushort DomainId;
    public ushort RelationId;
    public ushort AttributeIndex;
    public byte IndexType;
    public byte Reserved1;
    public uint NameOffset;
    public ulong DataOffset;
    public ulong DataBytes;
    public ulong PayloadFeatureMask;
    public fixed byte ReservedPadding[24];
}

/// <summary>
/// Domain Catalog Entry Descriptor (Fixed 32 Bytes).
/// Maps to impulse_domain_catalog_entry_v0_11_t in impulse_format_v0_11.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, Size = 32)]
public unsafe struct ImpulseDomainEntry
{
    public ushort DomainId;
    public byte KeyType;
    public byte DenseIdWidth;
    public uint NameOffset;
    public ulong NodeCount;
    public ushort AttrCount;
    public ushort KeyAttrIndex;
    public fixed byte Reserved2[12];
}

/// <summary>
/// Relation Directory Entry Descriptor (Fixed 192 Bytes).
/// Maps to impulse_relation_directory_entry_v0_11_t in impulse_format_v0_11.h.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, Size = 192)]
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
    // v0.11.0 COO Topology Extension Block (bytes 0x80..0xBF, 64 bytes)
    public ulong CooSrcOffset;
    public ulong CooSrcBytes;
    public ulong CooTgtOffset;
    public ulong CooTgtBytes;
    public ulong CooEdgeIdOffset;
    public ulong CooEdgeIdBytes;
    public byte CooOrder;
    public fixed byte Reserved3[15];
}

/// <summary>
/// Static AOT memory planner output (impulse_vm_memory_plan_t in impulse_vm_v11.h).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct ImpulseVmMemoryPlan
{
    public nuint MinScratchBytes;
    public nuint GuaranteedScratchBytes;
    public nuint MaxResultBytes;
}

/// <summary>
/// v0.11 ImpulseVM parameter binding (impulse_vm_param_binding_v11_t).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ImpulseVmParamBindingV11
{
    public uint ParamId;
    public uint Kind;
    public ushort DomainId;
    public ushort Reserved;
    public uint Count;
    public ulong ScalarBits;
    public void* DataPtr;
    public ulong* ValidBits;
}

/// <summary>
/// v0.11 ImpulseVM execution parameters (impulse_vm_params_v11_t).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ImpulseVmParamsV11
{
    public ulong GasLimit;
    public void* GeometryOverride;
    public ImpulseVmParamBindingV11* Bindings;
    public uint BindingCount;
}

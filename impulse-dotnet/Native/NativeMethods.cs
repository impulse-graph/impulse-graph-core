using System.Runtime.InteropServices;

namespace ImpulseGraph.Native;

/// <summary>
/// Source-generated P/Invoke native interop bindings using [LibraryImport].
/// Links against native libimpulse_graph shared library (.so / .dylib / .dll).
/// </summary>
internal static unsafe partial class NativeMethods
{
    private const string LibName = "impulse_graph";

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    public static partial nint impulse_snapshot_open(string filePath, out ImpulseStatus outStatus);

    [LibraryImport(LibName)]
    public static partial void impulse_snapshot_close(nint snapshot);

    [LibraryImport(LibName)]
    public static partial uint impulse_snapshot_magic(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_version(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_domain_count(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_relation_count(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_relation_entry(
        nint snapshot,
        ushort relationIndex,
        out ImpulseRelationEntry outEntry
    );

    [LibraryImport(LibName)]
    [return: MarshalAs(UnmanagedType.U1)]
    public static partial bool impulse_snapshot_is_reachable(
        nint snapshot,
        ushort relationIndex,
        ulong srcId,
        ulong tgtId
    );

    [LibraryImport(LibName)]
    public static partial nint impulse_get_last_error();

    [LibraryImport(LibName)]
    public static partial uint impulse_workspace_init(
        void* scratchBuf,
        nuint scratchBytes,
        out nint outWs
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_v11_prepare_snapshot(nint snapshot);

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_plan_memory(
        void* impbBytes,
        nuint impbLen,
        nint snapshot,
        ushort maxDop,
        out ImpulseVmMemoryPlan outPlan
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_execute_v11(
        void* impbBytes,
        nuint impbLen,
        nint snapshot,
        void* paramsPtr,
        nint scratchWs,
        void* resultBuf,
        nuint resultCap,
        out nuint outResultBytes
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_v11_get_output_count(nint ws);
}

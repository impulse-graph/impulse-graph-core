using System;
using System.Runtime.InteropServices;

namespace ImpulseGraph;

public unsafe class Snapshot : IDisposable
{
    private const string LibName = "impulse_graph";

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr impulse_snapshot_open(string filePath, out int outStatus);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void impulse_snapshot_close(IntPtr snapshot);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool impulse_snapshot_is_reachable(
        IntPtr snapshot,
        ushort srcDomain, uint srcId,
        ushort tgtDomain, uint tgtId
    );

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr impulse_get_last_error();

    private IntPtr _handle;

    public Snapshot(string filePath)
    {
        _handle = impulse_snapshot_open(filePath, out var status);
        if (_handle == IntPtr.Zero || status != 0)
        {
            var errPtr = impulse_get_last_error();
            var err = errPtr != IntPtr.Zero ? Marshal.PtrToStringAnsi(errPtr) : "Unknown error";
            throw new InvalidOperationException($"Failed to open snapshot '{filePath}': {err}");
        }
    }

    public bool IsReachable(ushort srcDomain, uint srcId, ushort tgtDomain, uint tgtId)
    {
        if (_handle == IntPtr.Zero) return false;
        return impulse_snapshot_is_reachable(_handle, srcDomain, srcId, tgtDomain, tgtId);
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            impulse_snapshot_close(_handle);
            _handle = IntPtr.Zero;
        }
    }
}

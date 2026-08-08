namespace ImpulseGraph.Native;

/// <summary>
/// Native C-ABI return status and error codes.
/// Maps to impulse_status_t in impulse_graph.h.
/// </summary>
public enum ImpulseStatus : int
{
    Ok = 0,
    ErrInvalidMagic = 1,
    ErrUnsupportedVersion = 2,
    ErrUnsupportedGlobalFeature = 3,
    ErrUnsupportedSectionFeature = 4,
    ErrCorruptChecksum = 5,
    ErrIoFailure = 6,
    ErrInvalidArgument = 7,
    ErrSignatureMismatch = 8,
    ErrBufferOverflow = 9
}

/// <summary>
/// Native Impulse VM execution status codes.
/// Maps to impulse_vm_status_t in impulse_vm.h.
/// </summary>
public enum ImpulseVmStatus : int
{
    Ok = 0,
    ErrInvalidOpcode = 1,
    ErrOutOfBounds = 2,
    ErrNullSnapshot = 3,
    ErrStackOverflow = 4,
    ErrStackUnderflow = 5,
    ErrInvalidRegister = 6
}

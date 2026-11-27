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
/// Maps to impulse_vm_status_v11_t in impulse_vm_v11.h.
/// </summary>
public enum ImpulseVmStatus : int
{
    Ok = 0,
    ErrInvalidOpcode = 1,
    ErrOutOfBounds = 2,
    ErrNullSnapshot = 3,
    ErrStackOverflow = 4,
    ErrStackUnderflow = 5,
    ErrInvalidRegister = 6,
    ErrInvalidOperand = 6,
    ErrUserThrow = 7,
    ErrAssertionFailed = 8,
    ErrTrap = 9,
    ErrDomainMismatch = 10,
    ErrBufferOverflow = 11,
    ErrFloatingPoint = 12,
    ErrGasExhausted = 13,
    ErrTypeMismatch = 14,
    ErrValidationFailed = 15,
    ArithmeticOverflow = 16,
    DivByZero = 17
}

/// <summary>
/// Attribute type codes (v0.11).
/// </summary>
public static class ImpulseAttrTypes
{
    public const byte AttrTypeBool = 0x0F;
    public const byte AttrTypeDecimal64 = 0x15;
    public const byte AttrTypeDecimal64Nullable = 0x95;
}

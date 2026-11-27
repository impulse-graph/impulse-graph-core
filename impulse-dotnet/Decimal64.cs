using System;
using System.Globalization;
using System.Text;

namespace ImpulseGraph;

/// <summary>
/// Immutable fixed-point DECIMAL(Precision, Scale) value (1 &lt;= Precision &lt;= 18,
/// 0 &lt;= Scale &lt;= Precision) backed by a signed 64-bit unscaled integer
/// (<c>Unscaled * 10^-Scale</c>). Bridges losslessly to .NET's 96-bit <see cref="decimal"/>.
/// </summary>
public readonly struct Decimal64 : IEquatable<Decimal64>, IComparable<Decimal64>
{
    private static readonly long[] Pow10 =
    {
        1L,
        10L,
        100L,
        1_000L,
        10_000L,
        100_000L,
        1_000_000L,
        10_000_000L,
        100_000_000L,
        1_000_000_000L,
        10_000_000_000L,
        100_000_000_000L,
        1_000_000_000_000L,
        10_000_000_000_000L,
        100_000_000_000_000L,
        1_000_000_000_000_000L,
        10_000_000_000_000_000L,
        100_000_000_000_000_000L,
        1_000_000_000_000_000_000L
    };

    public long Unscaled { get; }
    public byte Precision { get; }
    public byte Scale { get; }

    public Decimal64(long unscaled, byte precision = 18, byte scale = 2)
    {
        ValidatePrecisionScale(precision, scale);
        if (unscaled == long.MinValue || Math.Abs(unscaled) >= Pow10[precision])
        {
            throw new OverflowException(
                $"Decimal64 unscaled value {unscaled} exceeds DECIMAL({precision}, {scale}) bounds.");
        }
        Unscaled = unscaled;
        Precision = precision;
        Scale = scale;
    }

    public static void ValidatePrecisionScale(byte precision, byte scale)
    {
        if (precision < 1 || precision > 18)
        {
            throw new ArgumentOutOfRangeException(
                nameof(precision), precision, "DECIMAL64 precision must be in [1, 18].");
        }
        if (scale > precision)
        {
            throw new ArgumentOutOfRangeException(
                nameof(scale), scale, $"DECIMAL64 scale must be in [0, {precision}].");
        }
    }

    /// <summary>
    /// Converts this <see cref="Decimal64"/> to a .NET 96-bit <see cref="decimal"/>
    /// with zero precision loss and exact scale preservation.
    /// </summary>
    public decimal ToDecimal()
    {
        bool isNegative = Unscaled < 0L;
        ulong mag = isNegative ? unchecked((ulong)(-Unscaled)) : (ulong)Unscaled;
        int lo = unchecked((int)(mag & 0xFFFF_FFFFUL));
        int mid = unchecked((int)(mag >> 32));
        return new decimal(lo, mid, 0, isNegative, Scale);
    }

    /// <summary>
    /// Converts a .NET <see cref="decimal"/> into a <see cref="Decimal64"/> with the given
    /// <paramref name="precision"/> and <paramref name="scale"/>, rounding half-away-from-zero
    /// when rescaling is required.
    /// </summary>
    public static Decimal64 FromDecimal(decimal value, byte precision = 18, byte scale = 2)
    {
        ValidatePrecisionScale(precision, scale);
        decimal rounded = Math.Round(value, scale, MidpointRounding.AwayFromZero);
        decimal scaled = rounded * Pow10[scale];
        if (scaled < -(decimal)(Pow10[precision] - 1L) || scaled > (decimal)(Pow10[precision] - 1L))
        {
            throw new OverflowException(
                $"Decimal value {value} overflows DECIMAL({precision}, {scale}).");
        }
        long unscaled = decimal.ToInt64(scaled);
        return new Decimal64(unscaled, precision, scale);
    }

    /// <summary>
    /// Attempts to convert a .NET <see cref="decimal"/> into a <see cref="Decimal64"/>
    /// without throwing on range or overflow errors.
    /// </summary>
    public static bool TryFromDecimal(decimal value, byte precision, byte scale, out Decimal64 result)
    {
        if (precision < 1 || precision > 18 || scale > precision)
        {
            result = default;
            return false;
        }
        try
        {
            decimal rounded = Math.Round(value, scale, MidpointRounding.AwayFromZero);
            decimal scaled = rounded * Pow10[scale];
            decimal maxMag = (decimal)(Pow10[precision] - 1L);
            if (scaled < -maxMag || scaled > maxMag)
            {
                result = default;
                return false;
            }
            long unscaled = decimal.ToInt64(scaled);
            result = new Decimal64(unscaled, precision, scale);
            return true;
        }
        catch (OverflowException)
        {
            result = default;
            return false;
        }
    }

    /// <summary>
    /// Parses a base-10 decimal string into a <see cref="Decimal64"/> using exact integer
    /// arithmetic (zero floating-point drift), rounding half-away-from-zero if fractional
    /// digits exceed <paramref name="scale"/>.
    /// </summary>
    public static Decimal64 Parse(string text, byte precision = 18, byte scale = 2)
    {
        ArgumentNullException.ThrowIfNull(text);
        ValidatePrecisionScale(precision, scale);

        ReadOnlySpan<char> s = text.AsSpan().Trim();
        if (s.IsEmpty)
        {
            throw new FormatException("Empty DECIMAL64 string.");
        }

        int idx = 0;
        bool neg = false;
        if (s[0] == '-')
        {
            neg = true;
            idx = 1;
        }
        else if (s[0] == '+')
        {
            idx = 1;
        }
        if (idx >= s.Length)
        {
            throw new FormatException($"Malformed DECIMAL64 literal: '{text}'.");
        }

        var intPart = new StringBuilder();
        var fracPart = new StringBuilder();
        bool seenDot = false;
        bool seenDigit = false;

        for (; idx < s.Length; idx++)
        {
            char c = s[idx];
            if (c == '.')
            {
                if (seenDot)
                {
                    throw new FormatException($"Multiple decimal points in DECIMAL64 literal: '{text}'.");
                }
                seenDot = true;
            }
            else if (c >= '0' && c <= '9')
            {
                seenDigit = true;
                if (seenDot)
                {
                    fracPart.Append(c);
                }
                else
                {
                    intPart.Append(c);
                }
            }
            else
            {
                throw new FormatException($"Invalid character '{c}' in DECIMAL64 literal: '{text}'.");
            }
        }

        if (!seenDigit)
        {
            throw new FormatException($"No digits in DECIMAL64 literal: '{text}'.");
        }

        bool roundUp = false;
        string keptFrac;
        if (fracPart.Length <= scale)
        {
            keptFrac = fracPart.ToString().PadRight(scale, '0');
        }
        else
        {
            keptFrac = fracPart.ToString(0, scale);
            roundUp = fracPart[scale] >= '5';
        }

        ulong limit = (ulong)Pow10[precision];
        ulong mag = 0UL;
        string allDigits = intPart.ToString() + keptFrac;
        for (int i = 0; i < allDigits.Length; i++)
        {
            ulong d = (ulong)(allDigits[i] - '0');
            if (mag > (limit - 1UL) / 10UL)
            {
                throw new OverflowException($"DECIMAL64 literal '{text}' overflows DECIMAL({precision}, {scale}).");
            }
            mag = mag * 10UL + d;
            if (mag >= limit)
            {
                throw new OverflowException($"DECIMAL64 literal '{text}' overflows DECIMAL({precision}, {scale}).");
            }
        }

        if (roundUp)
        {
            mag++;
            if (mag >= limit)
            {
                throw new OverflowException(
                    $"DECIMAL64 literal '{text}' overflows DECIMAL({precision}, {scale}) after rounding.");
            }
        }

        long unscaled = (long)mag;
        if (neg && mag != 0UL)
        {
            unscaled = -unscaled;
        }
        return new Decimal64(unscaled, precision, scale);
    }

    /// <summary>
    /// Formats this fixed-point value as an exact base-10 string with <see cref="Scale"/>
    /// fractional digits when <see cref="Scale"/> &gt; 0.
    /// </summary>
    public override string ToString()
    {
        if (Scale == 0)
        {
            return Unscaled.ToString(CultureInfo.InvariantCulture);
        }
        bool neg = Unscaled < 0L;
        ulong mag = neg ? unchecked((ulong)(-Unscaled)) : (ulong)Unscaled;
        string digits = mag.ToString(CultureInfo.InvariantCulture);

        var sb = new StringBuilder(digits.Length + Scale + 3);
        if (neg)
        {
            sb.Append('-');
        }
        if (digits.Length <= Scale)
        {
            sb.Append("0.");
            sb.Append('0', Scale - digits.Length);
            sb.Append(digits);
        }
        else
        {
            int split = digits.Length - Scale;
            sb.Append(digits, 0, split);
            sb.Append('.');
            sb.Append(digits, split, Scale);
        }
        return sb.ToString();
    }

    public bool Equals(Decimal64 other) => ToDecimal() == other.ToDecimal();

    public override bool Equals(object? obj) => obj is Decimal64 other && Equals(other);

    public override int GetHashCode() => ToDecimal().GetHashCode();

    public int CompareTo(Decimal64 other) => ToDecimal().CompareTo(other.ToDecimal());

    public static bool operator ==(Decimal64 left, Decimal64 right) => left.Equals(right);
    public static bool operator !=(Decimal64 left, Decimal64 right) => !left.Equals(right);
    public static bool operator <(Decimal64 left, Decimal64 right) => left.CompareTo(right) < 0;
    public static bool operator <=(Decimal64 left, Decimal64 right) => left.CompareTo(right) <= 0;
    public static bool operator >(Decimal64 left, Decimal64 right) => left.CompareTo(right) > 0;
    public static bool operator >=(Decimal64 left, Decimal64 right) => left.CompareTo(right) >= 0;

    /// <summary>
    /// Lossless implicit conversion from <see cref="Decimal64"/> to .NET 96-bit <see cref="decimal"/>.
    /// </summary>
    public static implicit operator decimal(Decimal64 value) => value.ToDecimal();
}

/// <summary>
/// Schema metadata for a DECIMAL64 attribute column in an Impulse snapshot.
/// </summary>
public readonly record struct Decimal64AttributeMeta(
    ushort Precision,
    byte Scale,
    bool IsNullable,
    uint Dimension
);

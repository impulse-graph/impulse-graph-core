using System;
using System.Buffers.Binary;
using ImpulseGraph;
using ImpulseGraph.Native;
using Xunit;

namespace ImpulseGraph.Tests;

public class Decimal64Tests
{
    [Fact]
    public void TestAttrTypeAndVmStatusConstants()
    {
        Assert.Equal((byte)0x0F, ImpulseAttrTypes.AttrTypeBool);
        Assert.Equal((byte)0x15, ImpulseAttrTypes.AttrTypeDecimal64);
        Assert.Equal((byte)0x95, ImpulseAttrTypes.AttrTypeDecimal64Nullable);
        Assert.Equal(16, (int)ImpulseVmStatus.ArithmeticOverflow);
        Assert.Equal(17, (int)ImpulseVmStatus.DivByZero);
    }

    [Fact]
    public void TestFromDecimalToDecimalAndImplicitConversion()
    {
        var d = Decimal64.FromDecimal(123.45m, precision: 10, scale: 2);
        Assert.Equal(12345L, d.Unscaled);
        Assert.Equal((byte)10, d.Precision);
        Assert.Equal((byte)2, d.Scale);
        Assert.Equal(123.45m, d.ToDecimal());
        Assert.Equal("123.45", d.ToString());

        // Lossless implicit conversion to System.Decimal
        decimal native = d;
        Assert.Equal(123.45m, native);

        // Negative sub-unit formatting
        var neg = Decimal64.Parse("-0.0050", precision: 18, scale: 4);
        Assert.Equal(-50L, neg.Unscaled);
        Assert.Equal(-0.0050m, neg.ToDecimal());
        Assert.Equal("-0.0050", neg.ToString());
    }

    [Fact]
    public void TestHighPrecisionAboveTwoToFiftyThree()
    {
        // 9007199254740993.01 exceeds IEEE-754 double 2^53 precision
        var big = Decimal64.Parse("9007199254740993.01", precision: 18, scale: 2);
        Assert.Equal(900719925474099301L, big.Unscaled);
        Assert.Equal("9007199254740993.01", big.ToString());
        Assert.Equal(9007199254740993.01m, big.ToDecimal());

        var roundTrip = Decimal64.FromDecimal(big.ToDecimal(), precision: 18, scale: 2);
        Assert.Equal(big.Unscaled, roundTrip.Unscaled);
        Assert.Equal(big, roundTrip);

        var max18 = new Decimal64(999_999_999_999_999_999L, precision: 18, scale: 0);
        Assert.Equal(999_999_999_999_999_999m, max18.ToDecimal());
        Assert.Equal("999999999999999999", max18.ToString());

        var min18 = new Decimal64(-999_999_999_999_999_999L, precision: 18, scale: 0);
        Assert.Equal(-999_999_999_999_999_999m, min18.ToDecimal());
        Assert.Equal("-999999999999999999", min18.ToString());
    }

    [Fact]
    public void TestRoundingAndEquality()
    {
        // Half-away-from-zero rounding on Parse and FromDecimal
        Assert.Equal("100.00", Decimal64.Parse("99.995", 10, 2).ToString());
        Assert.Equal("99.99", Decimal64.Parse("99.994", 10, 2).ToString());
        Assert.Equal("-1.24", Decimal64.Parse("-1.235", 10, 2).ToString());
        Assert.Equal("-1.23", Decimal64.Parse("-1.234", 10, 2).ToString());
        Assert.Equal("100.00", Decimal64.FromDecimal(99.995m, 10, 2).ToString());
        Assert.Equal("-1.24", Decimal64.FromDecimal(-1.235m, 10, 2).ToString());

        // Cross-scale equality & ordering
        var a = Decimal64.Parse("12.50", 10, 2);
        var b = Decimal64.Parse("12.5000", 12, 4);
        var c = Decimal64.Parse("12.51", 10, 2);
        Assert.True(a == b);
        Assert.True(a.Equals(b));
        Assert.Equal(a.GetHashCode(), b.GetHashCode());
        Assert.True(a < c);
        Assert.True(c > b);
    }

    [Fact]
    public void TestValidationAndTryFromDecimal()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new Decimal64(1, 0, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new Decimal64(1, 19, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new Decimal64(1, 4, 5));
        Assert.Throws<OverflowException>(() => new Decimal64(1000, 3, 0));
        Assert.Throws<OverflowException>(() => new Decimal64(long.MinValue, 18, 0));
        Assert.Throws<OverflowException>(() => Decimal64.FromDecimal(1000m, 3, 0));
        Assert.Throws<FormatException>(() => Decimal64.Parse("", 10, 2));
        Assert.Throws<FormatException>(() => Decimal64.Parse("1.2.3", 10, 2));

        Assert.True(Decimal64.TryFromDecimal(42.50m, 10, 2, out var ok));
        Assert.Equal(4250L, ok.Unscaled);
        Assert.False(Decimal64.TryFromDecimal(1000m, 3, 0, out _));
        Assert.False(Decimal64.TryFromDecimal(1m, 0, 0, out _));
        Assert.False(Decimal64.TryFromDecimal(1m, 4, 5, out _));
    }

    [Fact]
    public void TestNullableDecimal64BufferDecoding()
    {
        // Pack 3-row nullable DECIMAL(12, 2) buffer: [500.00, null, 1250.75]
        // Validity bitmap: 1 uint64 word padded to 128 bytes; bits 0 and 2 set (0b101 = 5)
        byte[] buf = new byte[128 + 3 * 8];
        BinaryPrimitives.WriteUInt64LittleEndian(buf.AsSpan(0, 8), 0b101UL);
        BinaryPrimitives.WriteInt64LittleEndian(buf.AsSpan(128, 8), 50000L);
        BinaryPrimitives.WriteInt64LittleEndian(buf.AsSpan(136, 8), 0L);
        BinaryPrimitives.WriteInt64LittleEndian(buf.AsSpan(144, 8), 125075L);

        var decoded = Snapshot.DecodeDecimal64Buffer(
            buf,
            ImpulseAttrTypes.AttrTypeDecimal64Nullable,
            precision: 12,
            scale: 2,
            rowCount: 3);

        Assert.Equal(3, decoded.Length);
        Assert.True(decoded[0].HasValue);
        Assert.Equal(500.00m, decoded[0]!.Value.ToDecimal());
        Assert.False(decoded[1].HasValue);
        Assert.True(decoded[2].HasValue);
        Assert.Equal(1250.75m, decoded[2]!.Value.ToDecimal());

        // Non-DECIMAL64 typeCode rejected
        Assert.Throws<ArgumentException>(() =>
            Snapshot.DecodeDecimal64Buffer(buf, ImpulseAttrTypes.AttrTypeBool, 12, 2, 3));

        // Nullable with rowCount=0 and non-empty data rejected
        Assert.Throws<InvalidDataException>(() =>
            Snapshot.DecodeDecimal64Buffer(buf, ImpulseAttrTypes.AttrTypeDecimal64Nullable, 12, 2, 0));

        // Truncated payload (not a multiple of 8 bytes) rejected
        byte[] truncated = new byte[7];
        Assert.Throws<InvalidDataException>(() =>
            Snapshot.DecodeDecimal64Buffer(truncated, ImpulseAttrTypes.AttrTypeDecimal64, 12, 2, 1));
    }
}

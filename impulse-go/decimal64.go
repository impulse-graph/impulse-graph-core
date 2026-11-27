package impulse

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"math/big"
	"strconv"
	"strings"
)

var pow10Table = [19]int64{
	1,
	10,
	100,
	1_000,
	10_000,
	100_000,
	1_000_000,
	10_000_000,
	100_000_000,
	1_000_000_000,
	10_000_000_000,
	100_000_000_000,
	1_000_000_000_000,
	10_000_000_000_000,
	100_000_000_000_000,
	1_000_000_000_000_000,
	10_000_000_000_000_000,
	100_000_000_000_000_000,
	1_000_000_000_000_000_000,
}

// Decimal64 represents a fixed-point DECIMAL(Precision, Scale) value backed by
// a signed 64-bit unscaled integer (Unscaled * 10^-Scale) with 1 <= Precision <= 18
// and 0 <= Scale <= Precision.
type Decimal64 struct {
	Unscaled  int64
	Precision uint8
	Scale     uint8
}

// NullableDecimal64 represents an optionally null DECIMAL64 value (type code 0x95).
type NullableDecimal64 struct {
	Value Decimal64
	Valid bool
}

// Decimal64Meta describes the schema metadata of a DECIMAL64 attribute column.
type Decimal64Meta struct {
	Precision  uint16
	Scale      uint8
	IsNullable bool
	Dimension  uint32
}

func validateDecimal64PrecisionScale(precision, scale uint8) error {
	if precision < 1 || precision > 18 {
		return fmt.Errorf("decimal64 precision must be in [1, 18], got %d", precision)
	}
	if scale > precision {
		return fmt.Errorf("decimal64 scale must be in [0, %d], got %d", precision, scale)
	}
	return nil
}

// NewDecimal64 constructs and validates a Decimal64 from an unscaled int64 mantissa.
func NewDecimal64(unscaled int64, precision, scale uint8) (Decimal64, error) {
	if err := validateDecimal64PrecisionScale(precision, scale); err != nil {
		return Decimal64{}, err
	}
	if unscaled == math.MinInt64 {
		return Decimal64{}, fmt.Errorf("decimal64 unscaled %d exceeds DECIMAL(%d, %d) bounds", unscaled, precision, scale)
	}
	absVal := unscaled
	if absVal < 0 {
		absVal = -absVal
	}
	if absVal >= pow10Table[precision] {
		return Decimal64{}, fmt.Errorf("decimal64 unscaled %d exceeds DECIMAL(%d, %d) bounds", unscaled, precision, scale)
	}
	return Decimal64{
		Unscaled:  unscaled,
		Precision: precision,
		Scale:     scale,
	}, nil
}

// ParseDecimal64 parses a base-10 decimal string into a Decimal64 using exact integer
// arithmetic (zero floating-point drift). Fractional digits beyond scale are rounded
// half-away-from-zero.
func ParseDecimal64(str string, precision, scale uint8) (Decimal64, error) {
	if err := validateDecimal64PrecisionScale(precision, scale); err != nil {
		return Decimal64{}, err
	}
	s := strings.TrimSpace(str)
	if len(s) == 0 {
		return Decimal64{}, errors.New("empty decimal64 string")
	}

	idx := 0
	neg := false
	switch s[0] {
	case '-':
		neg = true
		idx = 1
	case '+':
		idx = 1
	}
	if idx >= len(s) {
		return Decimal64{}, fmt.Errorf("malformed decimal64 literal %q", str)
	}

	var intPart strings.Builder
	var fracPart strings.Builder
	seenDot := false
	seenDigit := false

	for ; idx < len(s); idx++ {
		c := s[idx]
		if c == '.' {
			if seenDot {
				return Decimal64{}, fmt.Errorf("multiple decimal points in decimal64 literal %q", str)
			}
			seenDot = true
		} else if c >= '0' && c <= '9' {
			seenDigit = true
			if seenDot {
				fracPart.WriteByte(c)
			} else {
				intPart.WriteByte(c)
			}
		} else {
			return Decimal64{}, fmt.Errorf("invalid character %q in decimal64 literal %q", c, str)
		}
	}
	if !seenDigit {
		return Decimal64{}, fmt.Errorf("no digits in decimal64 literal %q", str)
	}

	fracStr := fracPart.String()
	roundUp := false
	var keptFrac string
	scaleInt := int(scale)
	if len(fracStr) <= scaleInt {
		keptFrac = fracStr + strings.Repeat("0", scaleInt-len(fracStr))
	} else {
		keptFrac = fracStr[:scaleInt]
		roundUp = fracStr[scaleInt] >= '5'
	}

	limit := uint64(pow10Table[precision])
	var mag uint64
	allDigits := intPart.String() + keptFrac
	for i := 0; i < len(allDigits); i++ {
		d := uint64(allDigits[i] - '0')
		if mag > (limit-1)/10 {
			return Decimal64{}, fmt.Errorf("decimal64 literal %q overflows DECIMAL(%d, %d)", str, precision, scale)
		}
		mag = mag*10 + d
		if mag >= limit {
			return Decimal64{}, fmt.Errorf("decimal64 literal %q overflows DECIMAL(%d, %d)", str, precision, scale)
		}
	}
	if roundUp {
		mag++
		if mag >= limit {
			return Decimal64{}, fmt.Errorf("decimal64 literal %q overflows DECIMAL(%d, %d) after rounding", str, precision, scale)
		}
	}

	unscaled := int64(mag)
	if neg && mag != 0 {
		unscaled = -unscaled
	}
	return NewDecimal64(unscaled, precision, scale)
}

// String formats the Decimal64 as an exact base-10 string with Scale fractional digits
// when Scale > 0.
func (d Decimal64) String() string {
	if d.Scale == 0 {
		return strconv.FormatInt(d.Unscaled, 10)
	}
	neg := d.Unscaled < 0
	var mag uint64
	if neg {
		mag = uint64(-d.Unscaled)
	} else {
		mag = uint64(d.Unscaled)
	}
	digits := strconv.FormatUint(mag, 10)
	scaleInt := int(d.Scale)

	var b strings.Builder
	if neg {
		b.WriteByte('-')
	}
	if len(digits) <= scaleInt {
		b.WriteString("0.")
		for i := 0; i < scaleInt-len(digits); i++ {
			b.WriteByte('0')
		}
		b.WriteString(digits)
	} else {
		split := len(digits) - scaleInt
		b.WriteString(digits[:split])
		b.WriteByte('.')
		b.WriteString(digits[split:])
	}
	return b.String()
}

// Rat returns the exact rational value of this Decimal64 as a *big.Rat.
func (d Decimal64) Rat() *big.Rat {
	denom := int64(1)
	if d.Scale <= 18 {
		denom = pow10Table[d.Scale]
	}
	return big.NewRat(d.Unscaled, denom)
}

// Float64 converts this Decimal64 to an IEEE-754 float64 approximation.
func (d Decimal64) Float64() float64 {
	if d.Scale <= 18 {
		return float64(d.Unscaled) / float64(pow10Table[d.Scale])
	}
	f, _ := d.Rat().Float64()
	return f
}

// Equal reports whether d and other represent the same exact mathematical value,
// even when their Precision or Scale differ.
func (d Decimal64) Equal(other Decimal64) bool {
	return d.Rat().Cmp(other.Rat()) == 0
}

func packNullableDecimal64Bytes(values []NullableDecimal64) []byte {
	n := len(values)
	words := (n + 63) / 64
	rawBm := words * 8
	paddedBm := (rawBm + 127) &^ 127
	buf := make([]byte, paddedBm+n*8)

	for i, v := range values {
		if v.Valid {
			wordIdx := i >> 6
			bit := uint64(1) << uint(i&63)
			cur := binary.LittleEndian.Uint64(buf[wordIdx*8 : wordIdx*8+8])
			binary.LittleEndian.PutUint64(buf[wordIdx*8:wordIdx*8+8], cur|bit)
			binary.LittleEndian.PutUint64(buf[paddedBm+i*8:paddedBm+i*8+8], uint64(v.Value.Unscaled))
		}
	}
	return buf
}

func decodeDecimal64Bytes(data []byte, typeCode uint8, precision uint16, scale uint8, rowCount uint64) ([]NullableDecimal64, error) {
	if (typeCode & 0x7F) != AttrTypeDecimal64 {
		return nil, fmt.Errorf("attribute type code 0x%02x is not DECIMAL64", typeCode)
	}
	if precision < 1 || precision > 18 || uint16(scale) > precision {
		return nil, fmt.Errorf("invalid decimal64 metadata precision=%d scale=%d", precision, scale)
	}
	isNullable := (typeCode & 0x80) != 0
	var validWords []uint64
	payload := data

	if isNullable && rowCount > 0 {
		words := (rowCount + 63) / 64
		rawBm := words * 8
		paddedBm := (rawBm + 127) &^ 127
		if uint64(len(data)) < paddedBm {
			return nil, ErrBufferOverflow
		}
		validWords = make([]uint64, words)
		for w := uint64(0); w < words; w++ {
			validWords[w] = binary.LittleEndian.Uint64(data[w*8 : w*8+8])
		}
		payload = data[paddedBm:]
	}

	count := len(payload) / 8
	out := make([]NullableDecimal64, count)
	p8 := uint8(precision)
	for i := 0; i < count; i++ {
		if isNullable && validWords != nil {
			wordIdx := i >> 6
			if wordIdx < len(validWords) && ((validWords[wordIdx]>>uint(i&63))&1) == 0 {
				out[i] = NullableDecimal64{Valid: false}
				continue
			}
		}
		raw := int64(binary.LittleEndian.Uint64(payload[i*8 : i*8+8]))
		dec, err := NewDecimal64(raw, p8, scale)
		if err != nil {
			return nil, err
		}
		out[i] = NullableDecimal64{Value: dec, Valid: true}
	}
	return out, nil
}

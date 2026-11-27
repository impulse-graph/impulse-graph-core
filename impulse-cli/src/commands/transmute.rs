use crate::parquet_reader::{format_decimal64, parse_decimal64_spec};
use clap::Args;
use impulse_graph::spec::{IMPULSE_NULLABLE_FLAG, IMPULSE_TYPE_MASK};
use impulse_graph::{AttributeInfo, SnapshotReader, SnapshotWriter};
use std::collections::{HashMap, HashSet};
use std::error::Error;
use std::path::PathBuf;

#[derive(Args, Debug, Clone)]
pub struct TransmuteArgs {
    /// Path to input .imps binary snapshot file
    pub input: PathBuf,

    /// Path to output .imps binary snapshot file
    #[arg(short, long)]
    pub output: PathBuf,

    /// Automatically downcast node ID integer widths to u16/u32 where safely possible
    #[arg(long)]
    pub compact_widths: bool,

    /// Add CSC reverse index transposition to relation(s) ("all" or comma-separated relation names)
    #[arg(long, value_delimiter = ',')]
    pub add_csc: Option<Vec<String>>,

    /// Drop CSC reverse index transposition from relation(s) ("all" or comma-separated relation names)
    #[arg(long, value_delimiter = ',')]
    pub drop_csc: Option<Vec<String>>,

    /// Infer relation multiplicity (1:1, 1:M, M:1, M:N) and store in snapshot metadata
    #[arg(long)]
    pub infer_multiplicity: bool,

    /// Synthesize virtual joined relation (e.g. "CO_ACTED=ACTED_IN,ACTED_IN_REV")
    #[arg(long, value_delimiter = ',')]
    pub add_virtual_relation: Option<Vec<String>>,

    /// Rename domain (format: OldName=NewName)
    #[arg(long, value_delimiter = ',')]
    pub rename_domain: Option<Vec<String>>,

    /// Rename relation (format: OldName=NewName)
    #[arg(long, value_delimiter = ',')]
    pub rename_relation: Option<Vec<String>>,

    /// Rename attribute (format: OldName=NewName)
    #[arg(long, value_delimiter = ',')]
    pub rename_attr: Option<Vec<String>>,

    /// Drop domain by name
    #[arg(long, value_delimiter = ',')]
    pub drop_domain: Option<Vec<String>>,

    /// Drop relation by name
    #[arg(long, value_delimiter = ',')]
    pub drop_relation: Option<Vec<String>>,

    /// Drop attribute by name
    #[arg(long, value_delimiter = ',')]
    pub drop_attr: Option<Vec<String>>,

    /// Set metadata key-value pair (format: Key=Value)
    #[arg(long, value_delimiter = ',')]
    pub set_meta: Option<Vec<String>>,

    /// Delete metadata key
    #[arg(long, value_delimiter = ',')]
    pub delete_meta: Option<Vec<String>>,

    /// Clear all metadata
    #[arg(long)]
    pub clear_meta: bool,

    /// Cast attribute column type (repeatable, format: "<Domain|Rel>.<Attr>:<TargetType>", e.g. "Account.balance:decimal64(16,3)")
    #[arg(long = "cast", value_name = "SPEC")]
    pub cast: Vec<String>,

    /// Rounding mode for decimal/float casts: half-up (SQL half-away-from-zero), half-even, or trunc
    #[arg(long, default_value = "half-up", value_name = "MODE")]
    pub rounding: String,

    /// Overflow handling policy for --cast: fail (default) or null (sets validity bit 0 if target is nullable)
    #[arg(long = "on-overflow", default_value = "fail", value_name = "POLICY")]
    pub on_overflow: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RoundingMode {
    HalfUp,
    HalfEven,
    Trunc,
}

impl RoundingMode {
    pub fn parse(s: &str) -> Result<Self, String> {
        match s.trim().to_lowercase().as_str() {
            "half-up" | "half_up" | "halfup" => Ok(Self::HalfUp),
            "half-even" | "half_even" | "halfeven" | "bankers" => Ok(Self::HalfEven),
            "trunc" | "truncate" | "toward-zero" | "toward_zero" => Ok(Self::Trunc),
            other => Err(format!(
                "Invalid --rounding mode '{}': expected half-up, half-even, or trunc",
                other
            )),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OverflowPolicy {
    Fail,
    Null,
}

impl OverflowPolicy {
    pub fn parse(s: &str) -> Result<Self, String> {
        match s.trim().to_lowercase().as_str() {
            "fail" | "error" => Ok(Self::Fail),
            "null" => Ok(Self::Null),
            other => Err(format!(
                "Invalid --on-overflow policy '{}': expected fail or null",
                other
            )),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct CastTarget {
    base_type: u8,
    explicit_nullable: bool,
    precision: u16,
    scale: u8,
}

fn parse_cast_target(spec: &str) -> Result<CastTarget, String> {
    if let Some((is_nullable, precision, scale)) = parse_decimal64_spec(spec)? {
        return Ok(CastTarget {
            base_type: 0x15,
            explicit_nullable: is_nullable,
            precision,
            scale,
        });
    }
    let clean = spec.trim().to_lowercase();
    let explicit_nullable =
        clean.ends_with('?') || clean.starts_with("nullable_") || clean.starts_with("opt_");
    let name = clean
        .trim_end_matches('?')
        .trim_start_matches("nullable_")
        .trim_start_matches("opt_")
        .trim();
    let base_type: u8 = match name {
        "i8" | "int8" => 0x01,
        "i16" | "int16" => 0x02,
        "i32" | "int32" | "int" => 0x03,
        "i64" | "int64" | "long" => 0x04,
        "f32" | "float32" | "float" => 0x06,
        "f64" | "float64" | "double" => 0x07,
        "string" | "str" | "utf8" | "var_string" | "text" => 0x0B,
        "u8" | "uint8" => 0x11,
        "u16" | "uint16" => 0x12,
        "u32" | "uint32" => 0x13,
        "u64" | "uint64" => 0x14,
        other => {
            return Err(format!(
                "Unsupported --cast target type '{}': expected decimal64(P,S), i8/i16/i32/i64, u8/u16/u32/u64, f32/f64, or string",
                other
            ));
        }
    };
    Ok(CastTarget {
        base_type,
        explicit_nullable,
        precision: 0,
        scale: 0,
    })
}

fn parse_cast_specs(specs: &[String]) -> Result<HashMap<(String, String), CastTarget>, String> {
    let mut map = HashMap::new();
    for spec in specs {
        let (lhs, rhs) = spec.split_once(':').ok_or_else(|| {
            format!(
                "Invalid --cast spec '{}': expected '<Domain|Rel>.<Attr>:<TargetType>'",
                spec
            )
        })?;
        let (entity, attr) = lhs.split_once('.').ok_or_else(|| {
            format!(
                "Invalid --cast target '{}': expected '<Domain|Rel>.<Attr>'",
                lhs
            )
        })?;
        let entity = entity.trim().to_string();
        let attr = attr.trim().to_string();
        if entity.is_empty() || attr.is_empty() {
            return Err(format!(
                "Invalid --cast spec '{}': entity and attribute names must be non-empty",
                spec
            ));
        }
        let target = parse_cast_target(rhs)?;
        map.insert((entity, attr), target);
    }
    Ok(map)
}

#[inline]
fn padded_bitmap_bytes(row_count: usize) -> (usize, usize) {
    let word_count = row_count.div_ceil(64);
    let raw_bytes = word_count * 8;
    let padded = if raw_bytes == 0 {
        0
    } else {
        (raw_bytes + 127) & !127
    };
    (raw_bytes, padded)
}

#[inline]
fn div_round_i128(num: i128, den: i128, mode: RoundingMode) -> i128 {
    debug_assert!(den > 0);
    let q = num / den;
    let r = num % den;
    if r == 0 {
        return q;
    }
    let abs_r = r.unsigned_abs();
    let den_u = den as u128;
    let cmp = (abs_r * 2).cmp(&den_u);
    let bump = match mode {
        RoundingMode::Trunc => false,
        RoundingMode::HalfUp => cmp != std::cmp::Ordering::Less,
        RoundingMode::HalfEven => match cmp {
            std::cmp::Ordering::Greater => true,
            std::cmp::Ordering::Less => false,
            std::cmp::Ordering::Equal => q % 2 != 0,
        },
    };
    if bump {
        if num >= 0 {
            q + 1
        } else {
            q - 1
        }
    } else {
        q
    }
}

#[inline]
fn decimal64_max_abs(precision: u16) -> i128 {
    10i128.pow(precision as u32) - 1
}

fn check_decimal64_bounds(val: i128, precision: u16) -> Result<i64, String> {
    let max_abs = decimal64_max_abs(precision);
    if val.unsigned_abs() > max_abs as u128 {
        return Err(format!(
            "Value {} exceeds DECIMAL64({}) range [+/-{}]",
            val, precision, max_abs
        ));
    }
    i64::try_from(val).map_err(|_| format!("Value {} overflows i64", val))
}

fn rescale_decimal64(
    val: i64,
    from_scale: u8,
    to_precision: u16,
    to_scale: u8,
    rounding: RoundingMode,
) -> Result<i64, String> {
    let v128 = val as i128;
    let rescaled = if to_scale >= from_scale {
        let factor = 10i128.pow((to_scale - from_scale) as u32);
        v128.checked_mul(factor)
            .ok_or_else(|| format!("Decimal rescale overflow for {}", val))?
    } else {
        let divisor = 10i128.pow((from_scale - to_scale) as u32);
        div_round_i128(v128, divisor, rounding)
    };
    check_decimal64_bounds(rescaled, to_precision)
}

/// Exact base-10 string parser into `DECIMAL64(precision, scale)` with selectable rounding
/// (zero floating-point arithmetic).
fn parse_decimal64_str_rounded(
    s: &str,
    precision: u16,
    scale: u8,
    rounding: RoundingMode,
) -> Result<i64, String> {
    let trimmed = s.trim();
    if trimmed.is_empty() {
        return Err("Empty decimal string".to_string());
    }
    let (neg, rest) = if let Some(r) = trimmed.strip_prefix('-') {
        (true, r)
    } else if let Some(r) = trimmed.strip_prefix('+') {
        (false, r)
    } else {
        (false, trimmed)
    };
    let (int_part, frac_part) = match rest.split_once('.') {
        Some((i, f)) => {
            if f.contains('.') {
                return Err(format!("Invalid decimal string '{}'", s));
            }
            (i, f)
        }
        None => (rest, ""),
    };
    if int_part.is_empty() && frac_part.is_empty() {
        return Err(format!("Invalid decimal string '{}'", s));
    }
    if !int_part.bytes().all(|b| b.is_ascii_digit())
        || !frac_part.bytes().all(|b| b.is_ascii_digit())
    {
        return Err(format!("Invalid decimal string '{}'", s));
    }

    let scale_usize = scale as usize;
    let mut acc: i128 = 0;
    for b in int_part.bytes() {
        acc = acc
            .checked_mul(10)
            .and_then(|v| v.checked_add((b - b'0') as i128))
            .ok_or_else(|| format!("Decimal string '{}' overflows i128", s))?;
    }

    let kept_frac_len = frac_part.len().min(scale_usize);
    for b in frac_part.bytes().take(kept_frac_len) {
        acc = acc
            .checked_mul(10)
            .and_then(|v| v.checked_add((b - b'0') as i128))
            .ok_or_else(|| format!("Decimal string '{}' overflows i128", s))?;
    }
    for _ in kept_frac_len..scale_usize {
        acc = acc
            .checked_mul(10)
            .ok_or_else(|| format!("Decimal string '{}' overflows i128", s))?;
    }

    if frac_part.len() > scale_usize {
        let extra = &frac_part.as_bytes()[scale_usize..];
        let first = extra[0] - b'0';
        let has_nonzero_tail = extra[1..].iter().any(|&b| b != b'0');
        let bump = match rounding {
            RoundingMode::Trunc => false,
            RoundingMode::HalfUp => first >= 5,
            RoundingMode::HalfEven => {
                first > 5 || (first == 5 && (has_nonzero_tail || (acc & 1) == 1))
            }
        };
        if bump {
            acc = acc
                .checked_add(1)
                .ok_or_else(|| format!("Decimal string '{}' overflows i128", s))?;
        }
    }

    let signed = if neg { -acc } else { acc };
    check_decimal64_bounds(signed, precision)
}

fn round_f64_to_i128(val: f64, rounding: RoundingMode) -> Result<i128, String> {
    if !val.is_finite() {
        return Err(format!("Non-finite float {} cannot be converted", val));
    }
    let rounded = match rounding {
        RoundingMode::HalfUp => val.round(),
        RoundingMode::HalfEven => val.round_ties_even(),
        RoundingMode::Trunc => val.trunc(),
    };
    if !rounded.is_finite() || rounded < (i128::MIN as f64) || rounded >= (i128::MAX as f64) {
        return Err(format!("Float {} overflows integer range", val));
    }
    Ok(rounded as i128)
}

#[derive(Debug, Clone)]
enum SrcValue<'a> {
    Int(i128),
    Float(f64),
    Decimal(i64, u8),
    Str(&'a str),
}

enum CastOutcome<T> {
    Value(T),
    Null,
}

fn convert_to_decimal64(
    src: &SrcValue<'_>,
    precision: u16,
    scale: u8,
    rounding: RoundingMode,
    target_nullable: bool,
    on_overflow: OverflowPolicy,
) -> Result<CastOutcome<i64>, String> {
    match *src {
        SrcValue::Decimal(v, src_scale) => {
            match rescale_decimal64(v, src_scale, precision, scale, rounding) {
                Ok(out) => Ok(CastOutcome::Value(out)),
                Err(e) => handle_overflow(e, target_nullable, on_overflow),
            }
        }
        SrcValue::Int(v) => {
            let factor = 10i128.pow(scale as u32);
            let res = v
                .checked_mul(factor)
                .ok_or_else(|| {
                    format!(
                        "Integer {} overflows DECIMAL64({}, {})",
                        v, precision, scale
                    )
                })
                .and_then(|scaled| check_decimal64_bounds(scaled, precision));
            match res {
                Ok(out) => Ok(CastOutcome::Value(out)),
                Err(e) => handle_overflow(e, target_nullable, on_overflow),
            }
        }
        SrcValue::Float(f) => {
            if f.is_nan() {
                if target_nullable {
                    return Ok(CastOutcome::Null);
                }
                return Err("Cannot cast NaN to non-nullable DECIMAL64".to_string());
            }
            if !f.is_finite() {
                return handle_overflow(
                    format!(
                        "Non-finite float {} overflows DECIMAL64({}, {})",
                        f, precision, scale
                    ),
                    target_nullable,
                    on_overflow,
                );
            }
            let scaled = f * 10f64.powi(scale as i32);
            let res = round_f64_to_i128(scaled, rounding)
                .and_then(|v| check_decimal64_bounds(v, precision));
            match res {
                Ok(out) => Ok(CastOutcome::Value(out)),
                Err(e) => handle_overflow(e, target_nullable, on_overflow),
            }
        }
        SrcValue::Str(s) => match parse_decimal64_str_rounded(s, precision, scale, rounding) {
            Ok(out) => Ok(CastOutcome::Value(out)),
            Err(e) => handle_overflow(e, target_nullable, on_overflow),
        },
    }
}

fn convert_to_i128_bounded(
    src: &SrcValue<'_>,
    min_val: i128,
    max_val: i128,
    rounding: RoundingMode,
    target_nullable: bool,
    on_overflow: OverflowPolicy,
) -> Result<CastOutcome<i128>, String> {
    let val_res: Result<i128, String> = match *src {
        SrcValue::Int(v) => Ok(v),
        SrcValue::Decimal(v, src_scale) => {
            let den = 10i128.pow(src_scale as u32);
            Ok(div_round_i128(v as i128, den, rounding))
        }
        SrcValue::Float(f) => {
            if f.is_nan() {
                if target_nullable {
                    return Ok(CastOutcome::Null);
                }
                return Err("Cannot cast NaN to non-nullable integer".to_string());
            }
            round_f64_to_i128(f, rounding)
        }
        SrcValue::Str(s) => {
            let t = s.trim();
            t.parse::<i128>()
                .or_else(|_| parse_decimal64_str_rounded(t, 18, 0, rounding).map(|v| v as i128))
                .map_err(|_| format!("Cannot parse '{}' as integer", s))
        }
    };
    match val_res {
        Ok(v) if v >= min_val && v <= max_val => Ok(CastOutcome::Value(v)),
        Ok(v) => handle_overflow(
            format!("Value {} out of range [{}..={}]", v, min_val, max_val),
            target_nullable,
            on_overflow,
        ),
        Err(e) => handle_overflow(e, target_nullable, on_overflow),
    }
}

fn convert_to_f64(
    src: &SrcValue<'_>,
    target_nullable: bool,
    on_overflow: OverflowPolicy,
) -> Result<CastOutcome<f64>, String> {
    match *src {
        SrcValue::Int(v) => Ok(CastOutcome::Value(v as f64)),
        SrcValue::Float(f) => Ok(CastOutcome::Value(f)),
        SrcValue::Decimal(v, s) => {
            let formatted = format_decimal64(v, s);
            let f = formatted
                .parse::<f64>()
                .map_err(|e| format!("Failed to convert decimal '{}' to f64: {}", formatted, e))?;
            Ok(CastOutcome::Value(f))
        }
        SrcValue::Str(s) => match s.trim().parse::<f64>() {
            Ok(f) => Ok(CastOutcome::Value(f)),
            Err(e) => handle_overflow(
                format!("Cannot parse '{}' as f64: {}", s, e),
                target_nullable,
                on_overflow,
            ),
        },
    }
}

fn convert_to_string(src: &SrcValue<'_>) -> String {
    match *src {
        SrcValue::Int(v) => v.to_string(),
        SrcValue::Float(f) => f.to_string(),
        SrcValue::Decimal(v, s) => format_decimal64(v, s),
        SrcValue::Str(s) => s.to_string(),
    }
}

fn handle_overflow<T>(
    err: String,
    target_nullable: bool,
    on_overflow: OverflowPolicy,
) -> Result<CastOutcome<T>, String> {
    match on_overflow {
        OverflowPolicy::Fail => Err(err),
        OverflowPolicy::Null => {
            if target_nullable {
                Ok(CastOutcome::Null)
            } else {
                Err(format!(
                    "{} (--on-overflow null requires a nullable target column)",
                    err
                ))
            }
        }
    }
}

struct CastedAttribute {
    type_code: u8,
    dimension: u32,
    precision: u16,
    scale: u8,
    data: Vec<u8>,
    offsets: Option<Vec<u32>>,
}

#[allow(clippy::too_many_arguments)]
fn cast_attribute_column(
    attr: &AttributeInfo,
    raw_data: &[u8],
    raw_offsets: Option<&[u32]>,
    row_count: usize,
    target: &CastTarget,
    rounding: RoundingMode,
    on_overflow: OverflowPolicy,
) -> Result<CastedAttribute, Box<dyn Error>> {
    if attr.dimension != 1 {
        return Err(format!(
            "Cannot --cast multi-dimensional attribute '{}' (dimension={}); only scalar attributes (dimension=1) are supported",
            attr.name, attr.dimension
        )
        .into());
    }

    let src_nullable = (attr.type_code & IMPULSE_NULLABLE_FLAG) != 0;
    let src_base = attr.type_code & IMPULSE_TYPE_MASK;
    let target_nullable = src_nullable || target.explicit_nullable;
    let target_type_code = if target_nullable {
        target.base_type | IMPULSE_NULLABLE_FLAG
    } else {
        target.base_type
    };

    let (src_bm, src_payload) = if src_nullable {
        let (bm_len, padded_len) = padded_bitmap_bytes(row_count);
        if raw_data.len() < padded_len {
            return Err(format!(
                "Attribute '{}' nullable bitmap truncated ({} < {})",
                attr.name,
                raw_data.len(),
                padded_len
            )
            .into());
        }
        (&raw_data[..bm_len], &raw_data[padded_len..])
    } else {
        (&[][..], raw_data)
    };

    let (dst_bm_raw_len, dst_bm_padded_len) = if target_nullable {
        padded_bitmap_bytes(row_count)
    } else {
        (0, 0)
    };
    let mut dst_bm = vec![0u8; dst_bm_padded_len];

    let mut out_payload: Vec<u8> = Vec::new();
    let mut out_offsets: Option<Vec<u32>> = if target.base_type == 0x0B {
        let mut offs = Vec::with_capacity(row_count + 1);
        offs.push(0u32);
        Some(offs)
    } else {
        None
    };

    for row in 0..row_count {
        let is_src_null = src_nullable && {
            let byte_idx = row / 8;
            let bit_idx = row % 8;
            byte_idx < src_bm.len() && (src_bm[byte_idx] & (1u8 << bit_idx)) == 0
        };

        if is_src_null {
            // Validity bit remains 0 in dst_bm; emit zero placeholder
            push_zero_element(target.base_type, &mut out_payload, &mut out_offsets);
            continue;
        }

        let src_val = read_src_value(src_base, attr.scale, src_payload, raw_offsets, row)?;

        let mut is_valid = true;
        match target.base_type {
            0x15 => match convert_to_decimal64(
                &src_val,
                target.precision,
                target.scale,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&v.to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0i64.to_le_bytes());
                }
            },
            0x01 => match convert_to_i128_bounded(
                &src_val,
                i8::MIN as i128,
                i8::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.push((v as i8) as u8),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.push(0);
                }
            },
            0x02 => match convert_to_i128_bounded(
                &src_val,
                i16::MIN as i128,
                i16::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&(v as i16).to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0i16.to_le_bytes());
                }
            },
            0x03 => match convert_to_i128_bounded(
                &src_val,
                i32::MIN as i128,
                i32::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&(v as i32).to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0i32.to_le_bytes());
                }
            },
            0x04 => match convert_to_i128_bounded(
                &src_val,
                i64::MIN as i128,
                i64::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&(v as i64).to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0i64.to_le_bytes());
                }
            },
            0x11 => match convert_to_i128_bounded(
                &src_val,
                u8::MIN as i128,
                u8::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.push(v as u8),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.push(0);
                }
            },
            0x12 => match convert_to_i128_bounded(
                &src_val,
                u16::MIN as i128,
                u16::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&(v as u16).to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0u16.to_le_bytes());
                }
            },
            0x13 => match convert_to_i128_bounded(
                &src_val,
                u32::MIN as i128,
                u32::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&(v as u32).to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0u32.to_le_bytes());
                }
            },
            0x14 => match convert_to_i128_bounded(
                &src_val,
                u64::MIN as i128,
                u64::MAX as i128,
                rounding,
                target_nullable,
                on_overflow,
            )? {
                CastOutcome::Value(v) => out_payload.extend_from_slice(&(v as u64).to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0u64.to_le_bytes());
                }
            },
            0x06 => match convert_to_f64(&src_val, target_nullable, on_overflow)? {
                CastOutcome::Value(f) => {
                    let f32_val = f as f32;
                    if f.is_finite() && !f32_val.is_finite() {
                        match handle_overflow::<f32>(
                            format!("Value {} overflows f32", f),
                            target_nullable,
                            on_overflow,
                        )? {
                            CastOutcome::Value(v) => {
                                out_payload.extend_from_slice(&v.to_le_bytes())
                            }
                            CastOutcome::Null => {
                                is_valid = false;
                                out_payload.extend_from_slice(&0.0f32.to_le_bytes());
                            }
                        }
                    } else {
                        out_payload.extend_from_slice(&f32_val.to_le_bytes());
                    }
                }
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0.0f32.to_le_bytes());
                }
            },
            0x07 => match convert_to_f64(&src_val, target_nullable, on_overflow)? {
                CastOutcome::Value(f) => out_payload.extend_from_slice(&f.to_le_bytes()),
                CastOutcome::Null => {
                    is_valid = false;
                    out_payload.extend_from_slice(&0.0f64.to_le_bytes());
                }
            },
            0x0B => {
                let s = convert_to_string(&src_val);
                out_payload.extend_from_slice(s.as_bytes());
                if let Some(offs) = out_offsets.as_mut() {
                    offs.push(out_payload.len() as u32);
                }
            }
            other => {
                return Err(format!("Unsupported cast target base type 0x{:02X}", other).into());
            }
        }

        if target_nullable && is_valid {
            let byte_idx = row / 8;
            let bit_idx = row % 8;
            if byte_idx < dst_bm_raw_len {
                dst_bm[byte_idx] |= 1u8 << bit_idx;
            }
        }
    }

    let final_data = if target_nullable {
        dst_bm.extend_from_slice(&out_payload);
        dst_bm
    } else {
        out_payload
    };

    Ok(CastedAttribute {
        type_code: target_type_code,
        dimension: 1,
        precision: target.precision,
        scale: target.scale,
        data: final_data,
        offsets: out_offsets,
    })
}

fn push_zero_element(base_type: u8, payload: &mut Vec<u8>, offsets: &mut Option<Vec<u32>>) {
    match base_type {
        0x01 | 0x11 => payload.push(0),
        0x02 | 0x12 => payload.extend_from_slice(&[0u8; 2]),
        0x03 | 0x06 | 0x13 => payload.extend_from_slice(&[0u8; 4]),
        0x04 | 0x07 | 0x14 | 0x15 => payload.extend_from_slice(&[0u8; 8]),
        0x0B => {
            if let Some(offs) = offsets.as_mut() {
                offs.push(payload.len() as u32);
            }
        }
        _ => {}
    }
}

fn read_src_value<'a>(
    src_base: u8,
    src_scale: u8,
    payload: &'a [u8],
    offsets: Option<&[u32]>,
    row: usize,
) -> Result<SrcValue<'a>, Box<dyn Error>> {
    match src_base {
        0x01 => {
            if row >= payload.len() {
                return Err("Source INT8 attribute truncated".into());
            }
            Ok(SrcValue::Int((payload[row] as i8) as i128))
        }
        0x02 => {
            let off = row * 2;
            let v = i16::from_le_bytes(payload[off..off + 2].try_into()?);
            Ok(SrcValue::Int(v as i128))
        }
        0x03 => {
            let off = row * 4;
            let v = i32::from_le_bytes(payload[off..off + 4].try_into()?);
            Ok(SrcValue::Int(v as i128))
        }
        0x04 | 0x08 | 0x09 => {
            let off = row * 8;
            let v = i64::from_le_bytes(payload[off..off + 8].try_into()?);
            Ok(SrcValue::Int(v as i128))
        }
        0x11 => {
            if row >= payload.len() {
                return Err("Source UINT8 attribute truncated".into());
            }
            Ok(SrcValue::Int(payload[row] as i128))
        }
        0x12 => {
            let off = row * 2;
            let v = u16::from_le_bytes(payload[off..off + 2].try_into()?);
            Ok(SrcValue::Int(v as i128))
        }
        0x13 => {
            let off = row * 4;
            let v = u32::from_le_bytes(payload[off..off + 4].try_into()?);
            Ok(SrcValue::Int(v as i128))
        }
        0x14 => {
            let off = row * 8;
            let v = u64::from_le_bytes(payload[off..off + 8].try_into()?);
            Ok(SrcValue::Int(v as i128))
        }
        0x06 => {
            let off = row * 4;
            let v = f32::from_le_bytes(payload[off..off + 4].try_into()?);
            Ok(SrcValue::Float(v as f64))
        }
        0x07 => {
            let off = row * 8;
            let v = f64::from_le_bytes(payload[off..off + 8].try_into()?);
            Ok(SrcValue::Float(v))
        }
        0x15 => {
            let off = row * 8;
            let v = i64::from_le_bytes(payload[off..off + 8].try_into()?);
            Ok(SrcValue::Decimal(v, src_scale))
        }
        0x0B => {
            let offs = offsets.ok_or("Source VAR_STRING attribute missing offsets array")?;
            if row + 1 >= offs.len() {
                return Err("Source VAR_STRING offsets truncated".into());
            }
            let s = offs[row] as usize;
            let e = offs[row + 1] as usize;
            if s > e || e > payload.len() {
                return Err("Source VAR_STRING offset out of bounds".into());
            }
            let text = std::str::from_utf8(&payload[s..e])?.trim_end_matches('\0');
            Ok(SrcValue::Str(text))
        }
        other => Err(format!(
            "Unsupported source attribute type 0x{:02X} for --cast",
            other
        )
        .into()),
    }
}

pub fn run(args: &TransmuteArgs) -> Result<(), Box<dyn Error>> {
    println!(
        "Transmuting snapshot: {} -> {}",
        args.input.display(),
        args.output.display()
    );

    let reader = SnapshotReader::open(&args.input)?;
    let output_str = args
        .output
        .to_str()
        .ok_or("Output path is not valid UTF-8")?;
    let mut writer = SnapshotWriter::new(output_str);

    let rounding = RoundingMode::parse(&args.rounding)?;
    let on_overflow = OverflowPolicy::parse(&args.on_overflow)?;
    let cast_specs = parse_cast_specs(&args.cast)?;
    let mut matched_casts: HashSet<(String, String)> = HashSet::new();

    // 1. Parse renames and drops
    let mut domain_renames: HashMap<String, String> = HashMap::new();
    if let Some(list) = &args.rename_domain {
        for item in list {
            if let Some((k, v)) = item.split_once('=') {
                domain_renames.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    let mut rel_renames: HashMap<String, String> = HashMap::new();
    if let Some(list) = &args.rename_relation {
        for item in list {
            if let Some((k, v)) = item.split_once('=') {
                rel_renames.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    let mut attr_renames: HashMap<String, String> = HashMap::new();
    if let Some(list) = &args.rename_attr {
        for item in list {
            if let Some((k, v)) = item.split_once('=') {
                attr_renames.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    let drop_domains: HashSet<String> = args
        .drop_domain
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let drop_relations: HashSet<String> = args
        .drop_relation
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let drop_attrs: HashSet<String> = args
        .drop_attr
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let add_csc_set: HashSet<String> = args
        .add_csc
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let drop_csc_set: HashSet<String> = args
        .drop_csc
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let add_all_csc = add_csc_set.contains("all");
    let drop_all_csc = drop_csc_set.contains("all");

    // 2. Transfer Domains & Inline Node Attributes (Spec v0.10.0)
    let mut domain_id_map: HashMap<u16, u16> = HashMap::new();
    let mut next_new_domain_id: u16 = 0;

    for (d_idx, d) in reader.domains().iter().enumerate() {
        if drop_domains.contains(&d.name) {
            println!("  [DROP] Domain '{}' (ID={})", d.name, d.domain_id);
            continue;
        }
        let final_name = domain_renames
            .get(&d.name)
            .cloned()
            .unwrap_or_else(|| d.name.clone());
        let new_id = next_new_domain_id;
        next_new_domain_id += 1;
        domain_id_map.insert(d.domain_id, new_id);

        writer.add_domain(new_id, d.key_type, &final_name);
        writer.set_domain_node_count(new_id, d.node_count);

        let mut new_key_attr_idx: Option<u16> = None;
        let mut kept_attr_idx: u16 = 0;
        for (a_idx, a) in d.attributes.iter().enumerate() {
            if drop_attrs.contains(&a.name) {
                println!("  [DROP] Domain Attribute '{}.{}'", final_name, a.name);
                continue;
            }
            let final_attr_name = attr_renames
                .get(&a.name)
                .cloned()
                .unwrap_or_else(|| a.name.clone());
            let raw_data = reader.get_domain_attribute_by_index(d_idx, a_idx)?;
            let raw_offsets = reader.get_domain_attribute_offsets_by_index(d_idx, a_idx)?;

            let cast_key = if cast_specs.contains_key(&(d.name.clone(), a.name.clone())) {
                Some((d.name.clone(), a.name.clone()))
            } else if cast_specs.contains_key(&(final_name.clone(), final_attr_name.clone())) {
                Some((final_name.clone(), final_attr_name.clone()))
            } else {
                None
            };

            if let Some(key) = cast_key {
                let target = &cast_specs[&key];
                matched_casts.insert(key);
                let casted = cast_attribute_column(
                    a,
                    raw_data,
                    raw_offsets,
                    d.node_count as usize,
                    target,
                    rounding,
                    on_overflow,
                )?;
                println!(
                    "  [CAST] Domain Attribute '{}.{}': 0x{:02X} -> 0x{:02X} (P={}, S={})",
                    final_name,
                    final_attr_name,
                    a.type_code,
                    casted.type_code,
                    casted.precision,
                    casted.scale
                );
                writer.add_attribute_to_domain_with_meta(
                    new_id,
                    &final_attr_name,
                    casted.type_code,
                    casted.dimension,
                    casted.precision,
                    casted.scale,
                    casted.data,
                    casted.offsets,
                );
            } else {
                writer.add_attribute_to_domain_with_meta(
                    new_id,
                    &final_attr_name,
                    a.type_code,
                    a.dimension,
                    a.precision,
                    a.scale,
                    raw_data.to_vec(),
                    raw_offsets.map(|s| s.to_vec()),
                );
            }
            if d.key_attr_index == a_idx as u16 {
                new_key_attr_idx = Some(kept_attr_idx);
            }
            kept_attr_idx += 1;
        }
        if let Some(ka) = new_key_attr_idx {
            writer.set_domain_key_attr(new_id, ka);
        }

        println!(
            "  [KEEP] Domain '{}' -> '{}' (ID: {} -> {}, Nodes: {})",
            d.name, final_name, d.domain_id, new_id, d.node_count
        );
    }

    // 3. Metadata processing
    let mut metadata = if args.clear_meta {
        HashMap::new()
    } else {
        reader.get_metadata().unwrap_or_default()
    };

    if let Some(dels) = &args.delete_meta {
        for k in dels {
            metadata.remove(k.trim());
        }
    }

    if let Some(sets) = &args.set_meta {
        for item in sets {
            if let Some((k, v)) = item.split_once('=') {
                metadata.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    // 4. Transfer Relations
    let mut kept_relations: Vec<(usize, &impulse_graph::RelationInfo)> = Vec::new();
    for (r_idx, r) in reader.relations().iter().enumerate() {
        if drop_relations.contains(&r.name) {
            println!("  [DROP] Relation '{}'", r.name);
            continue;
        }
        if !domain_id_map.contains_key(&r.src_domain_id)
            || !domain_id_map.contains_key(&r.tgt_domain_id)
        {
            println!("  [DROP] Relation '{}' (references dropped domain)", r.name);
            continue;
        }
        kept_relations.push((r_idx, r));
    }

    let mut old_to_new_rel_id: HashMap<u16, u16> = HashMap::new();
    let mut next_new_rel_idx = 0;
    for (r_idx, r) in &kept_relations {
        let new_src = domain_id_map[&r.src_domain_id];
        let new_tgt = domain_id_map[&r.tgt_domain_id];
        let final_name = rel_renames
            .get(&r.name)
            .cloned()
            .unwrap_or_else(|| r.name.clone());

        if r.is_virtual() {
            let remapped_comps: Vec<u16> = r
                .virtual_component_ids
                .iter()
                .filter_map(|cid| old_to_new_rel_id.get(cid).copied())
                .collect();
            if !remapped_comps.is_empty() {
                writer.add_virtual_relation(
                    new_src,
                    new_tgt,
                    r.node_count,
                    &final_name,
                    remapped_comps,
                );
                old_to_new_rel_id.insert(r.relation_id, next_new_rel_idx as u16);
                next_new_rel_idx += 1;
            }
            continue;
        }

        let (row_offsets, col_indices, coo_srcs) =
            crate::commands::index::extract_relation_csr_and_coo(&reader, *r_idx, r)?;

        // Determine CSC inclusion
        let mut include_csc = r.has_csc();
        if add_all_csc || add_csc_set.contains(&r.name) {
            include_csc = true;
        }
        if drop_all_csc || drop_csc_set.contains(&r.name) {
            include_csc = false;
        }

        let mut card_bits = r.section_features & impulse_graph::spec::IMP_REL_CARDINALITY_MASK;

        // Multiplicity inference
        if args.infer_multiplicity && r.edge_count > 0 {
            let mut max_out = 0usize;
            for i in 0..row_offsets.len().saturating_sub(1) {
                let deg = (row_offsets[i + 1] - row_offsets[i]) as usize;
                if deg > max_out {
                    max_out = deg;
                }
            }
            let mut in_degrees: HashMap<u32, usize> = HashMap::new();
            for &c in &col_indices {
                *in_degrees.entry(c).or_insert(0) += 1;
            }
            let max_in = in_degrees.values().copied().max().unwrap_or(0);
            let (mult, inferred_bits) = match (max_out <= 1, max_in <= 1) {
                (true, true) => ("1:1", impulse_graph::spec::IMP_REL_CARDINALITY_1_1),
                (false, true) => ("1:M", impulse_graph::spec::IMP_REL_CARDINALITY_1_M),
                (true, false) => ("M:1", impulse_graph::spec::IMP_REL_CARDINALITY_M_1),
                (false, false) => ("M:N", impulse_graph::spec::IMP_REL_CARDINALITY_M_N),
            };
            card_bits = inferred_bits;
            metadata.insert(format!("multiplicity.{}", final_name), mult.to_string());
            println!(
                "  [MULTIPLICITY] Relation '{}': {} (max_out={}, max_in={})",
                final_name, mult, max_out, max_in
            );
        }

        // Add relation
        writer.add_relation(
            new_src,
            new_tgt,
            r.node_count,
            r.edge_count,
            row_offsets,
            col_indices.clone(),
        );
        writer.set_relation_name(next_new_rel_idx, &final_name);
        writer.set_relation_include_csc(next_new_rel_idx, include_csc);
        writer.set_relation_encoding(next_new_rel_idx, r.encoding_id);
        writer.set_relation_section_features(next_new_rel_idx, card_bits);
        if let Some(srcs) = coo_srcs {
            writer.set_relation_coo_sources(next_new_rel_idx, srcs);
        }

        let tgt_node_count = reader
            .domains()
            .iter()
            .find(|d| d.domain_id == r.tgt_domain_id)
            .map(|d| d.node_count)
            .unwrap_or(0);

        // Compact widths if requested (leave 0xFFFF free as sentinel for flat arrays)
        if args.compact_widths {
            let max_col = col_indices.iter().copied().max().unwrap_or(0);
            if max_col < 65535 && tgt_node_count <= 65535 && r.node_count <= 65535 {
                writer.set_relation_widths(next_new_rel_idx, 2, 4);
                println!(
                    "  [COMPACT] Relation '{}': downcasted node_id_width -> u16",
                    final_name
                );
            } else {
                writer.set_relation_widths(next_new_rel_idx, 4, 4);
            }
        } else {
            writer.set_relation_widths(next_new_rel_idx, r.node_id_width, r.edge_index_width);
        }

        // Copy / filter attributes
        for (a_idx, a) in r.attributes.iter().enumerate() {
            if drop_attrs.contains(&a.name) {
                println!("  [DROP] Attribute '{}.{}'", final_name, a.name);
                continue;
            }
            let final_attr_name = attr_renames
                .get(&a.name)
                .cloned()
                .unwrap_or_else(|| a.name.clone());
            let raw_data = reader.get_attribute_data(*r_idx, a_idx)?;
            let raw_offsets = reader.get_attribute_offsets(*r_idx, a_idx)?;

            let cast_key = if cast_specs.contains_key(&(r.name.clone(), a.name.clone())) {
                Some((r.name.clone(), a.name.clone()))
            } else if cast_specs.contains_key(&(final_name.clone(), final_attr_name.clone())) {
                Some((final_name.clone(), final_attr_name.clone()))
            } else {
                None
            };

            if let Some(key) = cast_key {
                let target = &cast_specs[&key];
                matched_casts.insert(key);
                let casted = cast_attribute_column(
                    a,
                    raw_data,
                    raw_offsets,
                    r.edge_count as usize,
                    target,
                    rounding,
                    on_overflow,
                )?;
                println!(
                    "  [CAST] Relation Attribute '{}.{}': 0x{:02X} -> 0x{:02X} (P={}, S={})",
                    final_name,
                    final_attr_name,
                    a.type_code,
                    casted.type_code,
                    casted.precision,
                    casted.scale
                );
                writer.add_attribute_to_relation_with_meta(
                    next_new_rel_idx,
                    &final_attr_name,
                    casted.type_code,
                    casted.dimension,
                    casted.precision,
                    casted.scale,
                    casted.data,
                    casted.offsets,
                );
            } else {
                writer.add_attribute_to_relation_with_meta(
                    next_new_rel_idx,
                    &final_attr_name,
                    a.type_code,
                    a.dimension,
                    a.precision,
                    a.scale,
                    raw_data.to_vec(),
                    raw_offsets.map(|s| s.to_vec()),
                );
            }
        }

        old_to_new_rel_id.insert(r.relation_id, next_new_rel_idx as u16);
        println!(
            "  [KEEP] Relation '{}' -> '{}' (E={}, CSC={})",
            r.name, final_name, r.edge_count, include_csc
        );
        next_new_rel_idx += 1;
    }

    // 5. Synthesize Virtual Joined Relations (A o B)
    if let Some(v_rels) = &args.add_virtual_relation {
        for v_spec in v_rels {
            if let Some((v_name, comp_spec)) = v_spec.split_once('=') {
                if let Some((rel_a_name, rel_b_name)) = comp_spec.split_once(',') {
                    let v_name = v_name.trim();
                    let rel_a_name = rel_a_name.trim();
                    let rel_b_name = rel_b_name.trim();

                    let find_a = kept_relations.iter().find(|(_, r)| {
                        rel_renames
                            .get(&r.name)
                            .map(|s| s.as_str())
                            .unwrap_or(&r.name)
                            == rel_a_name
                    });
                    let find_b = kept_relations.iter().find(|(_, r)| {
                        rel_renames
                            .get(&r.name)
                            .map(|s| s.as_str())
                            .unwrap_or(&r.name)
                            == rel_b_name
                    });

                    if let (Some((idx_a, rel_a)), Some((idx_b, rel_b))) = (find_a, find_b) {
                        if rel_a.tgt_domain_id != rel_b.src_domain_id {
                            return Err(format!(
                                "Cannot synthesize virtual relation '{} o {}': domain mismatch (tgt_domain {} != src_domain {})",
                                rel_a_name, rel_b_name, rel_a.tgt_domain_id, rel_b.src_domain_id
                            ).into());
                        }
                        let rows_a = reader.get_row_offsets(*idx_a)?;
                        let cols_a = reader.get_col_indices_as_u32_vec(*idx_a)?;
                        let rows_b = reader.get_row_offsets(*idx_b)?;
                        let cols_b = reader.get_col_indices_as_u32_vec(*idx_b)?;

                        let src_nodes =
                            (rel_a.node_count as usize).min(rows_a.len().saturating_sub(1));
                        let mut new_row_offsets = Vec::with_capacity(src_nodes + 1);
                        let mut new_col_indices = Vec::new();
                        new_row_offsets.push(0u32);

                        for u in 0..src_nodes {
                            let mut neighbors = HashSet::new();
                            let start_a = (rows_a[u] as usize).min(cols_a.len());
                            let end_a = (rows_a[u + 1] as usize).min(cols_a.len());
                            if start_a <= end_a {
                                for &v in &cols_a[start_a..end_a] {
                                    let v_idx = v as usize;
                                    if v_idx + 1 < rows_b.len() {
                                        let start_b = (rows_b[v_idx] as usize).min(cols_b.len());
                                        let end_b = (rows_b[v_idx + 1] as usize).min(cols_b.len());
                                        if start_b <= end_b {
                                            for &w in &cols_b[start_b..end_b] {
                                                neighbors.insert(w);
                                            }
                                        }
                                    }
                                }
                            }
                            let mut sorted_nb: Vec<u32> = neighbors.into_iter().collect();
                            sorted_nb.sort_unstable();
                            new_col_indices.extend(sorted_nb);
                            new_row_offsets.push(new_col_indices.len() as u32);
                        }

                        let total_edges = new_col_indices.len() as u64;
                        let new_src = domain_id_map[&rel_a.src_domain_id];
                        let new_tgt = domain_id_map[&rel_b.tgt_domain_id];

                        writer.add_relation(
                            new_src,
                            new_tgt,
                            rel_a.node_count,
                            total_edges,
                            new_row_offsets,
                            new_col_indices,
                        );
                        writer.set_relation_name(next_new_rel_idx, v_name);
                        writer.set_relation_widths(next_new_rel_idx, 4, 4);
                        println!(
                            "  [VIRTUAL] Synthesized relation '{}' = '{}' o '{}' (E={})",
                            v_name, rel_a_name, rel_b_name, total_edges
                        );
                        next_new_rel_idx += 1;
                    } else {
                        eprintln!(
                            "  [WARN] Could not find relations '{}' or '{}' for virtual join",
                            rel_a_name, rel_b_name
                        );
                    }
                }
            }
        }
    }

    // Verify all --cast specs matched an existing attribute
    for (entity, attr) in cast_specs.keys() {
        if !matched_casts.contains(&(entity.clone(), attr.clone())) {
            return Err(format!(
                "--cast target '{}.{}' was not found in snapshot domains or relations",
                entity, attr
            )
            .into());
        }
    }

    // 6. Set Final Metadata & Finalize Snapshot
    writer.set_metadata(metadata);
    writer.finalize()?;

    println!("Transmutation complete: {}", args.output.display());
    Ok(())
}

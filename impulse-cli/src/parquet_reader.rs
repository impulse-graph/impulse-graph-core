use arrow_array::{
    Array, BinaryArray, BooleanArray, Date32Array, Date64Array, Decimal128Array, Decimal64Array,
    FixedSizeBinaryArray, FixedSizeListArray, Float16Array, Float32Array, Float64Array, Int16Array,
    Int32Array, Int64Array, Int8Array, LargeBinaryArray, LargeStringArray, StringArray,
    TimestampMicrosecondArray, TimestampMillisecondArray, TimestampNanosecondArray,
    TimestampSecondArray, UInt16Array, UInt32Array, UInt64Array, UInt8Array,
};
use parquet::arrow::arrow_reader::ParquetRecordBatchReaderBuilder;
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct NativeColumn {
    pub name: String,
    pub type_code: u8,
    pub dimension: u32,
    pub precision: u16,
    pub scale: u8,
    pub data: Vec<u8>,
    pub offsets: Option<Vec<u32>>,
    pub null_count: usize,
    pub row_count: usize,
}

/// Parses a `decimal64(P, S)` / `decimal(P, S)` / `dec64(P, S)` type spec (with optional
/// `?` / `nullable_` / `opt_` nullability marker) and validates `1 <= P <= 18 && S <= P`.
/// Returns `Ok(None)` if `s` is not a decimal type spec, `Ok(Some((is_nullable, precision, scale)))`
/// if valid, or `Err(...)` if it is a decimal spec with invalid `(precision, scale)`.
pub fn parse_decimal64_spec(s: &str) -> Result<Option<(bool, u16, u8)>, String> {
    let s_clean = s.trim().to_lowercase();
    let is_nullable =
        s_clean.ends_with('?') || s_clean.starts_with("nullable_") || s_clean.starts_with("opt_");
    let name = s_clean
        .trim_end_matches('?')
        .trim_start_matches("nullable_")
        .trim_start_matches("opt_")
        .trim();

    let inner = if let Some(rest) = name.strip_prefix("decimal64") {
        rest
    } else if let Some(rest) = name.strip_prefix("decimal") {
        rest
    } else if let Some(rest) = name.strip_prefix("dec64") {
        rest
    } else {
        return Ok(None);
    };

    let inner = inner.trim();
    if inner.is_empty() {
        return Ok(Some((is_nullable, 18, 0)));
    }
    let body = inner
        .strip_prefix('(')
        .and_then(|r| r.strip_suffix(')'))
        .ok_or_else(|| {
            format!(
                "Invalid DECIMAL64 type spec '{}': expected decimal64(P, S)",
                s
            )
        })?;
    let parts: Vec<&str> = body.split(',').map(|p| p.trim()).collect();
    if parts.len() != 2 {
        return Err(format!(
            "Invalid DECIMAL64 type spec '{}': expected 2 arguments (precision, scale)",
            s
        ));
    }
    let precision: u16 = parts[0]
        .parse()
        .map_err(|_| format!("Invalid DECIMAL64 precision '{}' in '{}'", parts[0], s))?;
    let scale: u8 = parts[1]
        .parse()
        .map_err(|_| format!("Invalid DECIMAL64 scale '{}' in '{}'", parts[1], s))?;
    if !(1..=18).contains(&precision) || (scale as u16) > precision {
        return Err(format!(
            "Invalid DECIMAL64({}, {}): require 1 <= precision <= 18 and 0 <= scale <= precision",
            precision, scale
        ));
    }
    Ok(Some((is_nullable, precision, scale)))
}

/// Parses an exact base-10 decimal string (e.g. `"-123.45"`) into a signed unscaled `i64`
/// for `DECIMAL64(precision, scale)` without going through `f64`.
pub fn parse_decimal64_str(s: &str, precision: u16, scale: u8) -> Result<i64, String> {
    if !(1..=18).contains(&precision) || (scale as u16) > precision {
        return Err(format!(
            "Invalid DECIMAL64({}, {}): require 1 <= precision <= 18 and 0 <= scale <= precision",
            precision, scale
        ));
    }
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
    if frac_part.len() > scale as usize {
        return Err(format!(
            "Decimal value '{}' has {} fractional digits exceeding scale {}",
            s,
            frac_part.len(),
            scale
        ));
    }

    let mut acc: i128 = 0;
    for b in int_part.bytes() {
        acc = acc
            .checked_mul(10)
            .and_then(|v| v.checked_add((b - b'0') as i128))
            .ok_or_else(|| format!("Decimal value '{}' overflows i64", s))?;
    }
    for b in frac_part.bytes() {
        acc = acc
            .checked_mul(10)
            .and_then(|v| v.checked_add((b - b'0') as i128))
            .ok_or_else(|| format!("Decimal value '{}' overflows i64", s))?;
    }
    for _ in frac_part.len()..(scale as usize) {
        acc = acc
            .checked_mul(10)
            .ok_or_else(|| format!("Decimal value '{}' overflows i64", s))?;
    }

    let max_abs = 10i128.pow(precision as u32) - 1;
    if acc > max_abs {
        return Err(format!(
            "Decimal value '{}' exceeds DECIMAL64({}, {}) range",
            s, precision, scale
        ));
    }
    let signed = if neg { -acc } else { acc };
    i64::try_from(signed).map_err(|_| format!("Decimal value '{}' overflows i64", s))
}

/// Formats an unscaled signed `i64` for `DECIMAL64(_, scale)` as an exact base-10 string
/// without going through floating-point arithmetic.
pub fn format_decimal64(unscaled: i64, scale: u8) -> String {
    let neg = unscaled < 0;
    let abs_val: u128 = (unscaled as i128).unsigned_abs();
    if scale == 0 {
        if neg {
            format!("-{}", abs_val)
        } else {
            abs_val.to_string()
        }
    } else {
        let pow = 10u128.pow(scale as u32);
        let int_part = abs_val / pow;
        let frac_part = abs_val % pow;
        let sign = if neg { "-" } else { "" };
        format!(
            "{}{}.{:0width$}",
            sign,
            int_part,
            frac_part,
            width = scale as usize
        )
    }
}

pub fn parse_type_code(s: &str) -> u8 {
    if let Ok(Some((is_nullable, _, _))) = parse_decimal64_spec(s) {
        return if is_nullable { 0x15 | 0x80 } else { 0x15 };
    }
    let s_clean = s.trim().to_lowercase();
    let is_nullable =
        s_clean.ends_with('?') || s_clean.starts_with("nullable_") || s_clean.starts_with("opt_");
    let name = s_clean
        .trim_end_matches('?')
        .trim_start_matches("nullable_")
        .trim_start_matches("opt_");

    let base: u8 = match name {
        "int8" | "i8" | "uint8" | "u8" | "bool" | "boolean" => 0x01,
        "int16" | "i16" | "uint16" | "u16" => 0x02,
        "int32" | "i32" | "uint32" | "u32" | "int" | "integer" | "date32" => 0x03,
        "int64" | "i64" | "uint64" | "u64" | "long" | "date64" => 0x04,
        "float16" | "f16" | "bfloat16" | "bf16" | "half" => 0x05,
        "float32" | "f32" | "float" => 0x06,
        "float64" | "f64" | "double" => 0x07,
        "timestamp_ms" | "timestamp" | "time_ms" | "epoch_ms" => 0x08,
        "timestamp_ns" | "time_ns" | "epoch_ns" => 0x09,
        "fixed_bytes" | "bytes" | "fixed_binary" | "uuid" => 0x0A,
        "string" | "str" | "utf8" | "var_string" | "text" => 0x0B,
        "var_bytes" | "blob" | "binary" => 0x0C,
        "interval_sec_32" | "interval_sec" => 0x0D,
        "interval_ms_64" | "interval_ms" => 0x0E,
        "decimal64" | "dec64" => 0x15,
        _ => 0x03,
    };

    if is_nullable {
        base | 0x80
    } else {
        base
    }
}

pub fn build_padded_validity_bitmap(is_valid: &[bool]) -> Vec<u8> {
    let count = is_valid.len();
    let word_count = count.div_ceil(64);
    let mut words = vec![0u64; word_count];
    for (i, &valid) in is_valid.iter().enumerate() {
        if valid {
            words[i / 64] |= 1u64 << (i % 64);
        }
    }

    let raw_bytes = word_count * 8;
    let padded_len = if raw_bytes == 0 {
        0
    } else {
        (raw_bytes + 127) & !127
    };
    let mut bitmap_buf = Vec::with_capacity(padded_len);
    for w in words {
        bitmap_buf.extend_from_slice(&w.to_le_bytes());
    }
    bitmap_buf.resize(padded_len, 0x00);
    bitmap_buf
}

pub fn read_parquet_id_strings(
    file_path: &Path,
    id_col_name: &str,
) -> Result<Vec<String>, Box<dyn Error>> {
    let file = File::open(file_path)?;
    let builder = ParquetRecordBatchReaderBuilder::try_new(file)?;
    let reader = builder.build()?;

    let mut result = Vec::new();
    for batch_res in reader {
        let batch = batch_res?;
        let schema = batch.schema();
        let col_idx = schema
            .index_of(id_col_name)
            .map_err(|_| format!("Parquet ID column '{}' not found in schema", id_col_name))?;
        {
            let array = batch.column(col_idx);
            if let Some(str_arr) = array.as_any().downcast_ref::<StringArray>() {
                for i in 0..array.len() {
                    result.push(str_arr.value(i).to_string());
                }
            } else if let Some(lstr_arr) = array.as_any().downcast_ref::<LargeStringArray>() {
                for i in 0..array.len() {
                    result.push(lstr_arr.value(i).to_string());
                }
            } else if let Some(i64_arr) = array.as_any().downcast_ref::<Int64Array>() {
                for i in 0..array.len() {
                    result.push(i64_arr.value(i).to_string());
                }
            } else if let Some(i32_arr) = array.as_any().downcast_ref::<Int32Array>() {
                for i in 0..array.len() {
                    result.push(i32_arr.value(i).to_string());
                }
            } else if let Some(u64_arr) = array.as_any().downcast_ref::<UInt64Array>() {
                for i in 0..array.len() {
                    result.push(u64_arr.value(i).to_string());
                }
            } else if let Some(u32_arr) = array.as_any().downcast_ref::<UInt32Array>() {
                for i in 0..array.len() {
                    result.push(u32_arr.value(i).to_string());
                }
            } else {
                for i in 0..array.len() {
                    result.push(format!("{}", i));
                }
            }
        }
    }
    Ok(result)
}

pub fn read_parquet_column_data(
    file_path: &Path,
    columns: &[(&str, Option<&str>, Option<u32>)],
) -> Result<HashMap<String, NativeColumn>, Box<dyn Error>> {
    let file = File::open(file_path)?;
    let builder = ParquetRecordBatchReaderBuilder::try_new(file)?;
    let reader = builder.build()?;

    struct Accumulator {
        requested_type_code: Option<u8>,
        requested_decimal_ps: Option<(u16, u8)>,
        inferred_base_type: u8,
        dimension: u32,
        precision: u16,
        scale: u8,
        is_valid: Vec<bool>,
        payload_bytes: Vec<u8>,
        string_offsets: Option<Vec<u32>>,
        null_count: usize,
    }

    let mut accumulators: HashMap<String, Accumulator> = HashMap::new();
    for &(col_name, type_str_opt, dim_opt) in columns {
        let mut req_precision: u16 = 0;
        let mut req_scale: u8 = 0;
        let mut req_dec_ps: Option<(u16, u8)> = None;
        if let Some(ts) = type_str_opt {
            if let Some((_, p, s)) = parse_decimal64_spec(ts)? {
                req_precision = p;
                req_scale = s;
                req_dec_ps = Some((p, s));
            }
        }
        let req_tc = type_str_opt.map(parse_type_code);
        accumulators.insert(
            col_name.to_string(),
            Accumulator {
                requested_type_code: req_tc,
                requested_decimal_ps: req_dec_ps,
                inferred_base_type: req_tc.map(|c| c & 0x7F).unwrap_or(0x03),
                dimension: dim_opt.unwrap_or(1),
                precision: req_precision,
                scale: req_scale,
                is_valid: Vec::new(),
                payload_bytes: Vec::new(),
                string_offsets: None,
                null_count: 0,
            },
        );
    }

    for batch_res in reader {
        let batch = batch_res?;
        let schema = batch.schema();

        for (col_name, acc) in &mut accumulators {
            if let Ok(col_idx) = schema.index_of(col_name) {
                let array = batch.column(col_idx);

                let len = array.len();
                for i in 0..len {
                    let valid = !array.is_null(i);
                    acc.is_valid.push(valid);
                    if !valid {
                        acc.null_count += 1;
                    }
                }

                if let Some(arr) = array.as_any().downcast_ref::<Int32Array>() {
                    acc.inferred_base_type = 0x03;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i32 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Int64Array>() {
                    acc.inferred_base_type = 0x04;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i64 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Float32Array>() {
                    acc.inferred_base_type = 0x06;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0.0f32 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Float64Array>() {
                    acc.inferred_base_type = 0x07;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0.0f64 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Int16Array>() {
                    acc.inferred_base_type = 0x02;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i16 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Int8Array>() {
                    acc.inferred_base_type = 0x01;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i8 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<UInt32Array>() {
                    acc.inferred_base_type = 0x03;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0u32 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<UInt64Array>() {
                    acc.inferred_base_type = 0x04;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0u64 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<UInt16Array>() {
                    acc.inferred_base_type = 0x02;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0u16 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<UInt8Array>() {
                    acc.inferred_base_type = 0x01;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0u8 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<BooleanArray>() {
                    acc.inferred_base_type = 0x01;
                    for i in 0..len {
                        let v = if arr.is_null(i) {
                            0u8
                        } else {
                            arr.value(i) as u8
                        };
                        acc.payload_bytes.push(v);
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Float16Array>() {
                    acc.inferred_base_type = 0x05;
                    for i in 0..len {
                        let v = if arr.is_null(i) {
                            0u16
                        } else {
                            arr.value(i).to_bits()
                        };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Date32Array>() {
                    acc.inferred_base_type = 0x03;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i32 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Date64Array>() {
                    acc.inferred_base_type = 0x04;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i64 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Decimal64Array>() {
                    let p = arr.precision() as u16;
                    let s = arr.scale();
                    if !(1..=18).contains(&p) || s < 0 || (s as u16) > p {
                        return Err(format!(
                            "Parquet DECIMAL64 column '{}' has invalid precision/scale ({}, {})",
                            col_name, p, s
                        )
                        .into());
                    }
                    if let Some((rp, rs)) = acc.requested_decimal_ps {
                        if (rp, rs) != (p, s as u8) {
                            return Err(format!(
                                "Manifest DECIMAL64({}, {}) does not match Parquet column '{}' DECIMAL({}, {})",
                                rp, rs, col_name, p, s
                            )
                            .into());
                        }
                    }
                    let max_abs = 10i128.pow(p as u32) - 1;
                    acc.inferred_base_type = 0x15;
                    acc.precision = p;
                    acc.scale = s as u8;
                    for i in 0..len {
                        let v = if arr.is_null(i) {
                            0i64
                        } else {
                            let raw = arr.value(i);
                            if (raw as i128).abs() > max_abs {
                                return Err(format!(
                                    "Parquet DECIMAL64 value {} at row {} in '{}' exceeds precision {}",
                                    raw, i, col_name, p
                                )
                                .into());
                            }
                            raw
                        };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<Decimal128Array>() {
                    let p = arr.precision() as u16;
                    let s = arr.scale();
                    if !(1..=18).contains(&p) || s < 0 || (s as u16) > p {
                        return Err(format!(
                            "Parquet DECIMAL128 column '{}' with ({}, {}) cannot fit in DECIMAL64 (require 1 <= P <= 18 && 0 <= S <= P)",
                            col_name, p, s
                        )
                        .into());
                    }
                    if let Some((rp, rs)) = acc.requested_decimal_ps {
                        if (rp, rs) != (p, s as u8) {
                            return Err(format!(
                                "Manifest DECIMAL64({}, {}) does not match Parquet column '{}' DECIMAL({}, {})",
                                rp, rs, col_name, p, s
                            )
                            .into());
                        }
                    }
                    let max_abs = 10i128.pow(p as u32) - 1;
                    acc.inferred_base_type = 0x15;
                    acc.precision = p;
                    acc.scale = s as u8;
                    for i in 0..len {
                        let v: i64 = if arr.is_null(i) {
                            0i64
                        } else {
                            let raw = arr.value(i);
                            if raw.abs() > max_abs {
                                return Err(format!(
                                    "Parquet DECIMAL value {} at row {} in '{}' exceeds precision {}",
                                    raw, i, col_name, p
                                )
                                .into());
                            }
                            i64::try_from(raw).map_err(|_| {
                                format!(
                                    "Parquet DECIMAL value at row {} in '{}' overflows i64",
                                    i, col_name
                                )
                            })?
                        };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<TimestampMillisecondArray>()
                {
                    acc.inferred_base_type = 0x08;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i64 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<TimestampSecondArray>() {
                    acc.inferred_base_type = 0x08;
                    for i in 0..len {
                        let v = if arr.is_null(i) {
                            0i64
                        } else {
                            arr.value(i) * 1000
                        };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<TimestampMicrosecondArray>()
                {
                    acc.inferred_base_type = 0x09;
                    for i in 0..len {
                        let v = if arr.is_null(i) {
                            0i64
                        } else {
                            arr.value(i) * 1000
                        };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<TimestampNanosecondArray>()
                {
                    acc.inferred_base_type = 0x09;
                    for i in 0..len {
                        let v = if arr.is_null(i) { 0i64 } else { arr.value(i) };
                        acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<StringArray>() {
                    acc.inferred_base_type = 0x0B;
                    if acc.string_offsets.is_none() {
                        acc.string_offsets = Some(vec![0]);
                    }
                    let offsets = acc.string_offsets.as_mut().unwrap();
                    for i in 0..len {
                        if !arr.is_null(i) {
                            let s = arr.value(i);
                            acc.payload_bytes.extend_from_slice(s.as_bytes());
                        }
                        offsets.push(acc.payload_bytes.len() as u32);
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<LargeStringArray>() {
                    acc.inferred_base_type = 0x0B;
                    if acc.string_offsets.is_none() {
                        acc.string_offsets = Some(vec![0]);
                    }
                    let offsets = acc.string_offsets.as_mut().unwrap();
                    for i in 0..len {
                        if !arr.is_null(i) {
                            let s = arr.value(i);
                            acc.payload_bytes.extend_from_slice(s.as_bytes());
                        }
                        offsets.push(acc.payload_bytes.len() as u32);
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<FixedSizeBinaryArray>() {
                    let byte_w = arr.value_length() as u32;
                    acc.inferred_base_type = 0x0A;
                    acc.dimension = byte_w.max(1);
                    for i in 0..len {
                        if arr.is_null(i) {
                            acc.payload_bytes
                                .extend(std::iter::repeat_n(0u8, byte_w as usize));
                        } else {
                            acc.payload_bytes.extend_from_slice(arr.value(i));
                        }
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<BinaryArray>() {
                    acc.inferred_base_type = 0x0C;
                    if acc.string_offsets.is_none() {
                        acc.string_offsets = Some(vec![0]);
                    }
                    let offsets = acc.string_offsets.as_mut().unwrap();
                    for i in 0..len {
                        if !arr.is_null(i) {
                            acc.payload_bytes.extend_from_slice(arr.value(i));
                        }
                        offsets.push(acc.payload_bytes.len() as u32);
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<LargeBinaryArray>() {
                    acc.inferred_base_type = 0x0C;
                    if acc.string_offsets.is_none() {
                        acc.string_offsets = Some(vec![0]);
                    }
                    let offsets = acc.string_offsets.as_mut().unwrap();
                    for i in 0..len {
                        if !arr.is_null(i) {
                            acc.payload_bytes.extend_from_slice(arr.value(i));
                        }
                        offsets.push(acc.payload_bytes.len() as u32);
                    }
                } else if let Some(arr) = array.as_any().downcast_ref::<FixedSizeListArray>() {
                    let dim = arr.value_length() as u32;
                    acc.dimension = dim;
                    let values = arr.values();
                    if let Some(f32s) = values.as_any().downcast_ref::<Float32Array>() {
                        acc.inferred_base_type = 0x06;
                        for i in 0..len {
                            for d in 0..dim as usize {
                                let idx = i * (dim as usize) + d;
                                let v = if f32s.is_null(idx) {
                                    0.0f32
                                } else {
                                    f32s.value(idx)
                                };
                                acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                        }
                    } else if let Some(f64s) = values.as_any().downcast_ref::<Float64Array>() {
                        acc.inferred_base_type = 0x07;
                        for i in 0..len {
                            for d in 0..dim as usize {
                                let idx = i * (dim as usize) + d;
                                let v = if f64s.is_null(idx) {
                                    0.0f64
                                } else {
                                    f64s.value(idx)
                                };
                                acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                        }
                    } else if let Some(i32s) = values.as_any().downcast_ref::<Int32Array>() {
                        acc.inferred_base_type = 0x03;
                        for i in 0..len {
                            for d in 0..dim as usize {
                                let idx = i * (dim as usize) + d;
                                let v = if i32s.is_null(idx) {
                                    0i32
                                } else {
                                    i32s.value(idx)
                                };
                                acc.payload_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                        }
                    }
                }
            }
        }
    }

    let mut result = HashMap::new();
    for (name, acc) in accumulators {
        let is_nullable = acc
            .requested_type_code
            .map(|tc| (tc & 0x80) != 0)
            .unwrap_or(acc.null_count > 0);

        let base_type = acc
            .requested_type_code
            .map(|tc| tc & 0x7F)
            .unwrap_or(acc.inferred_base_type);

        if base_type == 0x15 && acc.inferred_base_type != 0x15 && !acc.is_valid.is_empty() {
            return Err(format!(
                "Parquet column '{}' requested as DECIMAL64 but source column is not a Parquet Decimal array (inferred base_type=0x{:02X})",
                name, acc.inferred_base_type
            )
            .into());
        }

        let final_type_code = if is_nullable {
            base_type | 0x80
        } else {
            base_type
        };

        let final_data = if is_nullable {
            let mut buf = build_padded_validity_bitmap(&acc.is_valid);
            buf.extend_from_slice(&acc.payload_bytes);
            buf
        } else {
            acc.payload_bytes
        };

        let row_count = acc.is_valid.len();

        result.insert(
            name.clone(),
            NativeColumn {
                name,
                type_code: final_type_code,
                dimension: acc.dimension,
                precision: acc.precision,
                scale: acc.scale,
                data: final_data,
                offsets: acc.string_offsets,
                null_count: acc.null_count,
                row_count,
            },
        );
    }

    Ok(result)
}

/// Fallback text reader (CSV, TSV, TXT) with Do The Right Thing (DTRT) defaults.
pub fn read_text_column_data(
    file_path: &Path,
    columns: &[(&str, Option<&str>, Option<u32>)],
) -> Result<HashMap<String, NativeColumn>, Box<dyn Error>> {
    let file = File::open(file_path)?;
    let mut reader = BufReader::new(file);

    let mut first_line = String::new();
    reader.read_line(&mut first_line)?;
    let delimiter = if first_line.contains('\t') {
        b'\t'
    } else if first_line.contains(',') {
        b','
    } else {
        b' '
    };

    let has_headers = {
        let trimmed = first_line.trim();
        let tokens: Vec<&str> = if delimiter == b'\t' {
            trimmed.split('\t').collect()
        } else if delimiter == b',' {
            trimmed.split(',').collect()
        } else {
            trimmed.split_whitespace().collect()
        };
        columns
            .iter()
            .any(|&(col_name, _, _)| tokens.iter().any(|t| t.trim() == col_name))
    };

    let mut csv_reader = csv::ReaderBuilder::new()
        .delimiter(delimiter)
        .has_headers(has_headers)
        .flexible(true)
        .from_path(file_path)?;

    let header_indices: HashMap<String, usize> = if has_headers {
        let headers = csv_reader.headers()?.clone();
        headers
            .iter()
            .enumerate()
            .map(|(idx, h)| (h.trim().to_string(), idx))
            .collect()
    } else {
        columns
            .iter()
            .enumerate()
            .map(|(idx, &(col_name, _, _))| (col_name.to_string(), idx + 2))
            .collect()
    };

    let mut raw_rows: HashMap<String, Vec<Option<String>>> = HashMap::new();
    for &(col_name, _, _) in columns {
        raw_rows.insert(col_name.to_string(), Vec::new());
    }

    for record_res in csv_reader.records() {
        let record = record_res?;
        for (col_name, vec) in &mut raw_rows {
            if let Some(&idx) = header_indices.get(col_name) {
                if idx < record.len() {
                    let val = record.get(idx).map(|s| s.trim()).unwrap_or("");
                    if val.is_empty() || val.eq_ignore_ascii_case("null") || val == "NA" {
                        vec.push(None);
                    } else {
                        vec.push(Some(val.to_string()));
                    }
                } else {
                    vec.push(None);
                }
            } else {
                vec.push(None);
            }
        }
    }

    let mut result = HashMap::new();
    for &(col_name, req_type_str, dim_opt) in columns {
        let vals = raw_rows.get(col_name).unwrap();
        let row_count = vals.len();
        let null_count = vals.iter().filter(|v| v.is_none()).count();

        let mut dec_precision: u16 = 0;
        let mut dec_scale: u8 = 0;
        if let Some(ts) = req_type_str {
            if let Some((_, p, s)) = parse_decimal64_spec(ts)? {
                dec_precision = p;
                dec_scale = s;
            }
        }
        let req_tc = req_type_str.map(parse_type_code);
        let is_nullable = req_tc.map(|tc| (tc & 0x80) != 0).unwrap_or(null_count > 0);
        let base_type = req_tc.map(|tc| tc & 0x7F).unwrap_or(0x03); // Default to INT32
        let dimension = dim_opt.unwrap_or(1);

        let mut is_valid = Vec::with_capacity(row_count);
        let mut payload_bytes = Vec::new();
        let mut string_offsets = if base_type == 0x0B || base_type == 0x0C {
            Some(vec![0])
        } else {
            None
        };

        for val_opt in vals {
            is_valid.push(val_opt.is_some());
            let s = val_opt.as_deref().unwrap_or("");
            match base_type {
                0x01 => {
                    // Int8 / Bool
                    let v: i8 = if s.eq_ignore_ascii_case("true") {
                        1
                    } else if s.eq_ignore_ascii_case("false") {
                        0
                    } else {
                        s.parse().unwrap_or(0)
                    };
                    payload_bytes.push(v as u8);
                }
                0x02 => {
                    // Int16
                    let v: i16 = s.parse().unwrap_or(0);
                    payload_bytes.extend_from_slice(&v.to_le_bytes());
                }
                0x03 => {
                    // Int32 (scalar or D-dimensional inline vector)
                    if dimension > 1 {
                        let parsed =
                            crate::embeddings::parse_inline_vector_str(s).unwrap_or_default();
                        for d in 0..(dimension as usize) {
                            let v = parsed.get(d).copied().unwrap_or(0.0f32) as i32;
                            payload_bytes.extend_from_slice(&v.to_le_bytes());
                        }
                    } else {
                        let v: i32 = s.parse().unwrap_or(0);
                        payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                }
                0x04 | 0x08 | 0x09 => {
                    // Int64 / TimestampMs / TimestampNs
                    let v: i64 = s.parse().unwrap_or(0);
                    payload_bytes.extend_from_slice(&v.to_le_bytes());
                }
                0x05 => {
                    // Float16 (IEEE 754 binary16, scalar or D-dimensional inline vector)
                    if dimension > 1 {
                        let parsed =
                            crate::embeddings::parse_inline_vector_str(s).unwrap_or_default();
                        for d in 0..(dimension as usize) {
                            let v = f32_to_f16_bits(parsed.get(d).copied().unwrap_or(0.0f32));
                            payload_bytes.extend_from_slice(&v.to_le_bytes());
                        }
                    } else {
                        let v = f32_to_f16_bits(s.parse::<f32>().unwrap_or(0.0));
                        payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                }
                0x06 => {
                    // Float32 (scalar or D-dimensional inline vector)
                    if dimension > 1 {
                        let parsed =
                            crate::embeddings::parse_inline_vector_str(s).unwrap_or_default();
                        for d in 0..(dimension as usize) {
                            let v = parsed.get(d).copied().unwrap_or(0.0f32);
                            payload_bytes.extend_from_slice(&v.to_le_bytes());
                        }
                    } else {
                        let v: f32 = s.parse().unwrap_or(0.0);
                        payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                }
                0x07 => {
                    // Float64 (scalar or D-dimensional inline vector)
                    if dimension > 1 {
                        let parsed =
                            crate::embeddings::parse_inline_vector_str(s).unwrap_or_default();
                        for d in 0..(dimension as usize) {
                            let v = parsed.get(d).copied().unwrap_or(0.0f32) as f64;
                            payload_bytes.extend_from_slice(&v.to_le_bytes());
                        }
                    } else {
                        let v: f64 = s.parse().unwrap_or(0.0);
                        payload_bytes.extend_from_slice(&v.to_le_bytes());
                    }
                }
                0x0A => {
                    // FixedBytes (hex or hyphenated UUID string, padded/truncated to `dimension` bytes)
                    let width = dimension.max(1) as usize;
                    let decoded = decode_hex_or_uuid_bytes(s);
                    for b_idx in 0..width {
                        payload_bytes.push(decoded.get(b_idx).copied().unwrap_or(0u8));
                    }
                }
                0x0B => {
                    // VarString (exact UTF-8 bytes per Spec §4.3.2, no trailing NUL)
                    payload_bytes.extend_from_slice(s.as_bytes());
                    if let Some(ref mut offs) = string_offsets {
                        offs.push(payload_bytes.len() as u32);
                    }
                }
                0x0C => {
                    // VarBytes (hex-encoded byte slice)
                    let decoded = decode_hex_or_uuid_bytes(s);
                    payload_bytes.extend_from_slice(&decoded);
                    if let Some(ref mut offs) = string_offsets {
                        offs.push(payload_bytes.len() as u32);
                    }
                }
                0x0D => {
                    // IntervalSec32: 8 bytes (u32 start_sec, u32 duration_sec) formatted as "start:dur" or "start;dur"
                    let (start_sec, dur_sec) = parse_u32_pair(s);
                    payload_bytes.extend_from_slice(&start_sec.to_le_bytes());
                    payload_bytes.extend_from_slice(&dur_sec.to_le_bytes());
                }
                0x0E => {
                    // IntervalMs64: 16 bytes (u64 start_ms, u64 duration_ms) formatted as "start:dur" or "start;dur"
                    let (start_ms, dur_ms) = parse_u64_pair(s);
                    payload_bytes.extend_from_slice(&start_ms.to_le_bytes());
                    payload_bytes.extend_from_slice(&dur_ms.to_le_bytes());
                }
                0x15 => {
                    // DECIMAL64(P, S): exact base-10 string -> signed unscaled i64 (zero f64 loss)
                    let v = if val_opt.is_none() {
                        0i64
                    } else {
                        parse_decimal64_str(s, dec_precision, dec_scale)?
                    };
                    payload_bytes.extend_from_slice(&v.to_le_bytes());
                }
                _ => {
                    let v: i32 = s.parse().unwrap_or(0);
                    payload_bytes.extend_from_slice(&v.to_le_bytes());
                }
            }
        }

        let final_type_code = if is_nullable {
            base_type | 0x80
        } else {
            base_type
        };
        let final_data = if is_nullable {
            let mut buf = build_padded_validity_bitmap(&is_valid);
            buf.extend_from_slice(&payload_bytes);
            buf
        } else {
            payload_bytes
        };

        result.insert(
            col_name.to_string(),
            NativeColumn {
                name: col_name.to_string(),
                type_code: final_type_code,
                dimension,
                precision: dec_precision,
                scale: dec_scale,
                data: final_data,
                offsets: string_offsets,
                null_count,
                row_count,
            },
        );
    }

    Ok(result)
}

/// Fallback CSV reader forwarding to `read_text_column_data`.
pub fn read_csv_column_data(
    file_path: &Path,
    columns: &[(&str, Option<&str>, Option<u32>)],
) -> Result<HashMap<String, NativeColumn>, Box<dyn Error>> {
    read_text_column_data(file_path, columns)
}

/// Legacy reader maintained for backward compatibility.
pub fn read_parquet_columns(
    file_path: &Path,
    columns: &[String],
) -> Result<HashMap<String, Vec<String>>, Box<dyn Error>> {
    let file = File::open(file_path)?;
    let builder = ParquetRecordBatchReaderBuilder::try_new(file)?;
    let reader = builder.build()?;

    let mut result: HashMap<String, Vec<String>> = HashMap::new();
    for col in columns {
        result.insert(col.clone(), Vec::new());
    }

    for batch_res in reader {
        let batch = batch_res?;
        let schema = batch.schema();
        for col_name in columns {
            if let Ok(col_idx) = schema.index_of(col_name) {
                let array = batch.column(col_idx);
                let col_vec = result.get_mut(col_name).unwrap();

                if let Some(string_array) = array.as_any().downcast_ref::<StringArray>() {
                    for i in 0..array.len() {
                        if string_array.is_null(i) {
                            col_vec.push(String::new());
                        } else {
                            col_vec.push(string_array.value(i).to_string());
                        }
                    }
                } else if let Some(i8_array) = array.as_any().downcast_ref::<Int8Array>() {
                    for i in 0..array.len() {
                        col_vec.push(i8_array.value(i).to_string());
                    }
                } else if let Some(i16_array) = array.as_any().downcast_ref::<Int16Array>() {
                    for i in 0..array.len() {
                        col_vec.push(i16_array.value(i).to_string());
                    }
                } else if let Some(u8_array) = array.as_any().downcast_ref::<UInt8Array>() {
                    for i in 0..array.len() {
                        col_vec.push(u8_array.value(i).to_string());
                    }
                } else if let Some(u16_array) = array.as_any().downcast_ref::<UInt16Array>() {
                    for i in 0..array.len() {
                        col_vec.push(u16_array.value(i).to_string());
                    }
                } else if let Some(i64_array) = array.as_any().downcast_ref::<Int64Array>() {
                    for i in 0..array.len() {
                        col_vec.push(i64_array.value(i).to_string());
                    }
                } else if let Some(i32_array) = array.as_any().downcast_ref::<Int32Array>() {
                    for i in 0..array.len() {
                        col_vec.push(i32_array.value(i).to_string());
                    }
                } else if let Some(u64_array) = array.as_any().downcast_ref::<UInt64Array>() {
                    for i in 0..array.len() {
                        col_vec.push(u64_array.value(i).to_string());
                    }
                } else if let Some(u32_array) = array.as_any().downcast_ref::<UInt32Array>() {
                    for i in 0..array.len() {
                        col_vec.push(u32_array.value(i).to_string());
                    }
                } else if let Some(f32_array) = array.as_any().downcast_ref::<Float32Array>() {
                    for i in 0..array.len() {
                        col_vec.push(f32_array.value(i).to_string());
                    }
                } else if let Some(f64_array) = array.as_any().downcast_ref::<Float64Array>() {
                    for i in 0..array.len() {
                        col_vec.push(f64_array.value(i).to_string());
                    }
                } else if let Some(fsb_array) =
                    array.as_any().downcast_ref::<FixedSizeBinaryArray>()
                {
                    for i in 0..array.len() {
                        let hex_s: String = fsb_array
                            .value(i)
                            .iter()
                            .map(|b| format!("{:02x}", b))
                            .collect();
                        col_vec.push(hex_s);
                    }
                } else {
                    for _i in 0..array.len() {
                        col_vec.push(format!("{:?}", array.to_data()));
                    }
                }
            }
        }
    }

    Ok(result)
}

pub fn f32_to_f16_bits(val: f32) -> u16 {
    let bits = val.to_bits();
    let sign = ((bits >> 16) & 0x8000) as u16;
    let exp = ((bits >> 23) & 0xFF) as i32;
    let mant = bits & 0x007F_FFFF;

    if exp == 255 {
        if mant != 0 {
            return sign | 0x7E00; // NaN
        }
        return sign | 0x7C00; // Inf
    }
    let half_exp = exp - 127 + 15;
    if half_exp >= 31 {
        return sign | 0x7C00; // Overflow -> Inf
    }
    if half_exp <= 0 {
        if half_exp < -10 {
            return sign;
        }
        let m = (mant | 0x0080_0000) >> (1 - half_exp);
        return sign | ((m + 0x1000) >> 13) as u16;
    }
    let rounded = mant + 0x0FFF + ((mant >> 13) & 1);
    if (rounded & 0x0080_0000) != 0 {
        let next_exp = half_exp + 1;
        if next_exp >= 31 {
            return sign | 0x7C00;
        }
        return sign | ((next_exp as u16) << 10);
    }
    sign | ((half_exp as u16) << 10) | ((rounded >> 13) as u16)
}

pub fn f16_bits_to_f32(bits: u16) -> f32 {
    let sign = ((bits & 0x8000) as u32) << 16;
    let exp = ((bits >> 10) & 0x1F) as u32;
    let mant = (bits & 0x03FF) as u32;

    if exp == 0 {
        if mant == 0 {
            return f32::from_bits(sign);
        }
        let mut m = mant;
        let mut e = -14i32;
        while (m & 0x0400) == 0 {
            m <<= 1;
            e -= 1;
        }
        m &= 0x03FF;
        let f_exp = ((e + 127) as u32) << 23;
        return f32::from_bits(sign | f_exp | (m << 13));
    }
    if exp == 31 {
        return f32::from_bits(sign | 0x7F80_0000 | (mant << 13));
    }
    let f_exp = (exp + 127 - 15) << 23;
    f32::from_bits(sign | f_exp | (mant << 13))
}

pub fn decode_hex_or_uuid_bytes(s: &str) -> Vec<u8> {
    let trimmed = s.trim().trim_start_matches("0x").trim_start_matches("0X");
    if trimmed.is_empty() {
        return Vec::new();
    }
    let hex_only: String = trimmed.chars().filter(|c| *c != '-').collect();
    if hex_only.len().is_multiple_of(2) && hex_only.chars().all(|c| c.is_ascii_hexdigit()) {
        let mut out = Vec::with_capacity(hex_only.len() / 2);
        for i in (0..hex_only.len()).step_by(2) {
            if let Ok(b) = u8::from_str_radix(&hex_only[i..i + 2], 16) {
                out.push(b);
            }
        }
        out
    } else {
        trimmed.as_bytes().to_vec()
    }
}

fn parse_u32_pair(s: &str) -> (u32, u32) {
    let clean = s
        .trim()
        .trim_matches(|c| c == '[' || c == ']' || c == '(' || c == ')');
    if clean.is_empty() {
        return (0, 0);
    }
    let parts: Vec<&str> = clean.split([':', ';', ',']).collect();
    let a = parts
        .first()
        .and_then(|p| p.trim().parse::<u32>().ok())
        .unwrap_or(0);
    let b = parts
        .get(1)
        .and_then(|p| p.trim().parse::<u32>().ok())
        .unwrap_or(0);
    (a, b)
}

fn parse_u64_pair(s: &str) -> (u64, u64) {
    let clean = s
        .trim()
        .trim_matches(|c| c == '[' || c == ']' || c == '(' || c == ')');
    if clean.is_empty() {
        return (0, 0);
    }
    let parts: Vec<&str> = clean.split([':', ';', ',']).collect();
    let a = parts
        .first()
        .and_then(|p| p.trim().parse::<u64>().ok())
        .unwrap_or(0);
    let b = parts
        .get(1)
        .and_then(|p| p.trim().parse::<u64>().ok())
        .unwrap_or(0);
    (a, b)
}

#[cfg(test)]
mod decimal64_tests {
    use super::*;
    use crate::commands::inspect::format_attr_type_label;
    use impulse_graph::spec::{BaseDataType, KeyType};
    use impulse_graph::{ImpulseError, SnapshotReader, SnapshotWriter};

    #[test]
    fn test_parse_decimal64_spec_valid_and_invalid() {
        assert_eq!(
            parse_decimal64_spec("decimal64(10, 2)").unwrap(),
            Some((false, 10, 2))
        );
        assert_eq!(
            parse_decimal64_spec("decimal(18, 0)?").unwrap(),
            Some((true, 18, 0))
        );
        assert_eq!(
            parse_decimal64_spec("nullable_dec64(1, 1)").unwrap(),
            Some((true, 1, 1))
        );
        assert_eq!(parse_decimal64_spec("int64").unwrap(), None);
        assert_eq!(parse_type_code("decimal64(10, 2)"), 0x15);
        assert_eq!(parse_type_code("decimal64(10, 2)?"), 0x95);

        // Invalid (P, S)
        assert!(parse_decimal64_spec("decimal64(0, 0)").is_err());
        assert!(parse_decimal64_spec("decimal64(19, 2)").is_err());
        assert!(parse_decimal64_spec("decimal64(5, 6)").is_err());
    }

    #[test]
    fn test_parse_decimal64_str_exact_no_f64_loss() {
        assert_eq!(parse_decimal64_str("123.45", 10, 2).unwrap(), 12345);
        assert_eq!(parse_decimal64_str("-0.05", 10, 2).unwrap(), -5);
        assert_eq!(parse_decimal64_str("42", 10, 2).unwrap(), 4200);
        // 18 digits: f64 only has 53 bits (~15.95 decimal digits), so this proves exact integer parsing
        assert_eq!(
            parse_decimal64_str("9999999999999999.99", 18, 2).unwrap(),
            999_999_999_999_999_999i64
        );
        assert_eq!(
            parse_decimal64_str("-9999999999999999.99", 18, 2).unwrap(),
            -999_999_999_999_999_999i64
        );
        // Reject overflow, excess scale, and empty/whitespace strings
        assert!(parse_decimal64_str("123.456", 10, 2).is_err());
        assert!(parse_decimal64_str("100000000.00", 10, 2).is_err());
        assert!(parse_decimal64_str("", 10, 2).is_err());
        assert!(parse_decimal64_str("   ", 10, 2).is_err());
    }

    #[test]
    fn test_decimal64_snapshot_roundtrip_inspect_and_invalid_rejection() {
        let tmp = std::env::temp_dir();
        let valid_path = tmp.join("test_decimal64_valid_m1.imps");

        let values: [i64; 3] = [12345, -9999, 0];
        let mut raw_bytes = Vec::with_capacity(24);
        for v in values {
            raw_bytes.extend_from_slice(&v.to_le_bytes());
        }

        let mut writer = SnapshotWriter::new(valid_path.to_str().unwrap());
        writer.add_domain(0, KeyType::Int32, "Account");
        writer.set_domain_node_count(0, 3);
        writer.add_attribute_to_domain_with_meta(
            0,
            "balance",
            0x15,
            1,
            10,
            2,
            raw_bytes.clone(),
            None,
        );
        writer.add_relation(0, 0, 3, 1, vec![0, 1, 1, 1], vec![1]);
        writer.finalize().expect("write valid decimal64 snapshot");

        let reader = SnapshotReader::open(&valid_path).expect("open valid decimal64 snapshot");
        let dom = &reader.domains()[0];
        assert_eq!(dom.attributes.len(), 1);
        let attr = &dom.attributes[0];
        assert_eq!(attr.type_code, 0x15);
        assert_eq!(attr.base_type(), Some(BaseDataType::Decimal64));
        assert_eq!(attr.precision, 10);
        assert_eq!(attr.scale, 2);
        assert_eq!(
            format_attr_type_label(attr.type_code & 0x7F, attr.precision, attr.scale),
            "DECIMAL64(10, 2)"
        );
        let data = reader.get_domain_attribute_by_index(0, 0).unwrap();
        assert_eq!(data.len(), 24);
        assert_eq!(i64::from_le_bytes(data[0..8].try_into().unwrap()), 12345);
        assert_eq!(i64::from_le_bytes(data[8..16].try_into().unwrap()), -9999);
        let _ = std::fs::remove_file(&valid_path);

        // Negative tests: P=0, P=19, S>P, truncated payload
        let invalid_cases = [
            (
                "p0",
                0u16,
                0u8,
                raw_bytes.clone(),
                ImpulseError::InvalidArgument,
            ),
            (
                "p19",
                19u16,
                2u8,
                raw_bytes.clone(),
                ImpulseError::InvalidArgument,
            ),
            (
                "s_gt_p",
                5u16,
                6u8,
                raw_bytes.clone(),
                ImpulseError::InvalidArgument,
            ),
            (
                "trunc",
                10u16,
                2u8,
                vec![0u8; 16],
                ImpulseError::BufferOverflow,
            ),
        ];
        for (label, p, s, payload, expected_err) in invalid_cases {
            let bad_path = tmp.join(format!("test_decimal64_bad_{}_m1.imps", label));
            let mut w = SnapshotWriter::new(bad_path.to_str().unwrap());
            w.add_domain(0, KeyType::Int32, "Account");
            w.set_domain_node_count(0, 3);
            w.add_attribute_to_domain_with_meta(0, "bad", 0x15, 1, p, s, payload, None);
            w.add_relation(0, 0, 3, 1, vec![0, 1, 1, 1], vec![1]);
            w.finalize().expect("write bad decimal64 snapshot");

            let res = SnapshotReader::open(&bad_path);
            assert_eq!(res.err(), Some(expected_err), "case {} should fail", label);
            let _ = std::fs::remove_file(&bad_path);
        }

        // Regression #5: dimension == 0 rejected for DECIMAL64
        let dim0_path = tmp.join("test_decimal64_dim0_m1.imps");
        let mut w = SnapshotWriter::new(dim0_path.to_str().unwrap());
        w.add_domain(0, KeyType::Int32, "Account");
        w.set_domain_node_count(0, 3);
        w.add_attribute_to_domain_with_meta(0, "bad_dim0", 0x15, 0, 10, 2, raw_bytes.clone(), None);
        w.add_relation(0, 0, 3, 1, vec![0, 1, 1, 1], vec![1]);
        w.finalize().expect("write dim0 snapshot");
        assert_eq!(
            SnapshotReader::open(&dim0_path).err(),
            Some(ImpulseError::InvalidArgument)
        );
        let _ = std::fs::remove_file(&dim0_path);

        // Regression #3: out-of-bounds relation attribute data_offset rejected on open
        let rel_oob_path = tmp.join("test_decimal64_rel_oob_m1.imps");
        let mut w = SnapshotWriter::new(rel_oob_path.to_str().unwrap());
        w.add_domain(0, KeyType::Int32, "Account");
        w.set_domain_node_count(0, 3);
        w.add_relation(0, 0, 3, 1, vec![0, 1, 1, 1], vec![1]);
        w.add_attribute_to_relation_with_meta(0, "fee", 0x15, 1, 10, 2, vec![0u8; 8], None);
        w.finalize().expect("write rel snapshot");
        let mut file_bytes = std::fs::read(&rel_oob_path).unwrap();
        // Patch relation attribute descriptor's data_bytes to 0x7FFF_FFFF_FFFF_FFFF
        // Find the 0x15 0x02 0x0A 0x00 0x01 0x00 0x00 0x00 pattern in Section 2 directory
        let needle: [u8; 8] = [0x15, 0x02, 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00];
        let pos = file_bytes
            .windows(needle.len())
            .position(|w| w == needle)
            .expect("locate rel attr descriptor");
        // Layout: type_code(+0), scale(+1), precision(+2..4), dim(+4..8), data_offset(+8..16), data_bytes(+16..24)
        file_bytes[pos + 16..pos + 24].copy_from_slice(&u64::MAX.to_le_bytes());
        std::fs::write(&rel_oob_path, &file_bytes).unwrap();
        assert_eq!(
            SnapshotReader::open(&rel_oob_path).err(),
            Some(ImpulseError::BufferOverflow)
        );
        let _ = std::fs::remove_file(&rel_oob_path);
    }
}

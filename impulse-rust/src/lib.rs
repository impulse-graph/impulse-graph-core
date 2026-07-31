//! Impulse Graph Engine Core Crate

#[no_mangle]
pub extern C fn impulse_rust_version() -> u32 {
    2
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version() {
        assert_eq!(impulse_rust_version(), 2);
    }
}

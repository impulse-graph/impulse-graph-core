use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use rand::rngs::OsRng;
use sha2::{Digest, Sha256};
use std::error::Error;
use std::fs;
use std::path::Path;

pub fn keygen(out_prefix: &str) -> Result<(), Box<dyn Error>> {
    let mut csprng = OsRng;
    let signing_key = SigningKey::generate(&mut csprng);
    let verifying_key = signing_key.verifying_key();

    let priv_path = format!("{}.priv", out_prefix);
    let pub_path = format!("{}.pub", out_prefix);

    fs::write(&priv_path, hex::encode(signing_key.to_bytes()))?;
    fs::write(&pub_path, hex::encode(verifying_key.to_bytes()))?;

    println!("Ed25519 keypair successfully generated:");
    println!("  Private key: {}", priv_path);
    println!("  Public key:  {}", pub_path);
    Ok(())
}

pub fn sign(snapshot_path: &Path, key_path: &Path) -> Result<(), Box<dyn Error>> {
    println!(
        "Signing snapshot {} with key {}...",
        snapshot_path.display(),
        key_path.display()
    );
    let key_hex = fs::read_to_string(key_path)?;
    let key_bytes = hex::decode(key_hex.trim())?;
    if key_bytes.len() < 32 {
        return Err("Private key must be 32 bytes".into());
    }
    let mut bytes = [0u8; 32];
    bytes.copy_from_slice(&key_bytes[..32]);
    let signing_key = SigningKey::from_bytes(&bytes);

    let file_data = fs::read(snapshot_path)?;
    if file_data.len() < 64 {
        return Err("Snapshot file too small to sign".into());
    }

    let header_hash = Sha256::digest(&file_data[..64]);
    let signature = signing_key.sign(&header_hash);

    let sig_path = format!("{}.sig", snapshot_path.to_string_lossy());
    fs::write(&sig_path, hex::encode(signature.to_bytes()))?;

    println!("SUCCESS: Signature saved to {}", sig_path);
    Ok(())
}

pub fn verify(snapshot_path: &Path, key_path: &Path) -> Result<(), Box<dyn Error>> {
    println!(
        "Verifying signature for {} with key {}...",
        snapshot_path.display(),
        key_path.display()
    );
    let key_hex = fs::read_to_string(key_path)?;
    let key_bytes = hex::decode(key_hex.trim())?;
    if key_bytes.len() < 32 {
        return Err("Public key must be 32 bytes".into());
    }
    let mut bytes = [0u8; 32];
    bytes.copy_from_slice(&key_bytes[..32]);
    let verifying_key = VerifyingKey::from_bytes(&bytes)?;

    let sig_path = format!("{}.sig", snapshot_path.to_string_lossy());
    if !Path::new(&sig_path).exists() {
        return Err(format!("Signature file {} not found", sig_path).into());
    }

    let sig_hex = fs::read_to_string(&sig_path)?;
    let sig_bytes = hex::decode(sig_hex.trim())?;
    if sig_bytes.len() < 64 {
        return Err("Signature file must be 64 bytes".into());
    }
    let mut sig_arr = [0u8; 64];
    sig_arr.copy_from_slice(&sig_bytes[..64]);
    let signature = Signature::from_bytes(&sig_arr);

    let file_data = fs::read(snapshot_path)?;
    let header_hash = Sha256::digest(&file_data[..64]);

    verifying_key.verify(&header_hash, &signature)?;

    println!("SUCCESS: Ed25519 signature is VALID.");
    Ok(())
}

mod hex {
    pub fn encode<T: AsRef<[u8]>>(data: T) -> String {
        data.as_ref()
            .iter()
            .map(|b| format!("{:02x}", b))
            .collect::<Vec<_>>()
            .join("")
    }

    pub fn decode(s: &str) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
        let s = s.trim();
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(|e| e.into()))
            .collect()
    }
}

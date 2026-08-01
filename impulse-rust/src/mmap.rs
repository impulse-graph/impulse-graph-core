//! Zero-Dependency Memory & File Mapping Abstraction

use std::fs::File;
use std::io::{self, Read};
use std::ops::Deref;
use std::path::Path;
use std::sync::Arc;

pub enum MemoryMapStorage {
    Slice(Vec<u8>),
}

pub struct MemoryMap {
    storage: MemoryMapStorage,
}

impl MemoryMap {
    pub fn from_vec(vec: Vec<u8>) -> Self {
        Self {
            storage: MemoryMapStorage::Slice(vec),
        }
    }

    pub fn open<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let mut file = File::open(path)?;
        let metadata = file.metadata()?;
        let len = metadata.len() as usize;

        if len == 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "File size is 0",
            ));
        }

        let mut buf = Vec::with_capacity(len);
        file.read_to_end(&mut buf)?;
        Ok(Self {
            storage: MemoryMapStorage::Slice(buf),
        })
    }

    pub fn as_slice(&self) -> &[u8] {
        match &self.storage {
            MemoryMapStorage::Slice(vec) => vec.as_slice(),
        }
    }
}

impl Deref for MemoryMap {
    type Target = [u8];
    fn deref(&self) -> &Self::Target {
        self.as_slice()
    }
}

#[derive(Clone)]
pub struct SharedMemoryMap(Arc<MemoryMap>);

impl SharedMemoryMap {
    pub fn new(mmap: MemoryMap) -> Self {
        Self(Arc::new(mmap))
    }

    pub fn as_slice(&self) -> &[u8] {
        self.0.as_slice()
    }
}

impl Deref for SharedMemoryMap {
    type Target = [u8];
    fn deref(&self) -> &Self::Target {
        self.0.as_slice()
    }
}

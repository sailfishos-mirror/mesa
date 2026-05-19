// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#[derive(Debug)]
pub enum EncodeError {
    Int(std::num::TryFromIntError),
    Str(&'static str),
}

impl From<std::convert::Infallible> for EncodeError {
    fn from(_err: std::convert::Infallible) -> EncodeError {
        panic!("Invallable can't happen");
    }
}

impl From<std::num::TryFromIntError> for EncodeError {
    fn from(err: std::num::TryFromIntError) -> EncodeError {
        EncodeError::Int(err)
    }
}

impl From<&'static str> for EncodeError {
    fn from(err: &'static str) -> EncodeError {
        EncodeError::Str(err)
    }
}

struct ArchSet {
    bits: u32,
}

impl ArchSet {
    pub fn contains(&self, arch: u8) -> bool {
        assert!(arch < 32);
        ((self.bits >> arch) & 1) != 0
    }
}

pub trait Encode {
    type Encoded;

    fn encode(self) -> Self::Encoded;
}

pub trait TryEncode {
    type Encoded;
    type Error;

    fn try_encode(self, arch: u8) -> Result<Self::Encoded, Self::Error>;
}

impl<T: Encode> TryEncode for T {
    type Encoded = <T as Encode>::Encoded;
    type Error = std::convert::Infallible;

    fn try_encode(self, _arch: u8) -> Result<Self::Encoded, Self::Error> {
        Ok(self.encode())
    }
}

pub trait TryDecode<T>: Sized {
    type Error;

    fn try_decode(value: T, arch: u8) -> Result<Self, Self::Error>;
}

pub mod v9 {
    use kraid_proc_macros::gen_isa_encode;
    gen_isa_encode!("isa-v9-v14.xml", 9..=14);
}

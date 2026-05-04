// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::float16::F16;
use std::fmt;
use std::num::NonZeroU16;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq)]
enum ByteMod {
    // There is no zero value by design
    Byte = 1,
    Sign = 2,
    Fext = 3,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq)]
enum SwizzleByte {
    // There is no zero value by design
    Invalid1 = 1,
    Invalid2 = 2,
    Zero = 3,
    Byte0 = ((ByteMod::Byte as u8) << 2) | 0,
    Byte1 = ((ByteMod::Byte as u8) << 2) | 1,
    Byte2 = ((ByteMod::Byte as u8) << 2) | 2,
    Byte3 = ((ByteMod::Byte as u8) << 2) | 3,
    Sign0 = ((ByteMod::Sign as u8) << 2) | 0,
    Sign1 = ((ByteMod::Sign as u8) << 2) | 1,
    Sign2 = ((ByteMod::Sign as u8) << 2) | 2,
    Sign3 = ((ByteMod::Sign as u8) << 2) | 3,
    Fext0 = ((ByteMod::Fext as u8) << 2) | 0,
    Fext1 = ((ByteMod::Fext as u8) << 2) | 1,
    Fext2 = ((ByteMod::Fext as u8) << 2) | 2,
    Fext3 = ((ByteMod::Fext as u8) << 2) | 3,
}

impl fmt::Display for SwizzleByte {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SwizzleByte::Invalid1 | SwizzleByte::Invalid2 => {
                panic!("Invalid swizzle");
            }
            SwizzleByte::Zero => write!(f, "z"),
            _ => {
                let idx = self.byte_idx().unwrap();
                match self.byte_mod().unwrap() {
                    ByteMod::Byte => write!(f, "b{idx}"),
                    ByteMod::Sign => write!(f, "s{idx}"),
                    ByteMod::Fext => write!(f, "f{idx}"),
                }
            }
        }
    }
}

impl SwizzleByte {
    const fn byte(byte_idx: u8) -> Self {
        SwizzleByte::from_byte_mod_idx(ByteMod::Byte, byte_idx)
    }

    const fn sign(byte_idx: u8) -> Self {
        SwizzleByte::from_byte_mod_idx(ByteMod::Sign, byte_idx)
    }

    const fn fext(byte_idx: u8) -> Self {
        SwizzleByte::from_byte_mod_idx(ByteMod::Fext, byte_idx)
    }

    const fn from_byte_mod_idx(byte_mod: ByteMod, byte_idx: u8) -> Self {
        // SAFETY: Every value of ButeMod is represented in SwizzleByte as
        // well as every index < 4
        assert!(byte_idx < 4);
        unsafe { std::mem::transmute(((byte_mod as u8) << 2) | byte_idx) }
    }

    fn byte_mod_raw(self) -> u8 {
        (self as u8) >> 2
    }

    fn byte_idx_raw(self) -> u8 {
        (self as u8) & 0x3
    }

    fn is_byte_mod_idx(self) -> bool {
        self.byte_mod_raw() != 0
    }

    fn is_byte_mod_idx_or_zero(self) -> bool {
        self.is_byte_mod_idx() || self == SwizzleByte::Zero
    }

    pub fn byte_mod(self) -> Option<ByteMod> {
        if self.is_byte_mod_idx() {
            // SAFETY: Every 2-bit raw mod value other than 0 is a ByteMod
            Some(unsafe { std::mem::transmute(self.byte_mod_raw()) })
        } else {
            None
        }
    }

    pub fn byte_idx(self) -> Option<u8> {
        if self.is_byte_mod_idx() {
            Some(self.byte_idx_raw())
        } else {
            None
        }
    }
}

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq)]
pub struct Swizzle {
    packed: NonZeroU16,
}

macro_rules! swizzle {
    ($b0: ident, $b1: ident, $b2: ident, $b3: ident) => {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::$b0,
            SwizzleByte::$b1,
            SwizzleByte::$b2,
            SwizzleByte::$b3,
        ])
    };
}

impl Swizzle {
    /// The identity swizzle
    pub const NONE: Swizzle = Swizzle::from_bytes([0, 1, 2, 3]);

    pub const B0000: Swizzle = Swizzle::replicate_byte(0);
    pub const B1111: Swizzle = Swizzle::replicate_byte(1);
    pub const B2222: Swizzle = Swizzle::replicate_byte(2);
    pub const B3333: Swizzle = Swizzle::replicate_byte(3);

    pub const B0123: Swizzle = Swizzle::from_bytes([0, 1, 2, 3]);
    pub const B0101: Swizzle = Swizzle::from_bytes([0, 1, 0, 1]);
    pub const B2301: Swizzle = Swizzle::from_bytes([2, 3, 0, 1]);
    pub const B2323: Swizzle = Swizzle::from_bytes([2, 3, 2, 3]);

    pub const H00: Swizzle = Swizzle::B0101;
    pub const H01: Swizzle = Swizzle::B0123;
    pub const H10: Swizzle = Swizzle::B2301;
    pub const H11: Swizzle = Swizzle::B2323;

    /// A swizzle which selects the first half word and widens it from a
    /// 16-bit to a 32-bit floating point value.
    pub const HF0: Swizzle = Swizzle::widen_f16(0);

    /// A swizzle which selects the second half word and widens it from a
    /// 16-bit to a 32-bit floating point value.
    pub const HF1: Swizzle = Swizzle::widen_f16(1);

    pub const ALL64: Swizzle = swizzle!(Invalid1, Invalid1, Invalid1, Invalid1);
    const LOW32: Swizzle = swizzle!(Invalid1, Invalid1, Invalid1, Invalid2);

    const fn from_swizzle_bytes(bytes: [SwizzleByte; 4]) -> Swizzle {
        let b0 = bytes[0] as u16;
        let b1 = bytes[1] as u16;
        let b2 = bytes[2] as u16;
        let b3 = bytes[3] as u16;
        let packed = b0 | (b1 << 4) | (b2 << 8) | (b3 << 12);

        // SAFETY: SwizzleByte cannot be zero so neither can packed
        debug_assert!(packed != 0);
        let packed = unsafe { NonZeroU16::new_unchecked(packed) };

        Swizzle { packed }
    }

    pub const fn from_bytes(bytes: [u8; 4]) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(bytes[0]),
            SwizzleByte::byte(bytes[1]),
            SwizzleByte::byte(bytes[2]),
            SwizzleByte::byte(bytes[3]),
        ])
    }

    pub const fn replicate_byte(byte: u8) -> Swizzle {
        Swizzle::from_bytes([byte, byte, byte, byte])
    }

    pub const fn from_halves(halves: [u8; 2]) -> Swizzle {
        Swizzle::from_bytes([
            halves[0] * 2,
            halves[0] * 2 + 1,
            halves[1] * 2,
            halves[1] * 2 + 1,
        ])
    }

    pub const fn replicate_half(half: u8) -> Swizzle {
        Swizzle::from_halves([half, half])
    }

    pub const fn widen_s8(byte: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(byte),
            SwizzleByte::sign(byte),
            SwizzleByte::sign(byte),
            SwizzleByte::sign(byte),
        ])
    }

    pub const fn widen_u8(byte: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(byte),
            SwizzleByte::Zero,
            SwizzleByte::Zero,
            SwizzleByte::Zero,
        ])
    }

    pub const fn widen_f16(half: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::fext(half * 2),
            SwizzleByte::fext(half * 2 + 1),
            SwizzleByte::fext(half * 2),
            SwizzleByte::fext(half * 2 + 1),
        ])
    }

    pub const fn widen_s16(half: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(half * 2),
            SwizzleByte::byte(half * 2 + 1),
            SwizzleByte::sign(half * 2 + 1),
            SwizzleByte::sign(half * 2 + 1),
        ])
    }

    pub const fn widen_u16(half: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(half * 2),
            SwizzleByte::byte(half * 2 + 1),
            SwizzleByte::Zero,
            SwizzleByte::Zero,
        ])
    }

    pub const fn widen_v2s8(x_byte: u8, y_byte: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(x_byte),
            SwizzleByte::sign(x_byte),
            SwizzleByte::byte(y_byte),
            SwizzleByte::sign(y_byte),
        ])
    }

    pub const fn widen_v2u8(x_byte: u8, y_byte: u8) -> Swizzle {
        Swizzle::from_swizzle_bytes([
            SwizzleByte::byte(x_byte),
            SwizzleByte::Zero,
            SwizzleByte::byte(y_byte),
            SwizzleByte::Zero,
        ])
    }

    const fn byte(&self, idx: u8) -> SwizzleByte {
        assert!(idx < 4);
        let b = ((self.packed.get() >> (idx * 4)) & 0xf) as u8;

        // SAFETY: We only ever construct Swizzle from SwizzleByte
        debug_assert!(b != 0);
        unsafe { std::mem::transmute(b) }
    }

    /// Applies this swizzle to a u32 value
    pub fn fold_u32(&self, u: u32) -> Option<u32> {
        let mut folded = 0_u32;
        let mut has_fext = false;
        for i in 0..4 {
            let sb = self.byte(i);
            if sb == SwizzleByte::Zero {
                continue;
            }

            let sbi = sb.byte_idx()?;
            let sbm = sb.byte_mod()?;

            let mut b = (u >> (sbi * 8)) as u8;
            if sbm == ByteMod::Sign {
                b = ((b as i8) >> 7) as u8;
            } else if sbm == ByteMod::Fext {
                has_fext = true;
            }

            folded |= (b as u32) << (i * 8);
        }

        if has_fext {
            debug_assert!(*self == Swizzle::HF0 || *self == Swizzle::HF1);
            // We already moved the data around, just widen
            folded = f32::from(F16::from_bits(folded as u16)).to_bits()
        }

        Some(folded)
    }

    pub fn bytes_read(&self) -> u8 {
        match *self {
            Swizzle::ALL64 => 0xff,
            Swizzle::LOW32 => 0x0f,
            _ => {
                let mut bytes = 0_u8;
                for i in 0..4 {
                    if let Some(b) = self.byte(i).byte_idx() {
                        bytes |= 1 << b;
                    }
                }
                bytes
            }
        }
    }

    pub fn replicates_byte(&self) -> bool {
        let b0 = self.byte(0);

        b0.is_byte_mod_idx_or_zero()
            && self.byte(1) == b0
            && self.byte(2) == b0
            && self.byte(3) == b0
    }

    pub fn replicates_half(&self) -> bool {
        let b0 = self.byte(0);
        let b1 = self.byte(1);

        b0.is_byte_mod_idx_or_zero()
            && b1.is_byte_mod_idx_or_zero()
            && self.byte(2) == b0
            && self.byte(3) == b1
    }

    pub fn swizzle(self, other: Swizzle) -> Option<Swizzle> {
        if self == Swizzle::ALL64 || self == Swizzle::LOW32 {
            unimplemented!("64-bit swizzling");
        }

        if other == Swizzle::ALL64 || other == Swizzle::LOW32 {
            unimplemented!("64-bit swizzling");
        }

        if other == Swizzle::NONE {
            return Some(self);
        } else if self == Swizzle::NONE {
            return Some(other);
        }

        let mut has_fext = false;
        let mut bytes = [SwizzleByte::Zero; 4];
        for i in 0..4 {
            let ob = other.byte(i);
            bytes[usize::from(i)] = if ob == SwizzleByte::Zero {
                SwizzleByte::Zero
            } else {
                let obi = ob.byte_idx()?;
                let sb = self.byte(obi);
                if sb == SwizzleByte::Zero {
                    SwizzleByte::Zero
                } else {
                    let sbi = sb.byte_idx()?;
                    let obm = ob.byte_mod()?;
                    let sbm = sb.byte_mod()?;

                    use ByteMod::*;

                    let m = match (obm, sbm) {
                        (Byte, Byte | Sign) => sbm,
                        (Sign, Byte | Sign) => Sign,
                        (Fext, Byte) => {
                            // We allow Fext of byte because we can swizzle the
                            // source of a f16 widen.  The has_fext check below
                            // will ensure that we end with one of H0 or H1.
                            //
                            // We don't allow byte of fext because the result
                            // of the widen is an f32 and that doesn't make
                            // sense to swizzle.
                            has_fext = true;
                            Fext
                        }
                        _ => return None,
                    };
                    SwizzleByte::from_byte_mod_idx(m, sbi)
                }
            };
        }

        let swz = Swizzle::from_swizzle_bytes(bytes);
        if has_fext && swz != Swizzle::HF0 && swz != Swizzle::HF1 {
            return None;
        }

        Some(swz)
    }
}

impl Default for Swizzle {
    fn default() -> Swizzle {
        Swizzle::NONE
    }
}

impl fmt::Display for Swizzle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            Swizzle::NONE => Ok(()),
            Swizzle::ALL64 => Ok(()),
            Swizzle::LOW32 => write!(f, ".low32"),
            Swizzle::H00 => write!(f, ".h00"),
            Swizzle::H10 => write!(f, ".h10"),
            Swizzle::H11 => write!(f, ".h11"),
            Swizzle::HF0 => write!(f, ".hf0"),
            Swizzle::HF1 => write!(f, ".hf1"),
            _ => {
                let mut is_bytes = true;
                for i in 0..4 {
                    if self.byte(i).byte_mod() != Some(ByteMod::Byte) {
                        is_bytes = false;
                        break;
                    }
                }

                if is_bytes {
                    write!(f, ".b")?;
                    for i in 0..4 {
                        write!(f, "{}", self.byte(i).byte_idx().unwrap())?;
                    }
                } else {
                    write!(f, ".")?;
                    for i in 0..4 {
                        write!(f, "{}", self.byte(i))?;
                    }
                }
                Ok(())
            }
        }
    }
}

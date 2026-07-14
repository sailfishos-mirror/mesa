// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::bitset::IntoBitIndex;

use std::fmt;

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct Phi(u32);

impl Phi {
    fn new(idx: u32, bits: u8) -> Phi {
        assert!(idx < (1 << 30));
        let mut packed = idx;
        assert!(8 <= bits && bits <= 64 && bits.is_power_of_two());
        packed |= (bits.ilog2() - 3) << 30;
        Phi(packed)
    }

    pub fn idx(&self) -> u32 {
        self.0 & 0x3fffffff
    }

    pub fn bits(&self) -> u8 {
        self.bytes() * 8
    }

    pub fn bytes(&self) -> u8 {
        1 << (self.0 >> 30)
    }
}

impl fmt::Display for Phi {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let m = match self.bits() {
            8 => ":b",
            16 => ":h",
            32 => ":w",
            64 => ":q",
            _ => panic!("Invalid SSA value bits"),
        };
        write!(f, "φ{}{m}", self.idx())
    }
}

impl IntoBitIndex for Phi {
    fn into_bit_index(self) -> usize {
        // Indices are guaranteed unique by the allocator
        self.idx().try_into().unwrap()
    }
}

#[derive(Default)]
pub struct PhiAllocator {
    count: u32,
}

impl PhiAllocator {
    /// Allocates an phi.
    pub fn alloc(&mut self, bits: u8) -> Phi {
        let idx = self.count;
        self.count += 1;
        Phi::new(idx, bits)
    }
}

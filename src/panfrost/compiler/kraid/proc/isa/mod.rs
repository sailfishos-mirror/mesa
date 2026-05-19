// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

pub mod encoder;

use proc_macro2::TokenStream as TokenStream2;
use quote::ToTokens;
use std::ops::Range;

#[macro_export]
macro_rules! ident {
    ($($args:expr),*) => {{
        let names = format!($($args),*);
        Ident::new(&names, Span::call_site())
    }};
}

mod result {
    #[allow(dead_code)]
    #[derive(Debug)]
    enum ErrorKind {
        XmlError(::xml::reader::Error),
        IsaError(&'static str),
    }

    #[allow(dead_code)]
    #[derive(Debug)]
    pub struct Error {
        kind: ErrorKind,
    }

    impl From<::xml::reader::Error> for Error {
        fn from(err: ::xml::reader::Error) -> Error {
            Error {
                kind: ErrorKind::XmlError(err),
            }
        }
    }

    impl From<std::io::Error> for Error {
        fn from(err: std::io::Error) -> Error {
            let kind = ::xml::reader::ErrorKind::Io(err);
            ::xml::reader::Error::from(kind).into()
        }
    }

    impl From<&'static str> for Error {
        fn from(s: &'static str) -> Error {
            Error {
                kind: ErrorKind::IsaError(s),
            }
        }
    }

    pub fn err(s: &'static str) -> Error {
        s.into()
    }

    pub type Result<T> = std::result::Result<T, Error>;
}

pub use result::{err, Error, Result};

pub(self) fn to_camel_case(s: &str) -> String {
    let mut out = String::new();
    let mut next_upper = true;
    for c in s.chars() {
        if c == '_' {
            next_upper = true;
        } else if next_upper {
            out.extend(c.to_uppercase());
            next_upper = false;
        } else {
            out.extend(c.to_lowercase());
        }
    }
    out
}

pub(self) fn to_snake_case(s: &str) -> String {
    let mut out = String::new();
    for c in s.chars() {
        if c.is_uppercase() {
            out.push('_');
            out.extend(c.to_lowercase());
        } else {
            out.push(c);
        }
    }
    out
}

pub type ArchRange = std::ops::Range<u8>;

#[derive(Clone, Copy, Default, PartialEq)]
pub struct ArchSet {
    bits: u32,
}

impl ArchSet {
    pub fn new() -> ArchSet {
        Default::default()
    }

    pub fn is_empty(&self) -> bool {
        self.bits == 0
    }

    pub fn contains(&self, arch: u8) -> bool {
        assert!(arch < 32);
        ((self.bits >> arch) & 1) != 0
    }

    pub fn first(&self) -> Option<u8> {
        let first = self.bits.trailing_zeros();
        if first < 32 {
            Some(first as u8)
        } else {
            None
        }
    }

    pub fn insert(&mut self, arch: u8) -> bool {
        let exists = self.contains(arch);
        self.bits |= 1 << arch;
        exists
    }

    pub fn insert_range(&mut self, arch: Range<u8>) {
        assert!(arch.end <= 32);
        if !arch.is_empty() {
            let mask = (!0_u32) >> (32 - arch.len());
            self.bits |= mask << arch.start;
        }
    }
}

impl ToTokens for ArchSet {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let bits = self.bits;
        tokens.extend(quote! {
            ArchSet { bits: #bits }
        });
    }
}

impl From<Range<u8>> for ArchSet {
    fn from(range: Range<u8>) -> Self {
        let mut set = ArchSet::new();
        set.insert_range(range);
        set
    }
}

impl std::ops::BitOrAssign<ArchSet> for ArchSet {
    fn bitor_assign(&mut self, rhs: ArchSet) {
        self.bits |= rhs.bits;
    }
}

pub struct ISA {
    pub arch: Range<u8>,
}

impl ISA {
    pub fn from_xml_file(file: std::fs::File, arch: Range<u8>) -> Result<ISA> {
        Ok(ISA { arch })
    }
}

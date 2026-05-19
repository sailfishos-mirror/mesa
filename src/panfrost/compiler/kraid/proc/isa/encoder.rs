// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::isa::*;
use proc_macro2::TokenStream as TokenStream2;

pub fn gen_encoder(
    xml_file: &str,
    arch: std::ops::Range<u8>,
) -> Result<TokenStream2> {
    let isa = ISA::from_xml_file(std::fs::File::open(xml_file)?, arch)?;

    let ts = quote! {
        use crate::isa::*;
    };

    Ok(ts)
}

// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::isa::*;
use proc_macro2::TokenStream as TokenStream2;
use quote::ToTokens;

pub fn gen_encoder(
    xml_file: &str,
    arch: std::ops::Range<u8>,
) -> Result<TokenStream2> {
    let mut isa = ISA::from_xml_file(std::fs::File::open(xml_file)?, arch)?;

    let mut ts = quote! {
        use crate::isa::*;
    };

    isa.enums
        .add_meta_enum(
            "src_swizzle",
            [
                "lane8_m",
                "lane16_m",
                "lane_all_m",
                "lanes_int_m",
                "shift_bitwise_i16_lanes_m",
                "shift_bitwise_i32_lanes_m",
                "swiz8_2_full_m",
                "swiz_int_m",
                "swiz_m",
                "widen_int_m",
                "widen_m",
            ],
        )
        .expect("Failed to create src_swizzle meta-enum");

    isa.enums.declare(&mut ts);

    Ok(ts)
}

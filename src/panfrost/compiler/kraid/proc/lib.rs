// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate proc_macro;
extern crate proc_macro2;
#[macro_use]
extern crate quote;
extern crate syn;

use compiler_proc::as_slice::*;
use proc_macro::TokenStream;

mod data_type;
mod ir;

#[proc_macro_attribute]
pub fn variants(attr: TokenStream, item: TokenStream) -> TokenStream {
    ir::variants(attr, item)
}

#[proc_macro_derive(Opcode, attributes(src_type, dst_type))]
pub fn derive_opcode(input: TokenStream) -> TokenStream {
    let mut s = TokenStream::new();
    s.extend(derive_as_slice(
        input.clone(),
        "Src",
        "src_type",
        "DataType",
    ));
    s.extend(derive_as_slice(
        input.clone(),
        "Dst",
        "dst_type",
        "DataType",
    ));
    s.extend(ir::derive_opcode(input));
    s
}

#[proc_macro_derive(DataType)]
pub fn derive_data_type(input: TokenStream) -> TokenStream {
    data_type::derive_data_type(input)
}

#[proc_macro_derive(FromVariants)]
pub fn derive_from_variants(input: TokenStream) -> TokenStream {
    compiler_proc::from_variants::derive_from_variants(input)
}

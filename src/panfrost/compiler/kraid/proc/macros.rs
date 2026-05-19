// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate kraid_proc;

use compiler_proc::as_slice::*;
use kraid_proc::*;
use proc_macro::TokenStream;

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

#[proc_macro_derive(AsmSwizzleWiden)]
pub fn derive_asm_swizzle_widen(input: TokenStream) -> TokenStream {
    swizzle::derive_asm_swizzle_widen(input)
}

#[proc_macro_derive(EnumAsU8)]
pub fn derive_enum_as_u8(input: TokenStream) -> TokenStream {
    compiler_proc::enum_as_u8::derive_enum_as_u8(input)
}

#[proc_macro_derive(FromVariants)]
pub fn derive_from_variants(input: TokenStream) -> TokenStream {
    compiler_proc::from_variants::derive_from_variants(input)
}

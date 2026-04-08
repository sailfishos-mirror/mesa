// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate proc_macro;
extern crate proc_macro2;
#[macro_use]
extern crate quote;
extern crate syn;

use proc_macro::TokenStream;

mod data_type;

#[proc_macro_derive(DataType)]
pub fn derive_data_type(input: TokenStream) -> TokenStream {
    data_type::derive_data_type(input)
}

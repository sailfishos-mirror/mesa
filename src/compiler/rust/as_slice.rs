// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::ops::Deref;
use std::ops::DerefMut;

/// A trait which indicates that the given type may be viewed as a slice of
/// (some of) its fields.
pub trait AsSlice<T> {
    /// The type of field attributes.  If no field attributes are needed, this
    /// can be `()`.
    type Attr;

    /// Returns a slice representing the fields of type `T`.
    fn as_slice(&self) -> &[T];

    /// Returns a mut slice representing the fields of type `T`.
    fn as_mut_slice(&mut self) -> &mut [T];

    /// Returns a slice containing the field attributes for fields of type T.
    fn attrs(&self) -> &'static [Self::Attr];
}

impl<T, A> AsSlice<T> for Box<A>
where
    A: AsSlice<T>,
{
    type Attr = A::Attr;

    fn as_slice(&self) -> &[T] {
        self.deref().as_slice()
    }
    fn as_mut_slice(&mut self) -> &mut [T] {
        self.deref_mut().as_mut_slice()
    }
    fn attrs(&self) -> &'static [Self::Attr] {
        self.deref().attrs()
    }
}

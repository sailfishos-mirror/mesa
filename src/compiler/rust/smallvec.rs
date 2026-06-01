// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

/// `SmallVec` is an optimized data structure that handles collections of items.
/// It is designed to avoid allocating a `Vec` unless multiple items are present.
///
/// # Variants
///
/// * `None` - Represents an empty collection, no items are stored.
/// * `One(T)` - Stores a single item without allocating a `Vec`.
/// * `Many(Vec<T>)` - Stores multiple items in a heap-allocated `Vec`.
///
/// This helps to reduce the amount of Vec's allocated in the optimization passes.
#[derive(Clone, Default)]
pub enum SmallVec<T> {
    #[default]
    None,
    One(T),
    Many(Vec<T>),
}

impl<T> SmallVec<T> {
    /// Constructs a new, empty `SmallVec`
    pub fn new() -> SmallVec<T> {
        Default::default()
    }

    /// Adds an item to the `SmallVec`.
    ///
    /// If the collection is empty (`None`), the item is stored as `One`.
    /// If the collection has one item (`One`), it transitions to `Many` and both items are stored in a `Vec`.
    /// If the collection is already in the `Many` variant, the new item is pushed into the existing `Vec`.
    ///
    /// # Arguments
    ///
    /// * `item` - The item to be added.
    ///
    /// # Example
    ///
    /// ```
    /// let mut vec: SmallVec<String> = SmallVec::None;
    /// vec.push("Hello".to_string());
    /// vec.push("World".to_string());
    /// ```
    pub fn push(&mut self, i: T) {
        self.push_mut(i);
    }

    /// Adds an item to the `SmallVec`, returning a reference to it.
    ///
    /// If the collection is empty (`None`), the item is stored as `One`.
    /// If the collection has one item (`One`), it transitions to `Many` and both items are stored in a `Vec`.
    /// If the collection is already in the `Many` variant, the new item is pushed into the existing `Vec`.
    ///
    /// # Arguments
    ///
    /// * `item` - The item to be added.
    ///
    /// # Example
    ///
    /// ```
    /// let mut vec: SmallVec<String> = SmallVec::None;
    /// let item = vec.push_mut("Hello".to_string());
    /// *item += "World";
    /// ```
    pub fn push_mut(&mut self, i: T) -> &mut T {
        match self {
            SmallVec::None => {
                *self = SmallVec::One(i);
                match self {
                    SmallVec::One(i) => i,
                    _ => panic!("Not a One"),
                }
            }
            SmallVec::One(_) => {
                *self = match std::mem::take(self) {
                    SmallVec::One(o) => SmallVec::Many(vec![o, i]),
                    _ => panic!("Not a One"),
                };
                match self {
                    SmallVec::Many(v) => v.last_mut().unwrap(),
                    _ => panic!("Not a Many"),
                }
            }
            SmallVec::Many(v) => {
                // TODO: Replace with v.push_mut() when we update to
                // Rust 1.95.0 or newer.
                v.push(i);
                v.last_mut().unwrap()
            }
        }
    }
}

impl<T> std::ops::Deref for SmallVec<T> {
    type Target = [T];

    fn deref(&self) -> &[T] {
        match self {
            SmallVec::None => &[],
            SmallVec::One(i) => std::slice::from_ref(i),
            SmallVec::Many(v) => v,
        }
    }
}

impl<T> std::ops::DerefMut for SmallVec<T> {
    fn deref_mut(&mut self) -> &mut [T] {
        match self {
            SmallVec::None => &mut [],
            SmallVec::One(i) => std::slice::from_mut(i),
            SmallVec::Many(v) => v,
        }
    }
}

impl<T> Extend<T> for SmallVec<T> {
    fn extend<I>(&mut self, iter: I)
    where
        I: IntoIterator<Item = T>,
    {
        for i in iter {
            self.push(i)
        }
    }
}

impl<T> From<Vec<T>> for SmallVec<T> {
    fn from(v: Vec<T>) -> SmallVec<T> {
        if v.is_empty() {
            SmallVec::None
        } else if v.len() == 1 {
            // Hopefully, Rust can fold away most of this based on the
            // `v.len() == 1` check above.
            SmallVec::One(v.into_iter().next().unwrap())
        } else {
            SmallVec::Many(v)
        }
    }
}

impl<T, const N: usize> From<[T; N]> for SmallVec<T> {
    fn from(i: [T; N]) -> SmallVec<T> {
        i.into_iter().collect()
    }
}

impl<T> FromIterator<T> for SmallVec<T> {
    fn from_iter<I>(iter: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut iter = iter.into_iter();
        let Some(x) = iter.next() else {
            return SmallVec::None;
        };
        let Some(y) = iter.next() else {
            return SmallVec::One(x);
        };
        SmallVec::Many([x, y].into_iter().chain(iter).collect())
    }
}

enum IntoIterImpl<T> {
    None,
    One(T),
    Many(std::vec::IntoIter<T>),
}

pub struct IntoIter<T>(IntoIterImpl<T>);

impl<T> Iterator for IntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        match &mut self.0 {
            IntoIterImpl::None => None,
            IntoIterImpl::One(_) => {
                match std::mem::replace(&mut self.0, IntoIterImpl::None) {
                    IntoIterImpl::One(i) => Some(i),
                    _ => panic!("Not a One"),
                }
            }
            IntoIterImpl::Many(vi) => vi.next(),
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        match &self.0 {
            IntoIterImpl::None => (0, Some(0)),
            IntoIterImpl::One(_) => (1, Some(1)),
            IntoIterImpl::Many(vi) => vi.size_hint(),
        }
    }
}

impl<T> std::iter::ExactSizeIterator for IntoIter<T> {}
impl<T> std::iter::FusedIterator for IntoIter<T> {}

impl<T> IntoIterator for SmallVec<T> {
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> IntoIter<T> {
        let imp = match self {
            SmallVec::None => IntoIterImpl::None,
            SmallVec::One(i) => IntoIterImpl::One(i),
            SmallVec::Many(v) => IntoIterImpl::Many(v.into_iter()),
        };
        IntoIter(imp)
    }
}

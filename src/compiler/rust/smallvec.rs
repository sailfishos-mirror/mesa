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

    /// Returns a mutable reference to the last item in the `SmallVec`, if it exists.
    ///
    /// * If the collection is empty (`None`), it returns `None`.
    /// * If the collection has one item (`One`), it returns a mutable reference to that item.
    /// * If the collection has multiple items (`Many`), it returns a mutable reference to the last item in the `Vec`.
    ///
    /// # Returns
    ///
    /// * `Option<&mut T>` - A mutable reference to the last item, or `None` if the collection is empty.
    ///
    /// # Example
    ///
    /// ```
    /// let mut vec: SmallVec<i32> = SmallVec::None;
    /// vec.push(1);
    /// vec.push(2);
    ///
    /// if let Some(last) = vec.last_mut() {
    ///     *last = 10;  // Modify the last element.
    /// }
    /// ```
    pub fn last_mut(&mut self) -> Option<&mut T> {
        match self {
            SmallVec::None => None,
            SmallVec::One(item) => Some(item),
            SmallVec::Many(v) => v.last_mut(),
        }
    }
}

// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The tables behind the natives' handles: Pawn holds a positive cell, 0 is never a handle.

use amxx::Cell;

/// Objects by handle. Freed slots are reused, so a stale handle may name a newer object, as
/// with AMX Mod X's own handles (`TrieDestroy` sets the variable to 0 for that reason, and so do
/// this repo's `*_free` natives).
#[derive(Debug)]
pub struct Handles<T> {
    slots: Vec<Option<T>>,
}

impl<T> Default for Handles<T> {
    fn default() -> Self {
        Handles { slots: Vec::new() }
    }
}

impl<T> Handles<T> {
    /// Stores `value` and returns its handle (1 or more).
    pub fn insert(&mut self, value: T) -> Cell {
        let i = match self.slots.iter().position(Option::is_none) {
            Some(i) => {
                self.slots[i] = Some(value);
                i
            }
            None => {
                self.slots.push(Some(value));
                self.slots.len() - 1
            }
        };
        i as Cell + 1
    }

    fn index(h: Cell) -> Option<usize> {
        usize::try_from(h).ok()?.checked_sub(1)
    }

    /// The object behind `h`.
    pub fn get(&self, h: Cell) -> Option<&T> {
        self.slots.get(Self::index(h)?)?.as_ref()
    }

    /// The object behind `h`, mutably.
    pub fn get_mut(&mut self, h: Cell) -> Option<&mut T> {
        self.slots.get_mut(Self::index(h)?)?.as_mut()
    }

    /// Takes the object out; its handle becomes free.
    pub fn remove(&mut self, h: Cell) -> Option<T> {
        self.slots.get_mut(Self::index(h)?)?.take()
    }

    /// Live objects.
    pub fn len(&self) -> usize {
        self.slots.iter().filter(|s| s.is_some()).count()
    }

    /// No live objects.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Every live object with its handle.
    pub fn iter(&self) -> impl Iterator<Item = (Cell, &T)> {
        self.slots.iter().enumerate().filter_map(|(i, s)| s.as_ref().map(|v| (i as Cell + 1, v)))
    }

    /// Drops everything.
    pub fn clear(&mut self) {
        self.slots.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handles_start_at_one_and_reuse_slots() {
        let mut h = Handles::default();
        assert_eq!(h.insert("a"), 1);
        assert_eq!(h.insert("b"), 2);
        assert_eq!(h.remove(1), Some("a"));
        assert_eq!(h.get(1), None);
        assert_eq!(h.insert("c"), 1);
        assert_eq!(h.get(0), None);
        assert_eq!(h.get(-3), None);
        assert_eq!(h.get(9), None);
        assert_eq!(h.len(), 2);
        assert_eq!(h.iter().map(|(k, v)| (k, *v)).collect::<Vec<_>>(), [(1, "c"), (2, "b")]);
    }
}

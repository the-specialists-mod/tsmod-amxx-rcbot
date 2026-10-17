// tsmod-amxx-rcbot: RCBot as AMX Mod X modules and a Pawn plugin for the Rust port of The
// Specialists 3.0. GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions; see LICENSE
// and GPL-EXCEPTION.txt.

//! What the modules share: [`Handles`] (the tables behind the natives' handles), [`DataDir`]
//! (RCBot's data directory, `<gamedir>/tsmod/rcbot/`, with atomic writes), [`ModuleRandom`] and
//! small conversions between Pawn cells and Rust values.
//!
//! No `unsafe` here: everything that touches AMX Mod X goes through the `amxx` crate.

#![deny(unsafe_code)]

pub mod data_dir;
pub mod handles;

pub use data_dir::DataDir;
pub use handles::Handles;

use amxx::Cell;
use amxx::native::Args;

/// The data directory below the mod directory, as the standalone `tsmod_rcbot` plugin uses it.
pub const DATA_DIR: &str = "tsmod/rcbot";

/// A float return value: Pawn's `Float:` is the cell holding the float's bits.
pub fn float_cell(v: f32) -> Cell {
    v.to_bits() as Cell
}

/// Reads `n` floats from the array argument `i`.
pub fn read_floats(a: &mut dyn Args, i: usize, n: usize) -> Vec<f32> {
    a.read_array(i, n).into_iter().map(|c| f32::from_bits(c as u32)).collect()
}

/// Writes `values` (at most `max`) into the float array argument `i`; returns how many.
pub fn write_floats(a: &mut dyn Args, i: usize, values: &[f32], max: usize) -> Cell {
    let n = values.len().min(max);
    let cells: Vec<Cell> = values[..n].iter().map(|&v| float_cell(v)).collect();
    a.write_array(i, 0, &cells);
    n as Cell
}

/// A count argument (array sizes): negative counts read as 0.
pub fn count(a: &dyn Args, i: usize) -> usize {
    a.cell(i).max(0) as usize
}

/// Copies `bytes` into the string argument `i` (at most `max` characters), returning how many it
/// copied. Pawn strings are one byte per cell; the core's `SetAmxString` takes UTF-8, so bytes
/// that aren't UTF-8 (names from the engine) are replaced (see the README's limits).
pub fn set_bytes(a: &mut dyn Args, i: usize, bytes: &[u8], max: Cell) -> Cell {
    a.set_string(i, &String::from_utf8_lossy(bytes), max)
}

/// [`set_bytes`] into an `out[], len` pair: the string argument `i` and its length at `i + 1`.
pub fn set_out(a: &mut dyn Args, i: usize, bytes: &[u8]) -> Cell {
    let max = a.cell(i + 1);
    set_bytes(a, i, bytes, max)
}

/// The module's random numbers (`RANDOM_LONG` / `RANDOM_FLOAT` upstream). A pure AMX Mod X
/// module has no engine table (that comes with metamod), so this is a seeded xorshift rather
/// than the engine's sequence: seeded from the clock, or by the plugin for reproducible runs.
pub struct ModuleRandom(rcbot::XorShiftRandom);

impl ModuleRandom {
    /// A generator seeded from the clock.
    pub fn from_clock() -> ModuleRandom {
        let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(1);
        ModuleRandom::seeded(t)
    }

    /// A generator with a fixed seed.
    pub fn seeded(seed: u64) -> ModuleRandom {
        ModuleRandom(rcbot::XorShiftRandom::new(seed))
    }
}

impl rcbot::Random for ModuleRandom {
    fn random_long(&mut self, lo: i32, hi: i32) -> i32 {
        self.0.random_long(lo, hi)
    }

    fn random_float(&mut self, lo: f32, hi: f32) -> f32 {
        self.0.random_float(lo, hi)
    }
}

/// Logs to the AMX Mod X log under the module's tag (stderr in tests).
pub fn log(msg: &str) {
    amxx::core::log(msg);
}

#[cfg(test)]
mod tests {
    use super::*;
    use amxx::native::FakeArgs;

    #[test]
    fn floats_round_trip_through_cells() {
        let mut a = FakeArgs::new(&[]);
        let arr = a.alloc(vec![0; 4]);
        a.params = vec![arr];
        assert_eq!(write_floats(&mut a, 1, &[1.5, -2.0, 3.25], 2), 2);
        assert_eq!(read_floats(&mut a, 1, 3), [1.5, -2.0, 0.0]);
        assert_eq!(f32::from_bits(float_cell(0.75) as u32), 0.75);
    }

    #[test]
    fn seeded_randomness_repeats() {
        use rcbot::Random;
        let (mut x, mut y) = (ModuleRandom::seeded(7), ModuleRandom::seeded(7));
        for _ in 0..10 {
            assert_eq!(x.random_long(0, 100), y.random_long(0, 100));
        }
    }
}

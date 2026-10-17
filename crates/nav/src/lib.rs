// tsmod-amxx-rcbot: RCBot as AMX Mod X modules and a Pawn plugin for the Rust port of The
// Specialists 3.0. RCBot's navigation (waypoint.cpp, bot_navigate.cpp, rc_compress.cpp, by Paul
// "Cheeseh" Murphy) and tsmod-rcbot-rs's additions to it, through `rcbot::nav`.
// GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions; see LICENSE and
// GPL-EXCEPTION.txt.

//! `rcbot_nav`: RCBot's waypoints, visibility, A* and heatmap as AMX Mod X natives
//! (`include/rcbot_nav.inc`, `#pragma reqlib rcbot_nav`).
//!
//! * The map's waypoints (`waypoints/ts/<map>.rcw`) and visibility table (`.rcv`), byte for byte
//!   as the standalone `tsmod_rcbot` writes them, with the queries the bot makes (nearest, goals,
//!   cover, paths, location buckets, visibility). Visibility rows a `.rcv` doesn't hold are traced
//!   a few per frame (`StartFrame`).
//! * The map's heatmap (`.rhm`), which is also A*'s danger source.
//! * Per-bot A* path objects by handle ([`paths`]), searched incrementally across frames.
//! * The waypoint editor's operations, auto-waypointing and its draw data.
//!
//! Unlike the other modules it is also a metamod plugin (`Meta_*`, `GiveFnptrsToDll`): traces,
//! point contents and entity reads need the engine table ([`world`]).

#![deny(unsafe_code)]

pub mod natives;
pub mod paths;
pub mod state;
pub mod world;

#[cfg(test)]
mod tests;

#[allow(unsafe_code)]
mod ffi;

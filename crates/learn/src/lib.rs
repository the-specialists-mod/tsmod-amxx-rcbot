// tsmod-amxx-rcbot: RCBot as AMX Mod X modules and a Pawn plugin for the Rust port of The
// Specialists 3.0. RCBot's learning (ga.cpp, perceptron.cpp, bot_nn.cpp, som.cpp, the learned
// data of bot.cpp and globals.cpp, by Paul "Cheeseh" Murphy) and tsmod-rcbot-rs's learners,
// through `rcbot::learn`. GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions; see
// LICENSE and GPL-EXCEPTION.txt.

//! `rcbot_learn`: RCBot's learners and learned-data files as AMX Mod X natives
//! (`include/rcbot_learn.inc`, `#pragma reqlib rcbot_learn`).
//!
//! * The global learned data (`team<n>.rld`, `tsweaps.rld`, `weaponstats.rwr`, `loadouts.rlo`,
//!   `botprofiles/players.rop`, the map's `.rhm` heatmap), one per server.
//! * Each bot's learned data by handle (`botprofiles/<key>.rld`, `.rwr`, `.rlo`) with the queries
//!   the bot logic makes: spawn and death bookkeeping, the loadout GA and bandit, the jump, duck,
//!   strafe and stunt perceptrons, weapon effectiveness by range, the combat ledger.
//! * The opponent model by player slot.
//! * Generic GAs, perceptrons, neural networks and SOMs by handle ([`objects`]).
//!
//! Every file has the byte layout the standalone `tsmod_rcbot` plugin (and, for `.rld`, RCBot)
//! uses, in the same folder (`<gamedir>/tsmod/rcbot/`).

#![deny(unsafe_code)]

pub mod natives;
pub mod objects;
pub mod state;

#[cfg(test)]
mod tests;

#[allow(unsafe_code)]
mod ffi;

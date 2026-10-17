// tsmod-amxx-rcbot: RCBot as AMX Mod X modules and a Pawn plugin for the Rust port of The
// Specialists 3.0. RCBot's MegaHAL (megahal.cpp, by Paul "Cheeseh" Murphy after PM's RACC port of
// Jason Hutchens' MegaHAL) through tsmod-rcbot-rs's `rcbot::chat`. GPL-3.0-or-later with the AMX
// Mod X and RCBot linking exceptions; see LICENSE and GPL-EXCEPTION.txt.

//! `rcbot_megahal`: the bots' MegaHAL brains and `bot_chat.ini` as AMX Mod X natives
//! (`include/rcbot_megahal.inc`, `#pragma reqlib rcbot_megahal`).
//!
//! Brains are handles (`RcHal`) loaded from and saved to the files the standalone `tsmod_rcbot`
//! plugin uses (`<gamedir>/tsmod/rcbot/botprofiles/<id>_hal.brn` and the keyword lists), so both
//! share them byte for byte. Chat line sets are handles too (`RcChat`); the context a line is
//! filled from (who the bot talks to, the players, the map) comes from the plugin.

#![deny(unsafe_code)]

pub mod natives;
pub mod state;

#[cfg(test)]
mod tests;

#[allow(unsafe_code)]
mod ffi;

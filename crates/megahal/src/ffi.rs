// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The module's exports: `AMXX_*` (from `amxx::export_module!`) and the native table. The only
//! place this crate allows `unsafe` (the macros' generated glue).

use crate::{natives as n, state};
use amxx::module::{ModuleDef, nothing};

amxx::natives! {
    pub static NATIVES = [
        c"hal_load" => n::hal_load,
        c"hal_save" => n::hal_save,
        c"hal_free" => n::hal_free,
        c"hal_learn" => n::hal_learn,
        c"hal_reply" => n::hal_reply,
        c"hal_bot_reply" => n::hal_bot_reply,
        c"hal_words" => n::hal_words,
        c"hal_profile" => n::hal_profile,
        c"hal_seed" => n::hal_seed,
        c"chat_load" => n::chat_load,
        c"chat_free" => n::chat_free,
        c"chat_count" => n::chat_count,
        c"chat_get" => n::chat_get,
        c"chat_context" => n::chat_context,
        c"chat_context_player" => n::chat_context_player,
        c"chat_pick" => n::chat_pick,
        c"chat_rep_array" => n::chat_rep_array,
        c"chat_humanize" => n::chat_humanize,
        c"chat_remove_name_tags" => n::chat_remove_name_tags,
        c"chat_typing_time" => n::chat_typing_time,
    ];
}

fn attach() {
    state::attach(rcbot_amxx_common::DataDir::from_core());
    amxx::core::add_natives(&NATIVES);
}

static MODULE: ModuleDef = ModuleDef {
    name: c"RCBot MegaHAL",
    author: c"Paul \"Cheeseh\" Murphy (RCBot); Rust: tsmod-amxx-rcbot",
    version: match core::ffi::CStr::from_bytes_with_nul(concat!(env!("CARGO_PKG_VERSION"), "\0").as_bytes()) {
        Ok(v) => v,
        Err(_) => c"0",
    },
    logtag: c"RCBOT_MEGAHAL",
    library: c"rcbot_megahal",
    libclass: c"",
    reload: false,
    check_game: None,
    attach,
    detach: nothing,
    plugins_loaded: nothing,
    plugins_unloading: nothing,
    // Plugins reload at every map change: their handles die with them.
    plugins_unloaded: state::forget_handles,
};

amxx::export_module!(MODULE);

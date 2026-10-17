// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The natives, as `fn(&mut dyn Args) -> Cell` so tests run them against `FakeArgs`.
//! `include/rcbot_megahal.inc` documents each one; the names here are the natives' names.

use crate::state::{self, Hal, State, with};
use amxx::Cell;
use amxx::native::Args;
use rcbot::chat::bot_chat::{self, ChatKind, ChatPlayer, ReplyParams};
use rcbot::chat::brain::HalFileOverrides;
use rcbot::chat::model::make_words;
use rcbot::chat::{BotChatMessages, text};
use rcbot_amxx_common::{float_cell, set_bytes, set_out};

/// `hal_bot_reply`'s flags (`HAL_*` in the include).
pub mod flags {
    pub const SENDER_SELF: i32 = 1;
    pub const SENDER_BOT: i32 = 2;
    pub const CHATTING: i32 = 4;
    pub const REAL_MODE: i32 = 8;
    pub const DONT_LEARN: i32 = 16;
}

/// The `ChatKind` of the include's `RcChatKind` values, in `SBotChatMessages`' member order.
pub fn chat_kind(v: Cell) -> Option<ChatKind> {
    use ChatKind::*;
    [Laugh, Thanks, Greetings, Killed, Kills, LeaveGame, Idle, Help].get(usize::try_from(v).ok()?).copied()
}

fn bytes(a: &mut dyn Args, i: usize) -> Vec<u8> {
    a.string(i).into_bytes()
}

fn opt_string(a: &mut dyn Args, i: usize) -> Option<String> {
    Some(a.string(i)).filter(|s| !s.is_empty())
}

/// Runs `f` on the brain behind argument 1, or raises the native error.
fn with_hal(a: &mut dyn Args, f: impl FnOnce(&mut State, Cell, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| if s.hals.get(h).is_some() { Some(f(s, h, a)) } else { None }).flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid HAL handle {h}"));
        0
    })
}

/// Runs `f` on the chat lines behind argument 1, or raises the native error.
fn with_chat(a: &mut dyn Args, f: impl FnOnce(&mut State, &BotChatMessages, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| {
        let m = s.chats.get(h)?.clone();
        Some(f(s, &m, a))
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid chat handle {h}"));
        0
    })
}

/// `RcHal:hal_load(profile_id, const pretrain[] = "", const aux[] = "", const ban[] = "", const swap[] = "")`
pub fn hal_load(a: &mut dyn Args) -> Cell {
    let id = a.cell(1);
    let o = HalFileOverrides { pretrain: opt_string(a, 2), aux: opt_string(a, 3), ban: opt_string(a, 4), swap: opt_string(a, 5) };
    with(|s| {
        let hal = state::wake(&s.dir, id, &o);
        s.hals.insert(hal)
    })
    .unwrap_or(0)
}

/// `bool:hal_save(RcHal:hal)`
pub fn hal_save(a: &mut dyn Args) -> Cell {
    with_hal(a, |s, h, _| s.hals.get(h).is_some_and(|hal| state::save(&s.dir, hal)) as Cell)
}

/// `bool:hal_free(&RcHal:hal, bool:save = false)`: sets the variable to 0.
pub fn hal_free(a: &mut dyn Args) -> Cell {
    let h = a.read_array(1, 1)[0];
    let save = a.cell(2) != 0;
    let freed = with(|s| {
        let hal = s.hals.remove(h)?;
        if save {
            state::save(&s.dir, &hal);
        }
        Some(())
    })
    .flatten();
    if freed.is_none() {
        a.error(&format!("Invalid HAL handle {h}"));
        return 0;
    }
    a.set_ref(1, 0);
    1
}

/// `hal_learn(RcHal:hal, const text[])`: learns a line (as a reply's learning does), returns the
/// number of words it had.
pub fn hal_learn(a: &mut dyn Args) -> Cell {
    let text = bytes(a, 2);
    with_hal(a, |s, h, _| {
        let hal = &mut s.hals.get_mut(h).unwrap().bot;
        let mut lower = text;
        text::strlow(&mut lower);
        make_words(&lower, &mut hal.input_words);
        hal.model.learn(&hal.input_words);
        hal.input_words.len() as Cell
    })
}

/// `hal_reply(RcHal:hal, const text[], out[], len)`: MegaHAL's reply to `text`, learning
/// nothing; the number of characters written (0: no reply).
pub fn hal_reply(a: &mut dyn Args) -> Cell {
    let mut msg = bytes(a, 2);
    with_hal(a, |s, h, a| {
        let State { hals, rng, .. } = s;
        let hal = &mut hals.get_mut(h).unwrap().bot;
        text::strlow(&mut msg);
        make_words(&msg, &mut hal.input_words);
        let mut reply = hal.generate_reply(rng).unwrap_or_default();
        text::strlow(&mut reply);
        if reply.is_empty() { 0 } else { set_out(a, 3, &reply) }
    })
}

/// `hal_bot_reply(RcHal:hal, message[], const bot_name[], const sender_name[], flags, sender_rep,
/// num_clients, reply_percent, out[], len)`: `BotChatReply`. `message` is changed in place.
pub fn hal_bot_reply(a: &mut dyn Args) -> Cell {
    let mut message = bytes(a, 2);
    let original_len = message.len() as Cell;
    let bot_name = bytes(a, 3);
    let sender_name = bytes(a, 4);
    let f = a.cell(5);
    let p = ReplyParams {
        bot_name: &bot_name,
        sender_name: &sender_name,
        sender_is_self: f & flags::SENDER_SELF != 0,
        sender_is_bot: f & flags::SENDER_BOT != 0,
        sender_rep: a.cell(6),
        num_clients: a.cell(7),
        chatting: f & flags::CHATTING != 0,
        real_mode: f & flags::REAL_MODE != 0,
        dont_learn: f & flags::DONT_LEARN != 0,
        reply_percent: a.cell(8),
    };
    with_hal(a, |s, h, a| {
        let State { hals, rng, .. } = s;
        let hal = &mut hals.get_mut(h).unwrap().bot;
        let reply = bot_chat::bot_chat_reply(hal, &mut message, &p, rng);
        // Upstream changes the caller's buffer: the next bot sees what this one did to it.
        set_bytes(a, 2, &message, original_len);
        match reply {
            Some(r) => set_out(a, 9, &r),
            None => 0,
        }
    })
}

/// `hal_words(RcHal:hal)`: the brain's dictionary size (`<ERROR>` and `<FIN>` included).
pub fn hal_words(a: &mut dyn Args) -> Cell {
    with_hal(a, |s, h, _| s.hals.get(h).map_or(0, |hal: &Hal| hal.bot.model.dictionary().len() as Cell))
}

/// `hal_profile(RcHal:hal)`: the brain's profile id.
pub fn hal_profile(a: &mut dyn Args) -> Cell {
    with_hal(a, |s, h, _| s.hals.get(h).map_or(0, |hal| hal.profile_id))
}

/// `hal_seed(seed)`: seeds the module's random numbers (replies, chat picks, typos).
pub fn hal_seed(a: &mut dyn Args) -> Cell {
    let seed = a.cell(1) as u32 as u64;
    with(|s| s.rng = rcbot_amxx_common::ModuleRandom::seeded(seed));
    1
}

/// `RcChat:chat_load(const file[] = "bot_chat.ini")`: 0 if the file can't be read.
pub fn chat_load(a: &mut dyn Args) -> Cell {
    let file = opt_string(a, 1).unwrap_or_else(|| "bot_chat.ini".into());
    with(|s| match s.dir.read(&file) {
        Some(text) => s.chats.insert(BotChatMessages::parse(&text)),
        None => 0,
    })
    .unwrap_or(0)
}

/// `bool:chat_free(&RcChat:chat)`: sets the variable to 0.
pub fn chat_free(a: &mut dyn Args) -> Cell {
    let h = a.read_array(1, 1)[0];
    if with(|s| s.chats.remove(h)).flatten().is_none() {
        a.error(&format!("Invalid chat handle {h}"));
        return 0;
    }
    a.set_ref(1, 0);
    1
}

fn kind_arg(a: &mut dyn Args, i: usize) -> Option<ChatKind> {
    let k = chat_kind(a.cell(i));
    if k.is_none() {
        let v = a.cell(i);
        a.error(&format!("Invalid chat kind {v}"));
    }
    k
}

/// `chat_count(RcChat:chat, RcChatKind:kind, array_num)`: the lines of that list, -1 for none
/// (`m_Laugh[-1]`).
pub fn chat_count(a: &mut dyn Args) -> Cell {
    let Some(kind) = kind_arg(a, 2) else { return -1 };
    with_chat(a, |_, m, a| m.lines(kind, a.cell(3)).map_or(-1, |l| l.len() as Cell))
}

/// `chat_get(RcChat:chat, RcChatKind:kind, array_num, index, out[], len)`: a line as written.
pub fn chat_get(a: &mut dyn Args) -> Cell {
    let Some(kind) = kind_arg(a, 2) else { return 0 };
    with_chat(a, |_, m, a| {
        let line = m.lines(kind, a.cell(3)).and_then(|l| l.get(usize::try_from(a.cell(4)).ok()?)).cloned();
        match line {
            Some(l) => set_out(a, 5, &l),
            None => 0,
        }
    })
}

/// `chat_context(const chat_to[] = "", const map[] = "")`: starts a new context, without players.
pub fn chat_context(a: &mut dyn Args) -> Cell {
    let chat_to = Some(bytes(a, 1)).filter(|n| !n.is_empty());
    let map = bytes(a, 2);
    with(|s| s.context = state::PawnChatContext { chat_to, players: Vec::new(), map });
    1
}

/// `chat_context_player(const name[], rep, bool:enemy)`: adds a player (not the bot itself).
pub fn chat_context_player(a: &mut dyn Args) -> Cell {
    let p = ChatPlayer { name: bytes(a, 1), rep: a.cell(2), is_enemy: a.cell(3) != 0 };
    with(|s| {
        s.context.players.push(p);
        s.context.players.len() as Cell
    })
    .unwrap_or(0)
}

/// `chat_pick(RcChat:chat, RcChatKind:kind, array_num, out[], len, &bool:team_only)`: the second
/// half of `CBot::BotChat`, filled from the context; 0 if there is nothing to say.
pub fn chat_pick(a: &mut dyn Args) -> Cell {
    let Some(kind) = kind_arg(a, 2) else { return 0 };
    with_chat(a, |s, m, a| {
        let State { context, rng, .. } = s;
        match bot_chat::bot_chat(m, kind, a.cell(3), context, rng) {
            Some(line) => {
                a.set_ref(6, line.team_only as Cell);
                set_out(a, 4, &line.text)
            }
            None => 0,
        }
    })
}

/// `chat_rep_array(rep)`: `BotFunc_GetRepArrayNum`.
pub fn chat_rep_array(a: &mut dyn Args) -> Cell {
    bot_chat::rep_array_num(a.cell(1))
}

/// `chat_humanize(text[], len)`: `HumanizeString` in place; the new length.
pub fn chat_humanize(a: &mut dyn Args) -> Cell {
    let mut t = bytes(a, 1);
    with(|s| text::humanize_string(&mut t, &mut s.rng));
    set_out(a, 1, &t)
}

/// `chat_remove_name_tags(const name[], out[], len)`: `RemoveNameTags`.
pub fn chat_remove_name_tags(a: &mut dyn Args) -> Cell {
    let name = bytes(a, 1);
    set_out(a, 2, &text::remove_name_tags(&name))
}

/// `Float:chat_typing_time(const text[])`: seconds a bot types the line.
pub fn chat_typing_time(a: &mut dyn Args) -> Cell {
    float_cell(bot_chat::typing_time(&bytes(a, 1)))
}

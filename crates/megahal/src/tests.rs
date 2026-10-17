// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The natives against a fake AMX ([`FakeArgs`]) and a temporary data directory. Each test runs
//! on its own thread, so each has its own module state.

use crate::natives::{self as n, flags};
use crate::state::{self, with};
use amxx::Cell;
use amxx::native::FakeArgs;
use rcbot::chat::HalBot;
use rcbot_amxx_common::DataDir;
use rcbot_amxx_common::data_dir::{TempDir, temp_dir};

fn setup(name: &str) -> TempDir {
    let d = temp_dir(name);
    state::attach(DataDir::new(&*d));
    with(|s| s.rng = rcbot_amxx_common::ModuleRandom::seeded(42));
    d
}

const TRAINING: &str = "hello there how are you\nthe specialists is a fun game\nkung fu is the best weapon\nhow are you doing today\n";

fn load(a: &mut FakeArgs, id: Cell) -> Cell {
    let e = a.alloc_str("");
    a.params = vec![id, e, e, e, e];
    n::hal_load(a)
}

#[test]
fn a_new_profile_gets_a_saved_empty_brain_then_pretrains() {
    let d = setup("newbrain");
    std::fs::write(d.join("botprofiles7_hal.trn"), TRAINING).unwrap();
    let mut a = FakeArgs::new(&[]);
    let h = load(&mut a, 7);
    assert_eq!(h, 1);
    // The file holds the empty brain written before pretraining (upstream's order).
    let file = std::fs::read(d.join("botprofiles/7_hal.brn")).unwrap();
    assert_eq!(file, HalBot::new(None, None, None).save_brain());
    a.params = vec![h];
    assert!(n::hal_words(&mut a) > 10, "pretrained from the .trn");
    assert_eq!(n::hal_profile(&mut a), 7);

    // Saved, it is the same bytes rcbot's own save gives, and loads back as it was.
    assert_eq!(n::hal_save(&mut a), 1);
    let words = n::hal_words(&mut a);
    let saved = std::fs::read(d.join("botprofiles/7_hal.brn")).unwrap();
    let expected = with(|s| s.hals.get(h).unwrap().bot.save_brain()).unwrap();
    assert_eq!(saved, expected);
    let h2 = load(&mut a, 7);
    a.params = vec![h2];
    assert_eq!(n::hal_words(&mut a), words, "a valid brain loads without pretraining again");
}

#[test]
fn profile_zero_stays_in_memory() {
    let d = setup("nobrain");
    let mut a = FakeArgs::new(&[]);
    let h = load(&mut a, 0);
    a.params = vec![h];
    assert_eq!(n::hal_save(&mut a), 0);
    assert!(!d.join("botprofiles/0_hal.brn").exists());
}

#[test]
fn learning_and_replying() {
    let _d = setup("reply");
    let mut a = FakeArgs::new(&[]);
    let h = load(&mut a, 0);
    let line = a.alloc_str("The quick brown fox jumps over the lazy dog");
    a.params = vec![h, line];
    assert_eq!(n::hal_learn(&mut a), 17, "nine words and the eight spaces between them");
    let line = a.alloc_str("a lazy dog sleeps all day long");
    a.params = vec![h, line];
    n::hal_learn(&mut a);

    let out = a.alloc(vec![0; 128]);
    let q = a.alloc_str("what about the fox?");
    a.params = vec![h, q, out, 127];
    let len = n::hal_reply(&mut a);
    assert!(len > 0, "a reply from a known keyword");
    let reply = a.read_str(out);
    assert_eq!(reply, reply.to_lowercase());

    // BotChatReply: two players means always reply; the message is changed in place.
    let msg = a.alloc_str("Hey Bot, the FOX is lazy");
    let (bot, sender) = (a.alloc_str("Bot"), a.alloc_str("[TS] Player"));
    a.params = vec![h, msg, bot, sender, flags::CHATTING, 5, 2, 33, out, 127];
    assert!(n::hal_bot_reply(&mut a) > 0);
    assert_eq!(a.read_str(msg), "hey %n, the fox is lazy");

    // Not chatting: nothing, and nothing learnt.
    let words = {
        a.params = vec![h];
        n::hal_words(&mut a)
    };
    let msg = a.alloc_str("zebras zebras zebras");
    a.params = vec![h, msg, bot, sender, 0, 5, 2, 33, out, 127];
    assert_eq!(n::hal_bot_reply(&mut a), 0);
    a.params = vec![h];
    assert_eq!(n::hal_words(&mut a), words);
}

#[test]
fn bad_handles_are_native_errors() {
    let _d = setup("bad");
    let mut a = FakeArgs::new(&[]);
    a.params = vec![3];
    assert_eq!(n::hal_words(&mut a), 0);
    let var = a.alloc(vec![9]);
    a.params = vec![var];
    assert_eq!(n::chat_free(&mut a), 0);
    assert_eq!(a.errors, ["Invalid HAL handle 3", "Invalid chat handle 9"]);
}

#[test]
fn free_zeroes_the_variable() {
    let _d = setup("free");
    let mut a = FakeArgs::new(&[]);
    let h = load(&mut a, 0);
    let var = a.alloc(vec![h]);
    a.params = vec![var, 0];
    assert_eq!(n::hal_free(&mut a), 1);
    assert_eq!(a.memory[var as usize][0], 0);
    a.params = vec![h];
    n::hal_words(&mut a);
    assert_eq!(a.errors.len(), 1);
}

const INI: &str = "[greetings-friendly]\nhi %n, welcome to %m\n[help]\nhelp me!\n[kills-unfriendly]\ntake that %e\n";

#[test]
fn chat_lines_and_picks() {
    let d = setup("chat");
    let mut a = FakeArgs::new(&[]);
    let empty = a.alloc_str("");
    a.params = vec![empty];
    assert_eq!(n::chat_load(&mut a), 0, "no bot_chat.ini");
    std::fs::write(d.join("bot_chat.ini"), INI).unwrap();
    let c = n::chat_load(&mut a);
    assert_eq!(c, 1);

    a.params = vec![c, 2, 2];
    assert_eq!(n::chat_count(&mut a), 1);
    a.params = vec![c, 0, -1];
    assert_eq!(n::chat_count(&mut a), -1, "m_Laugh[-1] is outside the struct");
    let out = a.alloc(vec![0; 128]);
    a.params = vec![c, 2, 2, 0, out, 127];
    assert_eq!(n::chat_get(&mut a), 20);
    assert_eq!(a.read_str(out), "hi %n, welcome to %m");

    // The context fills %n (without clan tags) and %m.
    let (to, map) = (a.alloc_str("[RC] Tony"), a.alloc_str("ts_dojo"));
    a.params = vec![to, map];
    n::chat_context(&mut a);
    let team = a.alloc(vec![7]);
    a.params = vec![c, 2, 2, out, 127, team];
    assert!(n::chat_pick(&mut a) > 0);
    assert_eq!(a.read_str(out), "hi tony, welcome to ts_dojo");
    assert_eq!(a.memory[team as usize][0], 0);

    a.params = vec![c, 7, 0, out, 127, team];
    n::chat_pick(&mut a);
    assert_eq!((a.read_str(out).as_str(), a.memory[team as usize][0]), ("help me!", 1));

    // %e needs an enemy among the context's players.
    a.params = vec![c, 4, 0, out, 127, team];
    assert_eq!(n::chat_pick(&mut a), 0);
    let p = a.alloc_str("Enemy{X}");
    a.params = vec![p, 5, 1];
    assert_eq!(n::chat_context_player(&mut a), 1);
    a.params = vec![c, 4, 0, out, 127, team];
    assert!(n::chat_pick(&mut a) > 0);
    assert_eq!(a.read_str(out), "take that enemy");

    a.params = vec![c, 8, 0, out, 127, team];
    assert_eq!(n::chat_pick(&mut a), 0);
    assert_eq!(a.errors, ["Invalid chat kind 8"]);
}

#[test]
fn chat_helpers() {
    let _d = setup("helpers");
    let mut a = FakeArgs::new(&[9]);
    assert_eq!(n::chat_rep_array(&mut a), 2);
    a.params = vec![-1];
    assert_eq!(n::chat_rep_array(&mut a), 1);
    let (name, out) = (a.alloc_str("-=TS=- (clan) Sn1per"), a.alloc(vec![0; 32]));
    a.params = vec![name, out, 31];
    n::chat_remove_name_tags(&mut a);
    assert_eq!(a.read_str(out), "ts sniper", "upstream's -= check only works at the start (rcbot §5)");
    let t = a.alloc_str("123456789");
    a.params = vec![t];
    assert_eq!(f32::from_bits(n::chat_typing_time(&mut a) as u32), 1.0);
    let t = a.alloc(b"hello world\0\0\0\0".iter().map(|&b| Cell::from(b)).collect());
    a.params = vec![t, 15];
    assert!(n::chat_humanize(&mut a) >= 10);
}

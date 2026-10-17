// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. `wake` ports
// tsmod-rcbot-rs's host `wake_hal` (RCBot's `PrepareHALBrainForPersonality` and
// `LoadHALBrainForPersonality`).

//! The module's state: the brains and chat line sets by handle, the chat context the plugin
//! fills in, the data directory and the random numbers.
//!
//! AMX Mod X calls natives on the server's main thread, so the state is thread-local (and each
//! test, on its own thread, gets its own). No native calls back into a plugin, so a borrow never
//! meets another.

use rcbot::chat::bot_chat::ChatPlayer;
use rcbot::chat::brain::{HalFileNames, HalFileOverrides, cookie_is_valid};
use rcbot::chat::{BotChatMessages, ChatContext, HalBot};
use rcbot_amxx_common::{DataDir, Handles, ModuleRandom};
use std::cell::RefCell;

/// A brain and the profile it belongs to.
pub struct Hal {
    pub bot: HalBot,
    /// The profile id (0: no profile, never saved).
    pub profile_id: i32,
    /// Where it lives.
    pub files: HalFileNames,
}

/// What `chat_pick` fills a line from (`ChatContext`), set by `chat_context` and
/// `chat_context_player`.
#[derive(Clone, Debug, Default)]
pub struct PawnChatContext {
    /// The player the bot talks to (`%n`), `None` when it talks to nobody.
    pub chat_to: Option<Vec<u8>>,
    pub players: Vec<ChatPlayer>,
    pub map: Vec<u8>,
}

impl ChatContext for PawnChatContext {
    fn chat_to_name(&self) -> Option<Vec<u8>> {
        self.chat_to.clone()
    }

    fn players(&self) -> Vec<ChatPlayer> {
        self.players.clone()
    }

    fn map_name(&self) -> Vec<u8> {
        self.map.clone()
    }
}

pub struct State {
    pub dir: DataDir,
    pub hals: Handles<Hal>,
    pub chats: Handles<BotChatMessages>,
    pub context: PawnChatContext,
    pub rng: ModuleRandom,
}

impl State {
    fn new(dir: DataDir) -> State {
        State {
            dir,
            hals: Handles::default(),
            chats: Handles::default(),
            context: PawnChatContext::default(),
            rng: ModuleRandom::from_clock(),
        }
    }
}

thread_local! {
    static STATE: RefCell<State> = RefCell::new(State::new(DataDir::new(rcbot_amxx_common::DATA_DIR)));
}

/// Runs `f` on the state; `None` if it is already borrowed (never, as no native re-enters).
pub fn with<R>(f: impl FnOnce(&mut State) -> R) -> Option<R> {
    STATE.with(|s| s.try_borrow_mut().ok().map(|mut s| f(&mut s)))
}

/// `AMXX_Attach`: the data directory as the core names it.
pub fn attach(dir: DataDir) {
    with(|s| s.dir = dir);
}

/// After the plugins unloaded (map change): every handle they held is gone.
pub fn forget_handles() {
    with(|s| {
        s.hals.clear();
        s.chats.clear();
        s.context = PawnChatContext::default();
    });
}

/// The keyword lists, the brain (a new one, saved, when the file is missing or damaged, then
/// pretrained from the `.trn`), as the standalone plugin wakes a profile's HAL up. A profile id
/// of 0 gets a brain in memory only, pretrained.
pub fn wake(dir: &DataDir, profile_id: i32, overrides: &HalFileOverrides) -> Hal {
    let log = rcbot_amxx_common::log;
    let f = HalFileNames::new(profile_id, overrides);
    let mut hal = HalBot::new(dir.read(&f.ban).as_deref(), dir.read(&f.aux).as_deref(), dir.read(&f.swap).as_deref());
    if profile_id != 0 {
        let brain = dir.read(&f.brain);
        let pretrain = !cookie_is_valid(brain.as_deref());
        if pretrain {
            log(&format!("bot profile ({profile_id}) HAL brain damaged!"));
            log(&format!("inferring a new HAL brain to profile ({profile_id})"));
            dir.write_logged(&f.brain, &hal.save_brain());
        }
        match dir.read(&f.brain) {
            Some(b) => {
                if hal.load_brain(&b).is_err() {
                    log(&format!("hal_load: profile {profile_id}'s HAL brain damaged!"));
                } else if pretrain {
                    hal.pretrain(dir.read(&f.pretrain).as_deref());
                }
            }
            None => log(&format!("hal_load: profile {profile_id}'s HAL brain refuses to wake up! (file does not exist)")),
        }
    } else {
        hal.pretrain(dir.read(&f.pretrain).as_deref());
    }
    Hal { bot: hal, profile_id, files: f }
}

/// Saves a brain with a profile (`SaveHALBrainForPersonality`); profile 0 is never saved.
pub fn save(dir: &DataDir, hal: &Hal) -> bool {
    hal.profile_id != 0 && dir.write_logged(&hal.files.brain, &hal.bot.save_brain())
}

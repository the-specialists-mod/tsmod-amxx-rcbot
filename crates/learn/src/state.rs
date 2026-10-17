// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. The file handling
// ports tsmod-rcbot-rs's host (`bots.rs`: `new_bot`, `save_bot`, `save_globals`, `load_heatmap`;
// `lib.rs`: the global files' load), RCBot's `CBot::loadLearnedData` / `saveLearnedData` and
// `CBotGlobals::loadLearnedData` / `saveLearnedData`.

//! The module's state: the global learned data (one per server, kept across map changes like
//! the module), each bot's learned data by handle, the generic learners by handle, the player
//! keys, the data directory and the random numbers.
//!
//! AMX Mod X calls natives on the server's main thread, so the state is thread-local (and each
//! test, on its own thread, gets its own). No native calls back into a plugin.

use rcbot::learn::data::{self, BotLearnedData, GlobalLearnedData, MAX_TEAMS};
use rcbot::learn::ledger::Ledger;
use rcbot::learn::loadout::{self, LoadoutBandit};
use rcbot::learn::opponents::{self, OpponentKey};
use rcbot::learn::store::profile_key;
use rcbot::learn::weapon_stats::{self, WeaponStats};
use rcbot::learn::{BotGaValues, Ga, Heatmap, Nn, Perceptron, Som};
use rcbot_amxx_common::{DataDir, Handles, ModuleRandom};
use std::cell::RefCell;

/// RCBot's bot folder for profiles (`BOT_PROFILES_FOLDER`).
pub const PROFILES: &str = rcbot::config::BOT_PROFILES_FOLDER;
/// The mod folder the heatmaps live under (`waypoints/<mod>/<map>.rhm`, beside the waypoints).
pub const MOD_FOLDER: &str = "ts";
/// Player slots (entity indexes 1-32).
pub const MAX_PLAYERS: usize = 32;

/// One bot's learning: upstream's `.rld` objects and the port's learners (`BotLearning`'s
/// weapon table, loadout bandit and combat ledger). The kung fu fighter (`.rkf`) belongs to the
/// Pawn kung fu code and isn't here.
pub struct BotLearn {
    pub profile_id: i32,
    /// [`profile_key`]: the profile id, or `default-<name>` for a bot without one.
    pub key: String,
    pub data: BotLearnedData,
    pub weapons: WeaponStats,
    pub loadout: LoadoutBandit,
    pub ledger: Ledger,
}

impl BotLearn {
    /// `botprofiles/<key>.<ext>`.
    pub fn file(&self, ext: &str) -> String {
        format!("{PROFILES}/{}.{ext}", self.key)
    }
}

pub struct State {
    pub dir: DataDir,
    pub rng: ModuleRandom,
    pub globals: GlobalLearnedData,
    /// Who each player slot is (index 1-32), for the opponent model.
    pub keys: [Option<OpponentKey>; MAX_PLAYERS + 1],
    pub bots: Handles<BotLearn>,
    pub gas: Handles<Ga<BotGaValues>>,
    pub perceptrons: Handles<Perceptron>,
    pub nns: Handles<Nn>,
    pub soms: Handles<Som>,
}

impl State {
    fn new(dir: DataDir) -> State {
        State {
            dir,
            rng: ModuleRandom::from_clock(),
            globals: GlobalLearnedData::default(),
            keys: [None; MAX_PLAYERS + 1],
            bots: Handles::default(),
            gas: Handles::default(),
            perceptrons: Handles::default(),
            nns: Handles::default(),
            soms: Handles::default(),
        }
    }

    /// The key of the player at `index`.
    pub fn key(&self, index: i32) -> Option<OpponentKey> {
        *self.keys.get(usize::try_from(index).ok()?)?
    }

    /// `CBotGlobals::loadLearnedData` and the port's global files: `team<n>.rld`,
    /// `tsweaps.rld`, `weaponstats.rwr`, `loadouts.rlo`, `botprofiles/players.rop`. Missing files
    /// start empty (the GAs append what they read, so this starts over rather than loading on
    /// top). The heatmap and the player keys stay. Returns how many files were read.
    pub fn load_globals(&mut self) -> usize {
        let heatmap = std::mem::take(&mut self.globals.heatmap);
        self.globals = GlobalLearnedData { heatmap, ..GlobalLearnedData::default() };
        let mut n = 0;
        for team in 0..MAX_TEAMS {
            if let Some(b) = self.dir.read(&data::team_file_name(team)) {
                self.globals.load_team(team, &b);
                n += 1;
            }
        }
        if let Some(b) = self.dir.read(data::TS_WEAPONS_FILE) {
            self.globals.load_ts_weapons(&b);
            n += 1;
        }
        if let Some(t) = self.dir.read(weapon_stats::GLOBAL_FILE) {
            self.globals.weapon_prior.load(&String::from_utf8_lossy(&t));
            n += 1;
        }
        if let Some(t) = self.dir.read(loadout::GLOBAL_FILE) {
            self.globals.loadout_prior.load(&String::from_utf8_lossy(&t));
            n += 1;
        }
        if let Some(t) = self.dir.read(&players_file()) {
            self.globals.opponents.load(&String::from_utf8_lossy(&t));
            n += 1;
        }
        n
    }

    /// `CBotGlobals::saveLearnedData` and the port's global files, and the map's heatmap when it
    /// has one (as the standalone plugin's map end does). Returns how many files were written.
    pub fn save_globals(&self) -> usize {
        let g = &self.globals;
        let mut files: Vec<(String, Vec<u8>)> = (0..MAX_TEAMS).map(|t| (data::team_file_name(t), g.save_team(t))).collect();
        files.push((data::TS_WEAPONS_FILE.into(), g.save_ts_weapons()));
        files.push((weapon_stats::GLOBAL_FILE.into(), g.weapon_prior.save().into_bytes()));
        files.push((loadout::GLOBAL_FILE.into(), g.loadout_prior.save().into_bytes()));
        files.push((players_file(), g.opponents.save().into_bytes()));
        if !g.heatmap.is_empty() {
            files.push((heatmap_path(g.heatmap.map_name()), g.heatmap.to_bytes()));
        }
        files.iter().filter(|(f, b)| self.dir.write_logged(f, b)).count()
    }

    /// `setupDataStructures` and `CBot::loadLearnedData` for a new bot, plus the port's
    /// `.rwr` and `.rlo`; the files are those the standalone plugin uses for the same profile.
    pub fn load_bot(&mut self, profile_id: i32, name: &str) -> BotLearn {
        let mut bot = BotLearn {
            profile_id,
            key: profile_key(profile_id, name),
            data: BotLearnedData::new(&mut self.rng),
            weapons: WeaponStats::default(),
            loadout: LoadoutBandit::default(),
            ledger: Ledger::default(),
        };
        if let Some(b) = self.dir.read(&bot.file("rld")) {
            bot.data.load(&b, profile_id);
        }
        if let Some(t) = self.dir.read(&bot.file(weapon_stats::FILE_EXT)) {
            bot.weapons.load(&String::from_utf8_lossy(&t));
        }
        if let Some(t) = self.dir.read(&bot.file(loadout::FILE_EXT)) {
            bot.loadout.load(&String::from_utf8_lossy(&t));
        }
        bot
    }

    /// `CBot::saveLearnedData` and the port's files (what the standalone plugin's `save_bot`
    /// writes, its brain and `.rkf` aside). True if every file was written.
    pub fn save_bot(&self, bot: &BotLearn) -> bool {
        let files = [
            (bot.file("rld"), bot.data.save(bot.profile_id)),
            (bot.file(weapon_stats::FILE_EXT), bot.weapons.save().into_bytes()),
            (bot.file(loadout::FILE_EXT), bot.loadout.save().into_bytes()),
        ];
        files.iter().filter(|(f, b)| self.dir.write_logged(f, b)).count() == files.len()
    }

    /// The map's heatmap for its waypoints as loaded (a new one when the file is missing or was
    /// made for other waypoints), faded once for this load. True if the file was used.
    pub fn load_heatmap(&mut self, map: &str, waypoints: usize) -> bool {
        let loaded = self.dir.read(&heatmap_path(map)).and_then(|b| Heatmap::from_bytes(&b, map, waypoints));
        let used = loaded.is_some();
        let mut h = loaded.unwrap_or_else(|| Heatmap::new(map, waypoints));
        h.decay(rcbot::learn::heatmap::MAP_DECAY);
        self.globals.heatmap = h;
        used
    }
}

/// `botprofiles/players.rop`.
pub fn players_file() -> String {
    format!("{PROFILES}/{}", opponents::PLAYERS_FILE)
}

/// `waypoints/ts/<map>.rhm`.
pub fn heatmap_path(map: &str) -> String {
    format!("waypoints/{MOD_FOLDER}/{}", rcbot::learn::heatmap::heatmap_file_name(map))
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

/// After the plugins unloaded (map change): every handle they held is gone. The global data
/// stays, as the standalone plugin's does across maps.
pub fn forget_handles() {
    with(|s| {
        s.bots.clear();
        s.gas.clear();
        s.perceptrons.clear();
        s.nns.clear();
        s.soms.clear();
        s.keys = [None; MAX_PLAYERS + 1];
    });
}

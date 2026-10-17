// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The files rcbot_amxx wrote on a server (`cargo xtask compat`'s first run, ts_dojo, four
//! profiles, kung fu on; checked in under `tests/compat/pawn/`) read with the standalone
//! tsmod_rcbot's loaders (`rcbot`'s, as its host calls them: `bots.rs` `new_bot` / `wake_hal` /
//! `load_heatmap`, `lib.rs` `game_init`), and written back byte for byte: nothing is lost or
//! read differently. The `.rkf` and `.rcr` files are the Pawn plugin's own writers
//! (`kungfu.inc`, `rep.inc`); the others are written by the modules through `rcbot`. With
//! `RCBOT_COMPAT_DIR` the tests read another run's files (`target/compat/win/pawn`, say).

use rcbot::XorShiftRandom;
use rcbot::bot::kungfu::KungFu;
use rcbot::chat::HalBot;
use rcbot::chat::brain::cookie_is_valid;
use rcbot::config::Reputations;
use rcbot::config::rep::parse_rep_file;
use rcbot::learn::Heatmap;
use rcbot::learn::data::{BotLearnedData, GlobalLearnedData, MAX_TEAMS};
use rcbot::learn::loadout::LoadoutBandit;
use rcbot::learn::opponents::OpponentModel;
use rcbot::learn::weapon_stats::WeaponStats;
use std::path::PathBuf;

/// The files: `RCBOT_COMPAT_DIR` (another run's `target/compat/<os>/pawn/`), else the fixtures.
fn file(rel: &str) -> Vec<u8> {
    let dir = std::env::var_os("RCBOT_COMPAT_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/compat/pawn"));
    let p = dir.join(rel);
    std::fs::read(&p).unwrap_or_else(|e| panic!("{}: {e}", p.display()))
}

fn text(rel: &str) -> String {
    String::from_utf8(file(rel)).unwrap()
}

const PROFILES: [i32; 4] = [1, 2, 3, 4];

#[test]
fn pawn_kung_fu_files_load_and_save_unchanged() {
    for id in PROFILES {
        let t = text(&format!("botprofiles/{id}.rkf"));
        let mut k = KungFu::new();
        k.load(&t);
        assert_eq!(k.save(), t, "{id}.rkf");
        assert_ne!(KungFu::new().save(), t, "{id}.rkf learned something (kung fu was on)");
    }
}

#[test]
fn pawn_reputation_files_have_every_player() {
    for id in PROFILES {
        let b = file(&format!("botprofiles/{id}.rcr"));
        let records = parse_rep_file(&b).expect("whole 8-byte records");
        assert_eq!(records.len(), 4, "{id}.rcr: one per bot on the server");
        let mut reps = Reputations::default();
        for r in &records {
            assert_eq!(reps.add_load_rep(r.player_rep_id, Some(&b)), None, "{id}.rcr has player {}", r.player_rep_id);
        }
        assert_eq!(reps.save_all_rep(Some(&b)).unwrap_or_else(|| b.clone()), b, "{id}.rcr saved back");
    }
}

#[test]
fn pawn_bot_learned_files_load_and_save_unchanged() {
    for id in PROFILES {
        let b = file(&format!("botprofiles/{id}.rld"));
        let mut d = BotLearnedData::new(&mut XorShiftRandom::new(1));
        d.load(&b, id);
        assert_eq!(d.save(id), b, "{id}.rld");
        let t = text(&format!("botprofiles/{id}.rwr"));
        let mut w = WeaponStats::default();
        w.load(&t);
        assert_eq!(w.save(), t, "{id}.rwr");
        let t = text(&format!("botprofiles/{id}.rlo"));
        let mut l = LoadoutBandit::default();
        l.load(&t);
        assert_eq!(l.save(), t, "{id}.rlo");
    }
}

#[test]
fn pawn_global_files_load_and_save_unchanged() {
    let mut g = GlobalLearnedData::default();
    for team in 0..MAX_TEAMS {
        let b = file(&rcbot::learn::data::team_file_name(team));
        g.load_team(team, &b);
        assert_eq!(g.save_team(team), b, "team{team}.rld");
    }
    let b = file(rcbot::learn::data::TS_WEAPONS_FILE);
    g.load_ts_weapons(&b);
    assert_eq!(g.save_ts_weapons(), b, "tsweaps.rld");
    let t = text(rcbot::learn::weapon_stats::GLOBAL_FILE);
    g.weapon_prior.load(&t);
    assert_eq!(g.weapon_prior.save(), t, "weaponstats.rwr");
    let t = text(rcbot::learn::loadout::GLOBAL_FILE);
    g.loadout_prior.load(&t);
    assert_eq!(g.loadout_prior.save(), t, "loadouts.rlo");
    let t = text("botprofiles/players.rop");
    let mut o = OpponentModel::default();
    o.load(&t);
    assert_eq!(o.save(), t, "players.rop");
}

#[test]
fn pawn_brains_load_and_save_unchanged() {
    for id in PROFILES {
        let b = file(&format!("botprofiles/{id}_hal.brn"));
        assert!(cookie_is_valid(Some(&b)), "{id}_hal.brn");
        let mut hal = HalBot::new(None, None, None);
        hal.load_brain(&b).unwrap();
        assert_eq!(hal.save_brain(), b, "{id}_hal.brn");
        assert!(hal.model.dictionary().len() > 2, "{id}_hal.brn was pretrained");
    }
}

#[test]
fn pawn_heatmap_loads_for_the_maps_waypoints() {
    let b = file("waypoints/ts/ts_dojo.rhm");
    let h = Heatmap::from_bytes(&b, "ts_dojo", 154).expect("ts_dojo.rhm for its 154 waypoints");
    assert_eq!(h.to_bytes(), b);
}

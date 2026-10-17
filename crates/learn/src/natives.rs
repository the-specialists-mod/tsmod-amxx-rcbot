// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. The bot-side
// learning ports what tsmod-rcbot-rs's bot (`bot/learning.rs`, `think.rs`, `combat.rs`,
// `manager.rs`) does with `rcbot::learn`, as calls the Pawn bot makes.

//! The natives for the global learned data, each bot's learned data, the opponent model and the
//! combat ledger (the heatmap is rcbot_nav's), as `fn(&mut dyn Args) -> Cell` so tests run them
//! against `FakeArgs`. `include/rcbot_learn.inc` documents each one.

use crate::state::{BotLearn, MAX_PLAYERS, State, with};
use amxx::Cell;
use amxx::native::Args;
use rcbot::learn::data::{BotLearnedData, GlobalLearnedData, MAX_TEAMS};
use rcbot::learn::ledger::{LedgerEntry, range_bin};
use rcbot::learn::loadout::{self, LOADOUTS};
use rcbot::learn::opponents::{OpponentKey, OpponentProfile};
use rcbot::learn::weapon_stats::{MIN_SAMPLES, WeaponStats};
use rcbot_amxx_common::{ModuleRandom, count, float_cell, read_floats, set_out, write_floats};

/// The most cells an array argument may name (a guard against a wrong size).
pub const MAX_ARRAY: usize = 4096;

/// A count argument, at most [`MAX_ARRAY`].
pub fn size(a: &dyn Args, i: usize) -> usize {
    count(a, i).min(MAX_ARRAY)
}

/// Runs `f` on the bot behind argument 1 with the globals and the random numbers, or raises the
/// native error.
fn with_bot(a: &mut dyn Args, f: impl FnOnce(&mut BotLearn, &mut GlobalLearnedData, &mut ModuleRandom, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| {
        let State { bots, globals, rng, .. } = s;
        bots.get_mut(h).map(|b| f(b, globals, rng, a))
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid learn handle {h}"));
        0
    })
}

/// A team argument as an index of the team GAs (`think.rs`: anything else is team 0).
fn team(a: &dyn Args, i: usize) -> usize {
    usize::try_from(a.cell(i)).ok().filter(|&t| t < MAX_TEAMS).unwrap_or(0)
}

/// An opponent argument: -1 (or any negative) is anyone.
fn opponent(a: &dyn Args, i: usize) -> Option<u32> {
    u32::try_from(a.cell(i)).ok()
}

// ---- globals ----

/// `rl_seed(seed)`
pub fn rl_seed(a: &mut dyn Args) -> Cell {
    let seed = a.cell(1) as u32 as u64;
    with(|s| s.rng = ModuleRandom::seeded(seed));
    1
}

/// `rl_load_globals()`
pub fn rl_load_globals(_: &mut dyn Args) -> Cell {
    with(|s| s.load_globals() as Cell).unwrap_or(0)
}

/// `rl_save_globals()`
pub fn rl_save_globals(_: &mut dyn Args) -> Cell {
    with(|s| s.save_globals() as Cell).unwrap_or(0)
}

// ---- a bot's learned data ----

/// `RcLearn:rl_bot_load(profile_id, const name[])`
pub fn rl_bot_load(a: &mut dyn Args) -> Cell {
    let id = a.cell(1);
    let name = a.string(2);
    with(|s| {
        let b = s.load_bot(id, &name);
        s.bots.insert(b)
    })
    .unwrap_or(0)
}

/// `bool:rl_bot_save(RcLearn:bot)`
pub fn rl_bot_save(a: &mut dyn Args) -> Cell {
    let h = a.cell(1);
    let r = with(|s| s.bots.get(h).map(|b| s.save_bot(b) as Cell)).flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid learn handle {h}"));
        0
    })
}

/// `bool:rl_bot_free(&RcLearn:bot, bool:save = false)`
pub fn rl_bot_free(a: &mut dyn Args) -> Cell {
    let h = a.read_array(1, 1)[0];
    let save = a.cell(2) != 0;
    let r = with(|s| {
        let b = s.bots.remove(h)?;
        if save {
            s.save_bot(&b);
        }
        Some(())
    })
    .flatten();
    if r.is_none() {
        a.error(&format!("Invalid learn handle {h}"));
        return 0;
    }
    a.set_ref(1, 0);
    1
}

/// `rl_bot_key(RcLearn:bot, out[], len)`
pub fn rl_bot_key(a: &mut dyn Args) -> Cell {
    with_bot(a, |b, _, _, a| set_out(a, 2, b.key.as_bytes()))
}

/// `rl_bot_on_spawn(RcLearn:bot, team)`: `CBot::SpawnInit`'s learning.
pub fn rl_bot_on_spawn(a: &mut dyn Args) -> Cell {
    let t = team(a, 2);
    with_bot(a, |b, g, rng, _| {
        b.data.on_spawn(&mut g.enemy_costs[t], rng);
        1
    })
}

/// `rl_bot_on_death(RcLearn:bot, team, Float:fitness, bool:used_melee)`
pub fn rl_bot_on_death(a: &mut dyn Args) -> Cell {
    let (t, fitness, melee) = (team(a, 2), a.float(3), a.cell(4) != 0);
    with_bot(a, |b, g, rng, _| {
        let GlobalLearnedData { enemy_costs, ts_weapon_choices, .. } = g;
        b.data.on_death(fitness, melee, ts_weapon_choices, &mut enemy_costs[t], rng);
        1
    })
}

/// `Float:rl_death_fitness(Float:survival_time, frags_since_death, team_score_since_death)`
pub fn rl_death_fitness(a: &mut dyn Args) -> Cell {
    float_cell(BotLearnedData::death_fitness(a.float(1), a.cell(2), a.cell(3)))
}

/// `rl_bot_select_ts_weapons(RcLearn:bot, bool:disable_learn, ids[3])`
pub fn rl_bot_select_ts_weapons(a: &mut dyn Args) -> Cell {
    let off = a.cell(2) != 0;
    with_bot(a, |b, g, rng, a| {
        let ids = b.data.select_ts_weapons(&mut g.ts_weapon_choices, off, rng);
        a.write_array(3, 0, &ids);
        3
    })
}

/// A decision's perceptron and the genes its weights come from.
pub type DecisionParts = fn(&mut BotLearnedData) -> (&mut rcbot::learn::Perceptron, &rcbot::learn::BotGaValues);

/// The decisions `rl_bot_decide` runs: the perceptron and where its weights come from (genes
/// `from..from + num`, after the bias), as `decideJumpDuckStrafe`, the TS stunt choice and the
/// electrified-enemy check set them.
pub fn decision(d: Cell) -> Option<(DecisionParts, usize, usize)> {
    Some(match d {
        0 => (|l| (&mut l.jump, &l.personal), 0, 5),
        1 => (|l| (&mut l.duck, &l.personal), 5, 5),
        2 => (|l| (&mut l.strafe, &l.personal), 10, 5),
        3 => (|l| (&mut l.stunt, &l.ts_weapon_select), 3, 5),
        4 => (|l| (&mut l.attack_electrified, &l.personal), 15, 7),
        _ => return None,
    })
}

/// `bool:rl_bot_decide(RcLearn:bot, RlDecision:which, const Float:inputs[], num, &Float:output = 0.0)`
pub fn rl_bot_decide(a: &mut dyn Args) -> Cell {
    let Some((pick, from, num)) = decision(a.cell(2)) else {
        let d = a.cell(2);
        a.error(&format!("Invalid decision {d}"));
        return 0;
    };
    let n = size(a, 4);
    let inputs = read_floats(a, 3, n);
    with_bot(a, |b, _, _, a| {
        let (p, genes) = pick(&mut b.data);
        p.set_weights_from_values(genes, from, num);
        p.input(&inputs);
        let out = p.execute();
        a.set_ref(5, float_cell(out));
        p.fired() as Cell
    })
}

fn genes(l: &BotLearnedData, set: Cell) -> Option<&[f32]> {
    Some(match set {
        0 => l.survival.values(),
        1 => l.personal.values(),
        2 => l.ts_weapon_select.values(),
        _ => return None,
    })
}

/// `Float:rl_bot_gene(RcLearn:bot, RlGenes:set, index)`: 0.0 out of range.
pub fn rl_bot_gene(a: &mut dyn Args) -> Cell {
    let (set, i) = (a.cell(2), a.cell(3));
    with_bot(a, |b, _, _, _| {
        let v = genes(&b.data, set).and_then(|g| g.get(usize::try_from(i).ok()?).copied()).unwrap_or(0.0);
        float_cell(v)
    })
}

/// `rl_bot_genes(RcLearn:bot, RlGenes:set, Float:out[], len)`: the number of genes written.
pub fn rl_bot_genes(a: &mut dyn Args) -> Cell {
    let (set, len) = (a.cell(2), size(a, 4));
    with_bot(a, |b, _, _, a| genes(&b.data, set).map_or(0, |g| write_floats(a, 3, g, len)))
}

// ---- weapon effectiveness by range ----

/// Adds to the bot's table (bot 0: none) and every bot's prior, as `learning.rs` does.
fn ws_add(a: &mut dyn Args, f: impl Fn(&mut WeaponStats, i32, f32, f32)) -> Cell {
    let (h, w, d, v) = (a.cell(1), a.cell(2), a.float(3), a.float(4));
    let ok = with(|s| {
        if h != 0 {
            f(&mut s.bots.get_mut(h)?.weapons, w, d, v);
        }
        f(&mut s.globals.weapon_prior, w, d, v);
        Some(())
    })
    .flatten();
    if ok.is_none() {
        a.error(&format!("Invalid learn handle {h}"));
        return 0;
    }
    1
}

/// `rl_ws_add_exposure(RcLearn:bot, weapon, Float:dist, Float:dt)`
pub fn rl_ws_add_exposure(a: &mut dyn Args) -> Cell {
    ws_add(a, WeaponStats::add_exposure)
}

/// `rl_ws_add_hit(RcLearn:bot, weapon, Float:dist, Float:damage)`
pub fn rl_ws_add_hit(a: &mut dyn Args) -> Cell {
    ws_add(a, WeaponStats::add_hit)
}

/// `rl_ws_add_shots(RcLearn:bot, weapon, Float:dist, Float:rounds)`
pub fn rl_ws_add_shots(a: &mut dyn Args) -> Cell {
    ws_add(a, WeaponStats::add_shots)
}

/// Runs `f` on the bot's table (bot 0: the prior) and the prior.
fn ws_query(a: &mut dyn Args, f: impl FnOnce(&WeaponStats, &WeaponStats, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| {
        let prior = &s.globals.weapon_prior;
        let t = if h == 0 { prior } else { &s.bots.get(h)?.weapons };
        Some(f(t, prior, a))
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid learn handle {h}"));
        0
    })
}

/// `bool:rl_ws_score(RcLearn:bot, weapon, Float:dist, &Float:score)`: `learned_weapon_score`.
pub fn rl_ws_score(a: &mut dyn Args) -> Cell {
    let (w, d) = (a.cell(2), a.float(3));
    let h = a.cell(1);
    ws_query(a, |t, prior, a| {
        let score = if h == 0 { WeaponStats::default().score(prior, w, d) } else { t.score(prior, w, d) };
        a.set_ref(4, float_cell(score));
        (t.samples(w, d) >= MIN_SAMPLES) as Cell
    })
}

/// `Float:rl_ws_samples(RcLearn:bot, weapon, Float:dist)`
pub fn rl_ws_samples(a: &mut dyn Args) -> Cell {
    let (w, d) = (a.cell(2), a.float(3));
    ws_query(a, |t, _, _| float_cell(t.samples(w, d)))
}

/// `bool:rl_ws_accuracy(RcLearn:bot, weapon, Float:dist, &Float:accuracy)`
pub fn rl_ws_accuracy(a: &mut dyn Args) -> Cell {
    let (w, d) = (a.cell(2), a.float(3));
    ws_query(a, |t, _, a| match t.accuracy(w, d) {
        Some(v) => {
            a.set_ref(4, float_cell(v));
            1
        }
        None => 0,
    })
}

// ---- loadouts ----

/// `rl_loadout_count()`
pub fn rl_loadout_count(_: &mut dyn Args) -> Cell {
    LOADOUTS.len() as Cell
}

fn arm(a: &dyn Args, i: usize) -> Option<usize> {
    usize::try_from(a.cell(i)).ok().filter(|&x| x < LOADOUTS.len())
}

/// `rl_loadout_weapons(arm, ids[], len)`: the number of weapon ids written (0: no such loadout).
pub fn rl_loadout_weapons(a: &mut dyn Args) -> Cell {
    let Some(i) = arm(a, 1) else { return 0 };
    let ids = &LOADOUTS[i][..LOADOUTS[i].len().min(count(a, 3))];
    a.write_array(2, 0, ids);
    ids.len() as Cell
}

/// `rl_loadout_slots(arm)`
pub fn rl_loadout_slots(a: &mut dyn Args) -> Cell {
    arm(a, 1).map_or(0, |i| loadout::loadout_slots(LOADOUTS[i]))
}

/// `rl_weapon_slots(weapon)`
pub fn rl_weapon_slots(a: &mut dyn Args) -> Cell {
    loadout::weapon_slots(a.cell(1))
}

/// `rl_loadout_choose(RcLearn:bot, capacity, bool:random)`: the arm for the next life.
pub fn rl_loadout_choose(a: &mut dyn Args) -> Cell {
    let (cap, random) = (a.cell(2), a.cell(3) != 0);
    with_bot(a, |b, g, rng, _| b.loadout.choose(&g.loadout_prior, cap, random, rng) as Cell)
}

/// `rl_loadout_reward(RcLearn:bot, arm, Float:reward)`: the bot's and every bot's estimates.
pub fn rl_loadout_reward(a: &mut dyn Args) -> Cell {
    let Some(i) = arm(a, 2) else {
        let v = a.cell(2);
        a.error(&format!("Invalid loadout {v}"));
        return 0;
    };
    let r = a.float(3);
    with_bot(a, |b, g, _, _| {
        b.loadout.reward(i, r);
        g.loadout_prior.reward(i, r);
        1
    })
}

/// `Float:rl_life_reward(kills, deaths, Float:dealt, Float:seconds)`
pub fn rl_life_reward(a: &mut dyn Args) -> Cell {
    float_cell(loadout::life_reward(a.cell(1).max(0) as u32, a.cell(2).max(0) as u32, a.float(3), a.float(4)))
}

/// `rl_credited_loadout(bought, const owned[], num)`: the arm, -1 for none.
pub fn rl_credited_loadout(a: &mut dyn Args) -> Cell {
    let bought = usize::try_from(a.cell(1)).ok();
    let n = size(a, 3);
    let owned = a.read_array(2, n);
    loadout::credited_loadout(bought, &owned).map_or(-1, |i| i as Cell)
}

// ---- the opponent model ----

fn player_index(a: &mut dyn Args, i: usize) -> Option<usize> {
    let p = a.cell(i);
    let ok = usize::try_from(p).ok().filter(|&p| (1..=MAX_PLAYERS).contains(&p));
    if ok.is_none() {
        a.error(&format!("Player {p} is not valid"));
    }
    ok
}

/// `rl_opp_set_key(player, RlKeyKind:kind, id)`
pub fn rl_opp_set_key(a: &mut dyn Args) -> Cell {
    let Some(p) = player_index(a, 1) else { return 0 };
    let key = match a.cell(2) {
        0 => None,
        1 => Some(OpponentKey::Player(a.cell(3))),
        2 => Some(OpponentKey::Bot(a.cell(3))),
        k => {
            a.error(&format!("Invalid key kind {k}"));
            return 0;
        }
    };
    with(|s| s.keys[p] = key);
    1
}

/// Runs `f` with the player's key (argument 1); 0 when the player has none.
fn with_key(a: &mut dyn Args, f: impl FnOnce(&mut State, OpponentKey, &mut dyn Args) -> Cell) -> Cell {
    let Some(p) = player_index(a, 1) else { return 0 };
    with(|s| match s.keys[p] {
        Some(k) => f(s, k, a),
        None => 0,
    })
    .unwrap_or(0)
}

/// `rl_opp_hit_on_bot(player, weapon, Float:dist, Float:damage, bool:headshot)`
pub fn rl_opp_hit_on_bot(a: &mut dyn Args) -> Cell {
    let (w, d, dmg, hs) = (a.cell(2), a.float(3), a.float(4), a.cell(5) != 0);
    with_key(a, |s, k, _| {
        s.globals.opponents.observe_hit_on_bot(k, w, d, dmg, hs);
        1
    })
}

/// `rl_opp_kill_of_bot(player)`
pub fn rl_opp_kill_of_bot(a: &mut dyn Args) -> Cell {
    with_key(a, |s, k, _| {
        s.globals.opponents.observe_kill_of_bot(k);
        1
    })
}

/// `rl_opp_death_to_bot(player)`
pub fn rl_opp_death_to_bot(a: &mut dyn Args) -> Cell {
    with_key(a, |s, k, _| {
        s.globals.opponents.observe_death_to_bot(k);
        1
    })
}

/// `rl_opp_exposure(player, Float:dt, Float:sideways)`
pub fn rl_opp_exposure(a: &mut dyn Args) -> Cell {
    let (dt, side) = (a.float(2), a.float(3));
    with_key(a, |s, k, _| {
        s.globals.opponents.observe_exposure(k, dt, side);
        1
    })
}

/// `rl_opp_stunt(player, Float:now)`
pub fn rl_opp_stunt(a: &mut dyn Args) -> Cell {
    let now = a.float(2);
    with_key(a, |s, k, _| {
        s.globals.opponents.observe_stunt(k, now);
        1
    })
}

/// `rl_opp_kungfu(player, const attacks[], num, blocks, Float:interval = -1.0)`
pub fn rl_opp_kungfu(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let attacks: Vec<u32> = a.read_array(2, n).into_iter().map(|c| c.max(0) as u32).collect();
    let (blocks, interval) = (a.cell(4).max(0) as u32, a.float(5));
    with_key(a, |s, k, _| {
        s.globals.opponents.observe_kungfu(k, &attacks, blocks, (interval >= 0.0).then_some(interval));
        1
    })
}

/// `Float:rl_opp_threat(player)`: 1.0 for an unknown player.
pub fn rl_opp_threat(a: &mut dyn Args) -> Cell {
    let Some(p) = player_index(a, 1) else { return float_cell(1.0) };
    float_cell(with(|s| s.keys[p].map_or(1.0, |k| s.globals.opponents.threat(k))).unwrap_or(1.0))
}

/// A profile's numbers in `players.rop`'s field order (the include's `RL_OPP_*`).
pub fn profile_fields(p: &OpponentProfile) -> Vec<f32> {
    let mut v = vec![
        p.exposure,
        p.damage_to_bots,
        p.hits_on_bots,
        p.kills_of_bots,
        p.deaths_to_bots,
        p.range,
        p.kungfu_share,
        p.stunts,
        p.stunt_rate,
        p.dodge,
        p.kf_fights,
        p.kf_blocks,
        p.kf_interval,
    ];
    v.extend_from_slice(&p.kf_attacks);
    v.extend_from_slice(&p.weapons);
    v.push(p.headshots_on_bots);
    v
}

/// `rl_opp_profile(player, Float:out[], len)`: the number of fields written, 0 for an unknown
/// player.
pub fn rl_opp_profile(a: &mut dyn Args) -> Cell {
    let len = size(a, 3);
    with_key(a, |s, k, a| s.globals.opponents.profile(k).map_or(0, |p| write_floats(a, 2, &profile_fields(p), len)))
}

/// `rl_opp_new_map()`: counts fade, stunt timing restarts.
pub fn rl_opp_new_map(_: &mut dyn Args) -> Cell {
    with(|s| s.globals.opponents.new_map());
    1
}

// ---- the combat ledger ----

/// `rl_ledger_record(RcLearn:bot, Float:time, other, weapon, Float:dealt, Float:taken, bool:kill,
/// bool:death, Float:dist, waypoint)`
pub fn rl_ledger_record(a: &mut dyn Args) -> Cell {
    let e = LedgerEntry {
        time: a.float(2),
        other: a.cell(3).max(0) as u32,
        weapon: a.cell(4),
        dealt: a.float(5),
        taken: a.float(6),
        kill: a.cell(7) != 0,
        death: a.cell(8) != 0,
        range_bin: range_bin(a.float(9)) as u8,
        waypoint: a.cell(10),
    };
    with_bot(a, |b, _, _, _| {
        b.ledger.record(e);
        1
    })
}

/// `rl_ledger_engage(RcLearn:bot, Float:now, opponent)`
pub fn rl_ledger_engage(a: &mut dyn Args) -> Cell {
    let (now, o) = (a.float(2), a.cell(3).max(0) as u32);
    with_bot(a, |b, _, _, _| {
        b.ledger.engage(now, o);
        1
    })
}

/// `rl_ledger_tick(RcLearn:bot, Float:now)`
pub fn rl_ledger_tick(a: &mut dyn Args) -> Cell {
    let now = a.float(2);
    with_bot(a, |b, _, _, _| {
        b.ledger.tick(now);
        1
    })
}

/// `rl_ledger_close(RcLearn:bot, opponent = -1)`: -1 closes every episode.
pub fn rl_ledger_close(a: &mut dyn Args) -> Cell {
    let o = opponent(a, 2);
    with_bot(a, |b, _, _, _| {
        match o {
            Some(o) => b.ledger.close(o),
            None => b.ledger.close_all(),
        }
        1
    })
}

/// `Float:rl_ledger_credit(RcLearn:bot, Float:decision, opponent, Float:horizon)`
pub fn rl_ledger_credit(a: &mut dyn Args) -> Cell {
    let (t, o, hz) = (a.float(2), opponent(a, 3), a.float(4));
    with_bot(a, |b, _, _, _| float_cell(b.ledger.credit(t, o, hz)))
}

/// `Float:rl_ledger_dealt_since(RcLearn:bot, Float:since, opponent = -1)`
pub fn rl_ledger_dealt_since(a: &mut dyn Args) -> Cell {
    let (t, o) = (a.float(2), opponent(a, 3));
    with_bot(a, |b, _, _, _| float_cell(b.ledger.dealt_since(t, o)))
}

/// `Float:rl_ledger_taken_since(RcLearn:bot, Float:since, opponent = -1)`
pub fn rl_ledger_taken_since(a: &mut dyn Args) -> Cell {
    let (t, o) = (a.float(2), opponent(a, 3));
    with_bot(a, |b, _, _, _| float_cell(b.ledger.taken_since(t, o)))
}

/// `Float:rl_ledger_reward_since(RcLearn:bot, Float:since, opponent)`
pub fn rl_ledger_reward_since(a: &mut dyn Args) -> Cell {
    let (t, o) = (a.float(2), a.cell(3).max(0) as u32);
    with_bot(a, |b, _, _, _| float_cell(b.ledger.reward_since(t, o)))
}

/// `rl_ledger_kills_since(RcLearn:bot, Float:since)`
pub fn rl_ledger_kills_since(a: &mut dyn Args) -> Cell {
    let t = a.float(2);
    with_bot(a, |b, _, _, _| b.ledger.kills_since(t) as Cell)
}

/// `rl_ledger_clear(RcLearn:bot)`
pub fn rl_ledger_clear(a: &mut dyn Args) -> Cell {
    with_bot(a, |b, _, _, _| {
        b.ledger.clear();
        1
    })
}

/// `rl_range_bin(Float:dist)`
pub fn rl_range_bin(a: &mut dyn Args) -> Cell {
    range_bin(a.float(1)) as Cell
}

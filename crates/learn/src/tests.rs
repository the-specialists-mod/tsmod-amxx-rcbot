// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The natives against a fake AMX ([`FakeArgs`]) and a temporary data directory. Each test runs
//! on its own thread, so each has its own module state. File checks compare against what
//! `rcbot::learn` itself writes, which is what the standalone plugin writes.

use crate::natives::{self as n, profile_fields};
use crate::objects as o;
use crate::state::{self, with};
use amxx::Cell;
use amxx::native::FakeArgs;
use rcbot::learn::data::{BotLearnedData, GlobalLearnedData};
use rcbot::learn::opponents::OpponentKey;
use rcbot::learn::perceptron::sigmoid;
use rcbot_amxx_common::data_dir::{TempDir, temp_dir};
use rcbot_amxx_common::{DataDir, float_cell};

fn setup(name: &str) -> TempDir {
    let d = temp_dir(name);
    state::attach(DataDir::new(&*d));
    with(|s| s.rng = rcbot_amxx_common::ModuleRandom::seeded(1));
    d
}

fn f(v: f32) -> Cell {
    float_cell(v)
}

fn fl(c: Cell) -> f32 {
    f32::from_bits(c as u32)
}

fn floats(a: &mut FakeArgs, v: &[f32]) -> Cell {
    a.alloc(v.iter().map(|&x| f(x)).collect())
}

fn bot(a: &mut FakeArgs, id: Cell, name: &str) -> Cell {
    let s = a.alloc_str(name);
    a.params = vec![id, s];
    n::rl_bot_load(a)
}

#[test]
fn bot_files_are_rcbots_own_bytes() {
    let d = setup("botfiles");
    let mut a = FakeArgs::new(&[]);
    let b = bot(&mut a, 5, "Bot");
    a.params = vec![b, 1];
    n::rl_bot_on_spawn(&mut a);
    a.params = vec![b, 1, f(0.6), 1];
    n::rl_bot_on_death(&mut a);
    a.params = vec![b, 6, f(300.0), f(2.0)];
    n::rl_ws_add_exposure(&mut a);
    a.params = vec![b, 3, f(1.5)];
    n::rl_loadout_reward(&mut a);
    a.params = vec![b];
    assert_eq!(n::rl_bot_save(&mut a), 1);

    let (rld, rwr, rlo) = with(|s| {
        let x = s.bots.get(b).unwrap();
        (x.data.save(5), x.weapons.save(), x.loadout.save())
    })
    .unwrap();
    assert_eq!(std::fs::read(d.join("botprofiles/5.rld")).unwrap(), rld);
    assert_eq!(std::fs::read_to_string(d.join("botprofiles/5.rwr")).unwrap(), rwr);
    assert_eq!(std::fs::read_to_string(d.join("botprofiles/5.rlo")).unwrap(), rlo);

    // Loaded back, the bot has the same learned data.
    let b2 = bot(&mut a, 5, "Bot");
    let same = with(|s| {
        let (x, y) = (s.bots.get(b).unwrap(), s.bots.get(b2).unwrap());
        x.data.to_bytes(5) == y.data.to_bytes(5) && x.weapons == y.weapons && x.loadout == y.loadout
    });
    assert_eq!(same, Some(true));

    // A file rcbot wrote for another profile loads nothing (the header names the profile).
    let mut rng = rcbot::XorShiftRandom::new(9);
    std::fs::write(d.join("botprofiles/6.rld"), BotLearnedData::new(&mut rng).save(5)).unwrap();
    let b3 = bot(&mut a, 6, "Other");
    let fresh = with(|s| s.bots.get(b3).unwrap().data.survival.is_empty());
    assert_eq!(fresh, Some(true));
}

#[test]
fn bots_without_a_profile_use_their_name() {
    let d = setup("nameless");
    let mut a = FakeArgs::new(&[]);
    let b = bot(&mut a, 0, "[RC] Bot 7!");
    let out = a.alloc(vec![0; 64]);
    a.params = vec![b, out, 63];
    n::rl_bot_key(&mut a);
    assert_eq!(a.read_str(out), "default-RCBot7");
    let var = a.alloc(vec![b]);
    a.params = vec![var, 1];
    assert_eq!(n::rl_bot_free(&mut a), 1);
    assert_eq!(a.memory[var as usize][0], 0);
    assert!(d.join("botprofiles/default-RCBot7.rld").exists());
    a.params = vec![b];
    assert_eq!(n::rl_bot_save(&mut a), 0);
    assert_eq!(a.errors, [format!("Invalid learn handle {b}")]);
}

#[test]
fn globals_round_trip_in_rcbots_layout() {
    let d = setup("globals");
    let mut a = FakeArgs::new(&[]);
    assert_eq!(n::rl_load_globals(&mut a), 0);
    // Breed something into team 2's GA and the loadout GA.
    let b = bot(&mut a, 1, "Bot");
    for _ in 0..20 {
        a.params = vec![b, 2];
        n::rl_bot_on_spawn(&mut a);
        let ids = a.alloc(vec![0; 3]);
        a.params = vec![b, 0, ids];
        assert_eq!(n::rl_bot_select_ts_weapons(&mut a), 3);
        a.params = vec![b, 2, f(0.4), 0];
        n::rl_bot_on_death(&mut a);
    }
    // Player 3 is reputation id 77.
    a.params = vec![3, 1, 77];
    n::rl_opp_set_key(&mut a);
    a.params = vec![3, 5, f(400.0), f(30.0), 1];
    assert_eq!(n::rl_opp_hit_on_bot(&mut a), 1);

    assert_eq!(n::rl_save_globals(&mut a), 8);
    let g = with(|s| s.globals.clone()).unwrap();
    assert!(g.enemy_costs[2].generations() > 0);
    assert_eq!(std::fs::read(d.join("team2.rld")).unwrap(), g.save_team(2));
    assert_eq!(std::fs::read(d.join("tsweaps.rld")).unwrap(), g.save_ts_weapons());
    assert_eq!(std::fs::read_to_string(d.join("botprofiles/players.rop")).unwrap(), g.opponents.save());

    // Loading starts over: what the files hold, not twice.
    assert_eq!(n::rl_load_globals(&mut a), 8);
    let mut want = GlobalLearnedData::default();
    want.load_team(2, &g.save_team(2));
    want.load_ts_weapons(&g.save_ts_weapons());
    let back = with(|s| s.globals.clone()).unwrap();
    assert_eq!(back.save_team(2), want.save_team(2));
    assert_eq!(back.save_ts_weapons(), want.save_ts_weapons());
    assert_eq!(back.opponents, g.opponents);
}

#[test]
fn decisions_use_the_genes() {
    let _d = setup("decide");
    let mut a = FakeArgs::new(&[]);
    let b = bot(&mut a, 0, "x");
    a.params = vec![b, 0];
    n::rl_bot_on_spawn(&mut a);
    let personal = with(|s| s.bots.get(b).unwrap().data.personal.values().to_vec()).unwrap();
    assert_eq!(personal.len(), 22);
    let inputs = [0.5, 1.0, -0.2, 1.0, 1.0];
    let ins = floats(&mut a, &inputs);
    let out = a.alloc(vec![0]);
    a.params = vec![b, 2, ins, 5, out];
    let fired = n::rl_bot_decide(&mut a);
    // Strafe: bias weight 1.0, then personal 10..15.
    let net: f32 = 1.0 + inputs.iter().zip(&personal[10..15]).map(|(i, w)| i * w).sum::<f32>();
    assert_eq!(fl(a.memory[out as usize][0]), sigmoid(net));
    assert_eq!(fired != 0, sigmoid(net) >= 0.5);

    a.params = vec![b, 1, 11];
    assert_eq!(fl(n::rl_bot_gene(&mut a)), personal[11]);
    let g = a.alloc(vec![0; 30]);
    a.params = vec![b, 1, g, 30];
    assert_eq!(n::rl_bot_genes(&mut a), 22);
    a.params = vec![b, 9, ins, 5, out];
    assert_eq!(n::rl_bot_decide(&mut a), 0);
    assert_eq!(a.errors, ["Invalid decision 9"]);
}

#[test]
fn weapon_scores_need_samples() {
    let _d = setup("weapons");
    let mut a = FakeArgs::new(&[]);
    let b = bot(&mut a, 0, "x");
    let score = a.alloc(vec![0]);
    a.params = vec![b, 13, f(500.0), score];
    assert_eq!(n::rl_ws_score(&mut a), 0);
    assert_eq!(fl(a.memory[score as usize][0]), rcbot::learn::weapon_stats::DEFAULT_DPS);
    for _ in 0..30 {
        a.params = vec![b, 13, f(500.0), f(0.25)];
        n::rl_ws_add_exposure(&mut a);
    }
    a.params = vec![b, 13, f(500.0), f(80.0)];
    n::rl_ws_add_hit(&mut a);
    a.params = vec![b, 13, f(500.0), f(4.0)];
    n::rl_ws_add_shots(&mut a);
    a.params = vec![b, 13, f(500.0), score];
    assert_eq!(n::rl_ws_score(&mut a), 1);
    let s = fl(a.memory[score as usize][0]);
    assert!(s > rcbot::learn::weapon_stats::DEFAULT_DPS, "{s}");
    a.params = vec![b, 13, f(500.0)];
    assert!(fl(n::rl_ws_samples(&mut a)) >= 5.0);
    a.params = vec![0, 13, f(500.0)];
    assert!(fl(n::rl_ws_samples(&mut a)) >= 5.0, "the prior learnt too");
    a.params = vec![b, 13, f(500.0), score];
    assert_eq!(n::rl_ws_accuracy(&mut a), 1);
    assert_eq!(fl(a.memory[score as usize][0]), 0.25);
}

#[test]
fn loadouts() {
    let _d = setup("loadouts");
    let mut a = FakeArgs::new(&[]);
    assert_eq!(n::rl_loadout_count(&mut a), rcbot::learn::loadout::LOADOUTS.len() as Cell);
    let ids = a.alloc(vec![0; 4]);
    a.params = vec![0, ids, 4];
    assert_eq!(n::rl_loadout_weapons(&mut a), 2);
    assert_eq!(&a.memory[ids as usize][..2], [5, 12]);
    a.params = vec![0];
    assert_eq!(n::rl_loadout_slots(&mut a), 55);
    a.params = vec![24];
    assert_eq!(n::rl_weapon_slots(&mut a), 7);
    let owned = a.alloc(vec![13, 12, 38]);
    a.params = vec![-1, owned, 3];
    assert_eq!(n::rl_credited_loadout(&mut a), 1);
    a.params = vec![2, 0, f(0.0), f(60.0)];
    assert_eq!(fl(n::rl_life_reward(&mut a)), 2.0);
    let b = bot(&mut a, 0, "x");
    a.params = vec![b, 40, 0];
    let arm = n::rl_loadout_choose(&mut a);
    assert!(rcbot::learn::loadout::loadout_slots(rcbot::learn::loadout::LOADOUTS[arm as usize]) <= 40);
    a.params = vec![b, 999, f(1.0)];
    assert_eq!(n::rl_loadout_reward(&mut a), 0);
    assert_eq!(a.errors, ["Invalid loadout 999"]);
}

#[test]
fn opponents_by_player_slot() {
    let _d = setup("opponents");
    let mut a = FakeArgs::new(&[]);
    a.params = vec![4];
    assert_eq!(fl(n::rl_opp_threat(&mut a)), 1.0, "unknown player");
    assert_eq!(n::rl_opp_kill_of_bot(&mut a), 0);
    a.params = vec![4, 2, 9];
    n::rl_opp_set_key(&mut a);
    assert_eq!(with(|s| s.key(4)), Some(Some(OpponentKey::Bot(9))));
    for _ in 0..5 {
        a.params = vec![4];
        n::rl_opp_kill_of_bot(&mut a);
    }
    a.params = vec![4, f(20.0), f(0.5)];
    n::rl_opp_exposure(&mut a);
    a.params = vec![4, 5, f(400.0), f(900.0), 1];
    n::rl_opp_hit_on_bot(&mut a);
    let attacks = a.alloc(vec![1, 0, 2, 0, 0]);
    a.params = vec![4, attacks, 5, 3, f(0.4)];
    n::rl_opp_kungfu(&mut a);
    a.params = vec![4];
    assert!(fl(n::rl_opp_threat(&mut a)) > 1.0);
    let out = a.alloc(vec![0; 64]);
    a.params = vec![4, out, 64];
    assert_eq!(n::rl_opp_profile(&mut a), 57);
    let want = with(|s| profile_fields(s.globals.opponents.profile(OpponentKey::Bot(9)).unwrap())).unwrap();
    assert_eq!(a.memory[out as usize][..57].iter().map(|&c| fl(c)).collect::<Vec<_>>(), want);
    assert_eq!(fl(a.memory[out as usize][3]), 5.0, "kills of bots");
    assert_eq!(fl(a.memory[out as usize][13 + 2]), 2.0, "kung fu attacks of kind 2");
    a.params = vec![33, 1, 1];
    assert_eq!(n::rl_opp_set_key(&mut a), 0);
    assert_eq!(a.errors, ["Player 33 is not valid"]);
}

#[test]
fn the_ledger() {
    let _d = setup("ledger");
    let mut a = FakeArgs::new(&[]);
    let b = bot(&mut a, 0, "x");
    a.params = vec![b, f(10.0), 3, 13, f(50.0), f(0.0), 0, 0, f(300.0), 2];
    n::rl_ledger_record(&mut a);
    a.params = vec![b, f(10.5), 3, 13, f(60.0), f(0.0), 1, 0, f(300.0), 2];
    n::rl_ledger_record(&mut a);
    a.params = vec![b, f(9.0), -1];
    assert_eq!(fl(n::rl_ledger_dealt_since(&mut a)), 110.0);
    a.params = vec![b, f(9.0)];
    assert_eq!(n::rl_ledger_kills_since(&mut a), 1);
    a.params = vec![b, f(9.0), 3];
    assert_eq!(fl(n::rl_ledger_reward_since(&mut a)), 2.1);
    a.params = vec![b, f(10.0), 3, f(5.0)];
    // 0.5 at the decision, then the kill's 1.6 half a second later, discounted.
    assert_eq!(fl(n::rl_ledger_credit(&mut a)), 0.5 + 1.6 * (-0.5f32).exp());
    a.params = vec![f(300.0)];
    assert_eq!(n::rl_range_bin(&mut a), 2);
}

#[test]
fn a_ga_breeds_saves_and_loads() {
    let d = setup("ga");
    let mut a = FakeArgs::new(&[4]);
    let ga = o::rl_ga_create(&mut a);
    for i in 0..4 {
        let g = floats(&mut a, &[i as f32, 1.0, 2.0]);
        a.params = vec![ga, g, 3, f(i as f32)];
        o::rl_ga_add(&mut a);
    }
    a.params = vec![ga];
    assert_eq!(o::rl_ga_generations(&mut a), 1);
    assert_eq!(o::rl_ga_can_pick(&mut a), 1);
    let (out, fit) = (a.alloc(vec![0; 8]), a.alloc(vec![0]));
    a.params = vec![ga, out, 8, fit];
    assert_eq!(o::rl_ga_best(&mut a), 3);
    assert_eq!(fl(a.memory[fit as usize][0]), 3.0);
    let file = a.alloc_str("custom/test.rga");
    a.params = vec![ga, file];
    assert_eq!(o::rl_ga_save(&mut a), 1);
    let mut want = Vec::new();
    with(|s| s.gas.get(ga).unwrap().save(&mut want));
    assert_eq!(std::fs::read(d.join("custom/test.rga")).unwrap(), want);

    a.params = vec![4];
    let ga2 = o::rl_ga_create(&mut a);
    a.params = vec![ga2, file, 3];
    assert_eq!(o::rl_ga_load(&mut a), 1);
    let mut back = Vec::new();
    with(|s| s.gas.get(ga2).unwrap().save(&mut back));
    assert_eq!(back, want);
    a.params = vec![ga2, out, 8, fit];
    assert_eq!(o::rl_ga_pick(&mut a), 3);

    let bad = a.alloc_str("../escape.rga");
    a.params = vec![ga, bad];
    assert_eq!(o::rl_ga_save(&mut a), 0);
}

#[test]
fn perceptrons_learn_and_persist() {
    let _d = setup("perceptron");
    let mut a = FakeArgs::new(&[2, f(0.5)]);
    let p = o::rl_perceptron_create(&mut a);
    let w = floats(&mut a, &[0.0, 1.0, -1.0]);
    a.params = vec![p, w, 3];
    o::rl_perceptron_set_weights(&mut a);
    let ins = floats(&mut a, &[2.0, 1.0]);
    a.params = vec![p, ins, 2];
    assert_eq!(fl(o::rl_perceptron_execute(&mut a)), sigmoid(1.0));
    a.params = vec![p];
    assert_eq!(o::rl_perceptron_fired(&mut a), 1);
    a.params = vec![p, f(0.0)];
    assert_eq!(o::rl_perceptron_train(&mut a), 1);
    let out = a.alloc(vec![0; 3]);
    a.params = vec![p, out, 3];
    assert_eq!(o::rl_perceptron_weights(&mut a), 3);
    assert!(fl(a.memory[out as usize][1]) < 1.0, "trained toward 0");

    let file = a.alloc_str("p.rp");
    a.params = vec![p, file];
    assert_eq!(o::rl_perceptron_save(&mut a), 1);
    a.params = vec![5, f(0.5)];
    let q = o::rl_perceptron_create(&mut a);
    a.params = vec![q, file];
    assert_eq!(o::rl_perceptron_load(&mut a), 1);
    let same = with(|s| s.perceptrons.get(p).unwrap().weights() == s.perceptrons.get(q).unwrap().weights());
    assert_eq!(same, Some(true));
    // Wrong input count: 0.0, as upstream's guard.
    a.params = vec![q, ins, 1];
    assert_eq!(fl(o::rl_perceptron_execute(&mut a)), 0.0);
}

#[test]
fn networks_and_maps() {
    let _d = setup("nnsom");
    let mut a = FakeArgs::new(&[1, 3, 4, 2]);
    let nn = o::rl_nn_create(&mut a);
    let ins = floats(&mut a, &[0.1, 0.2, 0.3]);
    let out = a.alloc(vec![0; 4]);
    a.params = vec![nn, ins, 3, out, 4];
    assert_eq!(o::rl_nn_execute(&mut a), 2);
    let w = a.alloc(vec![0; 64]);
    a.params = vec![nn, w, 64];
    // 4 neurons of 3 inputs + 4 of 4 + 2 of 4, each with a bias weight.
    assert_eq!(o::rl_nn_weights(&mut a), 4 * 4 + 4 * 5 + 2 * 5);
    let file = a.alloc_str("n.rnn");
    a.params = vec![nn, file];
    assert_eq!(o::rl_nn_save(&mut a), 1);
    a.params = vec![0, 1, 1, 1];
    let nn2 = o::rl_nn_create(&mut a);
    a.params = vec![nn2, file];
    assert_eq!(o::rl_nn_load(&mut a), 1);
    assert_eq!(with(|s| s.nns.get(nn).unwrap().weights() == s.nns.get(nn2).unwrap().weights()), Some(true));
    a.params = vec![0, 0, 1, 1];
    assert_eq!(o::rl_nn_create(&mut a), 0);
    assert_eq!(a.errors.len(), 1);

    a.params = vec![3, 2, 2];
    let som = o::rl_som_create(&mut a);
    let x = floats(&mut a, &[0.9, 0.1]);
    a.params = vec![som, x, 2];
    let bmu = o::rl_som_input(&mut a);
    assert!((0..6).contains(&bmu));
    assert_eq!(o::rl_som_bmu(&mut a), bmu, "the winner moved toward the input");
    let sw = a.alloc(vec![0; 2]);
    a.params = vec![som, bmu, sw, 2];
    assert_eq!(o::rl_som_weights(&mut a), 2);
    a.params = vec![som];
    assert_eq!(o::rl_som_epochs(&mut a), 1);
    let var = a.alloc(vec![som]);
    a.params = vec![var];
    assert_eq!(o::rl_som_free(&mut a), 1);
    a.params = vec![som];
    assert_eq!(o::rl_som_epochs(&mut a), 0);
    assert_eq!(a.errors.last().unwrap(), &format!("Invalid SOM handle {som}"));
}

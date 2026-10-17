// rcbot_learn.inc: RCBot's learners and learned-data files for AMX Mod X plugins.
// tsmod-amxx-rcbot (https://github.com/the-specialists-mod/tsmod-amxx-rcbot),
// GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions.
//
// Files are relative to RCBot's data directory, <gamedir>/tsmod/rcbot/, in the byte layouts the
// standalone tsmod_rcbot plugin (and, for .rld, RCBot) uses, so both share them. Writes are
// atomic (a temporary file renamed over the old one). Paths with ".." or absolute paths are
// refused. Handles die at map change (AMX Mod X reloads the plugins); the global data stays in
// the module. An invalid handle is a native error.
//
// TS weapon ids are TS 3.0's (tsconst.inc); kung fu is 0 or 38. Distances are in units.

#if defined _rcbot_learn_included
  #endinput
#endif
#define _rcbot_learn_included

#pragma reqlib rcbot_learn
#if !defined AMXMODX_NOAUTOLOAD
  #pragma loadlib rcbot_learn
#endif

/**
 * Seeds the module's random numbers (GA breeding, new genes, loadout ties), for reproducible
 * runs. They start seeded from the clock. Not the engine's RANDOM_LONG sequence.
 */
native rl_seed(seed);

// ---- the global learned data ----

/**
 * Reads the global files, starting from empty data: team0.rld-team3.rld (each team's enemy-cost
 * GA), tsweaps.rld (the loadout GA), weaponstats.rwr (every bot's weapon results),
 * loadouts.rlo (every bot's loadout estimates), botprofiles/players.rop (the opponent model).
 * Call once in plugin_init; missing files start empty.
 *
 * @return          The number of files read.
 */
native rl_load_globals();

/**
 * Writes the global files (and the map's heatmap, when one is loaded), as the standalone plugin
 * does at the map's end. Call in plugin_end.
 *
 * @return          The number of files written.
 */
native rl_save_globals();

// ---- a bot's learned data ----

/**
 * A bot's learned data (setupDataStructures, CBot::loadLearnedData): new perceptrons and genes,
 * then botprofiles/<key>.rld, .rwr (weapons by range) and .rlo (loadout estimates) if they exist.
 * The key is the profile id, or "default-<name>" (letters, digits, - and _) for profile 0.
 * The kung fu fighter's <key>.rkf is not handled here.
 *
 * @return          The handle (never 0).
 */
native RcLearn:rl_bot_load(profile_id, const name[]);

/**
 * Writes the bot's .rld (RCZ-compressed, as RCBot), .rwr and .rlo.
 *
 * @return          true if every file was written.
 */
native bool:rl_bot_save(RcLearn:bot);

/**
 * Frees a bot's data and sets the variable to 0.
 *
 * @param save      Save it first (rl_bot_save).
 */
native bool:rl_bot_free(&RcLearn:bot, bool:save = false);

/**
 * The bot's file key (see rl_bot_load).
 */
native rl_bot_key(RcLearn:bot, out[], len);

/**
 * CBot::SpawnInit's learning: the next bred personal genes (or fresh ones), then the team's next
 * bred survival genes. team is RCBot's team (rules team - 1); anything outside 0-3 is 0.
 */
native rl_bot_on_spawn(RcLearn:bot, team);

/**
 * The death bookkeeping: the loadout and survival genes (to the team's GA) and, if the bot used
 * melee this life, the personal genes are scored with fitness and bred.
 */
native rl_bot_on_death(RcLearn:bot, team, Float:fitness, bool:used_melee);

/**
 * The life's fitness: survival_time * 0.001 + frags * 0.2 + team score * 0.2.
 */
native Float:rl_death_fitness(Float:survival_time, frags_since_death, team_score_since_death);

/**
 * The TS loadout GA's pick for the next life (its genes also weight the stunt perceptron).
 *
 * @param ids       The three weapon ids upstream would buy (gene * 35).
 * @return          3.
 */
native rl_bot_select_ts_weapons(RcLearn:bot, bool:disable_learn, ids[3]);

// The perceptrons rl_bot_decide runs, and their weights (bias first, then genes).
enum RlDecision
{
	RlDecide_Jump = 0,          // decideJumpDuckStrafe: personal 0-4, 5 inputs
	RlDecide_Duck,              // personal 5-9, 5 inputs
	RlDecide_Strafe,            // personal 10-14, 5 inputs; output * 2 - 1 is the strafe speed
	RlDecide_Stunt,             // TS stunt: loadout genes 3-7, 5 inputs
	RlDecide_AttackElectrified  // personal 15-21, 7 inputs
};

/**
 * Runs a decision perceptron with its weights from the bot's genes.
 *
 * @param output    The sigmoid output (0-1).
 * @return          Whether it fired (output >= 0.5).
 */
native bool:rl_bot_decide(RcLearn:bot, RlDecision:which, const Float:inputs[], num, &Float:output = 0.0);

// Gene sets.
enum RlGenes
{
	RlGenes_Survival = 0,       // 16: 0-3 friendly/enemy/inanimate/grenade cost, 4-7 run for cover,
	                            //     8-11 face the hurt origin, 12-15 follow the enemy
	RlGenes_Personal,           // 22: jump, duck, strafe, electrified enemy
	RlGenes_TSWeaponSelect      // 8: 0-2 the loadout, 3-7 stunt weights
};

/**
 * One gene (0.0 out of range).
 */
native Float:rl_bot_gene(RcLearn:bot, RlGenes:set, index);

/**
 * A gene set.
 *
 * @return          The number of genes written.
 */
native rl_bot_genes(RcLearn:bot, RlGenes:set, Float:out[], len);

// ---- weapon effectiveness by range ----
// Each add goes to the bot's table and every bot's prior; bot 0 adds to the prior only, and
// queries with bot 0 read the prior.

/**
 * dt seconds with the enemy in sight at dist, weapon in hand.
 */
native rl_ws_add_exposure(RcLearn:bot, weapon, Float:dist, Float:dt);

/**
 * A hit for damage with weapon at dist.
 */
native rl_ws_add_hit(RcLearn:bot, weapon, Float:dist, Float:damage);

/**
 * Rounds fired with weapon at dist.
 */
native rl_ws_add_shots(RcLearn:bot, weapon, Float:dist, Float:rounds);

/**
 * How well weapon has worked at dist: damage per second in sight, shrunk toward every bot's.
 *
 * @param score     Set always.
 * @return          true once the bot has 5 seconds of its own there (learned_weapon_score).
 */
native bool:rl_ws_score(RcLearn:bot, weapon, Float:dist, &Float:score);

/**
 * Seconds of exposure behind weapon at dist.
 */
native Float:rl_ws_samples(RcLearn:bot, weapon, Float:dist);

/**
 * Hits per round fired.
 *
 * @return          false before any round.
 */
native bool:rl_ws_accuracy(RcLearn:bot, weapon, Float:dist, &Float:accuracy);

// ---- loadouts (the port's bandit over curated loadouts) ----

// Seconds after a spawn in which the inventory may show the loadout bought.
#define RL_CREDIT_WINDOW 3.0

/**
 * The number of curated loadouts (arms 0 .. count - 1).
 */
native rl_loadout_count();

/**
 * A loadout's weapon ids, primary first.
 *
 * @return          The number written (0: no such loadout).
 */
native rl_loadout_weapons(arm, ids[], len);

/**
 * The slots a loadout uses.
 */
native rl_loadout_slots(arm);

/**
 * TS 3.0's slot cost of a weapon.
 */
native rl_weapon_slots(weapon);

/**
 * The loadout for the next life among those within capacity slots (UCB1 on the bot's estimates
 * shrunk toward every bot's), at random when random (weapon learning off).
 *
 * @return          The arm.
 */
native rl_loadout_choose(RcLearn:bot, capacity, bool:random);

/**
 * A life with loadout arm earned reward (rl_life_reward): the bot's and every bot's estimates.
 */
native rl_loadout_reward(RcLearn:bot, arm, Float:reward);

/**
 * A life's reward: (kills - 0.5 deaths + dealt / 200) per minute (at least half a minute),
 * clamped to -6..12.
 */
native Float:rl_life_reward(kills, deaths, Float:dealt, Float:seconds);

/**
 * The arm the inventory after a spawn shows: the one bought (-1 none) if all its weapons are
 * owned, else a loadout that is exactly what is owned (grenades, knives, kung fu aside).
 *
 * @return          The arm, -1 for none.
 */
native rl_credited_loadout(bought, const owned[], num);

// ---- the opponent model (botprofiles/players.rop) ----
// Players are entity indexes 1-32. Each needs a key first: humans and other backends' bots by
// reputation id (bot_playerids.ini), RCBot's bots by profile id. Observations of a player without
// a key are ignored (return 0).

enum RlKeyKind
{
	RlKey_None = 0,
	RlKey_Player,               // id: the reputation id
	RlKey_Bot                   // id: the profile id (0: every bot without a profile)
};

native rl_opp_set_key(player, RlKeyKind:kind, id);

/**
 * The player hurt a bot.
 */
native rl_opp_hit_on_bot(player, weapon, Float:dist, Float:damage, bool:headshot);

/**
 * The player killed a bot.
 */
native rl_opp_kill_of_bot(player);

/**
 * A bot killed the player.
 */
native rl_opp_death_to_bot(player);

/**
 * dt seconds the player was some bot's enemy in sight, moving sideways (0-1 of 290 u/s) across
 * its view.
 */
native rl_opp_exposure(player, Float:dt, Float:sideways);

/**
 * The player did a stunt at game time now.
 */
native rl_opp_stunt(player, Float:now);

/**
 * A kung fu fight with the player ended.
 *
 * @param attacks   Its attacks by kind (5 kinds).
 * @param interval  Seconds between its attacks, negative for unknown.
 */
native rl_opp_kungfu(player, const attacks[], num, blocks, Float:interval = -1.0);

/**
 * How dangerous the player is to bots: 1.0 for an average or unknown player, 0.25-4.0.
 */
native Float:rl_opp_threat(player);

// rl_opp_profile's fields (players.rop's order).
#define RL_OPP_EXPOSURE        0
#define RL_OPP_DAMAGE_TO_BOTS  1
#define RL_OPP_HITS_ON_BOTS    2
#define RL_OPP_KILLS_OF_BOTS   3
#define RL_OPP_DEATHS_TO_BOTS  4
#define RL_OPP_RANGE           5
#define RL_OPP_KUNGFU_SHARE    6
#define RL_OPP_STUNTS          7
#define RL_OPP_STUNT_RATE      8
#define RL_OPP_DODGE           9
#define RL_OPP_KF_FIGHTS       10
#define RL_OPP_KF_BLOCKS       11
#define RL_OPP_KF_INTERVAL     12
#define RL_OPP_KF_ATTACKS      13  // 5 kinds
#define RL_OPP_WEAPONS         18  // 38 weapon ids (0 kung fu .. 37)
#define RL_OPP_HEADSHOTS       56
#define RL_OPP_FIELDS          57

/**
 * What the bots saw of the player.
 *
 * @return          The number of fields written, 0 for an unknown player.
 */
native rl_opp_profile(player, Float:out[], len);

/**
 * A new map: counts fade, stunt timing restarts. Call in plugin_init after rl_load_globals.
 */
native rl_opp_new_map();

// ---- the heatmap (waypoints/ts/<map>.rhm) ----
// Waypoints are indexes of the map's waypoints. Counts take the player's rules team (1, 2, ...;
// 0 none); danger and cover take RCBot's team (rules team - 1; -1 none), as the bot asks.

/**
 * The map's heatmap for its waypoints (a new one when the file is missing or was made for
 * another waypoint count), faded once for this load.
 *
 * @return          true if the file was used.
 */
native bool:rl_heatmap_load(const map[], num_waypoints);

/**
 * Writes the heatmap (also done by rl_save_globals).
 */
native bool:rl_heatmap_save();

native rl_heatmap_add_death(waypoint, rules_team);
native rl_heatmap_add_kill(waypoint, rules_team);
native rl_heatmap_add_damage(waypoint, rules_team, Float:amount);

/**
 * How dangerous the waypoint is for the team: 0..1 (0.5 at 2 deaths).
 */
native Float:rl_heatmap_danger(waypoint, bot_team);

/**
 * Units to add to a cover waypoint's distance (up to 256).
 */
native Float:rl_heatmap_cover_penalty(waypoint, bot_team);

/**
 * How good the waypoint has been to kill from, any team: 0..1.
 */
native Float:rl_heatmap_ambush(waypoint);

// ---- the combat ledger (per bot, in memory) ----
// opponent -1 means anyone.

/**
 * Something that passed between the bot and another player (0 the world). A kill or death
 * closes the episode with that player.
 */
native rl_ledger_record(RcLearn:bot, Float:time, other, weapon, Float:dealt, Float:taken, bool:kill, bool:death, Float:dist, waypoint);

/**
 * The bot engages opponent now: opens an episode, or keeps the open one alive.
 */
native rl_ledger_engage(RcLearn:bot, Float:now, opponent);

/**
 * Closes episodes without contact for 4 seconds.
 */
native rl_ledger_tick(RcLearn:bot, Float:now);

/**
 * Closes the episode with opponent (-1: all, as at the bot's death).
 */
native rl_ledger_close(RcLearn:bot, opponent = -1);

/**
 * The credit of a decision taken at time decision: the rewards with opponent within horizon
 * seconds after it, each discounted by exp(-dt).
 */
native Float:rl_ledger_credit(RcLearn:bot, Float:decision, opponent, Float:horizon);

native Float:rl_ledger_dealt_since(RcLearn:bot, Float:since, opponent = -1);
native Float:rl_ledger_taken_since(RcLearn:bot, Float:since, opponent = -1);

/**
 * (dealt - taken) / 100, +1 per kill, -1 per death, with opponent since since.
 */
native Float:rl_ledger_reward_since(RcLearn:bot, Float:since, opponent);
native rl_ledger_kills_since(RcLearn:bot, Float:since);
native rl_ledger_clear(RcLearn:bot);

/**
 * The ledger's range bin of a distance (0-5: up to 64, 192, 384, 768, 1500, farther).
 */
native rl_range_bin(Float:dist);

// ---- generic learners ----

/**
 * A genetic algorithm of float genes (CGA): roulette selection, 0.7 cross-over, adaptive
 * mutation, two elites.
 *
 * @param max_pop_size  The population that triggers an epoch (0: 16).
 */
native RcGa:rl_ga_create(max_pop_size = 0);
native bool:rl_ga_free(&RcGa:ga);

/**
 * Adds a scored individual; once the population is full, breeds the next generation.
 *
 * @return          The generation count.
 */
native rl_ga_add(RcGa:ga, const Float:genes[], num, Float:fitness);
native bool:rl_ga_can_pick(RcGa:ga);

/**
 * Takes the next bred individual.
 *
 * @return          The number of genes written (0: none to pick).
 */
native rl_ga_pick(RcGa:ga, Float:genes[], len, &Float:fitness = 0.0);

/**
 * The best individual of any epoch.
 *
 * @return          The number of genes written (0: no epoch yet).
 */
native rl_ga_best(RcGa:ga, Float:genes[], len, &Float:fitness = 0.0);
native rl_ga_generations(RcGa:ga);
native rl_ga_population(RcGa:ga);

/**
 * CGA::save's layout.
 */
native bool:rl_ga_save(RcGa:ga, const file[]);

/**
 * Empties the GA and reads a file; individuals must have genes genes.
 */
native bool:rl_ga_load(RcGa:ga, const file[], genes);

/**
 * A perceptron (CPerceptron): random weights, bias input 1.0, sigmoid output.
 */
native RcPerceptron:rl_perceptron_create(num_inputs, Float:learn_rate = 0.5);
native bool:rl_perceptron_free(&RcPerceptron:p);

/**
 * @return          The output (0.0 if num isn't the input count).
 */
native Float:rl_perceptron_execute(RcPerceptron:p, const Float:inputs[], num);
native bool:rl_perceptron_fired(RcPerceptron:p);
native Float:rl_perceptron_output(RcPerceptron:p);

/**
 * Trains on the last inputs toward expected.
 */
native rl_perceptron_train(RcPerceptron:p, Float:expected);

/**
 * The weights, bias weight first.
 *
 * @return          The number written.
 */
native rl_perceptron_weights(RcPerceptron:p, Float:out[], len);
native rl_perceptron_set_weights(RcPerceptron:p, const Float:weights[], num);
native rl_perceptron_randomize(RcPerceptron:p);
native bool:rl_perceptron_save(RcPerceptron:p, const file[]);
native bool:rl_perceptron_load(RcPerceptron:p, const file[]);

/**
 * A feed-forward network (NN): an input layer of per_hidden neurons, hidden_layers more, the
 * output layer.
 */
native RcNn:rl_nn_create(hidden_layers, num_inputs, per_hidden, num_outputs);
native bool:rl_nn_free(&RcNn:nn);

/**
 * @return          The number of outputs written.
 */
native rl_nn_execute(RcNn:nn, const Float:inputs[], num, Float:outputs[], len);

/**
 * NN::trainOutputs on the last inputs. As upstream, it trains the first layer's neurons.
 */
native rl_nn_train(RcNn:nn, const Float:wanted[], num);

/**
 * Every neuron's weights in order.
 *
 * @return          The total count (may be more than len).
 */
native rl_nn_weights(RcNn:nn, Float:out[], len);
native rl_nn_set_weights(RcNn:nn, const Float:weights[], num);
native rl_nn_randomize(RcNn:nn);
native bool:rl_nn_save(RcNn:nn, const file[]);
native bool:rl_nn_load(RcNn:nn, const file[]);

/**
 * A self-organising map (CSom), neurons row by row. It has no file format.
 */
native RcSom:rl_som_create(width, height, num_inputs);
native bool:rl_som_free(&RcSom:som);

/**
 * Trains on one input (CSom::inputOne).
 *
 * @return          The best matching neuron, -1 for none.
 */
native rl_som_input(RcSom:som, const Float:inputs[], num);

/**
 * The best matching neuron without training, -1 for none.
 */
native rl_som_bmu(RcSom:som, const Float:inputs[], num);

/**
 * @return          The number of weights written (0: no such neuron).
 */
native rl_som_weights(RcSom:som, neuron, Float:out[], len);
native rl_som_epochs(RcSom:som);

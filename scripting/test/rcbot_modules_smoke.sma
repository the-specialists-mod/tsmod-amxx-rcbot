// rcbot_modules_smoke: calls every native of rcbot_megahal, rcbot_learn and rcbot_nav once, so
// `cargo xtask pawn` proves the includes compile (and, loaded on a server, `rcbot_smoke` shows
// the modules answer). Writes learned files only below tsmod/rcbot/smoke/; the waypoints it edits
// in memory are reloaded from the map's file at the end, never saved.
// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions.

#include <amxmodx>
#include <rcbot_megahal>
#include <rcbot_learn>
#include <rcbot_nav>

// Never set: the savers are compiled, not run.
new bool:smoke_never = false;

public plugin_init()
{
	register_plugin("RCBot modules smoke", "0.1.0", "tsmod-amxx-rcbot");
	register_srvcmd("rcbot_smoke", "cmd_smoke");
}

public cmd_smoke()
{
	smoke_megahal();
	smoke_learn();
	smoke_objects();
	smoke_nav();
	server_print("[rcbot_smoke] DONE");
	return PLUGIN_HANDLED;
}

smoke_megahal()
{
	new out[128], message[128];
	hal_seed(1);
	new RcHal:hal = hal_load(0);
	hal_learn(hal, "the specialists is a fun game");
	hal_reply(hal, "what game?", out, charsmax(out));
	copy(message, charsmax(message), "Hey Bot, fun game");
	hal_bot_reply(hal, message, "Bot", "Player", HAL_CHATTING, 5, 2, 33, out, charsmax(out));
	server_print("[rcbot_smoke] hal words %d profile %d saved %d", hal_words(hal), hal_profile(hal), hal_save(hal));
	hal_free(hal);

	new bool:team_only;
	new RcChat:chat = chat_load();
	if (chat)
	{
		server_print("[rcbot_smoke] chat greetings %d", chat_count(chat, RcChat_Greetings, chat_rep_array(9)));
		chat_get(chat, RcChat_Idle, 0, 0, out, charsmax(out));
		chat_context("Player", "ts_dojo");
		chat_context_player("Player", 7, true);
		if (chat_pick(chat, RcChat_Idle, 0, out, charsmax(out), team_only))
		{
			chat_humanize(out, charsmax(out));
		}
		chat_free(chat);
	}
	chat_remove_name_tags("[TS] Player", out, charsmax(out));
	server_print("[rcbot_smoke] typing %.2f", chat_typing_time(out));
}

smoke_learn()
{
	new ids[3], key[64], owned[3] = {13, 12, 38}, attacks[5];
	new Float:genes[32], Float:out, Float:profile[RL_OPP_FIELDS];
	new Float:inputs[5] = {0.5, 1.0, -0.2, 1.0, 1.0};

	rl_seed(1);
	server_print("[rcbot_smoke] globals read %d", rl_load_globals());
	rl_opp_new_map();

	new RcLearn:bot = rl_bot_load(0, "smoke");
	rl_bot_key(bot, key, charsmax(key));
	rl_bot_on_spawn(bot, 0);
	rl_bot_select_ts_weapons(bot, false, ids);
	rl_bot_decide(bot, RlDecide_Strafe, inputs, sizeof(inputs), out);
	rl_bot_gene(bot, RlGenes_Personal, 0);
	rl_bot_genes(bot, RlGenes_Survival, genes, sizeof(genes));
	rl_bot_on_death(bot, 0, rl_death_fitness(30.0, 1, 0), false);

	rl_ws_add_exposure(bot, 13, 500.0, 0.25);
	rl_ws_add_hit(bot, 13, 500.0, 30.0);
	rl_ws_add_shots(bot, 13, 500.0, 3.0);
	rl_ws_score(bot, 13, 500.0, out);
	rl_ws_samples(bot, 13, 500.0);
	rl_ws_accuracy(bot, 13, 500.0, out);

	new arm = rl_loadout_choose(bot, 60, false);
	rl_loadout_weapons(arm, ids, sizeof(ids));
	rl_loadout_slots(arm);
	rl_weapon_slots(12);
	server_print("[rcbot_smoke] credited %d", rl_credited_loadout(arm, owned, sizeof(owned)));
	rl_loadout_reward(bot, arm, rl_life_reward(1, 1, 100.0, 60.0));
	server_print("[rcbot_smoke] loadouts %d key %s", rl_loadout_count(), key);

	rl_opp_set_key(1, RlKey_Player, 1);
	rl_opp_hit_on_bot(1, 13, 500.0, 30.0, false);
	rl_opp_kill_of_bot(1);
	rl_opp_death_to_bot(1);
	rl_opp_exposure(1, 0.25, 0.1);
	rl_opp_stunt(1, get_gametime());
	rl_opp_kungfu(1, attacks, sizeof(attacks), 0);
	rl_opp_profile(1, profile, sizeof(profile));
	server_print("[rcbot_smoke] threat %.2f", rl_opp_threat(1));

	new Float:now = get_gametime();
	rl_ledger_engage(bot, now, 1);
	rl_ledger_record(bot, now, 1, 13, 30.0, 0.0, false, false, 500.0, -1);
	rl_ledger_tick(bot, now);
	rl_ledger_credit(bot, now, -1, 2.0);
	rl_ledger_dealt_since(bot, now);
	rl_ledger_taken_since(bot, now);
	rl_ledger_reward_since(bot, now, 1);
	rl_ledger_kills_since(bot, now);
	rl_ledger_close(bot);
	rl_ledger_clear(bot);
	rl_range_bin(500.0);

	// The globals and this bot stay in memory: a smoke run must not change learned files.
	if (smoke_never)
	{
		rl_bot_save(bot);
		rl_save_globals();
	}
	rl_bot_free(bot);
}

smoke_objects()
{
	new Float:genes[3] = {0.1, 0.2, 0.3}, Float:got[64], Float:fitness;

	new RcGa:ga = rl_ga_create(2);
	rl_ga_add(ga, genes, sizeof(genes), 1.0);
	rl_ga_add(ga, genes, sizeof(genes), 2.0);
	if (rl_ga_can_pick(ga))
	{
		rl_ga_pick(ga, got, sizeof(got), fitness);
	}
	rl_ga_best(ga, got, sizeof(got), fitness);
	server_print("[rcbot_smoke] ga generations %d population %d", rl_ga_generations(ga), rl_ga_population(ga));
	rl_ga_save(ga, "smoke/ga.rga");
	rl_ga_load(ga, "smoke/ga.rga", sizeof(genes));
	rl_ga_free(ga);

	new RcPerceptron:p = rl_perceptron_create(3);
	rl_perceptron_execute(p, genes, sizeof(genes));
	rl_perceptron_fired(p);
	rl_perceptron_output(p);
	rl_perceptron_train(p, 1.0);
	rl_perceptron_set_weights(p, got, rl_perceptron_weights(p, got, sizeof(got)));
	rl_perceptron_randomize(p);
	rl_perceptron_save(p, "smoke/p.rpc");
	rl_perceptron_load(p, "smoke/p.rpc");
	rl_perceptron_free(p);

	new RcNn:nn = rl_nn_create(1, 3, 4, 2);
	rl_nn_execute(nn, genes, sizeof(genes), got, sizeof(got));
	rl_nn_train(nn, genes, 2);
	rl_nn_set_weights(nn, got, rl_nn_weights(nn, got, sizeof(got)));
	rl_nn_randomize(nn);
	rl_nn_save(nn, "smoke/nn.rnn");
	rl_nn_load(nn, "smoke/nn.rnn");
	rl_nn_free(nn);

	new RcSom:som = rl_som_create(3, 3, 3);
	new bmu = rl_som_input(som, genes, sizeof(genes));
	rl_som_bmu(som, genes, sizeof(genes));
	rl_som_weights(som, bmu, got, sizeof(got));
	server_print("[rcbot_smoke] som epochs %d", rl_som_epochs(som));
	rl_som_free(som);
}

smoke_nav()
{
	new map[64], out[64], text[64], params[7];
	new Float:origin[3] = {0.0, 0.0, 0.0}, Float:there[3] = {200.0, 0.0, 0.0}, Float:got[3], Float:end[3];
	new Float:weights[4], ignore[1] = {-1};
	nav_seed(1);
	new bool:loaded = nav_load();
	nav_map(map, charsmax(map));
	server_print("[rcbot_smoke] nav %s loaded %d count %d pending %d", map, loaded, nav_count(), nav_vis_pending());
	nav_vis_budget(nav_vis_budget(1024));
	nav_heatmap_load();

	// A few waypoints of its own, in memory only.
	new a = nav_add(origin, 0, 0, false, true, false);
	new b = nav_add(there, NAV_FL_CROUCH, 0, false, true, false);
	new c = nav_add_aiming(origin, Float:{0.0, 90.0, 0.0});
	nav_add_path(a, b);
	nav_remove_path(a, b);
	nav_add_path(a, b);
	nav_add_path(b, a);
	nav_remove_paths_to(c);
	nav_set_flags(b, NAV_FL_CROUCH);
	nav_add_flags(b, NAV_FL_DOOR);
	nav_clear_flags(b, NAV_FL_DOOR);
	nav_set_origin(b, there);
	nav_valid(a);
	nav_origin(a, got);
	nav_flags(b);
	nav_distance(b, origin);
	nav_nearest(origin);
	nav_nearest(origin, 400.0, a, NAV_NEAREST_VISIBLE | NAV_NEAREST_BOT, ignore, 0);
	nav_nearest_scan(origin, origin);
	nav_nearest_flag(origin, NAV_FL_CROUCH);
	nav_random_flag();
	nav_random_flag_near(origin, 500.0);
	nav_nearest_aiming(origin);
	nav_find_reachable(origin, Float:{0.0, 0.0, 17.0});
	nav_reachable(origin, there);
	nav_in_pvs(1, origin);
	nav_paths_out(a, out, sizeof(out));
	nav_paths_in(a, out, sizeof(out));
	nav_has_path(a, b);
	nav_cover(origin, there);
	nav_buckets_around(origin, out, sizeof(out));
	nav_nearby(origin, out, sizeof(out));
	nav_visible(a, b);
	nav_vis_rebuild(false);
	nav_vis_finish();
	nav_can_stand(origin, got);

	nav_heatmap_add_death(a, 1);
	nav_heatmap_add_kill(b, 1);
	nav_heatmap_add_damage(a, 1, 25.0);
	nav_heatmap_danger(a, 0);
	nav_heatmap_cover_penalty(a, 0);
	nav_heatmap_ambush(b);

	new RcPath:path = nav_path_create();
	new RcPath:other = nav_path_create();
	nav_path_set_weights(path);
	nav_path_set_threats(path, out, weights, 0);
	nav_path_add_threat(path, there, 1.0);
	nav_path_threats(path, out, weights, sizeof(weights));
	nav_path_clear_threats(path);
	nav_path_set_crowded(path, out, 0);
	new RcPath:others[1];
	others[0] = other;
	nav_path_crowd_from(path, others, sizeof(others));
	new NavPathStatus:st = nav_path_find(path, a, b, origin, -1, NAV_PATH_REVS, NAV_FIND_TACTICS | NAV_FIND_BACK_OFF);
	while (st == NAV_PATH_SEARCHING)
	{
		st = nav_path_continue(path);
	}
	nav_path_find(path, a, b, origin, -1, 1, NAV_FIND_UPSTREAM, true);
	server_print("[rcbot_smoke] path status %d next %d length %d", nav_path_status(path), nav_path_next(path), nav_path_length(path));
	nav_path_push(path, a);
	nav_path_get(path, out, sizeof(out));
	nav_path_contains(path, b);
	nav_path_after(path, a);
	nav_path_pop(path);
	nav_path_clear(path);
	nav_path_fail(path, a, b);
	nav_path_failed_count(path);
	nav_path_clear_failed_paths(path);
	nav_path_goal_reached(path, b);
	nav_path_failed_goals(path, out, sizeof(out));
	nav_path_ignored_goals(path, out, sizeof(out));
	nav_path_clear_failed_goals(path);
	nav_path_think(path);
	nav_path_touch(path, a);
	nav_path_since_visit(path, a);
	nav_path_roam_goal(path, -1, origin);
	nav_path_roam_goal(path, -1, origin, out, 0);
	nav_draw_path(path);
	nav_path_reset(path);
	nav_path_free(other);
	nav_path_free(path);

	nav_edit_create_path(1, origin, 1);
	nav_edit_create_path(1, there, 2);
	nav_edit_remove_path(1, origin, 1);
	nav_edit_remove_path(1, there, 2);
	nav_auto_waypoint(1, false);
	nav_auto_reset(1);
	nav_draw_index(a);
	nav_draw_around(1, origin);
	server_print("[rcbot_smoke] nav events %d", nav_event_count());
	while (nav_event_pop(got, end, params, text, charsmax(text)) != NAV_EVENT_NONE)
	{
	}
	nav_event_clear();
	nav_delete(c);
	nav_delete_near(there);

	if (smoke_never)
	{
		nav_save();
		nav_heatmap_save();
	}
	nav_clear();
	// The map's own waypoints back, as the bot had them.
	nav_load();
}

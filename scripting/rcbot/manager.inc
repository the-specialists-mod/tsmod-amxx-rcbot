// rcbot/manager.inc: the bots in the game and the frame loop.
// Owner: core. Ports host lib.rs start_frame (lib.rs:489-578: refresh, masters, editor, grips,
// map config, rejoin, every bot thinks, then every bot acts: actions, then run_cmd and
// command_ran), bot/manager.rs BotManager (frame, death, combat_event, sound, touch,
// item_pickup, weapon_fired, make_squad, say) and bot/think.rs Bot::frame / teleported /
// command_ran.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// "Think all, then act" (lib.rs:508): every bot thinks before any usercmd runs, so all of them
// read the same frame (world.inc). A bot's think only writes its own state, its usercmd
// (g_co_* CO_CMD_*) and its action queue (g_act_*); manager_act_all hands them to the game.

#if defined _rcbot_manager_included
	#endinput
#endif
#define _rcbot_manager_included

// ---- the frame (lib.rs start_frame) -------------------------------------------------------------

// ts_frame: StartFrame for the bots.
stock manager_frame()
{
	// The frame's players and rules, once for every bot and forward (world.rs refresh_frame);
	// m_iTeamScores and the masters (read once a map) come with it.
	world_refresh_frame();
	navigate_editor_frame();
	commands_force_grip_frame();
	config_map_config_frame();
	if (!g_active)
	{
		return;
	}
	backend_rejoin_frame();
	manager_think_all();
	squad_flush_messages();
	botcam_frame();
	manager_act_all();
	report_frame();
}

// BotManager::frame: each bot in index order may leave (BOTS_LEAVE_AND_JOIN: it says goodbye
// and is kicked 5-8 s later), then kicks, suicides, or thinks and moves. Then the squads follow
// their leaders' headings.
stock manager_think_all()
{
	new Float:now = g_now;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (!g_co_i[id][CO_USED])
		{
			continue;
		}
		squad_apply_orders();
		botcam_set_other_has_enemy(id);
		if (g_th_i[id][TH_CONDITIONS] & COND_WANT_TO_LEAVE_GAME)
		{
			if (g_ev_f[id][EV_LEAVE_TIME] == 0.0)
			{
				g_ev_f[id][EV_LEAVE_TIME] = now + random_float(5.0, 8.0);
				events_leaving(id);
			}
			else if (g_ev_f[id][EV_LEAVE_TIME] < now)
			{
				g_co_i[id][CO_KICK] = 1;
				g_ev_f[id][EV_LEAVE_TIME] = 0.0;
				g_th_i[id][TH_CONDITIONS] &= ~COND_WANT_TO_LEAVE_GAME;
			}
		}
		manager_bot_frame(id);
	}
	squad_apply_orders();
	squad_update_angles();
}

// Bot::is_alive: the entity alive (deadflag, health) and the game's alive flag.
stock bool:bot_alive(id)
{
	return world_alive(id) && world_health(id) > 0.0;
}

// think.rs Bot::frame: one server frame for this bot: the StartFrame loop's kick / kill / think,
// then RunPlayerMove. Without its entity in the frame nothing happens.
stock manager_bot_frame(id)
{
	g_co_i[id][CO_HAS_CMD] = 0;
	if (world_userid(id) == 0)
	{
		return;
	}
	new bool:was_alive = g_co_i[id][CO_PREV_ALIVE] != 0;
	new bool:alive = bot_alive(id);
	// The server sets v_angle from our usercmd (TSBOT_SETVIEW), unless the game turned the
	// player (spawn, teleport: fixangle); then its view is ours (design §4).
	if (g_co_i[id][CO_GAME_VIEW_SET])
	{
		g_co_i[id][CO_GAME_VIEW_SET] = 0;
		vec_copy(g_co_v[id][CO_GAME_VIEW], g_co_v[id][CO_VANGLE]);
	}
	else
	{
		world_vangle(id, g_co_v[id][CO_VANGLE]);
	}
	new Float:origin[3];
	world_origin(id, origin);
	if (was_alive && alive && vec_distance(origin, g_co_v[id][CO_PREV_ORIGIN]) > TELEPORT_DISTANCE)
	{
		bot_teleported(id);
	}
	g_co_i[id][CO_PREV_ALIVE] = alive;
	vec_copy(origin, g_co_v[id][CO_PREV_ORIGIN]);
	g_lk_f[id][LK_BASE_TURN_SPEED] = g_cfg_f[CFG_TURN_SPEED];
	g_co_f[id][CO_NOW] = g_now;

	// BotClient_TS_WeaponInfo: the weapon in hand, the inventory when it changed.
	if (world_alive(id))
	{
		botweapons_frame_sync(id);
	}
	// TS bots always have kung fu.
	botweapons_ensure_kungfu(id);

	if (g_co_i[id][CO_KICK])
	{
		g_co_i[id][CO_KICK] = 0;
		bot_action_kick(id);
		return;
	}
	if (alive && g_co_i[id][CO_KILL])
	{
		g_co_i[id][CO_KILL] = 0;
		bot_action_kill(id);
		return;
	}
	think_bot(id);
	movement_run_player_move(id);
	g_co_i[id][CO_HAS_CMD] = 1;
}

// The rest of lib.rs start_frame: each bot's actions, in order, then its usercmd.
stock manager_act_all()
{
	new Float:vang[3], events;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (!g_co_i[id][CO_USED])
		{
			continue;
		}
		new n = g_co_i[id][CO_ACT_COUNT];
		g_co_i[id][CO_ACT_COUNT] = 0;
		for (new k = 0; k < n; k++)
		{
			manager_run_action(id, k);
		}
		if (!g_co_i[id][CO_USED] || !g_co_i[id][CO_HAS_CMD])
		{
			continue;
		}
		// The server turns the bot (SETVIEW) and runs the frame's time (AUTOMSEC); design §4.
		new r = ts_bot_runcmd(id, g_co_v[id][CO_CMD_ANGLES], g_co_f[id][CO_CMD_FWD], g_co_f[id][CO_CMD_SIDE], g_co_f[id][CO_CMD_UP],
			g_co_i[id][CO_CMD_BUTTONS], g_co_i[id][CO_CMD_IMPULSE], TSBOT_SETVIEW | TSBOT_AUTOMSEC, 0, vang, events);
		report_runcmd(id, r);
		if (r == TSBOT_OK)
		{
			g_co_i[id][CO_LAST_EVENTS] = events;
			bot_command_ran(id, events, vang);
		}
	}
}

stock manager_run_action(id, k)
{
	switch (g_act_kind[id][k])
	{
		case ACT_COMMAND:
		{
			ts_bot_command(id, g_act_text[id][k]);
		}
		case ACT_SELECT_WEAPON:
		{
			new wid = g_act_arg[id][k];
			new r = ts_bot_select_weapon(id, wid);
			if (r == TSBOT_INVALID)
			{
				// Not owned: the bot forgets it and reads its inventory again.
				botweapons_select_failed(id, wid);
			}
		}
		case ACT_LOADOUT:
		{
			ts_bot_clear_loadout(id);
			new n = g_co_i[id][CO_LOADOUT_COUNT];
			for (new j = 0; j < n; j++)
			{
				new wid = g_loadout_ids[id][j];
				if (wid >= 1 && wid <= 37 && wid != 29)
				{
					ts_bot_buy(id, wid, 0, 0, 4);
				}
			}
		}
		case ACT_KILL:
		{
			ts_bot_command(id, "kill");
		}
		case ACT_KICK:
		{
			ts_bot_kick(id);
		}
	}
}

// ---- actions (Bot::actions) ----------------------------------------------------------------------

static manager_push_action(id, kind)
{
	new n = g_co_i[id][CO_ACT_COUNT];
	if (n >= RC_MAX_ACTIONS)
	{
		return -1;
	}
	g_co_i[id][CO_ACT_COUNT] = n + 1;
	g_act_kind[id][n] = kind;
	g_act_arg[id][n] = 0;
	return n;
}

// Action::Command: FakeClientCommand ("respawn", "say ...", "drop", "usepowerup").
stock bot_action_command(id, const line[])
{
	new n = manager_push_action(id, ACT_COMMAND);
	if (n >= 0)
	{
		copy(g_act_text[id][n], charsmax(g_act_text[][]), line);
	}
}

// Action::SelectWeapon: wid is the game's id (0 kung fu; ts_weapon_to_game_id).
stock bot_action_select_weapon(id, wid)
{
	new n = manager_push_action(id, ACT_SELECT_WEAPON);
	if (n >= 0)
	{
		g_act_arg[id][n] = wid;
	}
}

// Action::Loadout: the weapons the next spawn buys (RCBot ids; 38/29 and others skipped).
stock bot_action_loadout(id, const ids[], num)
{
	if (manager_push_action(id, ACT_LOADOUT) < 0)
	{
		return;
	}
	if (num > RC_MAX_LOADOUT)
	{
		num = RC_MAX_LOADOUT;
	}
	for (new k = 0; k < num; k++)
	{
		g_loadout_ids[id][k] = ids[k];
	}
	g_co_i[id][CO_LOADOUT_COUNT] = num;
}

// Action::Kill (ClientKill).
stock bot_action_kill(id)
{
	manager_push_action(id, ACT_KILL);
}

// Action::Kick.
stock bot_action_kick(id)
{
	manager_push_action(id, ACT_KICK);
}

// ---- what the game did with the usercmd (think.rs) -----------------------------------------------

// Bot::command_ran: a view the game set becomes the bot's; a teleport drops the path.
stock bot_command_ran(id, events, const Float:vang[3])
{
	if (events & TSBOT_EV_FIXANGLE)
	{
		g_co_i[id][CO_GAME_VIEW_SET] = 1;
		vec_copy(vang, g_co_v[id][CO_GAME_VIEW]);
		vec_copy(vang, g_co_v[id][CO_VANGLE]);
		g_lk_f[id][LK_IDEAL_YAW] = vang[1];
		g_lk_f[id][LK_IDEAL_PITCH] = vang[0];
	}
	if (events & TSBOT_EV_TELEPORTED)
	{
		bot_teleported(id);
	}
}

// Bot::teleported: the waypoints, path and stuck timers belong to where the bot was.
stock bot_teleported(id)
{
	navigate_teleported(id);
	movement_teleported(id);
	look_teleported(id);
}

// ---- events from the game (BotManager) ------------------------------------------------------------

// BotManager::death (BotClient_Generic_DeathMessage): killer -1 none, 0 the world.
stock manager_death(killer, victim)
{
	// A dead player isn't where it was last seen any more (design §4).
	if (victim >= 1 && victim <= MAX_PLAYERS)
	{
		new vserial = world_userid(victim);
		for (new b = 1; b <= MAX_PLAYERS; b++)
		{
			if (g_co_i[b][CO_USED])
			{
				remember_forget(b, victim, vserial);
			}
		}
		board_died(victim, vserial);
	}
	if (killer == victim || killer == -1)
	{
		if (is_rcbot(victim))
		{
			events_kill_self(victim);
		}
		return;
	}
	if (victim <= 0 || killer < 0 || killer > MAX_PLAYERS || victim > MAX_PLAYERS)
	{
		return;
	}
	if (is_rcbot(killer))
	{
		events_kill(killer, victim);
	}
	if (is_rcbot(victim))
	{
		events_died(victim, killer);
	}
	new bool:v_team = killer != 0 && world_team(victim) != 0;
	new bool:k_team = killer != 0 && world_team(killer) != 0;
	static not_handled[RC_SLOTS];
	new nh = 0;
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (!g_co_i[b][CO_USED] || b == killer || b == victim)
		{
			continue;
		}
		if (combat_is_visible(b, victim))
		{
			if (v_team && !combat_is_enemy(b, victim))
			{
				events_see_teammate_die(b, victim, killer);
			}
			else
			{
				events_see_player_die(b, victim, killer);
			}
			continue;
		}
		if (killer != 0 && combat_is_visible(b, killer))
		{
			if (k_team)
			{
				if (!combat_is_enemy(b, killer))
				{
					events_see_teammate_kill(b);
					continue;
				}
				else if (!combat_is_enemy(b, victim))
				{
					events_see_enemy_kill(b, killer, victim);
					continue;
				}
			}
			else
			{
				events_see_player_die(b, killer, victim);
				continue;
			}
		}
		not_handled[nh++] = b;
	}
	new Float:vo[3], Float:eyes[3];
	world_player_center(victim, vo);
	for (new k = 0; k < nh; k++)
	{
		new b = not_handled[k];
		world_eyes(b, eyes);
		if (!combat_is_enemy(b, victim) && vec_distance(eyes, vo) < BOT_HEAR_DISTANCE && combat_fvisible_point(b, vo))
		{
			events_hear_teammate_die(b, victim);
		}
	}
}

// BotManager::combat_event for a damage (the port's damage event, design §4): the hurt bot turns
// to where it came from; the learners record it.
stock manager_damage(victim, attacker, inflictor, Float:damage, hitgroup, wid, const Float:dir[3])
{
	if (attacker > 0 && attacker != victim && is_rcbot(victim))
	{
		combat_hurt_by(victim, attacker, dir);
	}
	learning_damage_event(victim, attacker, inflictor, damage, hitgroup, wid);
}

// BotManager::sound (pfnEmitSound's loop): every bot without an enemy within BOT_HEAR_DISTANCE
// of the sound (not the entity or its owner) hears it. Kinds no bot reacts to stop here.
stock manager_sound(ent, const Float:origin[3], const sample[], Float:vol)
{
	new kind = hearing_sound_kind(sample);
	if (kind == SOUND_UNKNOWN || vol <= 0.0)
	{
		return;
	}
	new owner = ent > 0 && pev_valid(ent) ? pev(ent, pev_owner) : 0;
	new Float:eyes[3];
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (!g_co_i[b][CO_USED] || b == ent || b == owner || g_cb_i[b][CB_ENEMY] != 0)
		{
			continue;
		}
		world_eyes(b, eyes);
		if (vec_distance(eyes, origin) < BOT_HEAR_DISTANCE)
		{
			hearing_hear_sound(b, kind, ent, origin);
		}
	}
}

// BotManager::touch (DispatchTouch): a living bot touched other.
stock manager_touch(bot, other)
{
	if (is_rcbot(bot) && bot_alive(bot))
	{
		special_touch(bot, other);
	}
}

// BotManager::item_pickup: player took ent (Bot::item_taken for every bot).
stock manager_item_pickup(player, ent)
{
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED])
		{
			items_taken(b, player, ent);
		}
	}
}

// BotManager::weapon_fired: every bot that can hear it remembers the shot (Bot::hear_gunfire).
stock manager_weapon_fired(shooter, const Float:origin[3], bool:silenced)
{
	if (shooter < 1 || shooter > MAX_PLAYERS || !bot_alive(shooter))
	{
		return;
	}
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED])
		{
			hearing_hear_gunfire(b, shooter, origin, silenced);
		}
	}
}

// BotManager::make_squad (BotFunc_MakeSquad): every bot within 512 units of the leader that can
// joins their squad.
stock manager_make_squad(leader)
{
	new Float:lo[3], Float:eyes[3];
	world_player_center(leader, lo);
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (!g_co_i[b][CO_USED])
		{
			continue;
		}
		world_eyes(b, eyes);
		if (vec_distance(eyes, lo) > 512.0)
		{
			continue;
		}
		if (squad_can_add(b, leader))
		{
			squad_add(b, leader, is_rcbot(leader));
		}
	}
	squad_apply_orders();
}

// BotManager::say (ClientCommand's say/say_team): every bot (the sender's team for say_team)
// may reply, all reading the same buffer.
stock manager_say(sender, message[], bool:team_only)
{
	new team = world_team(sender);
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED] && (!team_only || team == world_team(b)))
		{
			chat_reply_to_message(b, message, sender, is_rcbot(sender), team_only);
		}
	}
}

// Every subsystem's CBot::Init part for a new bot (after state_clear_bot and the profile).
stock manager_bot_init(id)
{
	tasks_flush(id);
	botweapons_bot_init(id);
	kungfu_bot_init(id);
	items_bot_init(id);
	remember_bot_init(id);
	hearing_bot_init(id);
	preaim_bot_init(id);
	navigate_bot_init(id);
	combat_bot_init(id);
}

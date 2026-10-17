// rcbot/special.inc: lifts, buttons, pushables, human towers, touches, special waypoint tasks.
// Owner: think. Ports bot/special.rs (MasterEntity is world.inc's g_master_*; is_button, best_pushable_distance, desired_pushable_vector, acceptable_pushable_vector, find_nearest_entity, player_near_vector, master_find_button, find_nearest_button, Bot::player_standing_on_me, standing_on_player, check_lift, touched_pushable_waypoint, next_waypoint_tasks, touched_human_tower, touch, task_push_pushable, task_wait_for_lift, task_wait_for_entity, task_human_tower, task_wait_for_bot_at_wpt, task_face_edict, task_throw_grenade, OtherBot).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Classnames and target names are read here (strings), but only on events: a touch, a special
// waypoint reached, a lift task running. The tasks run on the current task (slot 0).
//
// Status: ported.

#if defined _rcbot_special_included
	#endinput
#endif
#define _rcbot_special_included

#define SPECIAL_MAX_FOUND		64
// TS's M61 grenade (TsWeapon::M61Grenade).
#define SPECIAL_M61				24

// special.rs is_button (UTIL_IsButton): a brush entity (func_*).
static stock bool:special_is_button(ent)
{
	new cls[32];
	think_classname(ent, cls, charsmax(cls));
	return equal(cls, "func_", 5) != 0;
}

// special.rs best_pushable_distance (UTIL_GetBestPushableDistance).
static stock Float:special_best_pushable_distance(p)
{
	new Float:size[3];
	pev(p, pev_size, size);
	return (size[0] + size[1]) / 3.0;
}

// special.rs desired_pushable_vector (UTIL_GetDesiredPushableVector): where to stand to push p
// towards origin.
static stock special_desired_pushable_vector(const Float:origin[3], p, Float:out[3])
{
	new Float:v[3], Float:d[3];
	world_entity_origin(p, v);
	vec_sub(origin, v, d);
	vec_normalize(d, d);
	vec_mad(v, d, -special_best_pushable_distance(p), out);
}

// special.rs acceptable_pushable_vector (UTIL_AcceptablePushableVector): p is near enough origin
// (2D, from its origin).
static stock bool:special_acceptable_pushable_vector(p, const Float:origin[3])
{
	new Float:o[3];
	pev(p, pev_origin, o);
	return vec_distance_2d(o, origin) <= special_best_pushable_distance(p);
}

// special.rs find_nearest_entity (UTIL_FindNearestEntity): the nearest entity of those classes
// within range, with a clear line to it when visible (candidates traced nearest first); 0 none.
stock special_find_nearest_entity(const classnames[][], num, const Float:origin[3], Float:range, bool:visible)
{
	static found[SPECIAL_MAX_FOUND];
	static Float:dists[SPECIAL_MAX_FOUND];
	new n = 0;
	new Float:o[3];
	for (new c = 0; c < num; c++)
	{
		new e = 0;
		while ((e = find_ent_by_class(e, classnames[c])) > 0)
		{
			if (!world_entity_origin(e, o))
			{
				continue;
			}
			new Float:d = vec_distance(o, origin);
			if (d < range && n < SPECIAL_MAX_FOUND)
			{
				// Insert sorted, after equal distances (a stable sort of the search order).
				new at = n;
				while (at > 0 && dists[at - 1] > d)
				{
					found[at] = found[at - 1];
					dists[at] = dists[at - 1];
					at--;
				}
				found[at] = e;
				dists[at] = d;
				n++;
			}
		}
	}
	new Float:end[3], hit;
	for (new k = 0; k < n; k++)
	{
		if (!visible)
		{
			return found[k];
		}
		world_entity_origin(found[k], o);
		if (world_trace_line(origin, o, true, true, -1, end, hit) >= 1.0 || hit == found[k])
		{
			return found[k];
		}
	}
	return 0;
}

// special.rs player_near_vector (PlayerNearVector): the nearest living, named player within
// range of origin; 0 none.
stock special_player_near_vector(const Float:origin[3], Float:range)
{
	new nearest = 0;
	new Float:o[3];
	for (new p = 1; p <= g_maxclients; p++)
	{
		if (!world_userid(p) || !bot_alive(p))
		{
			continue;
		}
		world_player_center(p, o);
		new Float:d = vec_distance(o, origin);
		if (d <= range)
		{
			range = d;
			nearest = p;
		}
	}
	return nearest;
}

// special.rs master_find_button's consider: the nearer of the visible or the hidden buttons.
static stock special_consider_button(b, const Float:origin[3], &vis, &Float:vis_d, &hid, &Float:hid_d)
{
	new Float:o[3], Float:end[3], hit;
	if (!world_entity_origin(b, o))
	{
		return;
	}
	new Float:d = vec_distance(origin, o);
	if (world_trace_line(origin, o, true, false, -1, end, hit) >= 1.0)
	{
		if (!vis || d < vis_d)
		{
			vis = b;
			vis_d = d;
		}
	}
	else if (!hid || d < hid_d)
	{
		hid = b;
		hid_d = d;
	}
}

// special.rs master_find_button (CMasterEntity::FindButton): the nearest visible button targeting
// the master (or, for a relay, the first entity targeting the relay), else the nearest hidden
// one. Upstream's relay loop only looks at the first entity targeting the relay; kept.
static stock special_master_find_button(const master[], const Float:origin[3])
{
	new vis = 0, hid = 0, Float:vis_d = 0.0, Float:hid_d = 0.0;
	new b = 0, name[64];
	while ((b = find_ent_by_target(b, master)) > 0)
	{
		if (special_is_button(b))
		{
			special_consider_button(b, origin, vis, vis_d, hid, hid_d);
			continue;
		}
		pev(b, pev_targetname, name, charsmax(name));
		new b2 = find_ent_by_target(0, name);
		if (b2 > 0 && special_is_button(b2))
		{
			special_consider_button(b2, origin, vis, vis_d, hid, hid_d);
		}
	}
	return vis ? vis : hid;
}

// special.rs find_nearest_button (BotFunc_FindNearestButton): the master's button, else the
// nearest entity targeting the door that can be seen at its middle or either corner (triggers
// always count); 0 unless it is a button. press_at: where to go to reach it.
stock special_find_nearest_button(const Float:origin[3], door, Float:press_at[3])
{
	vec_zero(press_at);
	for (new m = 0; m < g_master_count; m++)
	{
		if (g_master_ent[m] != door)
		{
			continue;
		}
		new b = special_master_find_button(g_master_name[m], origin);
		if (b && world_entity_origin(b, press_at))
		{
			return b;
		}
		break;
	}
	new tname[64];
	pev(door, pev_targetname, tname, charsmax(tname));
	new best = 0, Float:best_d = 0.0, Float:best_at[3];
	if (tname[0])
	{
		new t = 0;
		new Float:o[3], Float:size[3], Float:target[3], Float:end[3], hit;
		while ((t = find_ent_by_target(t, tname)) > 0)
		{
			if (!world_entity_origin(t, o))
			{
				continue;
			}
			new Float:d = vec_distance(o, origin);
			if (best && d >= best_d)
			{
				continue;
			}
			if (pev(t, pev_solid) != RC_SOLID_TRIGGER)
			{
				pev(t, pev_size, size);
				new bool:found = false;
				for (new c = 0; c < 3 && !found; c++)
				{
					if (c == 0)
					{
						vec_copy(o, target);
					}
					else if (c == 1)
					{
						vec_add(o, size, target);
					}
					else
					{
						vec_sub(o, size, target);
					}
					new Float:f = world_trace_line(origin, target, true, false, -1, end, hit);
					found = hit == t || f >= 1.0;
				}
				if (found)
				{
					best = t;
					best_d = d;
					vec_copy(target, best_at);
					// Upstream stops at the first visible one.
					break;
				}
			}
			else
			{
				best = t;
				best_d = d;
				vec_copy(o, best_at);
			}
		}
	}
	if (best && special_is_button(best))
	{
		vec_copy(best_at, press_at);
		return best;
	}
	return 0;
}

// special.rs Bot::player_standing_on_me (PlayerStandingOnMe): a named player whose ground entity
// is the bot; 0 none.
stock special_player_standing_on_me(id)
{
	for (new p = 1; p <= g_maxclients; p++)
	{
		if (world_userid(p) && pev_valid(p) && pev(p, pev_groundentity) == id)
		{
			return p;
		}
	}
	return 0;
}

// special.rs Bot::standing_on_player (StandingOnPlayer): the client the bot stands on; 0 none.
stock special_standing_on_player(id)
{
	new g = pev(id, pev_groundentity);
	return g > 0 && pev_valid(g) && (pev(g, pev_flags) & FL_CLIENT) ? g : 0;
}

// special.rs Bot::check_lift (CheckLift): a point trace from the waypoint towards the next; if it
// hits a still door, plat or train with a name, plan to press its button (and, for a lift, wait
// for it at the lift waypoint). True while a USE_LIFT schedule runs.
stock bool:special_check_lift(id, const Float:from[3], const Float:to[3])
{
	new Float:dir[3], Float:end[3], Float:pos[3], hit;
	vec_sub(to, from, dir);
	vec_normalize(dir, dir);
	vec_mad(from, dir, 4096.0, end);
	new Float:f = world_trace_hull(from, end, true, 0, id, pos, hit);
	if (f < 1.0 && hit > 0 && pev_valid(hit))
	{
		new tname[64], cls[32];
		pev(hit, pev_targetname, tname, charsmax(tname));
		think_classname(hit, cls, charsmax(cls));
		if (tname[0] && (equal(cls, "func_seethroughdoor") || equal(cls, "func_door", 9) || equal(cls, "func_plat", 9) || equal(cls, "func_train", 10)))
		{
			new Float:movedir[3], Float:size[3], Float:my_size[3];
			pev(hit, pev_movedir, movedir);
			pev(hit, pev_size, size);
			pev(id, pev_size, my_size);
			new bool:is_lift = movedir[2] != 0.0 && size[0] > my_size[2] && size[1] > my_size[2];
			if (!think_entity_moving(hit))
			{
				special_plan_lift(id, hit, is_lift, size);
			}
		}
	}
	return tasks_has_schedule(id, SCHED_USE_LIFT);
}

// check_lift's plan for a still door, plat or train hit.
static stock special_plan_lift(id, hit, bool:is_lift, const Float:size[3])
{
	new Float:eye[3], Float:button_at[3], Float:gun[3], Float:at[3];
	world_eyes(id, eye);
	vec_copy(eye, gun);
	new sched = tasks_new_schedule_id(id);
	new hit_serial = think_handle_serial(hit);
	static failed[THINK_MAX_FAILED];
	new nf = think_failed_goals(id, failed, THINK_MAX_FAILED);
	new button = special_find_nearest_button(eye, hit, button_at);
	if (button)
	{
		new bool:normal_door = true;
		new bs = think_handle_serial(button);
		tasks_flush(id);
		if (g_nv_i[id][NV_PATH])
		{
			nav_path_clear_failed_paths(RcPath:g_nv_i[id][NV_PATH]);
		}
		if (is_lift)
		{
			new wait = nav_nearest_flag(gun, NAV_FL_LIFT, NAV_REACHABLE_RANGE, g_th_i[id][TH_TEAM], failed, nf);
			if (wait == -1)
			{
				wait = g_nv_i[id][NV_PREV_WPT];
			}
			if (wait != -1)
			{
				nav_origin(wait, at);
				new Float:other_at[3];
				new other = special_find_nearest_button(at, hit, other_at);
				if (other)
				{
					new os = think_handle_serial(other);
					tasks_add(id, TASK_FIND_PATH, sched, other, os, 0, -1.0, VEC_ZERO);
					tasks_add(id, TASK_USE, sched, other, os, 0, 1.0, VEC_ZERO);
				}
				else
				{
					tasks_add(id, TASK_FIND_PATH, sched, 0, 0, 0, 0.0, button_at);
					tasks_add(id, TASK_USE, sched, button, bs, 0, 1.0, VEC_ZERO);
				}
				tasks_add(id, TASK_FIND_PATH, sched, 0, 0, wait, -1.0, VEC_ZERO);
				tasks_add(id, TASK_WAIT_FOR_ENTITY, sched, hit, hit_serial, 0, 0.0, VEC_ZERO);
				tasks_set_time_to_complete_schedule(id, sched, random_float(10.0, 20.0));
				normal_door = false;
			}
		}
		if (normal_door)
		{
			tasks_add(id, TASK_FIND_PATH, sched, 0, 0, 0, 0.0, button_at);
			tasks_add(id, TASK_USE, sched, button, bs, 0, 1.0, VEC_ZERO);
		}
		new Float:goal[3];
		if (g_nv_i[id][NV_GOAL_ORIGIN_SET])
		{
			vec_copy(g_nv_v[id][NV_GOAL_ORIGIN], goal);
		}
		tasks_add(id, TASK_FIND_PATH, 0, 0, 0, 0, 0.0, goal);
	}
	else if (!tasks_has_schedule(id, SCHED_USE_LIFT))
	{
		// No button found: a lift button waypoint near the lift?
		new Float:range = vec_length_2d(size);
		new wpt = nav_nearest_flag(gun, NAV_FL_LIFT, range, -1, failed, nf);
		if (wpt != -1)
		{
			static const buttons[][] = { "func_button", "button_target", "func_rot_button" };
			nav_origin(wpt, at);
			new b = special_find_nearest_entity(buttons, sizeof(buttons), at, range, true);
			if (b)
			{
				new bs = think_handle_serial(b);
				new sid = tasks_new_schedule_id(id);
				tasks_add_priority(id, TASK_WAIT_FOR_ENTITY, sid, hit, hit_serial, 0, 0.0, VEC_ZERO);
				tasks_add_priority(id, TASK_FIND_PATH, sid, 0, 0, wpt, -1.0, VEC_ZERO);
				tasks_add_priority(id, TASK_USE, sid, b, bs, 0, 0.0, VEC_ZERO);
				tasks_add_priority(id, TASK_FIND_PATH, sid, b, bs, 0, 0.0, VEC_ZERO);
				tasks_give_schedule_description(id, sid, SCHED_USE_LIFT);
			}
		}
	}
}

// special.rs Bot::touched_pushable_waypoint: right after a waypoint was touched, a pushable to put
// in place.
stock special_touched_pushable_waypoint(id, const Float:wpt_origin[3])
{
	if (!(g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_PUSHABLE) || tasks_has(id, TASK_PUSH_PUSHABLE))
	{
		return;
	}
	static const pushables[][] = { "func_pushable" };
	new p = special_find_nearest_entity(pushables, 1, wpt_origin, 512.0, false);
	if (p && !special_acceptable_pushable_vector(p, wpt_origin))
	{
		new sched = tasks_new_schedule_id(id);
		tasks_add_priority(id, TASK_PUSH_PUSHABLE, sched, p, think_handle_serial(p), 0, 0.0, wpt_origin);
		new Float:push[3];
		special_desired_pushable_vector(wpt_origin, p, push);
		tasks_add_priority(id, TASK_FIND_PATH, sched, 0, 0, 0, 0.0, push);
	}
}

// special.rs Bot::next_waypoint_tasks: once the next waypoint is chosen, grenade throws, waits
// before jumps, lifts. Upstream asked for weapon 12 (TS's Desert Eagle) and threw at the waypoint
// (design §5.59): a TS bot with an M61 throws one from here at an enemy seen in the last 5 s
// 300-750 units away.
stock special_next_waypoint_tasks(id, prev_flags)
{
	new Float:now = g_now;
	new Float:wpt[3];
	nav_origin(g_nv_i[id][NV_CURRENT_WPT], wpt);
	new f = g_nv_i[id][NV_CURRENT_WPT_FLAGS];
	new Float:last = g_cb_f[id][CB_LAST_SEE_ENEMY_TIME];
	new Float:dist = think_distance_from(id, g_cb_v[id][CB_LAST_SEE_ENEMY_POS], false);
	if ((f & NAV_FL_GREN_THROW) && g_bw_weapon[id][SPECIAL_M61][BW_HAS_WEAPON] && g_bw_weapon[id][SPECIAL_M61][BW_CLIP] > 0
		&& last > 0.0 && last + 5.0 > now && dist >= 300.0 && dist < 750.0)
	{
		tasks_set_time_to_complete(id, tasks_add_priority(id, TASK_THROW_GRENADE, 0, 0, 0, 0, 0.0, g_cb_v[id][CB_LAST_SEE_ENEMY_POS]), 4.0);
	}
	else if ((pev(id, pev_flags) & FL_ONGROUND) && pev(id, pev_waterlevel) < 3
		&& (((f & NAV_FL_JUMP) && (prev_flags & NAV_FL_CROUCH)) || ((f & NAV_FL_JUMP) && (f & NAV_FL_STAY_NEAR))))
	{
		tasks_add_priority(id, TASK_WAIT_AND_FACE_VECTOR, 0, 0, 0, 0, 0.75, wpt);
	}
	new next = navigate_next_waypoint(id);
	new Float:next_at[3];
	if (next != -1)
	{
		nav_origin(next, next_at);
	}
	if (f & NAV_FL_WAIT_FOR_LIFT)
	{
		tasks_add_priority(id, TASK_WAIT_FOR_LIFT, 0, 0, 0, 0, 0.0, wpt);
	}
	else if ((f & (NAV_FL_JUMP | NAV_FL_CROUCHJUMP)) && next != -1 && (nav_flags(next) & NAV_FL_WAIT_FOR_LIFT))
	{
		tasks_add_priority(id, TASK_WAIT_FOR_LIFT, 0, 0, 0, 0, 0.0, next_at);
	}
	else if (!tasks_has_schedule(id, SCHED_USE_LIFT))
	{
		new bool:check_next = next != -1 && (f & NAV_FL_CHECK_LIFT);
		if (!(check_next && special_check_lift(id, wpt, next_at)))
		{
			new Float:prev[3];
			nav_origin(g_nv_i[id][NV_PREV_WPT], prev);
			special_check_lift(id, prev, wpt);
		}
	}
}

// special.rs Bot::touched_human_tower: high enough, then within 100 units if someone crouches
// there already, else within 24.
stock bool:special_touched_human_tower(id, const Float:wpt_origin[3], Float:distance)
{
	static near[64];
	new n = world_entities_in_sphere(wpt_origin, 24.0, near, sizeof(near));
	new bool:crouching = false;
	for (new k = 0; k < n && !crouching; k++)
	{
		new p = near[k];
		crouching = (pev(p, pev_flags) & FL_CLIENT) && p <= MAX_PLAYERS && world_userid(p)
			&& ((pev(p, pev_flags) & FL_DUCKING) || (pev(p, pev_button) & IN_DUCK));
	}
	new Float:me[3];
	world_origin(id, me);
	if (me[2] < wpt_origin[2] - 16.0)
	{
		return false;
	}
	return crouching ? distance <= 100.0 : distance <= 24.0;
}

// special.rs Bot::touch (CBot::Touch, TS): picking up the item it went for, doors and lifts in
// the way. Returns upstream's "don't let the game see the touch" (never true in TS).
stock bool:special_touch(id, other)
{
	new Float:now = g_now;
	new pickup = g_th_i[id][TH_PICKUP_ENT], ps = g_th_i[id][TH_PICKUP_SERIAL];
	if (pickup && think_same_handle(pickup, ps, other, think_handle_serial(other)))
	{
		items_touched(id, pickup, ps);
		if (g_tk_count[id] > 0 && g_tk_i[id][0][TK_KIND] == TASK_PICKUP_ITEM
			&& think_same_handle(g_tk_i[id][0][TK_EDICT], g_tk_i[id][0][TK_EDICT_SERIAL], pickup, ps))
		{
			tasks_finished_current(id, false);
		}
		g_th_f[id][TH_PICKUP_ITEM_TIME] = now + random_float(5.0, 10.0);
		g_th_i[id][TH_PICKUP_ENT] = 0;
		g_th_i[id][TH_PICKUP_SERIAL] = 0;
		return false;
	}
	if (!pev_valid(other) || pev(other, pev_solid) == RC_SOLID_TRIGGER)
	{
		return false;
	}
	new cls[32];
	think_classname(other, cls, charsmax(cls));
	new bool:is_door = equal(cls, "func_door", 9) || equal(cls, "func_plat", 9);
	if (!is_door)
	{
		return false;
	}
	if (!think_entity_moving(other))
	{
		g_nv_f[id][NV_LAST_SEE_WPT] = 0.0;
	}
	new bool:using_lift = tasks_has_schedule(id, SCHED_USE_LIFT);
	new cur = g_nv_i[id][NV_CURRENT_WPT];
	new Float:movedir[3], Float:wpt[3], Float:gun[3];
	pev(other, pev_movedir, movedir);
	if (cur == -1 || movedir[2] == 0.0)
	{
		return false;
	}
	nav_origin(cur, wpt);
	world_eyes(id, gun);
	if (!(wpt[2] > gun[2] + THINK_MAX_JUMP_HEIGHT || !combat_fvisible_point(id, g_mv_v[id][MV_MOVE_TO])))
	{
		return false;
	}
	new Float:origin[3], Float:press_at[3];
	world_origin(id, origin);
	new button = special_find_nearest_button(origin, other, press_at);
	if (button)
	{
		if (!using_lift)
		{
			new bs = think_handle_serial(button);
			new sched = tasks_new_schedule_id(id);
			tasks_add_priority(id, TASK_USE, sched, button, bs, 0, 0.0, VEC_ZERO);
			tasks_add_priority(id, TASK_FIND_PATH, sched, button, bs, 0, 0.0, VEC_ZERO);
			tasks_give_schedule_description(id, sched, SCHED_USE_LIFT);
			tasks_set_time_to_complete_schedule(id, sched, random_float(10.0, 15.0));
		}
		return false;
	}
	new Float:size[3];
	pev(other, pev_size, size);
	new Float:range = vec_length_2d(size);
	static failed[THINK_MAX_FAILED];
	new nf = think_failed_goals(id, failed, THINK_MAX_FAILED);
	new lift = nav_nearest_flag(gun, NAV_FL_LIFT, range, -1, failed, nf);
	if (lift == -1)
	{
		return false;
	}
	static const buttons[][] = { "func_button", "button_target", "func_rot_button" };
	new Float:at[3];
	nav_origin(lift, at);
	new b = special_find_nearest_entity(buttons, sizeof(buttons), at, range, true);
	if (!b)
	{
		return false;
	}
	new bs = think_handle_serial(b);
	new sched = tasks_new_schedule_id(id);
	// The current task goes after the button's (Tasks::add_to_end of a copy).
	new bool:had = g_tk_count[id] > 0;
	new c_i[Tk_I], Float:c_f[Tk_F], Float:c_v[3];
	if (had)
	{
		for (new Tk_I:k; k < Tk_I; k++) c_i[k] = g_tk_i[id][0][k];
		for (new Tk_F:k; k < Tk_F; k++) c_f[k] = g_tk_f[id][0][k];
		vec_copy(g_tk_vec[id][0], c_v);
	}
	tasks_flush(id);
	tasks_add(id, TASK_FIND_PATH, sched, b, bs, 0, 0.0, VEC_ZERO);
	tasks_add(id, TASK_USE, sched, b, bs, 0, 0.0, VEC_ZERO);
	if (had)
	{
		new k = tasks_add(id, c_i[TK_KIND], c_i[TK_SCHEDULE_ID], c_i[TK_EDICT], c_i[TK_EDICT_SERIAL], c_i[TK_INT], c_f[TK_FLOAT], c_v);
		if (k != -1)
		{
			g_tk_i[id][k][TK_SCHEDULE_DESC] = c_i[TK_SCHEDULE_DESC];
			g_tk_i[id][k][TK_FOUND_PATH] = c_i[TK_FOUND_PATH];
			g_tk_f[id][k][TK_TIME_TO_COMPLETE] = c_f[TK_TIME_TO_COMPLETE];
		}
	}
	tasks_give_schedule_description(id, sched, SCHED_USE_LIFT);
	return false;
}

// special.rs Bot::task_push_pushable (BOT_TASK_PUSH_PUSHABLE): go behind the pushable, face it,
// and walk it to the waypoint (6-8 s per state).
stock special_task_push_pushable(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	new p = g_tk_i[id][0][TK_EDICT];
	if (!think_resolve(p, g_tk_i[id][0][TK_EDICT_SERIAL]))
	{
		failed = true;
		return;
	}
	new Float:origin[3];
	vec_copy(g_tk_vec[id][0], origin);
	new Float:size = special_best_pushable_distance(p);
	if (special_acceptable_pushable_vector(p, origin))
	{
		done = true;
		return;
	}
	switch (g_tk_i[id][0][TK_INT])
	{
		case 0:
		{
			g_tk_f[id][0][TK_FLOAT] = now + random_float(6.0, 8.0);
			g_tk_i[id][0][TK_INT] = 1;
		}
		case 1:
		{
			new Float:desired[3];
			special_desired_pushable_vector(origin, p, desired);
			if (think_distance_from(id, desired, false) > size / 2.0)
			{
				movement_set_move_vector(id, desired);
			}
			else
			{
				g_tk_i[id][0][TK_INT] = 2;
			}
		}
		case 2:
		{
			if (!think_facing(id, p))
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_EDICT;
			}
			else
			{
				g_tk_i[id][0][TK_INT] = 3;
			}
			g_tk_f[id][0][TK_FLOAT] = now + random_float(6.0, 8.0);
		}
		default:
		{
			if (random_num(0, 100) != 0)
			{
				movement_use(id);
			}
			movement_set_move_vector(id, origin);
		}
	}
	if (g_tk_count[id] > 0 && g_tk_f[id][0][TK_FLOAT] < now)
	{
		failed = true;
	}
}

// special.rs Bot::task_wait_for_lift (BOT_TASK_WAIT_FOR_LIFT): stand and face where the lift
// comes until a door, plat or train is within 72 units, or 10 s.
stock special_task_wait_for_lift(id, &bool:done)
{
	new Float:now = g_now;
	g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_VECTOR;
	movement_stop_moving(id);
	static near[64];
	new n = world_entities_in_sphere(g_tk_vec[id][0], 72.0, near, sizeof(near));
	new cls[32];
	for (new k = 0; k < n; k++)
	{
		new l = near[k];
		if ((pev(l, pev_effects) & RC_EF_NODRAW) || l == id)
		{
			continue;
		}
		think_classname(l, cls, charsmax(cls));
		if (equal(cls, "func_door", 9) || equal(cls, "func_plat", 9) || equal(cls, "func_train", 10))
		{
			done = true;
			break;
		}
	}
	if (!done)
	{
		if (g_tk_i[id][0][TK_INT] == 0)
		{
			g_tk_f[id][0][TK_FLOAT] = now + 10.0;
			g_tk_i[id][0][TK_INT] = 1;
		}
		else if (g_tk_f[id][0][TK_FLOAT] < now)
		{
			done = true;
		}
	}
}

// special.rs Bot::task_wait_for_entity (BOT_TASK_WAIT_FOR_ENTITY): wait while the entity moves.
stock special_task_wait_for_entity(id, &bool:done, &bool:failed)
{
	new e = g_tk_i[id][0][TK_EDICT];
	if (!think_resolve(e, g_tk_i[id][0][TK_EDICT_SERIAL]))
	{
		failed = true;
	}
	else if (think_entity_moving(e))
	{
		movement_stop_moving(id);
		g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_EDICT;
	}
	else
	{
		done = true;
	}
}

// special.rs Bot::task_human_tower (BOT_TASK_HUMAN_TOWER): walk onto the waypoint (jumping onto
// whoever is there), then crouch until someone stands on the bot, stand up for them and jump;
// 8-10 s at most.
stock special_task_human_tower(id, &bool:done)
{
	new Float:now = g_now;
	new Float:tower[3];
	vec_copy(g_tk_vec[id][0], tower);
	new Float:dist_2d = think_distance_from(id, tower, true);
	new nearby = special_player_near_vector(tower, 36.0);
	new Float:t_float = g_tk_f[id][0][TK_FLOAT];
	if (t_float == 0.0)
	{
		g_tk_f[id][0][TK_FLOAT] = now + random_float(8.0, 10.0);
	}
	else if (t_float < now)
	{
		done = true;
		return;
	}
	if (dist_2d > 36.0)
	{
		movement_set_move_vector(id, tower);
		if (dist_2d < 48.0 && nearby && nearby != id && g_mv_f[id][MV_END_JUMP_TIME] < now)
		{
			movement_jump_and_duck(id);
		}
		return;
	}
	movement_stop_moving(id);
	new Float:me[3];
	world_origin(id, me);
	if (special_standing_on_player(id))
	{
		if (g_mv_v[id][MV_MOVE_TO][2] - me[2] < THINK_MAX_JUMP_HEIGHT)
		{
			done = true;
		}
		else
		{
			movement_jump_and_duck(id);
		}
	}
	else if (special_player_standing_on_me(id))
	{
		g_co_i[id][CO_BUTTON] &= ~IN_DUCK;
		g_tk_i[id][0][TK_INT] = 1;
	}
	else if (g_tk_i[id][0][TK_INT] == 1)
	{
		g_co_i[id][CO_BUTTON] &= ~IN_DUCK;
		if (g_mv_f[id][MV_END_JUMP_TIME] + 0.5 < now)
		{
			movement_jump(id);
		}
		else
		{
			g_tk_i[id][0][TK_INT] = 0;
		}
	}
	else
	{
		movement_duck(id);
	}
}

// special.rs Bot::task_wait_for_bot_at_wpt (BOT_TASK_WAIT_FOR_BOT_AT_WPT): wait 4-6 s, or until the
// bot is nearer the waypoint than the other bot (OtherBot: one of our bots, by handle).
stock special_task_wait_for_bot_at_wpt(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	new wpt = g_tk_i[id][0][TK_INT], other = g_tk_i[id][0][TK_EDICT];
	if (!other || wpt == -1)
	{
		failed = true;
		return;
	}
	if (g_tk_f[id][0][TK_FLOAT] == 0.0)
	{
		g_tk_f[id][0][TK_FLOAT] = now + random_float(4.0, 6.0);
	}
	if (!is_rcbot(other) || world_userid(other) != g_tk_i[id][0][TK_EDICT_SERIAL])
	{
		failed = true;
		return;
	}
	new Float:v[3], Float:gun[3];
	nav_origin(wpt, v);
	world_eyes(other, gun);
	movement_stop_moving(id);
	done = g_tk_f[id][0][TK_FLOAT] < now || think_distance_from(id, v, false) <= vec_distance(v, gun);
}

// special.rs Bot::task_face_edict (BOT_TASK_FACE_EDICT): face the entity for the task's time.
stock special_task_face_edict(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	if (!think_resolve(g_tk_i[id][0][TK_EDICT], g_tk_i[id][0][TK_EDICT_SERIAL]))
	{
		failed = true;
		return;
	}
	g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_EDICT;
	if (g_tk_i[id][0][TK_INT] == 0)
	{
		g_tk_f[id][0][TK_FLOAT] += now;
		g_tk_i[id][0][TK_INT] = 1;
	}
	else if (g_tk_f[id][0][TK_FLOAT] < now)
	{
		done = true;
	}
}

// special.rs Bot::task_throw_grenade (BOT_TASK_THROW_GRENADE, TS; design §4): takes out the M61,
// turns to the view pitch whose throw lands at the task's vector (fire_throw_pitch, gravity
// 0.75 sv_gravity), pulls the pin (+attack) for 0.2 s and lets go (TS throws on release), then
// takes out a loaded gun.
stock special_task_throw_grenade(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	movement_stop_moving(id);
	switch (g_tk_i[id][0][TK_INT])
	{
		case 0:
		{
			if (!(g_bw_weapon[id][SPECIAL_M61][BW_HAS_WEAPON] && g_bw_weapon[id][SPECIAL_M61][BW_CLIP] > 0))
			{
				failed = true;
				return;
			}
			if (g_bw_i[id][BW_CURRENT] != SPECIAL_M61)
			{
				think_switch_weapon(id, SPECIAL_M61);
				return;
			}
			new Float:src[3], Float:pitch, Float:time;
			world_eyes(id, src);
			if (!fire_throw_pitch(src, g_tk_vec[id][0], world_cvar_gravity() * 0.75, pitch, time))
			{
				failed = true;
				return;
			}
			new Float:d[3], Float:ang[3], Float:fwd[3], Float:right[3], Float:aim[3];
			vec_sub(g_tk_vec[id][0], src, d);
			vec_to_angles(d, ang);
			new Float:yaw = ang[1];
			vec_set(ang, pitch, yaw, 0.0);
			make_vectors(ang, fwd, right);
			vec_mad(src, fwd, 128.0, aim);
			g_lk_i[id][LK_LOOK_TASK] = LOOK_NONE;
			vec_copy(aim, g_fi_v[id][FI_AIM_TRUE]);
			g_fi_i[id][FI_AIM_TRUE_SET] = 1;
			look_turn_to(id, aim, 0.04);
			if (floatabs(angle_diff(pitch, g_co_v[id][CO_VANGLE][0])) < 1.5 && floatabs(angle_diff(yaw, g_co_v[id][CO_VANGLE][1])) < 1.5)
			{
				bot_primary_attack(id);
				g_tk_i[id][0][TK_INT] = 1;
				g_tk_f[id][0][TK_FLOAT] = now + 0.2;
			}
			#pragma unused time
		}
		case 1:
		{
			g_lk_i[id][LK_LOOK_TASK] = LOOK_NONE;
			if (g_tk_f[id][0][TK_FLOAT] > now)
			{
				bot_primary_attack(id);
			}
			else
			{
				g_tk_i[id][0][TK_INT] = 2;
				g_tk_f[id][0][TK_FLOAT] = now + 0.6;
			}
		}
		default:
		{
			if (g_tk_f[id][0][TK_FLOAT] < now)
			{
				done = true;
				new next = botweapons_best_loaded_gun(id);
				if (next != -1 && next != SPECIAL_M61)
				{
					think_switch_weapon(id, next);
				}
			}
		}
	}
}

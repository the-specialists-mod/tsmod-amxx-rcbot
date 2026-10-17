// rcbot/preaim.inc: pre-aiming where an enemy may appear (port addition, design §4).
// Owner: combat. Ports bot/preaim.rs (PreAim, Bot::watch, pre_aim_target, update_pre_aim, pre_aim_look).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. The visibility table and the heatmap are rcbot_nav's (nav_visible,
// nav_heatmap_danger: BotGlobals::danger_at).

#if defined _rcbot_preaim_included
	#endinput
#endif
#define _rcbot_preaim_included

// preaim.rs PRE_AIM_RANGE, CHECKED_FOR, CHEST.
#define PA_RANGE_MIN		150.0
#define PA_RANGE_MAX		1500.0
#define PA_CHECKED_FOR		4.0
#define PA_CHEST			8.0

// preaim.rs PreAim::default (a new bot).
stock preaim_bot_init(id)
{
	for (new Pa_F:k; k < Pa_F; k++) g_pa_f[id][k] = 0.0;
	for (new Pa_V:k; k < Pa_V; k++) vec_zero(g_pa_v[id][k]);
	g_pa_checked_count[id] = 0;
}

// preaim.rs Bot::watch: looks at `at` for secs (a sound), then goes back to pre-aiming.
stock preaim_watch(id, const Float:at[3], Float:secs)
{
	vec_copy(at, g_pa_v[id][PA_WATCH]);
	g_pa_f[id][PA_WATCH_UNTIL] = g_now + secs;
	new look = g_lk_i[id][LK_LOOK_TASK];
	if (look != LOOK_FACE_ENEMY && look != LOOK_FACE_TASK_EDICT && look != LOOK_FACE_TASK_VECTOR)
	{
		g_lk_i[id][LK_LOOK_TASK] = LOOK_PRE_AIM;
	}
}

// preaim.rs Bot::pre_aim_target: the waypoint to pre-aim at from the bot's path: visible from
// next (where it will be) but not from cur (where it is), between PRE_AIM_RANGE, not ahead of the
// bot along its path; with cur == next (standing), the ones cur sees. Most dangerous and longest
// unchecked first. -1 none.
stock preaim_target(id, cur, next)
{
	new Float:now = g_now;
	new Float:cur_o[3], Float:next_o[3], Float:d[3], Float:heading[3];
	nav_origin(cur, cur_o);
	nav_origin(next, next_o);
	vec_sub(next_o, cur_o, d);
	look_normalize(d, heading);
	new team = g_th_i[id][TH_TEAM];
	new best = -1, Float:best_score = 0.0;
	new n = nav_count();
	new Float:o[3], Float:dir[3];
	for (new i = 0; i < n; i++)
	{
		// The visibility check first: it rules most waypoints out (the order changes nothing).
		if (i == cur || i == next || !nav_visible(next, i))
		{
			continue;
		}
		if (nav_flags(i) & (NAV_FL_DELETED | NAV_FL_AIMING))
		{
			continue;
		}
		if (cur != next && nav_visible(cur, i))
		{
			continue;
		}
		nav_origin(i, o);
		new Float:dist = vec_distance(o, next_o);
		if (dist < PA_RANGE_MIN || dist > PA_RANGE_MAX)
		{
			continue;
		}
		// Where the path goes the bot looks anyway.
		if (cur != next)
		{
			vec_sub(o, next_o, d);
			look_normalize(d, dir);
			if (vec_dot(dir, heading) > 0.9)
			{
				continue;
			}
		}
		new Float:score = 1.0 + 3.0 * nav_heatmap_danger(i, team);
		for (new k = 0; k < g_rm_count[id]; k++)
		{
			if (vec_distance(g_rm_origin[id][k], o) < 400.0)
			{
				score += 2.0;
				break;
			}
		}
		for (new k = 0; k < g_pa_checked_count[id]; k++)
		{
			if (g_pa_checked_wpt[id][k] == i && now - g_pa_checked_time[id][k] < PA_CHECKED_FOR)
			{
				score *= 0.2;
				break;
			}
		}
		score += random_float(0.0, 0.5);
		if (best == -1 || score > best_score)
		{
			best = i;
			best_score = score;
		}
	}
	return best;
}

// preaim.rs Bot::update_pre_aim: keeps PreAim::point up to date while the look task is PRE_AIM:
// a sound being watched, else the next place to check for [0.6, 1.2) s, else the move target.
stock preaim_update(id)
{
	if (g_lk_i[id][LK_LOOK_TASK] != LOOK_PRE_AIM)
	{
		return;
	}
	new Float:now = g_now;
	if (g_pa_f[id][PA_WATCH_UNTIL] > now)
	{
		vec_copy(g_pa_v[id][PA_WATCH], g_pa_v[id][PA_POINT]);
		return;
	}
	if (g_pa_f[id][PA_UNTIL] > now)
	{
		return;
	}
	new cur = g_nv_i[id][NV_CURRENT_WPT];
	if (cur < 0)
	{
		vec_copy(g_mv_v[id][MV_MOVE_TO], g_pa_v[id][PA_POINT]);
		g_pa_f[id][PA_UNTIL] = now + 0.3;
		return;
	}
	// navigate_next_waypoint (mod.rs Bot::next_waypoint), here until navigate.inc's compiles when
	// called (its ternary's tag override needs parentheses).
	new next = cur;
	if (g_nv_i[id][NV_PATH])
	{
		new RcPath:path = RcPath:g_nv_i[id][NV_PATH];
		next = nav_path_after(path, cur);
	}
	if (next < 0)
	{
		next = cur;
	}
	new i = preaim_target(id, cur, next);
	if (i != -1)
	{
		nav_origin(i, g_pa_v[id][PA_POINT]);
		g_pa_v[id][PA_POINT][2] += PA_CHEST;
		g_pa_f[id][PA_UNTIL] = now + random_float(0.6, 1.2);
		// checked.retain(now - t < CHECKED_FOR), then the new one at the end (oldest dropped
		// when full).
		new kept = 0;
		for (new k = 0; k < g_pa_checked_count[id]; k++)
		{
			if (now - g_pa_checked_time[id][k] < PA_CHECKED_FOR)
			{
				g_pa_checked_wpt[id][kept] = g_pa_checked_wpt[id][k];
				g_pa_checked_time[id][kept] = g_pa_checked_time[id][k];
				kept++;
			}
		}
		if (kept >= RC_MAX_PREAIM_CHECKED)
		{
			for (new k = 1; k < kept; k++)
			{
				g_pa_checked_wpt[id][k - 1] = g_pa_checked_wpt[id][k];
				g_pa_checked_time[id][k - 1] = g_pa_checked_time[id][k];
			}
			kept--;
		}
		g_pa_checked_wpt[id][kept] = i;
		g_pa_checked_time[id][kept] = now;
		g_pa_checked_count[id] = kept + 1;
	}
	else
	{
		if (g_mv_i[id][MV_MOVE_TO_VALID])
		{
			vec_copy(g_mv_v[id][MV_MOVE_TO], g_pa_v[id][PA_POINT]);
		}
		g_pa_f[id][PA_UNTIL] = now + 0.3;
	}
}

// preaim.rs Bot::pre_aim_look (WorkViewAngles for LOOK_PRE_AIM).
stock preaim_look(id)
{
	if (vec_is_zero(g_pa_v[id][PA_POINT]))
	{
		look_set_view_angles(id, g_mv_v[id][MV_MOVE_TO]);
	}
	else
	{
		look_set_view_angles(id, g_pa_v[id][PA_POINT]);
	}
}

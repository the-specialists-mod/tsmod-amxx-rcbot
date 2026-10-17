// rcbot/squad.inc: squads (CBotSquad, CBotSquads) and the bots' squad behaviour.
// Owner: social. Ports bot/squad.rs (Squad new / init / leader / is_leader / is_member /
// add_member / remove_member / num_members / toggle_fire_mode / can_shoot / is_stealth /
// is_prone / is_crouch / set_combat_type / change_formation / spread / change_spread /
// formation_position / formation_vector / update_angles / change_leader; Squads clear / get /
// find_by_leader / add_member / remove_member / remove / change_leader / return_all_to_formation
// / update_angles; SquadOrder; Bot clear_squad / set_squad / is_squad_leader / can_add_to_squad /
// add_to_squad / remove_my_squad / squad_order / squad_think / update_squad_conditions /
// follow_formation / squad_leader_think / squad_modes / squad_enemy_found / see_leader_again /
// say_in_position / task_follow_leader), manager.rs apply_squad_orders, host bots.rs
// flush_squad_messages / remove_all_bot_squads, disconnect's change_leader, commands.rs Host's
// squad effects (with_squad, SquadToggleCombatType).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// TS bots never assemble squads of their own; players make them ("Make Squad", say_team
// "form up") and change them (the squad command and menus). A squad lives in a slot of the
// tables below under an id (the slots in stack order, top at the end); a bot holds the id
// (g_sq_i[SQ_SQUAD], -1 none): a removed squad's id stops resolving. What squad changes ask of
// bots is queued as orders, handed to them before they next think (squad_apply_orders). Leaders
// and members are EntRefs: (player index, user id).
//
// The team boards squads.rs keeps beside the squads are board.inc's (movement).

#if defined _rcbot_squad_included
	#endinput
#endif
#define _rcbot_squad_included

#define SQUAD_DEFAULT_SPREAD	50.0
#define RC_MAX_SQUADS			16
#define RC_MAX_SQUAD_MEMBERS	8
#define RC_MAX_SQUAD_ORDERS		64
#define RC_MAX_SQUAD_MESSAGES	16

// Squads, slots 0..g_squad_count-1 in stack order.
new g_squad_count;
new g_squad_next_id;
new g_squad_id[RC_MAX_SQUADS];
new g_squad_leader[RC_MAX_SQUADS];			// 0 none
new g_squad_leader_serial[RC_MAX_SQUADS];
new g_squad_members[RC_MAX_SQUADS][RC_MAX_SQUAD_MEMBERS];		// m_theSquad, top at the end
new g_squad_member_serial[RC_MAX_SQUADS][RC_MAX_SQUAD_MEMBERS];
new g_squad_member_count[RC_MAX_SQUADS];
new g_squad_formation[RC_MAX_SQUADS];
new Float:g_squad_spread[RC_MAX_SQUADS];
new bool:g_squad_can_fire[RC_MAX_SQUADS];
new g_squad_combat_type[RC_MAX_SQUADS];
new Float:g_squad_leader_angle[RC_MAX_SQUADS][3];

// Squads::orders: SquadOrder::SetSquad / ReturnToFormation (bot index, squad id).
enum { SQO_SET_SQUAD = 0, SQO_RETURN_TO_FORMATION };
new g_sqo_kind[RC_MAX_SQUAD_ORDERS];
new g_sqo_bot[RC_MAX_SQUAD_ORDERS];
new g_sqo_squad[RC_MAX_SQUAD_ORDERS];
new g_sqo_count;

// Squads::messages: lines for players' chat areas (flush_squad_messages).
new g_sqm_player[RC_MAX_SQUAD_MESSAGES];
new g_sqm_text[RC_MAX_SQUAD_MESSAGES][96];
new g_sqm_count;

// ---- CBotSquads --------------------------------------------------------------------------------

// Squads::clear (FreeMemory, each map).
stock squad_clear()
{
	g_squad_count = 0;
	g_sqo_count = 0;
	g_sqm_count = 0;
}

// Squads::get: the slot of squad id, -1 when it doesn't exist (any more).
stock squad_get(sid)
{
	if (sid < 0)
	{
		return -1;
	}
	for (new s = 0; s < g_squad_count; s++)
	{
		if (g_squad_id[s] == sid)
		{
			return s;
		}
	}
	return -1;
}

static bool:squad_is_leader_slot(s, player, serial)
{
	return g_squad_leader[s] != 0 && g_squad_leader[s] == player && g_squad_leader_serial[s] == serial;
}

static bool:squad_is_member_slot(s, player, serial)
{
	for (new k = 0; k < g_squad_member_count[s]; k++)
	{
		if (g_squad_members[s][k] == player && g_squad_member_serial[s][k] == serial)
		{
			return true;
		}
	}
	return false;
}

// Squads::find_by_leader (FindSquadByLeader), from the top: the squad id, -1 none.
stock squad_find_by_leader(player)
{
	new serial = world_userid(player);
	for (new s = g_squad_count - 1; s >= 0; s--)
	{
		if (squad_is_leader_slot(s, player, serial))
		{
			return g_squad_id[s];
		}
	}
	return -1;
}

static squad_add_member_slot(s, player, serial)
{
	if (squad_is_member_slot(s, player, serial))
	{
		return;
	}
	new n = g_squad_member_count[s];
	if (n >= RC_MAX_SQUAD_MEMBERS)
	{
		rc_debug(DBG_MESSAGE, "squad %d full: member %d not added", g_squad_id[s], player);
		return;
	}
	g_squad_members[s][n] = player;
	g_squad_member_serial[s][n] = serial;
	g_squad_member_count[s] = n + 1;
}

// Squad::init: wedge, the default spread, normal combat, open fire, the leader's angles.
static squad_init_slot(s, leader)
{
	g_squad_formation[s] = FORM_WEDGE;
	g_squad_spread[s] = SQUAD_DEFAULT_SPREAD;
	g_squad_combat_type[s] = COMBAT_COMBAT;
	g_squad_can_fire[s] = true;
	if (leader > 0 && is_user_connected(leader))
	{
		pev(leader, pev_angles, g_squad_leader_angle[s]);
	}
}

static squad_push_order(kind, bot, sid)
{
	if (g_sqo_count >= RC_MAX_SQUAD_ORDERS)
	{
		return;
	}
	g_sqo_kind[g_sqo_count] = kind;
	g_sqo_bot[g_sqo_count] = bot;
	g_sqo_squad[g_sqo_count] = sid;
	g_sqo_count++;
}

static squad_push_message(player, const text[])
{
	if (g_sqm_count >= RC_MAX_SQUAD_MESSAGES)
	{
		return;
	}
	g_sqm_player[g_sqm_count] = player;
	copy(g_sqm_text[g_sqm_count], charsmax(g_sqm_text[]), text);
	g_sqm_count++;
}

// Squads::add_member (AddSquadMember(leader, member)): joins the squad the leader leads or is in,
// else a new squad led by them (a bot leader is told). Players of different teams can't. Tells
// the leader who joined. The squad id, -1 none.
stock squad_add_member(leader, member, bool:leader_is_bot)
{
	if (world_team(leader) != world_team(member))
	{
		return -1;
	}
	new name[RC_NAME_LEN], text[96];
	get_user_name(member, name, charsmax(name));
	formatex(text, charsmax(text), "%s %s has joined your squad", RC_MSG_TAG, name);
	squad_push_message(leader, text);
	new ls = world_userid(leader), ms = world_userid(member);
	for (new s = g_squad_count - 1; s >= 0; s--)
	{
		if (squad_is_leader_slot(s, leader, ls) || squad_is_member_slot(s, leader, ls))
		{
			squad_add_member_slot(s, member, ms);
			return g_squad_id[s];
		}
	}
	if (g_squad_count >= RC_MAX_SQUADS)
	{
		rc_debug(DBG_MESSAGE, "too many squads: none made for %d", leader);
		return -1;
	}
	new s = g_squad_count++;
	new sid = ++g_squad_next_id;
	g_squad_id[s] = sid;
	g_squad_leader[s] = leader;
	g_squad_leader_serial[s] = ls;
	g_squad_member_count[s] = 0;
	squad_add_member_slot(s, member, ms);
	squad_init_slot(s, leader);
	if (leader_is_bot)
	{
		squad_push_order(SQO_SET_SQUAD, leader, sid);
	}
	return sid;
}

// Squads::remove (RemoveSquad).
stock squad_remove(sid)
{
	new s = squad_get(sid);
	if (s == -1)
	{
		return;
	}
	for (new t = s; t < g_squad_count - 1; t++)
	{
		squad_copy_slot(t + 1, t);
	}
	g_squad_count--;
}

static squad_copy_slot(from, to)
{
	g_squad_id[to] = g_squad_id[from];
	g_squad_leader[to] = g_squad_leader[from];
	g_squad_leader_serial[to] = g_squad_leader_serial[from];
	g_squad_member_count[to] = g_squad_member_count[from];
	for (new k = 0; k < g_squad_member_count[from]; k++)
	{
		g_squad_members[to][k] = g_squad_members[from][k];
		g_squad_member_serial[to][k] = g_squad_member_serial[from][k];
	}
	g_squad_formation[to] = g_squad_formation[from];
	g_squad_spread[to] = g_squad_spread[from];
	g_squad_can_fire[to] = g_squad_can_fire[from];
	g_squad_combat_type[to] = g_squad_combat_type[from];
	vec_copy(g_squad_leader_angle[from], g_squad_leader_angle[to]);
}

// Squads::change_leader (CBotSquads::ChangeLeader, with Squad::change_leader): the newest member
// leads; with nobody left (or only the new leader), no leader and the squad goes. The new
// leader's index, 0 none.
stock squad_change_leader_of(sid)
{
	new s = squad_get(sid);
	if (s == -1)
	{
		return 0;
	}
	if (g_squad_member_count[s] == 0)
	{
		g_squad_leader[s] = 0;
	}
	else
	{
		new n = --g_squad_member_count[s];
		g_squad_leader[s] = g_squad_members[s][n];
		g_squad_leader_serial[s] = g_squad_member_serial[s][n];
		if (n == 0)
		{
			g_squad_leader[s] = 0;
		}
		else
		{
			new l = g_squad_leader[s];
			squad_init_slot(s, world_userid(l) == g_squad_leader_serial[s] ? l : 0);
		}
	}
	new leader = g_squad_leader[s];
	if (leader == 0)
	{
		squad_remove(sid);
	}
	return leader;
}

// Squads::return_all_to_formation (ReturnAllToFormation): every bot member (newest first)
// flushes its tasks and follows the leader again.
stock squad_return_all_to_formation(sid)
{
	new s = squad_get(sid);
	if (s == -1)
	{
		return;
	}
	for (new k = g_squad_member_count[s] - 1; k >= 0; k--)
	{
		new m = g_squad_members[s][k];
		if (is_rcbot(m))
		{
			squad_push_order(SQO_RETURN_TO_FORMATION, m, sid);
		}
	}
}

// Squads::update_angles (m_Squads.UpdateAngles, every frame): the leader's heading while it moves.
stock squad_update_angles()
{
	new Float:vel[3];
	for (new s = 0; s < g_squad_count; s++)
	{
		new l = g_squad_leader[s];
		if (l == 0 || world_userid(l) != g_squad_leader_serial[s])
		{
			continue;
		}
		world_velocity(l, vel);
		if (vec_length_2d(vel) != 0.0)
		{
			vec_to_angles(vel, g_squad_leader_angle[s]);
		}
	}
}

// manager.rs apply_squad_orders: the queued orders to their bots (Bot::squad_order).
stock squad_apply_orders()
{
	if (g_sqo_count == 0)
	{
		return;
	}
	new n = g_sqo_count;
	g_sqo_count = 0;
	for (new k = 0; k < n; k++)
	{
		new b = g_sqo_bot[k];
		if (!is_rcbot(b))
		{
			continue;
		}
		if (g_sqo_kind[k] == SQO_RETURN_TO_FORMATION)
		{
			tasks_flush(b);
			tasks_add_priority(b, TASK_FOLLOW_LEADER, tasks_new_schedule_id(b), 0, 0, 0, 0.0, VEC_ZERO);
		}
		squad_set_squad(b, g_sqo_squad[k]);
	}
}

// host bots.rs flush_squad_messages: what squads told their leaders (ClientPrint HUD_PRINTTALK,
// the host's console print).
stock squad_flush_messages()
{
	if (g_sqm_count == 0)
	{
		return;
	}
	for (new k = 0; k < g_sqm_count; k++)
	{
		new p = g_sqm_player[k];
		if (p >= 1 && p <= MAX_PLAYERS && is_user_connected(p))
		{
			client_print(p, print_console, "%s", g_sqm_text[k]);
		}
	}
	g_sqm_count = 0;
}

// host bots.rs disconnect: a leading player hands the squad on (Squads::change_leader).
stock squad_player_left(player)
{
	new sid = squad_find_by_leader(player);
	if (sid != -1)
	{
		squad_change_leader_of(sid);
	}
}

// ---- CBotSquad --------------------------------------------------------------------------------

// Squad::change_formation (ChangeFormation).
stock squad_change_formation(sid, form)
{
	new s = squad_get(sid);
	if (s != -1)
	{
		g_squad_formation[s] = form;
	}
}

// Squad::spread (GetSpread).
stock Float:squad_spread(sid)
{
	new s = squad_get(sid);
	return s == -1 ? 0.0 : g_squad_spread[s];
}

// Squad::change_spread (ChangeSpread).
stock squad_change_spread(sid, Float:spread)
{
	new s = squad_get(sid);
	if (s != -1)
	{
		g_squad_spread[s] = spread;
	}
}

// Effect::SquadToggleFireMode (ToggleFireMode): the leader is told (Host::with_squad's TalkOne).
stock squad_toggle_fire_mode(sid)
{
	new s = squad_get(sid);
	if (s == -1)
	{
		return;
	}
	g_squad_can_fire[s] = !g_squad_can_fire[s];
	commands_print(g_squad_leader[s], g_squad_can_fire[s] ? "Squad can now OPEN FIRE" : "Squad is now HOLDING FIRE");
}

// Squad::set_combat_type (SetCombatType): the message for the leader into out (false without a
// leader).
static bool:squad_set_combat_type_slot(s, t, out[], len)
{
	g_squad_combat_type[s] = t;
	if (g_squad_leader[s] == 0)
	{
		return false;
	}
	new name[8];
	switch (t)
	{
		case COMBAT_STEALTH: copy(name, charsmax(name), "STEALTH");
		case COMBAT_PRONE: copy(name, charsmax(name), "PRONE");
		case COMBAT_CROUCH: copy(name, charsmax(name), "CROUCH");
		default: copy(name, charsmax(name), "NORMAL");
	}
	formatex(out, len, "Combat mode is now %s^n", name);
	return true;
}

// Effect::SquadToggleCombatType: crouch / stealth on, or back to COMBAT_COMBAT when already on.
stock squad_toggle_combat_type(sid, t)
{
	new s = squad_get(sid);
	if (s == -1)
	{
		return;
	}
	new cur = g_squad_combat_type[s];
	new nt = (t == COMBAT_CROUCH && cur != COMBAT_CROUCH) ? COMBAT_CROUCH : ((t == COMBAT_STEALTH && cur != COMBAT_STEALTH) ? COMBAT_STEALTH : COMBAT_COMBAT);
	new msg[48];
	if (squad_set_combat_type_slot(s, nt, msg, charsmax(msg)))
	{
		commands_print(g_squad_leader[s], msg);
	}
}

// Squad::formation_position (GetFormationPosition): 1 for the newest member, 0 not a member.
static squad_formation_position(s, member, serial)
{
	new n = g_squad_member_count[s];
	for (new k = n - 1; k >= 0; k--)
	{
		if (g_squad_members[s][k] == member && g_squad_member_serial[s][k] == serial)
		{
			return n - k;
		}
	}
	return 0;
}

// Squad::formation_vector (GetFormationVector): where member stands: the formation's direction
// off the leader's heading, times the spread and the member's position, traced from the leader.
// Upstream takes the trace's end position and, when blocked, normalises it and shortens it by
// 32 as if it were the offset, so a blocked point moves towards the map's origin (kept).
stock squad_formation_vector(sid, member, Float:out[3])
{
	vec_zero(out);
	new s = squad_get(sid);
	if (s == -1)
	{
		return;
	}
	new l = g_squad_leader[s];
	if (l == 0 || world_userid(l) != g_squad_leader_serial[s])
	{
		return;
	}
	new position = squad_formation_position(s, member, world_userid(member));
	new Float:lo[3], Float:fwd[3], Float:right[3], Float:base[3];
	world_player_center(l, lo);
	new bool:odd = position % 2 != 0;
	make_vectors(g_squad_leader_angle[s], fwd, right);
	switch (g_squad_formation[s])
	{
		case FORM_VEE:
		{
			// odd: forward - right, else forward + right.
			vec_mad(fwd, right, odd ? -1.0 : 1.0, base);
		}
		case FORM_WEDGE:
		{
			vec_mad(fwd, right, odd ? -1.0 : 1.0, base);
			vec_scale(base, -1.0, base);
		}
		case FORM_LINE:
		{
			vec_scale(right, odd ? 1.0 : -1.0, base);
		}
		case FORM_COLUMN:
		{
			vec_scale(fwd, -1.0, base);
		}
		case FORM_ECH_LEFT:
		{
			vec_add(fwd, right, base);
			vec_scale(base, -1.0, base);
		}
		case FORM_ECH_RIGHT:
		{
			vec_sub(right, fwd, base);
		}
		default:
		{
			vec_zero(base);
		}
	}
	vec_scale(base, g_squad_spread[s] * float(position), base);
	new Float:to[3], hit;
	vec_add(lo, base, to);
	new Float:fraction = world_trace_line(lo, to, true, false, l, out, hit);
	if (fraction < 1.0)
	{
		new Float:d = vec_length(out);
		vec_normalize(out, out);
		vec_scale(out, d - 32.0, out);
	}
}

// ---- the bots' side (Bot) -------------------------------------------------------------------------

// Bot::clear_squad (ClearSquad).
stock squad_clear_bot(id)
{
	g_sq_i[id][SQ_SQUAD] = -1;
	g_sq_i[id][SQ_LEADER] = 0;
	g_sq_i[id][SQ_LEADER_SERIAL] = 0;
}

// Bot::set_squad (SetSquad).
stock squad_set_squad(id, sid)
{
	squad_clear_bot(id);
	g_sq_i[id][SQ_SQUAD] = sid;
}

// Bot::is_squad_leader (IsSquadLeader): in a squad and following nobody.
stock bool:squad_is_leader(id)
{
	return g_sq_i[id][SQ_LEADER] == 0 && g_sq_i[id][SQ_SQUAD] != -1;
}

// Bot::can_add_to_squad (CanAddToSquad): no squad yet, and the leader is a teammate.
stock bool:squad_can_add(id, leader)
{
	return g_sq_i[id][SQ_SQUAD] == -1 && !combat_is_enemy(id, leader) && world_team(id) == world_team(leader);
}

// Bot::add_to_squad (AddToSquad): join and follow.
stock squad_add(id, leader, bool:leader_is_bot)
{
	new sid = squad_add_member(leader, id, leader_is_bot);
	g_sq_i[id][SQ_SQUAD] = sid;
	if (sid != -1)
	{
		tasks_add_priority(id, TASK_FOLLOW_LEADER, tasks_new_schedule_id(id), leader, world_userid(leader), 0, 0.0, VEC_ZERO);
	}
}

// Bot::remove_my_squad (RemoveMySquad): a leader removes its squad.
stock bool:squad_remove_mine(id)
{
	new sid = g_sq_i[id][SQ_SQUAD];
	new s = squad_get(sid);
	if (s == -1)
	{
		return false;
	}
	if (squad_is_leader_slot(s, id, world_userid(id)))
	{
		squad_remove(sid);
		g_sq_i[id][SQ_SQUAD] = -1;
		return true;
	}
	return false;
}

// host bots.rs remove_all_bot_squads (BotMenu_Func_Squad_RemoveAllBotSquads): the bots of the
// rules team drop the squads they lead; how many.
stock squad_remove_all_bot_squads(team)
{
	new n = 0;
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED] && world_team(b) == team && squad_remove_mine(b))
		{
			n++;
		}
	}
	return n;
}

static bool:squad_leader_alive(id)
{
	new l = g_sq_i[id][SQ_LEADER];
	return l != 0 && entref_alive(l, g_sq_i[id][SQ_LEADER_SERIAL]);
}

// Bot::squad_think (the squad part at the top of Think): who leads the bot, the leader's
// "Let's go!", and whether the leader is dead (or turned enemy).
stock squad_think(id)
{
	new Float:now = g_now;
	g_sq_i[id][SQ_LEADER] = 0;
	g_sq_i[id][SQ_LEADER_SERIAL] = 0;
	new sid = g_sq_i[id][SQ_SQUAD];
	if (sid == -1)
	{
		return;
	}
	new s = squad_get(sid);
	if (s == -1)
	{
		// Removed, or it lost its leader (ClearSquad).
		squad_clear_bot(id);
		return;
	}
	if (!squad_is_leader_slot(s, id, world_userid(id)))
	{
		g_sq_i[id][SQ_LEADER] = g_squad_leader[s];
		g_sq_i[id][SQ_LEADER_SERIAL] = g_squad_leader_serial[s];
	}
	else if (!g_sq_i[id][SQ_SAID_MOVE_OUT] && g_sq_f[id][SQ_NEXT_USE_SAY_MESSAGE] < now)
	{
		bot_action_command(id, "say_team ^"Let's go!^"");
		g_sq_i[id][SQ_SAID_MOVE_OUT] = 1;
		g_sq_f[id][SQ_NEXT_USE_SAY_MESSAGE] = now + 5.0;
	}
	if (g_sq_i[id][SQ_LEADER] != 0)
	{
		// The conditions are think's; the Rust bot sets these from squad.rs.
		new l = g_sq_i[id][SQ_LEADER];
		if (squad_leader_alive(id) && bot_alive(l))
		{
			if (combat_is_enemy(id, l))
			{
				g_th_i[id][TH_CONDITIONS] |= COND_SQUAD_LEADER_DEAD;
			}
			else
			{
				g_th_i[id][TH_CONDITIONS] &= ~COND_SQUAD_LEADER_DEAD;
			}
		}
		else
		{
			g_th_i[id][TH_CONDITIONS] |= COND_SQUAD_LEADER_DEAD;
		}
	}
}

// Bot::update_squad_conditions (UpdateConditions' squad part): whether the bot sees its leader.
stock squad_update_conditions(id)
{
	if (!squad_leader_alive(id))
	{
		return;
	}
	new Float:eyes[3], Float:lo[3], Float:end[3], hit;
	world_eyes(id, eyes);
	world_player_center(g_sq_i[id][SQ_LEADER], lo);
	if (world_trace_line(eyes, lo, true, true, id, end, hit) >= 1.0)
	{
		g_th_i[id][TH_CONDITIONS] |= COND_SEE_SQUAD_LEADER;
	}
	else
	{
		g_th_i[id][TH_CONDITIONS] &= ~COND_SEE_SQUAD_LEADER;
	}
}

// Bot::distance_from (DistanceFrom(v, twoD)), from the gun position.
static Float:squad_distance_from(id, const Float:v[3], bool:two_d)
{
	new Float:eyes[3];
	world_eyes(id, eyes);
	return two_d ? vec_distance_2d(v, eyes) : vec_distance(v, eyes);
}

// Bot::follow_formation (Think's formation step): a member who sees a living leader (and isn't
// finding a path or fighting with a knife) walks to its place; else the bot follows waypoints
// (a leader also runs BotSquadLeaderThink). True when the formation moved the bot.
stock bool:squad_follow_formation(id)
{
	new bool:follow = true;
	if (g_tk_count[id] > 0)
	{
		new kind = g_tk_i[id][0][TK_KIND];
		follow = kind != TASK_FIND_PATH;
		new w = g_bw_i[id][BW_CURRENT];
		if (kind == TASK_NORMAL_ATTACK && w != -1 && botweapons_is_melee(id, w))
		{
			follow = false;
		}
	}
	new sid = g_sq_i[id][SQ_SQUAD];
	new s = squad_get(sid);
	new cond = g_th_i[id][TH_CONDITIONS];
	if (follow && s != -1 && g_sq_i[id][SQ_LEADER] != 0 && (cond & COND_SEE_SQUAD_LEADER) && !(cond & COND_SQUAD_LEADER_DEAD))
	{
		new Float:to[3];
		squad_formation_vector(sid, id, to);
		if (squad_distance_from(id, to, true) > g_squad_spread[s])
		{
			movement_set_move_vector(id, to);
		}
		else
		{
			new l = g_sq_i[id][SQ_LEADER];
			if (squad_leader_alive(id) && !(world_flags(l) & TSPF_ON_GROUND))
			{
				// touched_waypoints is navigate's; the Rust bot counts it here.
				g_nv_i[id][NV_TOUCHED_WPTS]++;
			}
			movement_stop_moving(id);
		}
		return true;
	}
	if (squad_is_leader(id))
	{
		squad_leader_think(id);
	}
	return false;
}

// Bot::squad_leader_think (BotSquadLeaderThink): a leader listening to an enemy's sound makes the
// squad a vee.
static squad_leader_think(id)
{
	if (g_tk_count[id] == 0 || g_tk_i[id][0][TK_KIND] != TASK_LISTEN_TO_SOUND)
	{
		return;
	}
	new e = g_tk_i[id][0][TK_EDICT];
	if (e <= 0 || !entref_alive(e, g_tk_i[id][0][TK_EDICT_SERIAL]) || !combat_is_enemy(id, e))
	{
		return;
	}
	new s = squad_get(g_sq_i[id][SQ_SQUAD]);
	if (s != -1)
	{
		g_squad_formation[s] = FORM_VEE;
	}
}

// Bot::squad_modes (Think's squad modes): prone (TS's dive key) and crouch. The "can't shoot"
// condition it sets is cleared right after, before any task reads it, so holding fire never
// works (kept).
stock squad_modes(id)
{
	new s = squad_get(g_sq_i[id][SQ_SQUAD]);
	if (s == -1)
	{
		return;
	}
	new t = g_squad_combat_type[s];
	if (t == COMBAT_PRONE && movement_stunt_ready(id))
	{
		movement_alt(id);
	}
	if (t == COMBAT_CROUCH)
	{
		movement_duck(id);
	}
	if (!g_squad_can_fire[s])
	{
		g_th_i[id][TH_CONDITIONS] |= COND_CANT_SHOOT;
	}
	else
	{
		g_th_i[id][TH_CONDITIONS] &= ~COND_CANT_SHOOT;
	}
}

// Bot::squad_enemy_found (EnemyFound's squad part): a leader's squad goes back to a normal wedge.
stock squad_enemy_found(id)
{
	if (!squad_is_leader(id))
	{
		return;
	}
	new s = squad_get(g_sq_i[id][SQ_SQUAD]);
	if (s == -1)
	{
		return;
	}
	g_squad_formation[s] = FORM_WEDGE;
	new msg[48];
	if (squad_set_combat_type_slot(s, COMBAT_COMBAT, msg, charsmax(msg)))
	{
		squad_push_message(g_squad_leader[s], msg);
	}
}

// Bot::see_leader_again (UpdateVisibles' last step): seeing a leader it isn't following, the bot
// follows again.
stock squad_see_leader_again(id, bool:saw_leader, bool:need_to_follow)
{
	if (!saw_leader || !need_to_follow || !squad_leader_alive(id))
	{
		return;
	}
	new l = g_sq_i[id][SQ_LEADER];
	if (combat_is_enemy(id, l))
	{
		return;
	}
	tasks_add_priority(id, TASK_FOLLOW_LEADER, tasks_new_schedule_id(id), l, g_sq_i[id][SQ_LEADER_SERIAL], 0, 0.0, VEC_ZERO);
	g_sq_i[id][SQ_SAID_IN_POSITION] = 0;
}

// Bot::say_in_position (SayInPosition).
static squad_say_in_position(id, Float:now)
{
	if (!g_sq_i[id][SQ_SAID_IN_POSITION] && g_sq_f[id][SQ_NEXT_USE_SAY_MESSAGE] < now)
	{
		bot_action_command(id, "say_team ^"In Position!^"");
		g_sq_i[id][SQ_SAID_IN_POSITION] = 1;
		g_sq_f[id][SQ_NEXT_USE_SAY_MESSAGE] = now + 5.0;
	}
}

// Bot::task_follow_leader (BOT_TASK_FOLLOW_LEADER, the current task): go to the formation place
// (by path when the leader is out of sight), crouch with the leader; a dead leader hands the
// squad on. The task fields and waypoints it changes are tasks' and navigate's; the Rust bot
// changes them here.
stock squad_task_follow_leader(id, &bool:failed)
{
	new Float:now = g_now;
	new sid = g_sq_i[id][SQ_SQUAD];
	if (!squad_leader_alive(id) || sid == -1)
	{
		failed = true;
		return;
	}
	new leader = g_sq_i[id][SQ_LEADER];
	g_lk_i[id][LK_LOOK_TASK] = LOOK_LOOK_AROUND;
	if (g_th_i[id][TH_CONDITIONS] & COND_SQUAD_LEADER_DEAD)
	{
		squad_change_leader_of(sid);
		if (squad_get(sid) == -1)
		{
			squad_clear_bot(id);
		}
		// SetEdict(m_pSquadLeader): the old leader, until the next think finds the new one
		// (none after ClearSquad).
		if (g_tk_count[id] > 0)
		{
			g_tk_i[id][0][TK_EDICT] = g_sq_i[id][SQ_LEADER];
			g_tk_i[id][0][TK_EDICT_SERIAL] = g_sq_i[id][SQ_LEADER_SERIAL];
		}
		return;
	}
	new s = squad_get(sid);
	if (s == -1)
	{
		failed = true;
		return;
	}
	new Float:to[3];
	squad_formation_vector(sid, id, to);
	new bool:find_path = squad_distance_from(id, to, true) > g_squad_spread[s];
	if (!find_path)
	{
		squad_say_in_position(id, now);
		movement_stop_moving(id);
	}
	if (!find_path || (g_th_i[id][TH_CONDITIONS] & COND_SEE_SQUAD_LEADER))
	{
		if (find_path)
		{
			movement_set_move_vector(id, to);
		}
		if (world_flags(leader) & TSPF_DUCKING)
		{
			movement_duck(id);
		}
		if (g_tk_count[id] > 0)
		{
			g_tk_i[id][0][TK_FOUND_PATH] = 0;
		}
		g_nv_i[id][NV_CURRENT_WPT] = -1;
		g_nv_i[id][NV_GOAL_WPT] = -1;
		g_nv_i[id][NV_PREV_WPT] = -1;
	}
	else if (g_tk_count[id] > 0 && !g_tk_i[id][0][TK_FOUND_PATH])
	{
		tasks_add_priority(id, TASK_FIND_PATH, g_tk_i[id][0][TK_SCHEDULE_ID], leader, g_sq_i[id][SQ_LEADER_SERIAL], -1, 0.0, VEC_ZERO);
		new k = tasks_find(id, TASK_FOLLOW_LEADER);
		if (k != -1)
		{
			g_tk_i[id][k][TK_FOUND_PATH] = 1;
		}
	}
	else
	{
		failed = true;
	}
}

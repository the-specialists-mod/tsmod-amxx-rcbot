// rcbot/menu.inc: RCBot's menus (bot_menu.cpp), as TS gets them.
// Owner: social. Ports config/menu.rs (MenuId, MenuAction, MenuItemKind, MenuItem, Menu get /
// render / show, Activated, activate) and host api.rs show_menu (UTIL_ShowMenu: the game's
// ShowMenu message). The functions items run (BotMenu_Func_*) are commands.inc's
// commands_menu_action.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Every menu has items on keys 1-4 and "Exit" on 5. menuselect reaches menu_select through
// commands.inc (a client command, before AMX Mod X's own menus).

#if defined _rcbot_menu_included
	#endinput
#endif
#define _rcbot_menu_included

// menu.rs MenuId (eBotMenus on TS).
enum
{
	MENU_WAYPOINT_MAIN = 0,
	MENU_WAYPOINT_TEAM_SPEC,
	MENU_WAYPOINT_GIVE_FLAGS,
	MENU_WAYPOINT_GIVE_FLAGS2,
	MENU_WAYPOINT_GIVE_FLAGS3,
	MENU_WAYPOINT_GIVE_FLAGS4,
	MENU_WAYPOINT_GIVE_FLAGS5,
	MENU_WAYPOINT_GIVE_FLAGS6,
	MENU_WAYPOINT_GIVE_FLAGS7,
	MENU_WAYPOINT_EDIT_PATHS,
	MENU_WAYPOINT_CONFIRM_DELETE,
	MENU_BOT_MAIN,
	MENU_SQUAD,
	MENU_SQUAD_FORMATION,
	MENU_SQUAD_SPREAD,
	MENU_SQUAD_OPTIONS1,
	MENU_SQUAD_FORMATION2,
	MENU_SQUAD_MODES1
};

// menu.rs MenuAction (BotMenu_Func_*).
enum
{
	MA_TOGGLE_FLAG = 0,			// with the flag bits
	MA_TEAM_WAYPOINT,
	MA_UNREACHABLE_WAYPOINT,
	MA_REMOVE_ALL_PATHS,
	MA_REMOVE_PATHS_TO,
	MA_REMOVE_PATHS_FROM,
	MA_DELETE_WAYPOINTS,
	MA_ADD_BOT,
	MA_KICK_BOT,
	MA_MAKE_SQUAD,
	MA_SQUAD_LEAVE,
	MA_SQUAD_REMOVE,
	MA_SQUAD_REMOVE_ALL_BOT_SQUADS,
	MA_SQUAD_MODE1,
	MA_SQUAD_FORM,
	MA_SQUAD_FORM2,
	MA_SQUAD_SPREAD
};

// menu.rs MenuItemKind.
enum { MI_NONE = 0, MI_NEXT, MI_ACTION, MI_EXIT };

// menu.rs Activated.
enum { ACTIVATED_NOTHING = 0, ACTIVATED_CLOSED, ACTIVATED_OPENED, ACTIVATED_RUN };

// Menu::get's caption.
stock menu_caption(menu, out[], len)
{
	static const captions[][] = {"Waypoint Menu", "Team Specific", "Give Flags", "Give More Flags...", "Give More Flags...",
		"Give More Flags...", "Give More Flags...", "More Waypoint Flags...", "More Waypoint Flags...", "Edit Paths",
		"Clear All Waypoints?", "Bot Menu", "Edit Squad", "Squad Formation", "Squad Formation", "More Squad Options",
		"More Squad Formations", "Squad Modes"};
	copy(out, len, captions[menu]);
}

// Menu::get's item on key: its kind (MI_NONE: no item), caption, and argument (the next menu,
// or the action with its flag bits).
stock menu_item(menu, key, caption[], len, &arg, &bits)
{
	arg = 0;
	bits = 0;
	if (key == 5)
	{
		copy(caption, len, "Exit");
		return MI_EXIT;
	}
	if (key < 1 || key > 4)
	{
		return MI_NONE;
	}
	new k = key - 1;
	switch (menu)
	{
		case MENU_WAYPOINT_MAIN:
		{
			static const c[4][] = {"Team Specific Flags", "Give Other Flags", "Edit Paths", "Clear Waypoints"};
			static const n[4] = {MENU_WAYPOINT_TEAM_SPEC, MENU_WAYPOINT_GIVE_FLAGS, MENU_WAYPOINT_EDIT_PATHS, MENU_WAYPOINT_CONFIRM_DELETE};
			copy(caption, len, c[k]);
			arg = n[k];
			return MI_NEXT;
		}
		case MENU_WAYPOINT_CONFIRM_DELETE:
		{
			if (k == 0)
			{
				copy(caption, len, "Yes");
				arg = MA_DELETE_WAYPOINTS;
				return MI_ACTION;
			}
			if (k == 1)
			{
				copy(caption, len, "No");
				arg = MENU_WAYPOINT_MAIN;
				return MI_NEXT;
			}
		}
		case MENU_WAYPOINT_TEAM_SPEC:
		{
			formatex(caption, len, "Team %d", key);
			arg = MA_TEAM_WAYPOINT;
			return MI_ACTION;
		}
		case MENU_WAYPOINT_GIVE_FLAGS .. MENU_WAYPOINT_GIVE_FLAGS7:
		{
			return menu_flag_item(menu, k, caption, len, arg, bits);
		}
		case MENU_WAYPOINT_EDIT_PATHS:
		{
			static const c[3][] = {"Remove All Paths", "Remove Paths TO", "Remove Paths FROM"};
			static const a[3] = {MA_REMOVE_ALL_PATHS, MA_REMOVE_PATHS_TO, MA_REMOVE_PATHS_FROM};
			if (k < 3)
			{
				copy(caption, len, c[k]);
				arg = a[k];
				return MI_ACTION;
			}
		}
		case MENU_BOT_MAIN:
		{
			if (k < 2)
			{
				copy(caption, len, k == 0 ? "Add random bot" : "Kick random bot");
				arg = k == 0 ? MA_ADD_BOT : MA_KICK_BOT;
				return MI_ACTION;
			}
		}
		case MENU_SQUAD:
		{
			static const c[4][] = {"Edit Formation", "Edit Member Spread", "Edit Modes", "More Options"};
			static const n[4] = {MENU_SQUAD_FORMATION, MENU_SQUAD_SPREAD, MENU_SQUAD_MODES1, MENU_SQUAD_OPTIONS1};
			copy(caption, len, c[k]);
			arg = n[k];
			return MI_NEXT;
		}
		case MENU_SQUAD_OPTIONS1:
		{
			static const c[4][] = {"Make Squad", "Leave Squad", "Remove Squad", "Remove All Bot Squads"};
			static const a[4] = {MA_MAKE_SQUAD, MA_SQUAD_LEAVE, MA_SQUAD_REMOVE, MA_SQUAD_REMOVE_ALL_BOT_SQUADS};
			copy(caption, len, c[k]);
			arg = a[k];
			return MI_ACTION;
		}
		case MENU_SQUAD_MODES1:
		{
			static const c[4][] = {"Open/Hold Fire", "Keep Low/Stand up", "Stealth/Normal", "Return to formation"};
			copy(caption, len, c[k]);
			arg = MA_SQUAD_MODE1;
			return MI_ACTION;
		}
		case MENU_SQUAD_FORMATION:
		{
			static const c[4][] = {"Wedge", "Line", "Column", "More"};
			copy(caption, len, c[k]);
			if (k == 3)
			{
				arg = MENU_SQUAD_FORMATION2;
				return MI_NEXT;
			}
			arg = MA_SQUAD_FORM;
			return MI_ACTION;
		}
		case MENU_SQUAD_FORMATION2:
		{
			static const c[4][] = {"Echelon Left", "Echelon Right", "Vee", "Back"};
			copy(caption, len, c[k]);
			if (k == 3)
			{
				arg = MENU_SQUAD_FORMATION;
				return MI_NEXT;
			}
			arg = MA_SQUAD_FORM2;
			return MI_ACTION;
		}
		case MENU_SQUAD_SPREAD:
		{
			// Upstream captions the spread menu "Squad Formation" too.
			if (k < 2)
			{
				copy(caption, len, k == 0 ? "Wider" : "Narrower");
				arg = MA_SQUAD_SPREAD;
				return MI_ACTION;
			}
		}
	}
	caption[0] = 0;
	return MI_NONE;
}

// The flag menus (SetupMenus' MOD_TS captions: bit 18 is "Stunt"; "Scientist Point" is bit 11
// like "Important Waypoint", "Barney Point" bit 12).
static menu_flag_item(menu, k, caption[], len, &arg, &bits)
{
	static const c[7][4][] = {
		{"Jump", "Crouch-Jump", "Crouch", "More Flags"},
		{"Lift Button", "Stunt", "Fly/Grapple", "More Flags"},
		{"Teleport", "Tank", "Wait For Lift", "More Flags"},
		{"End Of Level/Objective", "Stay Close To", "Opens Later", "More Flags"},
		{"Human Tower", "Un-Reachable", "Ladder", "More Flags"},
		{"Important Waypoint", "Pushable Point", "Scientist Point", "More Flags"},
		{"Barney Point", "Check for lift", "Defend Zone", ""}
	};
	static const f[7][3] = {
		{NAV_FL_JUMP, NAV_FL_CROUCHJUMP, NAV_FL_CROUCH},
		{NAV_FL_LIFT, NAV_FL_STUNT, NAV_FL_FLY},
		{NAV_FL_TELEPORT, NAV_FL_TANK, NAV_FL_WAIT_FOR_LIFT},
		{NAV_FL_ENDLEVEL, NAV_FL_STAY_NEAR, NAV_FL_OPENS_LATER},
		{NAV_FL_HUMAN_TOWER, 0, NAV_FL_LADDER},
		{NAV_FL_IMPORTANT, NAV_FL_PUSHABLE, NAV_FL_IMPORTANT},
		{NAV_FL_RESCUE, NAV_FL_CHECK_LIFT, NAV_FL_DEFEND_ZONE}
	};
	new m = menu - MENU_WAYPOINT_GIVE_FLAGS;
	copy(caption, len, c[m][k]);
	if (k == 3)
	{
		if (m == 6)
		{
			return MI_NONE;
		}
		arg = menu + 1;
		return MI_NEXT;
	}
	if (m == 4 && k == 1)
	{
		arg = MA_UNREACHABLE_WAYPOINT;
		return MI_ACTION;
	}
	arg = MA_TOGGLE_FLAG;
	bits = f[m][k];
	return MI_ACTION;
}

// Menu::render (CBotMenu::Render): the key mask (1 << (key - 1)) and the text
// ("<caption>\n-----\nOptions:\n", then "<key>. <caption>" with "..." for sub-menus, each line
// cut to 63 bytes and the whole to 511).
stock menu_render(menu, text[], len, &slots)
{
	new caption[64], line[64], arg, bits;
	slots = 0;
	menu_caption(menu, caption, charsmax(caption));
	new n = formatex(text, len, "%s^n-----^nOptions:^n", caption);
	for (new key = 1; key <= 5; key++)
	{
		new kind = menu_item(menu, key, caption, charsmax(caption), arg, bits);
		if (kind == MI_NONE)
		{
			continue;
		}
		slots |= 1 << (key - 1);
		formatex(line, charsmax(line), "%d. %s%s^n", key, caption, kind == MI_NEXT ? "..." : "");
		n += formatex(text[n], len - n, "%s", line);
	}
	return n;
}

// Menu::show: the menu becomes the client's open menu and is rendered to the player with
// UTIL_ShowMenu(player, slots, -1, false, text) (the host's Engine::show_menu).
stock menu_show(player, menu_id)
{
	g_cl_i[player][CL_MENU] = menu_id;
	static text[512];
	new slots;
	menu_render(menu_id, text, charsmax(text), slots);
	if (!is_user_connected(player) || is_user_bot(player))
	{
		return;
	}
	new msg = get_user_msgid("ShowMenu");
	if (msg <= 0)
	{
		return;
	}
	message_begin(MSG_ONE, msg, _, player);
	write_short(slots);
	write_char(-1);
	write_byte(0);
	write_string(text);
	message_end();
}

// menu.rs activate (CBotMenu::Activate) on the client's open menu: a sub-menu opens (and is
// shown), an action closes the menu and runs (commands_menu_action), Exit closes it. Returns
// ACTIVATED_*.
stock menu_select(player, key)
{
	new menu = g_cl_i[player][CL_MENU];
	if (menu == -1 || key < 0 || key >= 10)
	{
		return ACTIVATED_NOTHING;
	}
	new caption[64], arg, bits;
	switch (menu_item(menu, key, caption, charsmax(caption), arg, bits))
	{
		case MI_NEXT:
		{
			menu_show(player, arg);
			return ACTIVATED_OPENED;
		}
		case MI_ACTION:
		{
			g_cl_i[player][CL_MENU] = -1;
			commands_menu_action(player, arg, bits);
			return ACTIVATED_RUN;
		}
		case MI_EXIT:
		{
			g_cl_i[player][CL_MENU] = -1;
			return ACTIVATED_CLOSED;
		}
	}
	return ACTIVATED_NOTHING;
}

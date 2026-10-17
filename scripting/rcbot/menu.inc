// rcbot/menu.inc: RCBot's menus (bot_menu.cpp).
// Owner: social. Ports config/menu.rs (MenuId, MenuAction, MenuItemKind, MenuItem, Menu get / render / show, Activated, activate).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(menu).

#if defined _rcbot_menu_included
	#endinput
#endif
#define _rcbot_menu_included

// TODO(menu): Menu::show: render the menu for the client (show_menu).
stock menu_show(player, menu_id)
{
	#pragma unused player, menu_id
}

// TODO(menu): menu.rs activate: a key pressed in the menu (menuselect).
stock menu_select(player, key)
{
	#pragma unused player, key
}

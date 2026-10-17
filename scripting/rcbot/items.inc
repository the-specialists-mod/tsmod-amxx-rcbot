// rcbot/items.inc: the map's items as the bot knows them (port addition, design §4).
// Owner: movement. Ports bot/items.rs (KnownItem there_at, ItemMemory iter / get / untakeable / ignored / scan / learn / learn_item / look / seen / taken / touched; SCAN_INTERVAL, DEFAULT_WEAPON_RESPAWN, DEFAULT_POWERUP_RESPAWN, UNTAKEABLE_FOR, TAKE_TIME, SEE_ITEM_RANGE), think.rs Bot::item_taken.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(items). The frame's item list: world_items / world_item_field.

#if defined _rcbot_items_included
	#endinput
#endif
#define _rcbot_items_included

#define ITEMS_SCAN_INTERVAL		5.0
#define ITEMS_UNTAKEABLE_FOR	30.0
#define ITEMS_SEE_RANGE			1200.0

// TODO(items): ItemMemory::default (a new bot).
stock items_bot_init(id)
{
	g_it_count[id] = 0;
}

// TODO(items): think.rs Bot::item_taken (the port's item_pickup): player took ent.
stock items_taken(id, player, ent)
{
	#pragma unused id, player, ent
}

// TODO(items): ItemMemory::scan (the list now and then).
stock items_scan(id)
{
	#pragma unused id
}

// TODO(items): ItemMemory::look (the items in view: there or gone).
stock items_look(id)
{
	#pragma unused id
}

// TODO(items): ItemMemory::get: the slot of (ent, serial), -1 none.
stock items_find(id, ent, serial)
{
	#pragma unused id, ent, serial
	return -1;
}

// TODO(items): ItemMemory::untakeable.
stock items_untakeable(id, ent, serial)
{
	#pragma unused id, ent, serial
}

// TODO(items): ItemMemory::ignored.
stock bool:items_ignored(id, ent, serial)
{
	#pragma unused id, ent, serial
	return false;
}

// TODO(items): ItemMemory::touched.
stock items_touched(id, ent, serial)
{
	#pragma unused id, ent, serial
}

// rcbot/hearing.inc: what the bot hears (port addition, design §4).
// Owner: movement. Ports bot/hearing.rs (Heard, Hearing add / tick / iter / investigate / newest_new; HEARD_FOR, SAME_SOUND, MAX_SOUNDS, LOUD_STEPS, LOUD_GUNFIRE, GUNFIRE_RANGE, SILENCED_RANGE), events.rs SoundKind::of_sample / is_heard and Bot::hear_sound, think.rs Bot::hear_gunfire, manager.rs sound / weapon_fired's per-bot part.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(hearing). hearing_sound_kind answers SOUND_UNKNOWN, so manager_sound stops at once.

#if defined _rcbot_hearing_included
	#endinput
#endif
#define _rcbot_hearing_included

#define HEARD_FOR			8.0
#define LOUD_STEPS			1.0
#define LOUD_GUNFIRE		2.0
#define GUNFIRE_RANGE		1500.0
#define SILENCED_RANGE		350.0

// TODO(hearing): hearing.rs Hearing::default (a new bot).
stock hearing_bot_init(id)
{
	g_hr_count[id] = 0;
}

// TODO(hearing): events.rs SoundKind::of_sample filtered by is_heard: SOUND_UNKNOWN for a sound no bot reacts to. Runs per game sound: compare prefixes cheaply.
stock hearing_sound_kind(const sample[])
{
	#pragma unused sample
	return SOUND_UNKNOWN;
}

// TODO(hearing): events.rs Bot::hear_sound.
stock hearing_hear_sound(id, kind, ent, const Float:origin[3])
{
	#pragma unused id, kind, ent, origin
}

// TODO(hearing): think.rs Bot::hear_gunfire (the port's weapon_fired).
stock hearing_hear_gunfire(id, shooter, const Float:origin[3], bool:silenced)
{
	#pragma unused id, shooter, origin, silenced
}

// TODO(hearing): hearing.rs Hearing::add (merges a sound within SAME_SOUND).
stock hearing_add(id, const Float:origin[3], Float:loud, source, source_serial)
{
	#pragma unused id, origin, loud, source, source_serial
}

// TODO(hearing): hearing.rs Hearing::tick (drops sounds older than HEARD_FOR).
stock hearing_tick(id)
{
	#pragma unused id
}

// TODO(hearing): hearing.rs Hearing::investigate.
stock hearing_investigate(id, const Float:origin[3])
{
	#pragma unused id, origin
}

// TODO(hearing): hearing.rs Hearing::newest_new: its slot, -1 none.
stock hearing_newest_new(id)
{
	#pragma unused id
	return -1;
}

// rcbot/botcam.inc: the bot camera (CBotCam).
// Owner: social. Ports bot/botcam.rs (CamState, Camera, BotCam clear / is_working / current_bot / set_current_bot / tune_in_error / think / move_camera), host bots.rs botcam_frame / spawn_camera / botcam_tune_in, manager.rs frame's other_has_enemy.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(botcam).

#if defined _rcbot_botcam_included
	#endinput
#endif
#define _rcbot_botcam_included

enum { CAM_NONE = 0, CAM_BOT, CAM_ENEMY, CAM_WAYPOINT, CAM_FIRST_PERSON };

stock g_cam_entity;				// m_pCameraEdict, 0 none
stock g_cam_current_bot;		// 0 none
stock g_cam_state;				// CAM_*
stock bool:g_cam_tried_to_spawn;
stock bool:g_cam_other_has_enemy;
stock Float:g_cam_next_change_bot_time;
stock Float:g_cam_next_change_state;

// TODO(botcam): host bots.rs botcam_frame (StartFrame's m_BotCam.Think / Spawn).
stock botcam_frame()
{
}

// TODO(botcam): manager.rs frame: BotCam::other_has_enemy for a current bot other than the one thinking.
stock botcam_set_other_has_enemy(id)
{
	#pragma unused id
}

// TODO(botcam): BotCam::clear (ServerActivate / Deactivate).
stock botcam_clear()
{
}

// TODO(botcam): host bots.rs botcam_tune_in (TuneIn): true if player now watches the camera.
stock bool:botcam_tune_in(player)
{
	#pragma unused player
	return false;
}

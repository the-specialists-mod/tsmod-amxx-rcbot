// rcbot/tasks.inc: the task queue (CBotTask, CBotTasks) and schedules.
// Owner: think. Ports bot/tasks.rs (EntRef, TaskKind::description, Task new / simple / with_timeout / timed_out / set_time_to_complete / same, Tasks iter / current / is_empty / flush / has_task / has_same_task / get_task / has_schedule / new_schedule_id / finish_schedule / finish_schedule_desc / give_schedule_description / set_time_to_complete_schedule / remove_timed_out_schedules / remove_same_task / remove_similar_task / add_to_end / add_to_front / add_priority / remove_paths / finished_current / forget_edict), mod.rs Bot::add_task / add_priority_task.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: flush / count / current kind are ported; the rest is TODO(tasks). A task is slot k of g_tk_* (state.inc), current first.

#if defined _rcbot_tasks_included
	#endinput
#endif
#define _rcbot_tasks_included

// Tasks::flush.
stock tasks_flush(id)
{
	g_tk_count[id] = 0;
}

// Tasks::is_empty / len.
stock tasks_count(id)
{
	return g_tk_count[id];
}

// Tasks::current's kind, TASK_NONE for none.
stock tasks_current_kind(id)
{
	return g_tk_count[id] > 0 ? g_tk_i[id][0][TK_KIND] : TASK_NONE;
}

// TODO(tasks): Task::new + Tasks::add_to_end (AddTask): the new task's slot, -1 when full.
stock tasks_add(id, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	#pragma unused id, kind, schedule_id, edict, edict_serial, int_info, float_info, vec
	return -1;
}

// TODO(tasks): Tasks::add_priority (AddPriorityTask).
stock tasks_add_priority(id, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	#pragma unused id, kind, schedule_id, edict, edict_serial, int_info, float_info, vec
	return -1;
}

// TODO(tasks): Tasks::add_to_front.
stock tasks_add_to_front(id, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	#pragma unused id, kind, schedule_id, edict, edict_serial, int_info, float_info, vec
	return -1;
}

// TODO(tasks): Tasks::has_task.
stock bool:tasks_has(id, kind)
{
	#pragma unused id, kind
	return false;
}

// TODO(tasks): Tasks::get_task: its slot, -1 none.
stock tasks_find(id, kind)
{
	#pragma unused id, kind
	return -1;
}

// TODO(tasks): Tasks::has_schedule.
stock bool:tasks_has_schedule(id, desc)
{
	#pragma unused id, desc
	return false;
}

// TODO(tasks): Tasks::new_schedule_id.
stock tasks_new_schedule_id(id)
{
	#pragma unused id
	return 1;
}

// TODO(tasks): Tasks::finish_schedule.
stock bool:tasks_finish_schedule(id, schedule_id)
{
	#pragma unused id, schedule_id
	return false;
}

// TODO(tasks): Tasks::finish_schedule_desc.
stock bool:tasks_finish_schedule_desc(id, desc)
{
	#pragma unused id, desc
	return false;
}

// TODO(tasks): Tasks::give_schedule_description.
stock tasks_give_schedule_description(id, schedule_id, desc)
{
	#pragma unused id, schedule_id, desc
}

// TODO(tasks): Tasks::set_time_to_complete_schedule.
stock tasks_set_time_to_complete_schedule(id, schedule_id, Float:secs)
{
	#pragma unused id, schedule_id, secs
}

// TODO(tasks): Tasks::remove_timed_out_schedules.
stock tasks_remove_timed_out_schedules(id)
{
	#pragma unused id
}

// TODO(tasks): Tasks::remove_similar_task.
stock tasks_remove_similar(id, kind)
{
	#pragma unused id, kind
}

// TODO(tasks): Tasks::remove_paths.
stock tasks_remove_paths(id)
{
	#pragma unused id
}

// TODO(tasks): Tasks::finished_current (done or failed; find-path hands its path on).
stock tasks_finished_current(id, bool:failed)
{
	#pragma unused id, failed
}

// TODO(tasks): Tasks::forget_edict.
stock tasks_forget_edict(id, ent, serial)
{
	#pragma unused id, ent, serial
}

// TODO(tasks): Task::timed_out.
stock bool:tasks_timed_out(id, slot)
{
	#pragma unused id, slot
	return false;
}

// TODO(tasks): TaskKind::description (CBotTask::_getTaskDescription) into out.
stock tasks_description(kind, out[], len)
{
	#pragma unused kind
	copy(out, len, "BOT_TASK_NONE");
}

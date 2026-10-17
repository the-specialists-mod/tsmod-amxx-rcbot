// rcbot/tasks.inc: the task queue (CBotTask, CBotTasks) and schedules.
// Owner: think. Ports bot/tasks.rs (EntRef, TaskKind::description, Task new / simple / with_timeout / timed_out / set_time_to_complete / same, Tasks iter / current / is_empty / flush / has_task / has_same_task / get_task / has_schedule / new_schedule_id / finish_schedule / finish_schedule_desc / give_schedule_description / set_time_to_complete_schedule / remove_timed_out_schedules / remove_same_task / remove_similar_task / add_to_end / add_to_front / add_priority / remove_paths / finished_current / forget_edict), mod.rs Bot::add_task / add_priority_task.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// A task is slot k of g_tk_* (state.inc), current first (upstream's dataQueue front, the Rust
// VecDeque's front). The queue holds RC_MAX_TASKS: a task added to the end of a full queue is
// dropped; one added to the front of a full queue drops the last task instead (the newest
// plans go before the task the bot must do now; the Rust queue is unbounded, so this only
// matters past RC_MAX_TASKS tasks).

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

// Task::new into slot k (no time limit, no schedule description, no path).
static stock tasks_fill(id, k, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	g_tk_i[id][k][TK_KIND] = kind;
	g_tk_i[id][k][TK_INT] = int_info;
	g_tk_i[id][k][TK_EDICT] = edict;
	g_tk_i[id][k][TK_EDICT_SERIAL] = edict ? edict_serial : 0;
	g_tk_i[id][k][TK_SCHEDULE_ID] = schedule_id;
	g_tk_i[id][k][TK_SCHEDULE_DESC] = SCHED_NONE;
	g_tk_i[id][k][TK_FOUND_PATH] = 0;
	g_tk_f[id][k][TK_FLOAT] = float_info;
	g_tk_f[id][k][TK_TIME_TO_COMPLETE] = -1.0;
	vec_copy(vec, g_tk_vec[id][k]);
}

// Slot from into slot to.
static stock tasks_copy(id, from, to)
{
	for (new Tk_I:f; f < Tk_I; f++) g_tk_i[id][to][f] = g_tk_i[id][from][f];
	for (new Tk_F:f; f < Tk_F; f++) g_tk_f[id][to][f] = g_tk_f[id][from][f];
	vec_copy(g_tk_vec[id][from], g_tk_vec[id][to]);
}

// VecDeque::remove(k): the tasks after k move up.
static stock tasks_remove_at(id, k)
{
	new n = g_tk_count[id];
	for (new j = k; j < n - 1; j++)
	{
		tasks_copy(id, j + 1, j);
	}
	g_tk_count[id] = n - 1;
}

// Task::new + Tasks::add_to_end (AddTask): the new task's slot, -1 when full.
stock tasks_add(id, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	new k = g_tk_count[id];
	if (k >= RC_MAX_TASKS)
	{
		rc_debug(DBG_THINK, "bot %d: task queue full, task %d dropped", id, kind);
		return -1;
	}
	tasks_fill(id, k, kind, schedule_id, edict, edict_serial, int_info, float_info, vec);
	g_tk_count[id] = k + 1;
	return k;
}

// Tasks::add_priority (AddPriorityTask): RemovePaths then AddTaskToFront. Slot 0, -1 never.
stock tasks_add_priority(id, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	tasks_remove_paths(id);
	return tasks_add_to_front(id, kind, schedule_id, edict, edict_serial, int_info, float_info, vec);
}

// Tasks::add_to_front (AddTaskToFront): an equal task (and the rest of its schedule) goes first.
stock tasks_add_to_front(id, kind, schedule_id, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	tasks_remove_same(id, kind, edict, edict ? edict_serial : 0, int_info, float_info, vec);
	new n = g_tk_count[id];
	if (n >= RC_MAX_TASKS)
	{
		rc_debug(DBG_THINK, "bot %d: task queue full, last task %d dropped", id, g_tk_i[id][n - 1][TK_KIND]);
		n = RC_MAX_TASKS - 1;
	}
	for (new j = n; j > 0; j--)
	{
		tasks_copy(id, j - 1, j);
	}
	tasks_fill(id, 0, kind, schedule_id, edict, edict_serial, int_info, float_info, vec);
	g_tk_count[id] = n + 1;
	return 0;
}

// Task::same (CBotTask::operator==): kind, int, float, vector and edict.
static stock bool:tasks_same(id, k, kind, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	return g_tk_i[id][k][TK_KIND] == kind && g_tk_i[id][k][TK_INT] == int_info && g_tk_f[id][k][TK_FLOAT] == float_info
		&& g_tk_vec[id][k][0] == vec[0] && g_tk_vec[id][k][1] == vec[1] && g_tk_vec[id][k][2] == vec[2]
		&& g_tk_i[id][k][TK_EDICT] == edict && g_tk_i[id][k][TK_EDICT_SERIAL] == edict_serial;
}

// Tasks::remove_same_task (RemoveSameTask): equal tasks go, and their schedules.
static stock tasks_remove_same(id, kind, edict, edict_serial, int_info, Float:float_info, const Float:vec[3])
{
	for (new k = 0; k < g_tk_count[id]; )
	{
		if (!tasks_same(id, k, kind, edict, edict_serial, int_info, float_info, vec))
		{
			k++;
			continue;
		}
		new sched = g_tk_i[id][k][TK_SCHEDULE_ID];
		tasks_remove_at(id, k);
		if (sched != 0)
		{
			tasks_finish_schedule(id, sched);
		}
		// Search again from the start, as the Rust loop's position() does.
		k = 0;
	}
}

// Tasks::has_task (HasTask(eBotTask)).
stock bool:tasks_has(id, kind)
{
	return tasks_find(id, kind) != -1;
}

// Tasks::get_task (GetTask): the first task of a kind, its slot, -1 none.
stock tasks_find(id, kind)
{
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_KIND] == kind)
		{
			return k;
		}
	}
	return -1;
}

// Tasks::has_schedule (HasSchedule).
stock bool:tasks_has_schedule(id, desc)
{
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_DESC] == desc)
		{
			return true;
		}
	}
	return false;
}

// Tasks::new_schedule_id (GetNewScheduleId): the lowest id from 1 no task uses.
stock tasks_new_schedule_id(id)
{
	new sched = 1;
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_ID] == sched)
		{
			sched++;
			k = -1;
		}
	}
	return sched;
}

// Tasks::finish_schedule (FinishSchedule(int)): every task of the schedule goes; true if any went.
stock bool:tasks_finish_schedule(id, schedule_id)
{
	new n = g_tk_count[id], kept = 0;
	for (new k = 0; k < n; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_ID] != schedule_id)
		{
			if (kept != k)
			{
				tasks_copy(id, k, kept);
			}
			kept++;
		}
	}
	g_tk_count[id] = kept;
	return kept != n;
}

// Tasks::finish_schedule_desc (FinishSchedule(eScheduleDesc)).
stock bool:tasks_finish_schedule_desc(id, desc)
{
	new n = g_tk_count[id], kept = 0;
	for (new k = 0; k < n; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_DESC] != desc)
		{
			if (kept != k)
			{
				tasks_copy(id, k, kept);
			}
			kept++;
		}
	}
	g_tk_count[id] = kept;
	return kept != n;
}

// Tasks::give_schedule_description (GiveSchedIdDescription).
stock tasks_give_schedule_description(id, schedule_id, desc)
{
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_ID] == schedule_id)
		{
			g_tk_i[id][k][TK_SCHEDULE_DESC] = desc;
		}
	}
}

// Tasks::set_time_to_complete_schedule: every task of the schedule times out secs from now
// (upstream added the time twice, design §5.50; fixed as the Rust port, §4).
stock tasks_set_time_to_complete_schedule(id, schedule_id, Float:secs)
{
	new Float:until = g_now + secs;
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_ID] == schedule_id)
		{
			g_tk_f[id][k][TK_TIME_TO_COMPLETE] = until;
		}
	}
}

// Tasks::remove_timed_out_schedules (RemoveTimedOutSchedules): fails the schedules of timed-out
// tasks other than the current task's.
stock tasks_remove_timed_out_schedules(id)
{
	if (g_tk_count[id] == 0)
	{
		return;
	}
	new ignore = g_tk_i[id][0][TK_SCHEDULE_ID];
	for (;;)
	{
		new found = -1;
		for (new k = 0; k < g_tk_count[id]; k++)
		{
			if (tasks_timed_out(id, k) && g_tk_i[id][k][TK_SCHEDULE_ID] != ignore)
			{
				found = k;
				break;
			}
		}
		if (found == -1 || !tasks_finish_schedule(id, g_tk_i[id][found][TK_SCHEDULE_ID]))
		{
			break;
		}
	}
}

// Tasks::remove_similar_task (RemoveSimilarTask): every task of a kind goes, and their schedules.
stock tasks_remove_similar(id, kind)
{
	new k;
	while ((k = tasks_find(id, kind)) != -1)
	{
		new sched = g_tk_i[id][k][TK_SCHEDULE_ID];
		tasks_remove_at(id, k);
		if (sched != 0)
		{
			tasks_finish_schedule(id, sched);
		}
	}
}

// Tasks::remove_paths (RemovePaths): every task after the current one forgets its path.
stock tasks_remove_paths(id)
{
	for (new k = 1; k < g_tk_count[id]; k++)
	{
		g_tk_i[id][k][TK_FOUND_PATH] = 0;
	}
}

// Tasks::finished_current (FinishedCurrentTask(bFailed)): drops the current task (and on failure
// its schedule); after a FIND_PATH the next task inherits its path info.
stock tasks_finished_current(id, bool:failed)
{
	if (g_tk_count[id] == 0)
	{
		return;
	}
	new kind = g_tk_i[id][0][TK_KIND], sched = g_tk_i[id][0][TK_SCHEDULE_ID], found = g_tk_i[id][0][TK_FOUND_PATH];
	tasks_remove_at(id, 0);
	if (failed && sched != 0)
	{
		tasks_finish_schedule(id, sched);
	}
	if (kind == TASK_FIND_PATH && g_tk_count[id] > 0)
	{
		tasks_remove_paths(id);
		g_tk_i[id][0][TK_FOUND_PATH] = found;
	}
}

// Tasks::forget_edict: tasks pointing at an entity that is gone forget it.
stock tasks_forget_edict(id, ent, serial)
{
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_EDICT] == ent && g_tk_i[id][k][TK_EDICT_SERIAL] == serial)
		{
			g_tk_i[id][k][TK_EDICT] = 0;
			g_tk_i[id][k][TK_EDICT_SERIAL] = 0;
		}
	}
}

// Task::timed_out (TimedOut).
stock bool:tasks_timed_out(id, slot)
{
	new Float:t = g_tk_f[id][slot][TK_TIME_TO_COMPLETE];
	return t != -1.0 && t < g_now;
}

// Task::set_time_to_complete (SetTimeToComplete): slot times out secs from now.
stock tasks_set_time_to_complete(id, slot, Float:secs)
{
	if (slot >= 0)
	{
		g_tk_f[id][slot][TK_TIME_TO_COMPLETE] = g_now + secs;
	}
}

// TaskKind::description (CBotTask::_getTaskDescription) into out.
stock tasks_description(kind, out[], len)
{
	static const names[TASK_KINDS][] =
	{
		"BOT_TASK_NONE", "BOT_TASK_FIND_PATH", "BOT_TASK_NORMAL_ATTACK", "BOT_TASK_SEARCH_FOR_ENEMY", "BOT_TASK_RELOAD",
		"BOT_TASK_WAIT_AND_FACE_VECTOR", "BOT_TASK_FACE_VECTOR", "BOT_TASK_MOVE_TO_VECTOR", "BOT_TASK_PICKUP_ITEM",
		"BOT_TASK_USE", "BOT_TASK_WAIT", "BOT_TASK_DEFEND", "BOT_TASK_LISTEN_TO_SOUND", "BOT_TASK_CHANGE_WEAPON",
		"BOT_TASK_DROP_WEAPON", "BOT_TASK_CROUCH", "BOT_TASK_PUSH_PUSHABLE", "BOT_TASK_WAIT_FOR_LIFT",
		"BOT_TASK_WAIT_FOR_ENTITY", "BOT_TASK_HUMAN_TOWER", "BOT_TASK_THROW_GRENADE", "BOT_TASK_WAIT_FOR_BOT_AT_WPT",
		"BOT_TASK_FACE_EDICT", "BOT_TASK_FOLLOW_LEADER", "BOT_TASK_TYPE_MESSAGE"
	};
	copy(out, len, kind >= 0 && kind < TASK_KINDS ? names[kind] : names[TASK_NONE]);
}

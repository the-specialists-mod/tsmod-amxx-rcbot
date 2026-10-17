// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The module's exports: `AMXX_*` (from `amxx::export_module!`), the metamod plugin's `Meta_*` and
//! `GiveFnptrsToDll` (from `amxx::export_metamod!`, for the engine table), the native table and
//! the `StartFrame` hook. The only place this crate allows `unsafe` (the macros' generated glue).

use crate::{natives as n, paths as p, state};
use amxx::meta::MetaDef;
use amxx::module::{ModuleDef, nothing};
use amxx::sys::{DLL_FUNCTIONS, enginefuncs_t};

amxx::natives! {
    pub static NATIVES = [
        c"nav_seed" => n::nav_seed,
        c"nav_clock_us" => n::nav_clock_us,
        c"nav_load" => n::nav_load,
        c"nav_save" => n::nav_save,
        c"nav_count" => n::nav_count,
        c"nav_clear" => n::nav_clear,
        c"nav_map" => n::nav_map,
        c"nav_valid" => n::nav_valid,
        c"nav_origin" => n::nav_origin,
        c"nav_flags" => n::nav_flags,
        c"nav_distance" => n::nav_distance,
        c"nav_nearest" => n::nav_nearest,
        c"nav_nearest_scan" => n::nav_nearest_scan,
        c"nav_nearest_flag" => n::nav_nearest_flag,
        c"nav_random_flag" => n::nav_random_flag,
        c"nav_random_flag_near" => n::nav_random_flag_near,
        c"nav_nearest_aiming" => n::nav_nearest_aiming,
        c"nav_find_reachable" => n::nav_find_reachable,
        c"nav_reachable" => n::nav_reachable,
        c"nav_in_pvs" => n::nav_in_pvs,
        c"nav_paths_out" => n::nav_paths_out,
        c"nav_paths_in" => n::nav_paths_in,
        c"nav_has_path" => n::nav_has_path,
        c"nav_cover" => n::nav_cover,
        c"nav_buckets_around" => n::nav_buckets_around,
        c"nav_nearby" => n::nav_nearby,
        c"nav_visible" => n::nav_visible,
        c"nav_vis_pending" => n::nav_vis_pending,
        c"nav_vis_budget" => n::nav_vis_budget,
        c"nav_vis_rebuild" => n::nav_vis_rebuild,
        c"nav_vis_finish" => n::nav_vis_finish,
        c"nav_heatmap_load" => n::nav_heatmap_load,
        c"nav_heatmap_save" => n::nav_heatmap_save,
        c"nav_heatmap_add_death" => n::nav_heatmap_add_death,
        c"nav_heatmap_add_kill" => n::nav_heatmap_add_kill,
        c"nav_heatmap_add_damage" => n::nav_heatmap_add_damage,
        c"nav_heatmap_danger" => n::nav_heatmap_danger,
        c"nav_heatmap_cover_penalty" => n::nav_heatmap_cover_penalty,
        c"nav_heatmap_ambush" => n::nav_heatmap_ambush,
        c"nav_add" => n::nav_add,
        c"nav_add_aiming" => n::nav_add_aiming,
        c"nav_delete" => n::nav_delete,
        c"nav_delete_near" => n::nav_delete_near,
        c"nav_add_path" => n::nav_add_path,
        c"nav_remove_path" => n::nav_remove_path,
        c"nav_remove_paths_to" => n::nav_remove_paths_to,
        c"nav_set_origin" => n::nav_set_origin,
        c"nav_set_flags" => n::nav_set_flags,
        c"nav_add_flags" => n::nav_add_flags,
        c"nav_clear_flags" => n::nav_clear_flags,
        c"nav_edit_create_path" => n::nav_edit_create_path,
        c"nav_edit_remove_path" => n::nav_edit_remove_path,
        c"nav_auto_waypoint" => n::nav_auto_waypoint,
        c"nav_auto_reset" => n::nav_auto_reset,
        c"nav_can_stand" => n::nav_can_stand,
        c"nav_draw_index" => n::nav_draw_index,
        c"nav_draw_around" => n::nav_draw_around,
        c"nav_draw_path" => n::nav_draw_path,
        c"nav_event_count" => n::nav_event_count,
        c"nav_event_pop" => n::nav_event_pop,
        c"nav_event_clear" => n::nav_event_clear,
        c"nav_path_create" => p::nav_path_create,
        c"nav_path_free" => p::nav_path_free,
        c"nav_path_find" => p::nav_path_find,
        c"nav_path_continue" => p::nav_path_continue,
        c"nav_path_status" => p::nav_path_status,
        c"nav_path_next" => p::nav_path_next,
        c"nav_path_pop" => p::nav_path_pop,
        c"nav_path_push" => p::nav_path_push,
        c"nav_path_length" => p::nav_path_length,
        c"nav_path_get" => p::nav_path_get,
        c"nav_path_contains" => p::nav_path_contains,
        c"nav_path_after" => p::nav_path_after,
        c"nav_path_clear" => p::nav_path_clear,
        c"nav_path_reset" => p::nav_path_reset,
        c"nav_path_fail" => p::nav_path_fail,
        c"nav_path_failed_count" => p::nav_path_failed_count,
        c"nav_path_clear_failed_paths" => p::nav_path_clear_failed_paths,
        c"nav_path_goal_reached" => p::nav_path_goal_reached,
        c"nav_path_failed_goals" => p::nav_path_failed_goals,
        c"nav_path_ignored_goals" => p::nav_path_ignored_goals,
        c"nav_path_clear_failed_goals" => p::nav_path_clear_failed_goals,
        c"nav_path_think" => p::nav_path_think,
        c"nav_path_set_weights" => p::nav_path_set_weights,
        c"nav_path_set_threats" => p::nav_path_set_threats,
        c"nav_path_add_threat" => p::nav_path_add_threat,
        c"nav_path_threats" => p::nav_path_threats,
        c"nav_path_clear_threats" => p::nav_path_clear_threats,
        c"nav_path_set_crowded" => p::nav_path_set_crowded,
        c"nav_path_crowd_from" => p::nav_path_crowd_from,
        c"nav_path_touch" => p::nav_path_touch,
        c"nav_path_since_visit" => p::nav_path_since_visit,
        c"nav_path_roam_goal" => p::nav_path_roam_goal,
    ];
}

const VERSION: &core::ffi::CStr = match core::ffi::CStr::from_bytes_with_nul(concat!(env!("CARGO_PKG_VERSION"), "\0").as_bytes()) {
    Ok(v) => v,
    Err(_) => c"0",
};

const AUTHOR: &core::ffi::CStr = c"Paul \"Cheeseh\" Murphy (RCBot); Rust: tsmod-amxx-rcbot";

fn attach() {
    state::attach(rcbot_amxx_common::DataDir::from_core());
    amxx::core::add_natives(&NATIVES);
}

static MODULE: ModuleDef = ModuleDef {
    name: c"RCBot Nav",
    author: AUTHOR,
    version: VERSION,
    logtag: c"RCBOT_NAV",
    library: c"rcbot_nav",
    libclass: c"",
    reload: false,
    check_game: None,
    attach,
    detach: nothing,
    plugins_loaded: nothing,
    plugins_unloading: nothing,
    // Plugins reload at every map change: their path handles die with them; the waypoints stay
    // until the next map's plugin loads its own.
    plugins_unloaded: state::forget_handles,
};

/// `StartFrame`: the missing visibility rows, a few each frame.
extern "C" fn start_frame() {
    state::frame();
    amxx::meta::ignored();
}

fn dll(t: &mut DLL_FUNCTIONS) {
    t.pfnStartFrame = Some(start_frame);
}

fn no_dll(_: &mut DLL_FUNCTIONS) {}

fn no_engine(_: &mut enginefuncs_t) {}

static META: MetaDef = MetaDef {
    name: c"RCBot Nav",
    version: VERSION,
    date: c"",
    author: AUTHOR,
    url: c"https://github.com/the-specialists-mod/tsmod-amxx-rcbot",
    logtag: c"RCBOT_NAV",
    attach: nothing,
    dll,
    dll_post: no_dll,
    engine: no_engine,
    engine_post: no_engine,
};

amxx::export_module!(MODULE);
amxx::export_metamod!(META);

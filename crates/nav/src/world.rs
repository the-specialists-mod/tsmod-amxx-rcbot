// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. Ports
// tsmod-rcbot-rs's `HostWorld` (crates/host/src/world.rs: `trace_line`, `point_contents`, the
// `BotWorld` entity reads) onto tsmod-amxx-rs's engine wrappers.

//! What `rcbot::nav` asks the engine: [`EngineWorld`] implements `rcbot`'s [`World`] (traces,
//! point contents) and the part of [`BotWorld`] the waypoint editor reads (time, entities, the
//! PVS, sphere searches) with `amxx::engine`, the engine table metamod hands the module.
//!
//! Natives reach it through [`with_world`]; tests install their own world there instead.

use amxx::engine::{Edict, contents, engine, hull, trace_flags};
use rcbot::Vector;
use rcbot::bot::{BotWorld, Entity, EntityKey};
use rcbot::nav::{Glass, Monsters, Trace, World};

fn vector(a: [f32; 3]) -> Vector {
    Vector::new(a[0], a[1], a[2])
}

/// The engine as `rcbot` sees it. Without the engine table (before `GiveFnptrsToDll`) it is an
/// empty world: traces reach their end, everything is air, there are no entities.
#[derive(Clone, Copy, Debug, Default)]
pub struct EngineWorld;

impl EngineWorld {
    fn edict(index: u32) -> Edict {
        engine().map_or(Edict::NULL, |e| e.edict(index as i32))
    }

    fn index(e: Edict) -> Option<u32> {
        if e.is_null() {
            return None;
        }
        engine().and_then(|g| u32::try_from(g.ent_index(e)).ok())
    }

    fn trace(t: amxx::engine::Trace) -> Trace {
        Trace { fraction: t.fraction, end_pos: vector(t.end_pos), hit: Self::index(t.hit) }
    }
}

impl World for EngineWorld {
    /// `UTIL_TraceLine` (`pfnTraceLine`), as the host's `HostWorld::trace_line`.
    fn trace_line(&self, start: Vector, end: Vector, monsters: Monsters, glass: Glass, ignore: Option<u32>) -> Trace {
        let Some(e) = engine() else { return Trace::clear(end) };
        let mut flags = 0;
        if monsters == Monsters::Ignore {
            flags |= trace_flags::IGNORE_MONSTERS;
        }
        if glass == Glass::Ignore {
            flags |= trace_flags::IGNORE_GLASS;
        }
        let ignore = ignore.map_or(Edict::NULL, Self::edict);
        Self::trace(e.trace_line(start.to_array(), end.to_array(), flags, ignore))
    }

    /// `POINT_CONTENTS`.
    fn point_contents(&self, at: Vector) -> i32 {
        engine().map_or(contents::EMPTY, |e| e.point_contents(at.to_array()))
    }
}

impl BotWorld for EngineWorld {
    fn time(&self) -> f32 {
        engine().map_or(0.0, |e| e.time())
    }

    fn frame_time(&self) -> f32 {
        engine().map_or(0.0, |e| e.frametime())
    }

    /// As the host: the highest index, `maxEntities - 1`.
    fn max_entities(&self) -> u32 {
        engine().map_or(1, |e| e.max_entities().max(1) as u32 - 1)
    }

    fn max_clients(&self) -> u32 {
        engine().map_or(0, |e| e.max_clients().max(0) as u32)
    }

    /// The entvars the editor and the auto-waypointer read (`HostWorld::read_entity` without the
    /// port's player and item info, which waypoint editing doesn't use).
    fn entity(&self, index: u32) -> Option<Entity> {
        let e = Self::edict(index);
        if e.is_free() {
            return None;
        }
        Some(Entity {
            index,
            classname: e.classname(),
            netname: e.netname(),
            origin: vector(e.origin()),
            view_ofs: vector(e.view_ofs()),
            absmin: vector(e.absmin()),
            absmax: vector(e.absmax()),
            size: vector(e.size()),
            velocity: vector(e.velocity()),
            angles: vector(e.angles()),
            v_angle: vector(e.v_angle()),
            flags: e.flags(),
            health: e.health(),
            max_health: e.max_health(),
            spawnflags: e.spawnflags(),
            target: e.target(),
            targetname: e.targetname(),
            movedir: vector(e.movedir()),
            frame: e.frame(),
            deadflag: e.deadflag(),
            takedamage: e.takedamage(),
            effects: e.effects(),
            solid: e.solid(),
            movetype: e.movetype(),
            waterlevel: e.waterlevel(),
            maxspeed: e.maxspeed(),
            button: e.button(),
            frags: e.frags(),
            owner: Self::index(e.owner()),
            groundentity: Self::index(e.groundentity()),
            ..Entity::default()
        })
    }

    fn in_pvs(&self, viewer: u32, origin: Vector) -> bool {
        engine().is_none_or(|e| e.in_pvs(e.edict(viewer as i32), origin.to_array()))
    }

    fn entities_in_sphere(&self, origin: Vector, radius: f32) -> Vec<u32> {
        engine().map_or_else(Vec::new, |e| {
            e.entities_in_sphere(origin.to_array(), radius).into_iter().filter_map(|i| u32::try_from(i).ok()).collect()
        })
    }

    fn teamplay(&self) -> bool {
        engine().is_some_and(|e| e.cvar_float(c"mp_teamplay") != 0.0)
    }

    fn find_entities(&self, key: EntityKey, value: &str) -> Vec<u32> {
        let field = match key {
            EntityKey::Classname => "classname",
            EntityKey::Target => "target",
            EntityKey::Targetname => "targetname",
        };
        engine()
            .map_or_else(Vec::new, |e| e.find_entities_by_string(field, value).into_iter().filter_map(|i| u32::try_from(i).ok()).collect())
    }

    fn trace_point_hull(&self, start: Vector, end: Vector, monsters: Monsters, ignore: Option<u32>) -> Trace {
        let Some(e) = engine() else { return Trace::clear(end) };
        let flags = if monsters == Monsters::Ignore { trace_flags::IGNORE_MONSTERS } else { 0 };
        let ignore = ignore.map_or(Edict::NULL, Self::edict);
        Self::trace(e.trace_hull(start.to_array(), end.to_array(), flags, hull::POINT, ignore))
    }
}

#[cfg(test)]
thread_local! {
    static TEST_WORLD: std::cell::RefCell<Option<std::rc::Rc<dyn BotWorld>>> = const { std::cell::RefCell::new(None) };
}

/// Installs the world the natives see on this test's thread (`None`: the engine's, which is empty
/// in tests).
#[cfg(test)]
pub fn set_test_world(w: Option<std::rc::Rc<dyn BotWorld>>) {
    TEST_WORLD.with(|t| *t.borrow_mut() = w);
}

/// Runs `f` with the world: the engine's ([`EngineWorld`]), or a test's.
pub fn with_world<R>(f: impl FnOnce(&dyn BotWorld) -> R) -> R {
    #[cfg(test)]
    if let Some(w) = TEST_WORLD.with(|t| t.borrow().clone()) {
        return f(&*w);
    }
    f(&EngineWorld)
}

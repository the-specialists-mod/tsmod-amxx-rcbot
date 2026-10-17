// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. Exposes RCBot's
// `CGA` (ga.cpp), `CPerceptron` (perceptron.cpp), `NN` (bot_nn.cpp) and `CSom` (som.cpp) through
// tsmod-rcbot-rs's `rcbot::learn`.

//! The generic learners as handles: GAs of float genes, perceptrons, neural networks and
//! self-organising maps, for plugins (the Pawn bot's own learners, or anything else). Files are
//! upstream's byte layouts (`CGA::save`, `CPerceptron::save`, `NN::save`); a SOM has none.

use crate::natives::size;
use crate::state::{State, with};
use amxx::Cell;
use amxx::native::Args;
use rcbot::learn::ga::Individual;
use rcbot::learn::{BotGaValues, Ga, Nn, Perceptron, Reader, Som};
use rcbot_amxx_common::{Handles, ModuleRandom, float_cell, read_floats, write_floats};

/// Largest layer, input count and SOM side `*_create` accepts.
pub const MAX_SIZE: i32 = 1024;

type Field<T> = fn(&mut State) -> (&mut Handles<T>, &mut ModuleRandom);

fn gas(s: &mut State) -> (&mut Handles<Ga<BotGaValues>>, &mut ModuleRandom) {
    (&mut s.gas, &mut s.rng)
}

fn perceptrons(s: &mut State) -> (&mut Handles<Perceptron>, &mut ModuleRandom) {
    (&mut s.perceptrons, &mut s.rng)
}

fn nns(s: &mut State) -> (&mut Handles<Nn>, &mut ModuleRandom) {
    (&mut s.nns, &mut s.rng)
}

fn soms(s: &mut State) -> (&mut Handles<Som>, &mut ModuleRandom) {
    (&mut s.soms, &mut s.rng)
}

/// Runs `f` on the object behind argument 1, or raises the native error.
fn with_obj<T>(a: &mut dyn Args, what: &str, field: Field<T>, f: impl FnOnce(&mut T, &mut ModuleRandom, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| {
        let (objs, rng) = field(s);
        objs.get_mut(h).map(|o| f(o, rng, a))
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid {what} handle {h}"));
        0
    })
}

fn create<T>(field: Field<T>, make: impl FnOnce(&mut ModuleRandom) -> T) -> Cell {
    with(|s| {
        let (objs, rng) = field(s);
        let o = make(rng);
        objs.insert(o)
    })
    .unwrap_or(0)
}

/// `*_free(&handle)`: frees and sets the variable to 0.
fn free<T>(a: &mut dyn Args, what: &str, field: Field<T>) -> Cell {
    let h = a.read_array(1, 1)[0];
    if with(|s| field(s).0.remove(h)).flatten().is_none() {
        a.error(&format!("Invalid {what} handle {h}"));
        return 0;
    }
    a.set_ref(1, 0);
    1
}

/// Sizes for `*_create`, each in `0..=MAX_SIZE` (`min` the least).
fn sizes<const N: usize>(a: &mut dyn Args, min: [i32; N]) -> Option<[i32; N]> {
    let v: [i32; N] = std::array::from_fn(|i| a.cell(i + 1));
    if v.iter().zip(min).any(|(&x, m)| x < m || x > MAX_SIZE) {
        a.error(&format!("Invalid sizes {v:?} (each {min:?} to {MAX_SIZE})"));
        return None;
    }
    Some(v)
}

fn write_file(a: &mut dyn Args, i: usize, bytes: &[u8]) -> Cell {
    let file = a.string(i);
    with(|s| s.dir.write_logged(&file, bytes)).unwrap_or(false) as Cell
}

fn read_file(a: &mut dyn Args, i: usize) -> Option<Vec<u8>> {
    let file = a.string(i);
    with(|s| s.dir.read(&file)).flatten()
}

// ---- GA ----

/// `RcGa:rl_ga_create(max_pop_size = 0)`: 0 is upstream's default, 16.
pub fn rl_ga_create(a: &mut dyn Args) -> Cell {
    let n = a.cell(1).clamp(0, MAX_SIZE) as u32;
    create(gas, |_| Ga::new(n))
}

/// `bool:rl_ga_free(&RcGa:ga)`
pub fn rl_ga_free(a: &mut dyn Args) -> Cell {
    free(a, "GA", gas)
}

/// `rl_ga_add(RcGa:ga, const Float:genes[], num, Float:fitness)`: `addToPopulation`; breeds once
/// the population is full. Returns the generation count.
pub fn rl_ga_add(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let mut ind = BotGaValues::from_values(read_floats(a, 2, n));
    ind.set_fitness(a.float(4));
    with_obj(a, "GA", gas, |ga, rng, _| {
        ga.add_to_population(ind, rng);
        ga.generations() as Cell
    })
}

/// `bool:rl_ga_can_pick(RcGa:ga)`
pub fn rl_ga_can_pick(a: &mut dyn Args) -> Cell {
    with_obj(a, "GA", gas, |ga, _, _| ga.can_pick() as Cell)
}

fn put_individual(a: &mut dyn Args, ind: Option<&BotGaValues>, len: usize) -> Cell {
    match ind {
        Some(i) => {
            a.set_ref(4, float_cell(i.fitness()));
            write_floats(a, 2, i.values(), len)
        }
        None => 0,
    }
}

/// `rl_ga_pick(RcGa:ga, Float:genes[], len, &Float:fitness = 0.0)`: the next bred individual;
/// the number of genes written (0: none to pick).
pub fn rl_ga_pick(a: &mut dyn Args) -> Cell {
    let len = size(a, 3);
    with_obj(a, "GA", gas, |ga, _, a| {
        let ind = ga.pick();
        put_individual(a, ind.as_ref(), len)
    })
}

/// `rl_ga_best(RcGa:ga, Float:genes[], len, &Float:fitness = 0.0)`: the best individual of any
/// epoch.
pub fn rl_ga_best(a: &mut dyn Args) -> Cell {
    let len = size(a, 3);
    with_obj(a, "GA", gas, |ga, _, a| put_individual(a, ga.best(), len))
}

/// `rl_ga_generations(RcGa:ga)`
pub fn rl_ga_generations(a: &mut dyn Args) -> Cell {
    with_obj(a, "GA", gas, |ga, _, _| ga.generations() as Cell)
}

/// `rl_ga_population(RcGa:ga)`: scored individuals waiting for the next epoch.
pub fn rl_ga_population(a: &mut dyn Args) -> Cell {
    with_obj(a, "GA", gas, |ga, _, _| ga.population().len() as Cell)
}

/// `bool:rl_ga_save(RcGa:ga, const file[])`: `CGA::save`.
pub fn rl_ga_save(a: &mut dyn Args) -> Cell {
    let mut out = Vec::new();
    if with_obj(a, "GA", gas, |ga, _, _| {
        ga.save(&mut out);
        1
    }) == 0
    {
        return 0;
    }
    write_file(a, 2, &out)
}

/// `bool:rl_ga_load(RcGa:ga, const file[], genes)`: `CGA::load` into an emptied GA; individuals
/// must have `genes` genes.
pub fn rl_ga_load(a: &mut dyn Args) -> Cell {
    let Some(bytes) = read_file(a, 2) else { return 0 };
    let genes = a.cell(3);
    with_obj(a, "GA", gas, |ga, _, _| {
        *ga = Ga::new(ga.max_pop_size);
        ga.load(&mut Reader::new(&bytes), genes);
        1
    })
}

// ---- perceptron ----

/// `RcPerceptron:rl_perceptron_create(num_inputs, Float:learn_rate = 0.5)`
pub fn rl_perceptron_create(a: &mut dyn Args) -> Cell {
    let Some([n]) = sizes(a, [0]) else { return 0 };
    let rate = if a.count() >= 2 { a.float(2) } else { 0.0 };
    create(perceptrons, |rng| Perceptron::new(n, rate, rng))
}

/// `bool:rl_perceptron_free(&RcPerceptron:p)`
pub fn rl_perceptron_free(a: &mut dyn Args) -> Cell {
    free(a, "perceptron", perceptrons)
}

/// `Float:rl_perceptron_execute(RcPerceptron:p, const Float:inputs[], num)`: 0.0 when `num` isn't
/// the perceptron's input count.
pub fn rl_perceptron_execute(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let inputs = read_floats(a, 2, n);
    with_obj(a, "perceptron", perceptrons, |p, _, _| {
        p.input(&inputs);
        float_cell(p.execute())
    })
}

/// `bool:rl_perceptron_fired(RcPerceptron:p)`: the last output was 0.5 or more.
pub fn rl_perceptron_fired(a: &mut dyn Args) -> Cell {
    with_obj(a, "perceptron", perceptrons, |p, _, _| p.fired() as Cell)
}

/// `Float:rl_perceptron_output(RcPerceptron:p)`
pub fn rl_perceptron_output(a: &mut dyn Args) -> Cell {
    with_obj(a, "perceptron", perceptrons, |p, _, _| float_cell(p.output()))
}

/// `rl_perceptron_train(RcPerceptron:p, Float:expected)`: trains on the last inputs.
pub fn rl_perceptron_train(a: &mut dyn Args) -> Cell {
    let e = a.float(2);
    with_obj(a, "perceptron", perceptrons, |p, _, _| {
        p.train(e);
        p.trained() as Cell
    })
}

/// `rl_perceptron_weights(RcPerceptron:p, Float:out[], len)`: bias weight first; the count
/// written.
pub fn rl_perceptron_weights(a: &mut dyn Args) -> Cell {
    let len = size(a, 3);
    with_obj(a, "perceptron", perceptrons, |p, _, a| write_floats(a, 2, p.weights(), len))
}

/// `rl_perceptron_set_weights(RcPerceptron:p, const Float:weights[], num)`: bias weight first.
pub fn rl_perceptron_set_weights(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let w = read_floats(a, 2, n);
    with_obj(a, "perceptron", perceptrons, |p, _, _| {
        p.set_weights(&w);
        1
    })
}

/// `rl_perceptron_randomize(RcPerceptron:p)`
pub fn rl_perceptron_randomize(a: &mut dyn Args) -> Cell {
    with_obj(a, "perceptron", perceptrons, |p, rng, _| {
        p.randomize(rng);
        1
    })
}

/// `bool:rl_perceptron_save(RcPerceptron:p, const file[])`
pub fn rl_perceptron_save(a: &mut dyn Args) -> Cell {
    let mut out = Vec::new();
    if with_obj(a, "perceptron", perceptrons, |p, _, _| {
        p.save(&mut out);
        1
    }) == 0
    {
        return 0;
    }
    write_file(a, 2, &out)
}

/// `bool:rl_perceptron_load(RcPerceptron:p, const file[])`: false if the file is missing or
/// isn't a perceptron.
pub fn rl_perceptron_load(a: &mut dyn Args) -> Cell {
    let Some(bytes) = read_file(a, 2) else { return 0 };
    with_obj(a, "perceptron", perceptrons, |p, _, _| {
        let loaded = Perceptron::from_reader(&mut Reader::new(&bytes));
        if loaded.num_weights() == 0 {
            return 0;
        }
        *p = loaded;
        1
    })
}

// ---- neural network ----

/// `RcNn:rl_nn_create(hidden_layers, num_inputs, per_hidden, num_outputs)`
pub fn rl_nn_create(a: &mut dyn Args) -> Cell {
    let Some([h, i, p, o]) = sizes(a, [0, 1, 1, 1]) else { return 0 };
    if h > 64 {
        a.error(&format!("Too many hidden layers ({h}, at most 64)"));
        return 0;
    }
    create(nns, |rng| Nn::new(h, i, p, o, rng))
}

/// `bool:rl_nn_free(&RcNn:nn)`
pub fn rl_nn_free(a: &mut dyn Args) -> Cell {
    free(a, "NN", nns)
}

/// `rl_nn_execute(RcNn:nn, const Float:inputs[], num, Float:outputs[], len)`: the number of
/// outputs written.
pub fn rl_nn_execute(a: &mut dyn Args) -> Cell {
    let (n, len) = (size(a, 3), size(a, 5));
    let inputs = read_floats(a, 2, n);
    with_obj(a, "NN", nns, |nn, _, a| {
        let out = nn.execute(&inputs);
        write_floats(a, 4, &out, len)
    })
}

/// `rl_nn_train(RcNn:nn, const Float:wanted[], num)`: `NN::trainOutputs` on the last inputs
/// (which, as upstream, trains the first layer's neurons).
pub fn rl_nn_train(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let wanted = read_floats(a, 2, n);
    with_obj(a, "NN", nns, |nn, _, _| {
        nn.train_outputs(&wanted);
        1
    })
}

/// `rl_nn_weights(RcNn:nn, Float:out[], len)`: every neuron's weights in order; returns the
/// total count (which may be more than `len`).
pub fn rl_nn_weights(a: &mut dyn Args) -> Cell {
    let len = size(a, 3);
    with_obj(a, "NN", nns, |nn, _, a| {
        let w = nn.weights();
        write_floats(a, 2, &w, len);
        w.len() as Cell
    })
}

/// `rl_nn_set_weights(RcNn:nn, const Float:weights[], num)`
pub fn rl_nn_set_weights(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let w = read_floats(a, 2, n);
    with_obj(a, "NN", nns, |nn, _, _| {
        nn.set_weights(&w);
        1
    })
}

/// `rl_nn_randomize(RcNn:nn)`
pub fn rl_nn_randomize(a: &mut dyn Args) -> Cell {
    with_obj(a, "NN", nns, |nn, rng, _| {
        nn.randomize(rng);
        1
    })
}

/// `bool:rl_nn_save(RcNn:nn, const file[])`: `NN::save`.
pub fn rl_nn_save(a: &mut dyn Args) -> Cell {
    let mut out = Vec::new();
    if with_obj(a, "NN", nns, |nn, _, _| {
        nn.save(&mut out);
        1
    }) == 0
    {
        return 0;
    }
    write_file(a, 2, &out)
}

/// `bool:rl_nn_load(RcNn:nn, const file[])`: replaces the network with the file's layers.
pub fn rl_nn_load(a: &mut dyn Args) -> Cell {
    let Some(bytes) = read_file(a, 2) else { return 0 };
    with_obj(a, "NN", nns, |nn, _, _| {
        let mut loaded = Nn::default();
        loaded.load(&mut Reader::new(&bytes));
        if loaded.layers().is_empty() {
            return 0;
        }
        *nn = loaded;
        1
    })
}

// ---- self-organising map ----

/// `RcSom:rl_som_create(width, height, num_inputs)`
pub fn rl_som_create(a: &mut dyn Args) -> Cell {
    let Some([w, h, i]) = sizes(a, [1, 1, 1]) else { return 0 };
    if w * h > MAX_SIZE * 16 {
        a.error(&format!("SOM too large ({w}x{h})"));
        return 0;
    }
    create(soms, |rng| Som::new(w, h, i, rng))
}

/// `bool:rl_som_free(&RcSom:som)`
pub fn rl_som_free(a: &mut dyn Args) -> Cell {
    free(a, "SOM", soms)
}

/// `rl_som_input(RcSom:som, const Float:inputs[], num)`: `inputOne` (trains); the best matching
/// neuron, -1 for none.
pub fn rl_som_input(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let inputs = read_floats(a, 2, n);
    with_obj(a, "SOM", soms, |som, _, _| som.input_one(&inputs).map_or(-1, |i| i as Cell))
}

/// `rl_som_bmu(RcSom:som, const Float:inputs[], num)`: the best matching neuron without
/// training, -1 for none.
pub fn rl_som_bmu(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let inputs = read_floats(a, 2, n);
    with_obj(a, "SOM", soms, |som, _, _| som.bmu(&inputs).map_or(-1, |i| i as Cell))
}

/// `rl_som_weights(RcSom:som, neuron, Float:out[], len)`: the count written (0: no such neuron).
pub fn rl_som_weights(a: &mut dyn Args) -> Cell {
    let (i, len) = (a.cell(2), size(a, 4));
    with_obj(a, "SOM", soms, |som, _, a| {
        let w = usize::try_from(i).ok().and_then(|i| som.neurons().get(i)).map(|n| n.weights().to_vec());
        w.map_or(0, |w| write_floats(a, 3, &w, len))
    })
}

/// `rl_som_epochs(RcSom:som)`: inputs trained on.
pub fn rl_som_epochs(a: &mut dyn Args) -> Cell {
    with_obj(a, "SOM", soms, |som, _, _| som.epochs() as Cell)
}

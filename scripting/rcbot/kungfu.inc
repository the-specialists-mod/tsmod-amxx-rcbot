// rcbot/kungfu.inc: the kung fu fighter (port addition with learned stances, reactions and combos; documented gameplay deviation, rcbot-gameplay-deviations).
// Owner: kungfu. Ports bot/kungfu.rs (Move, MOVES, Reaction, Stance, AttackKind, Combo, seed_combos, Fight, Answering, FightEnd, PerOpponent, KungFu new / reset_fight / opponent / fighting / save / load / combo_runs / combo_done / breed / lesson_weight / stance_estimate / answer_estimate / learn_stance / learn_answer / decay, is_ranged, kungfu_file_name, Bot::kf_power, kf_ready, kf_reaction_time, kungfu_fight, kf_trade, kf_settle, kf_combo_reward, kf_stance_reward, kf_watch, kf_choose_stance, kf_judge_reaction, kf_react, kf_pick_answer, kf_offence, kf_find_wall, kf_pick_combo, kf_run_combo, kf_close_in, kf_next_step, kf_airborne, kf_phase, kungfu_face), learn/bandit.rs (Arm, ucb, lcb, argmax), learn/store.rs (header, read_header, floats, Rust's f32 Display and parse) and host bots.rs's .rkf load/save (botprofiles/<key>.rkf, FILE_KIND "kungfu", FILE_VERSION 2).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Two parts:
//  * the .rkf codec (kfc_*): the file's text, byte for byte what tsmod-rcbot-rs's KungFu::save
//    writes (numbers as Rust prints an f32: the shortest digits that read back the same float,
//    never an exponent) and what KungFu::load accepts (Rust's correctly rounded f32 parse), with
//    no bot state, so scripting/test/rkf_roundtrip.sma includes it alone
//    (#define RC_KUNGFU_CODEC_ONLY before the include);
//  * the fighter (kungfu_*, static kf_*), on state.inc's kungfu section.

#if defined _rcbot_kungfu_included
	#endinput
#endif
#define _rcbot_kungfu_included

// kungfu.rs Move codes (a move's code in .rkf files is its place in MOVES).
enum
{
	KF_MOVE_PUNCH = 0,
	KF_MOVE_JAB,
	KF_MOVE_HEAVY_PUNCH,
	KF_MOVE_KICK,
	KF_MOVE_QUICK_KICK,
	KF_MOVE_SWEEP,
	KF_MOVE_BACK_KICK,
	KF_MOVE_JUMP_KICK,
	KF_MOVE_JUMP_PUNCH,
	KF_MOVE_FLY_KICK,
	KF_MOVE_WALL_KICK,
	KF_MOVE_WALL_FLY_KICK,
	KF_MOVES
};

// kungfu.rs Stance, Reaction (ANSWERS order, then not reacting), AttackKind, FightEnd.
enum { KF_STANCE_DEFENSIVE = 0, KF_STANCE_OFFENSIVE };
enum { KF_REACT_BLOCK = 0, KF_REACT_DUCK, KF_REACT_STEP_BACK, KF_REACT_SIDE_STEP, KF_NO_REACTION };
enum { KF_ATTACK_PUNCH = 0, KF_ATTACK_KICK, KF_ATTACK_LOW, KF_ATTACK_AIR, KF_ATTACK_BACK };
enum { KF_END_KILL = 0, KF_END_DEATH, KF_END_BROKEN };

// ================================================================================================
// The .rkf codec (learn/store.rs, KungFu::save / load). No bot state: the learned part of a
// KungFu is passed as a flat array of arms (value, n pairs, in the file's order) and the combos.
// ================================================================================================

// Arms in the file's order: stance_all (2), stances[class][stance] (4 x 2), reaction_all (5),
// reactions[kind][answer] (5 x 5). Arm a is (arms[a * 2], arms[a * 2 + 1]) = (value, n).
#define KFC_ARMS			40
#define KFC_ARM_FLOATS		80
#define KFC_STANCE_ALL		0
#define KFC_STANCES			2
#define KFC_REACTION_ALL	10
#define KFC_REACTIONS		15
#define KFC_MAX_COMBOS		14
#define KFC_MAX_COMBO_LEN	5
// The text of a file. A save is at most about 6000 characters (80 + 14 numbers of at most 56
// characters each); a longer file is read up to this many (the rest of it is ignored).
#define KFC_TEXT			8192
// Decimal digits of an exact float or midpoint: (4m + 2) * 5^152 has 116.
#define KFC_DIG				128
// Fields of a line kept (a valid line has at most 12).
#define KFC_FIELDS			24
// The .rkf kind and version (1 had one value per stance and reaction).
#define KFC_VERSION			2

static const KFC_POW5[] = {1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125};

// t[0..len) (a little-endian decimal) *= mul (at most 2^20).
static stock kfc_mul(t[], &len, mul)
{
	new carry = 0;
	for (new i = 0; i < len; i++)
	{
		new v = t[i] * mul + carry;
		t[i] = v % 10;
		carry = v / 10;
	}
	while (carry > 0 && len < KFC_DIG)
	{
		t[len++] = carry % 10;
		carry /= 10;
	}
}

// K * 2^j exactly: digits d (most significant first, no trailing zeros), their count n and the
// exponent e, the value being 0.d1 d2 ... dn * 10^e (n = 0 for zero).
static stock kfc_exact(K, j, d[], &n, &e)
{
	new t[KFC_DIG], len = 0, s = 0, p;
	while (K > 0)
	{
		t[len++] = K % 10;
		K /= 10;
	}
	if (len == 0)
	{
		n = 0;
		e = 0;
		return;
	}
	if (j >= 0)
	{
		while (j > 0)
		{
			p = j > 20 ? 20 : j;
			kfc_mul(t, len, 1 << p);
			j -= p;
		}
	}
	else
	{
		// K * 2^j = K * 5^-j / 10^-j.
		s = -j;
		for (new k = s; k > 0; k -= p)
		{
			p = k > 9 ? 9 : k;
			kfc_mul(t, len, KFC_POW5[p]);
		}
	}
	e = len - s;
	new lo = 0;
	while (t[lo] == 0)
	{
		lo++;
	}
	n = len - lo;
	for (new i = 0; i < n; i++)
	{
		d[i] = t[len - 1 - i];
	}
}

// Compares two non-negative decimals (kfc_exact's form; trailing zeros allowed).
static stock kfc_cmp(const a[], an, ae, const b[], bn, be)
{
	if (an == 0 || bn == 0)
	{
		return (an > 0 ? 1 : 0) - (bn > 0 ? 1 : 0);
	}
	if (ae != be)
	{
		return ae > be ? 1 : -1;
	}
	new m = an > bn ? an : bn;
	for (new i = 0; i < m; i++)
	{
		new x = i < an ? a[i] : 0, y = i < bn ? b[i] : 0;
		if (x != y)
		{
			return x > y ? 1 : -1;
		}
	}
	return 0;
}

static stock kfc_put(out[], maxlen, &pos, c)
{
	if (pos < maxlen)
	{
		out[pos++] = c;
	}
}

// f as Rust's `{}` prints an f32 (core::fmt's float_to_decimal_display, flt2dec's shortest mode):
// the fewest significant digits that read back as f, the closest of them to f (ties up), in
// plain decimal ("1", "0.1", "-0.0000001", "16777216", "-0", "inf", "NaN"). As flt2dec::decode,
// the interval's ends count when the mantissa is even (always for a subnormal), and a power of
// two has a lower neighbour half as far. Returns the length written.
stock kfc_float_str(Float:f, out[], maxlen)
{
	new bits = _:f;
	new bool:neg = bits < 0;
	new a = bits & 0x7FFFFFFF;
	new ef = a >>> 23, fr = a & 0x7FFFFF;
	new pos = 0;
	if (ef == 255)
	{
		if (fr != 0)
		{
			return copy(out, maxlen, "NaN");
		}
		if (neg)
		{
			kfc_put(out, maxlen, pos, '-');
		}
		kfc_put(out, maxlen, pos, 'i');
		kfc_put(out, maxlen, pos, 'n');
		kfc_put(out, maxlen, pos, 'f');
		out[pos] = 0;
		return pos;
	}
	if (neg)
	{
		kfc_put(out, maxlen, pos, '-');
	}
	if (a == 0)
	{
		kfc_put(out, maxlen, pos, '0');
		out[pos] = 0;
		return pos;
	}
	new m, e2, bool:incl;
	if (ef == 0)
	{
		m = fr;
		e2 = -149;
		incl = true;
	}
	else
	{
		m = fr | 0x800000;
		e2 = ef - 150;
		incl = (m & 1) == 0;
	}
	// The value and its rounding interval's ends, at 2^(e2 - 2).
	static vd[KFC_DIG], hd[KFC_DIG], ld[KFC_DIG], cd[KFC_DIG + 1], rd[KFC_DIG + 1];
	new vn, ve, hn, he, ln, le;
	kfc_exact(4 * m, e2 - 2, vd, vn, ve);
	kfc_exact(4 * m + 2, e2 - 2, hd, hn, he);
	kfc_exact((ef > 0 && fr == 0) ? 4 * m - 1 : 4 * m - 2, e2 - 2, ld, ln, le);
	new rn = 0, re = 0;
	for (new k = 1; k <= vn; k++)
	{
		// The value cut to k digits, and that plus one in the k-th digit.
		new fn = k;
		while (fn > 0 && vd[fn - 1] == 0)
		{
			fn--;
		}
		for (new i = 0; i < k; i++)
		{
			cd[i] = vd[i];
		}
		new i = k - 1;
		while (i >= 0)
		{
			cd[i]++;
			if (cd[i] < 10)
			{
				break;
			}
			cd[i] = 0;
			i--;
		}
		new cn = k, ce = ve;
		if (i < 0)
		{
			for (new j = k; j > 0; j--)
			{
				cd[j] = cd[j - 1];
			}
			cd[0] = 1;
			cn = k + 1;
			ce = ve + 1;
		}
		while (cn > 0 && cd[cn - 1] == 0)
		{
			cn--;
		}
		new c1 = kfc_cmp(vd, fn, ve, ld, ln, le);
		new bool:down = c1 > 0 || (incl && c1 == 0);
		new c2 = kfc_cmp(cd, cn, ce, hd, hn, he);
		new bool:up = c2 < 0 || (incl && c2 == 0);
		if (!down && !up)
		{
			continue;
		}
		new bool:ceil = up;
		if (down && up)
		{
			// The closer one; the rest of the value's digits against a half, ties up.
			ceil = k < vn && vd[k] >= 5;
		}
		if (ceil)
		{
			for (new j = 0; j < cn; j++)
			{
				rd[j] = cd[j];
			}
			rn = cn;
			re = ce;
		}
		else
		{
			for (new j = 0; j < fn; j++)
			{
				rd[j] = vd[j];
			}
			rn = fn;
			re = ve;
		}
		break;
	}
	if (re <= 0)
	{
		kfc_put(out, maxlen, pos, '0');
		kfc_put(out, maxlen, pos, '.');
		for (new z = re; z < 0; z++)
		{
			kfc_put(out, maxlen, pos, '0');
		}
		for (new j = 0; j < rn; j++)
		{
			kfc_put(out, maxlen, pos, '0' + rd[j]);
		}
	}
	else
	{
		for (new j = 0; j < rn || j < re; j++)
		{
			if (j == re)
			{
				kfc_put(out, maxlen, pos, '.');
			}
			kfc_put(out, maxlen, pos, j < rn ? '0' + rd[j] : '0');
		}
	}
	out[pos] = 0;
	return pos;
}

// A non-negative integer as Rust prints it.
stock kfc_int_str(v, out[], maxlen)
{
	new tmp[12], n = 0, pos = 0;
	if (v < 0)
	{
		kfc_put(out, maxlen, pos, '-');
		v = -v;
	}
	do
	{
		tmp[n++] = '0' + v % 10;
		v /= 10;
	}
	while (v > 0 && n < sizeof(tmp));
	while (n > 0)
	{
		kfc_put(out, maxlen, pos, tmp[--n]);
	}
	out[pos] = 0;
	return pos;
}

static stock bool:kfc_is_space(c)
{
	// char::is_whitespace for ASCII: tab, newline, vertical tab, form feed, return, space.
	return c == ' ' || (c >= 9 && c <= 13);
}

// Rust's str::parse::<f32> on s[start..end), then is_finite (store::floats): false for text
// it refuses and for what would read as infinite or NaN ("inf", "NaN", "1e39"). Correctly
// rounded (ties to even): an estimate, then moved a float at a time while the text lies
// beyond the midpoint to a neighbour, comparing exact decimals.
stock bool:kfc_parse_float(const s[], start, end, &Float:out)
{
	new i = start, bool:neg = false;
	if (i < end && (s[i] == '+' || s[i] == '-'))
	{
		neg = s[i] == '-';
		i++;
	}
	// The digits as 0.x1 x2 ... * 10^xe, leading zeros skipped; digits past the buffer only
	// matter as "more than shown" (a sticky 1).
	static xd[KFC_DIG];
	new xn = 0, xe = 0, bool:any = false, bool:dot = false, bool:sticky = false;
	while (i < end)
	{
		new c = s[i];
		if (c >= '0' && c <= '9')
		{
			any = true;
			if (xn == 0 && c == '0')
			{
				if (dot)
				{
					xe--;
				}
			}
			else
			{
				if (xn < KFC_DIG - 1)
				{
					xd[xn++] = c - '0';
				}
				else if (c != '0')
				{
					sticky = true;
				}
				if (!dot)
				{
					xe++;
				}
			}
		}
		else if (c == '.' && !dot)
		{
			dot = true;
		}
		else
		{
			break;
		}
		i++;
	}
	if (!any)
	{
		return false;
	}
	if (i < end && (s[i] == 'e' || s[i] == 'E'))
	{
		i++;
		new bool:eneg = false;
		if (i < end && (s[i] == '+' || s[i] == '-'))
		{
			eneg = s[i] == '-';
			i++;
		}
		if (i >= end)
		{
			return false;
		}
		new ev = 0;
		while (i < end && s[i] >= '0' && s[i] <= '9')
		{
			if (ev < 100000)
			{
				ev = ev * 10 + (s[i] - '0');
			}
			i++;
		}
		if (i < end)
		{
			return false;
		}
		xe += eneg ? -ev : ev;
	}
	else if (i < end)
	{
		return false;
	}
	if (sticky)
	{
		xd[xn++] = 1;
	}
	if (xn == 0 || xe < -46)
	{
		new z = neg ? 0x80000000 : 0;
		out = Float:z;
		return true;
	}
	if (xe > 40)
	{
		return false;
	}
	// The estimate: up to 9 digits times a power of ten, in steps that neither overflow nor
	// underflow.
	new mant = 0, k = xn > 9 ? 9 : xn;
	for (new j = 0; j < k; j++)
	{
		mant = mant * 10 + xd[j];
	}
	new Float:approx = float(mant);
	for (new p = xe - k; p != 0;)
	{
		new step = p > 30 ? 30 : (p < -30 ? -30 : p);
		approx = approx * floatpower(10.0, float(step));
		p -= step;
	}
	new b = _:approx & 0x7FFFFFFF;
	if (b >= 0x7F800000)
	{
		b = 0x7F7FFFFF;
	}
	static md[KFC_DIG];
	new mn, me;
	for (new guard = 0; guard < 400; guard++)
	{
		new bef = b >>> 23, bfr = b & 0x7FFFFF, bm, be2;
		if (bef == 0)
		{
			bm = bfr;
			be2 = -149;
		}
		else
		{
			bm = bfr | 0x800000;
			be2 = bef - 150;
		}
		kfc_exact(2 * bm + 1, be2 - 1, md, mn, me);
		new c = kfc_cmp(xd, xn, xe, md, mn, me);
		if (c > 0 || (c == 0 && (b & 1)))
		{
			b++;
			if (b >= 0x7F800000)
			{
				return false;
			}
			continue;
		}
		if (b == 0)
		{
			break;
		}
		if (bfr == 0 && bef > 1)
		{
			kfc_exact(4 * bm - 1, be2 - 2, md, mn, me);
		}
		else
		{
			kfc_exact(2 * bm - 1, be2 - 1, md, mn, me);
		}
		c = kfc_cmp(xd, xn, xe, md, mn, me);
		if (c < 0 || (c == 0 && (b & 1)))
		{
			b--;
			continue;
		}
		break;
	}
	if (neg)
	{
		b |= 0x80000000;
	}
	out = Float:b;
	return true;
}

// Rust's str::parse of an unsigned integer on s[start..end) (a '+' allowed), at most max.
stock bool:kfc_parse_uint(const s[], start, end, &v, max)
{
	new i = start;
	if (i < end && s[i] == '+')
	{
		i++;
	}
	if (i >= end)
	{
		return false;
	}
	v = 0;
	for (; i < end; i++)
	{
		new c = s[i];
		if (c < '0' || c > '9')
		{
			return false;
		}
		new d = c - '0';
		if (v > (max - d) / 10)
		{
			return false;
		}
		v = v * 10 + d;
	}
	return true;
}

static stock bool:kfc_field_is(const s[], start, end, const word[])
{
	new n = strlen(word);
	if (end - start != n)
	{
		return false;
	}
	for (new i = 0; i < n; i++)
	{
		if (s[start + i] != word[i])
		{
			return false;
		}
	}
	return true;
}

static stock kfc_append(out[], maxlen, &pos, const s[])
{
	for (new i = 0; s[i] != 0; i++)
	{
		kfc_put(out, maxlen, pos, s[i]);
	}
}

// " <value> <n>" for num arms from first.
static stock kfc_append_pairs(out[], maxlen, &pos, const Float:arms[], first, num)
{
	new num_text[64];
	for (new a = first; a < first + num; a++)
	{
		kfc_float_str(arms[a * 2], num_text, charsmax(num_text));
		kfc_put(out, maxlen, pos, ' ');
		kfc_append(out, maxlen, pos, num_text);
		kfc_float_str(arms[a * 2 + 1], num_text, charsmax(num_text));
		kfc_put(out, maxlen, pos, ' ');
		kfc_append(out, maxlen, pos, num_text);
	}
}

// KungFu::save: `<id>.rkf`'s text (version 2: a value and a count per stance and opponent class,
// per answer and attack kind, then the combos). Returns the length.
stock kfc_format(const Float:arms[], const moves[][KFC_MAX_COMBO_LEN], const lens[], const Float:values[], const uses[], count, out[], maxlen)
{
	new pos = 0, num_text[64];
	kfc_append(out, maxlen, pos, "rcbot-kungfu 2^n");
	kfc_append(out, maxlen, pos, "stance all");
	kfc_append_pairs(out, maxlen, pos, arms, KFC_STANCE_ALL, 2);
	kfc_put(out, maxlen, pos, '^n');
	for (new c = 0; c < 4; c++)
	{
		kfc_append(out, maxlen, pos, "stance ");
		kfc_int_str(c, num_text, charsmax(num_text));
		kfc_append(out, maxlen, pos, num_text);
		kfc_append_pairs(out, maxlen, pos, arms, KFC_STANCES + c * 2, 2);
		kfc_put(out, maxlen, pos, '^n');
	}
	kfc_append(out, maxlen, pos, "reaction all");
	kfc_append_pairs(out, maxlen, pos, arms, KFC_REACTION_ALL, 5);
	kfc_put(out, maxlen, pos, '^n');
	for (new k = 0; k < 5; k++)
	{
		kfc_append(out, maxlen, pos, "reaction ");
		kfc_int_str(k, num_text, charsmax(num_text));
		kfc_append(out, maxlen, pos, num_text);
		kfc_append_pairs(out, maxlen, pos, arms, KFC_REACTIONS + k * 5, 5);
		kfc_put(out, maxlen, pos, '^n');
	}
	for (new c = 0; c < count; c++)
	{
		kfc_append(out, maxlen, pos, "combo ");
		kfc_float_str(values[c], num_text, charsmax(num_text));
		kfc_append(out, maxlen, pos, num_text);
		kfc_put(out, maxlen, pos, ' ');
		kfc_int_str(uses[c], num_text, charsmax(num_text));
		kfc_append(out, maxlen, pos, num_text);
		kfc_put(out, maxlen, pos, ' ');
		for (new m = 0; m < lens[c]; m++)
		{
			if (m > 0)
			{
				kfc_put(out, maxlen, pos, ',');
			}
			kfc_int_str(moves[c][m], num_text, charsmax(num_text));
			kfc_append(out, maxlen, pos, num_text);
		}
		kfc_put(out, maxlen, pos, '^n');
	}
	out[pos] = 0;
	return pos;
}

// The floats of fields fs[from..nf) into v (store::floats): false if any isn't a finite float.
static stock bool:kfc_floats(const text[], const fs[], const fe[], from, nf, Float:v[], &nv)
{
	nv = 0;
	for (new f = from; f < nf; f++)
	{
		if (!kfc_parse_float(text, fs[f], fe[f], v[nv]))
		{
			return false;
		}
		nv++;
	}
	return true;
}

// KungFu::load's `arms` closure: num arms from first, if v has exactly their pairs.
static stock kfc_set_arms(Float:arms[], first, num, const Float:v[], nv)
{
	if (nv != num * 2)
	{
		return;
	}
	for (new a = 0; a < num; a++)
	{
		arms[(first + a) * 2] = v[a * 2];
		// p[1].max(0.0)
		arms[(first + a) * 2 + 1] = v[a * 2 + 1] < 0.0 ? 0.0 : v[a * 2 + 1];
	}
}

// KungFu::load: reads save's text, or a version 1 file (one value per stance and per reaction,
// taken as the overall values worth 4 samples); anything else (or a damaged line) leaves that
// part as it was. A file without combos keeps the combos. Returns the version read, 0 for none.
stock kfc_parse(const text[], Float:arms[], moves[][KFC_MAX_COMBO_LEN], lens[], Float:values[], uses[], &count)
{
	new len = strlen(text);
	// store::read_header: the first line, trimmed, is "rcbot-kungfu <version>".
	new ls = 0, le = 0;
	while (le < len && text[le] != '^n')
	{
		le++;
	}
	new hs = ls, he = le;
	while (hs < he && kfc_is_space(text[hs]))
	{
		hs++;
	}
	while (he > hs && kfc_is_space(text[he - 1]))
	{
		he--;
	}
	static const HEADER[] = "rcbot-kungfu ";
	new hl = sizeof(HEADER) - 1;
	if (he - hs < hl)
	{
		return 0;
	}
	for (new i = 0; i < hl; i++)
	{
		if (text[hs + i] != HEADER[i])
		{
			return 0;
		}
	}
	new vs = hs + hl;
	while (vs < he && kfc_is_space(text[vs]))
	{
		vs++;
	}
	new version;
	if (!kfc_parse_uint(text, vs, he, version, cellmax) || version < 1 || version > KFC_VERSION)
	{
		return 0;
	}

	static cm[KFC_MAX_COMBOS][KFC_MAX_COMBO_LEN], cl[KFC_MAX_COMBOS], Float:cv[KFC_MAX_COMBOS], cu[KFC_MAX_COMBOS];
	new cn = 0;
	new fs[KFC_FIELDS], fe[KFC_FIELDS];
	new Float:v[KFC_FIELDS];
	while (le < len)
	{
		ls = le + 1;
		le = ls;
		while (le < len && text[le] != '^n')
		{
			le++;
		}
		// split_whitespace; a line with too many fields fits no rule but a combo's.
		new nf = 0, bool:many = false;
		for (new i = ls; i < le;)
		{
			while (i < le && kfc_is_space(text[i]))
			{
				i++;
			}
			if (i >= le)
			{
				break;
			}
			new st = i;
			while (i < le && !kfc_is_space(text[i]))
			{
				i++;
			}
			if (nf < KFC_FIELDS)
			{
				fs[nf] = st;
				fe[nf] = i;
				nf++;
			}
			else
			{
				many = true;
			}
		}
		if (nf == 0)
		{
			continue;
		}
		new nv, which;
		if (kfc_field_is(text, fs[0], fe[0], "stance"))
		{
			if (many)
			{
				continue;
			}
			if (version == 1)
			{
				if (kfc_floats(text, fs, fe, 1, nf, v, nv) && nv == 2)
				{
					for (new s = 0; s < 2; s++)
					{
						arms[(KFC_STANCE_ALL + s) * 2] = v[s];
						arms[(KFC_STANCE_ALL + s) * 2 + 1] = 4.0;
					}
				}
				continue;
			}
			if (nf < 2)
			{
				continue;
			}
			if (!kfc_floats(text, fs, fe, 2, nf, v, nv))
			{
				continue;
			}
			if (kfc_field_is(text, fs[1], fe[1], "all"))
			{
				kfc_set_arms(arms, KFC_STANCE_ALL, 2, v, nv);
			}
			else if (kfc_parse_uint(text, fs[1], fe[1], which, cellmax) && which < 4)
			{
				kfc_set_arms(arms, KFC_STANCES + which * 2, 2, v, nv);
			}
		}
		else if (kfc_field_is(text, fs[0], fe[0], "reaction"))
		{
			if (many)
			{
				continue;
			}
			if (version == 1)
			{
				if (kfc_floats(text, fs, fe, 1, nf, v, nv) && nv == 4)
				{
					for (new a = 0; a < 4; a++)
					{
						arms[(KFC_REACTION_ALL + a) * 2] = v[a];
						arms[(KFC_REACTION_ALL + a) * 2 + 1] = 4.0;
					}
				}
				continue;
			}
			if (nf < 2)
			{
				continue;
			}
			if (!kfc_floats(text, fs, fe, 2, nf, v, nv))
			{
				continue;
			}
			if (kfc_field_is(text, fs[1], fe[1], "all"))
			{
				kfc_set_arms(arms, KFC_REACTION_ALL, 5, v, nv);
			}
			else if (kfc_parse_uint(text, fs[1], fe[1], which, cellmax) && which < 5)
			{
				kfc_set_arms(arms, KFC_REACTIONS + which * 5, 5, v, nv);
			}
		}
		else if (kfc_field_is(text, fs[0], fe[0], "combo"))
		{
			new Float:value, used;
			if (nf < 4 || !kfc_parse_float(text, fs[1], fe[1], value) || !kfc_parse_uint(text, fs[2], fe[2], used, cellmax))
			{
				continue;
			}
			// Every code a u8 that names a move; then 1-5 of them.
			new codes[KFC_MAX_COMBO_LEN], ncodes = 0, bool:ok = true;
			for (new i = fs[3]; ok;)
			{
				new st = i;
				while (i < fe[3] && text[i] != ',')
				{
					i++;
				}
				new code;
				if (!kfc_parse_uint(text, st, i, code, 255) || code >= KF_MOVES)
				{
					ok = false;
					break;
				}
				if (ncodes < KFC_MAX_COMBO_LEN)
				{
					codes[ncodes] = code;
				}
				ncodes++;
				if (i >= fe[3])
				{
					break;
				}
				i++;
			}
			if (!ok || ncodes < 1 || ncodes > KFC_MAX_COMBO_LEN || cn >= KFC_MAX_COMBOS)
			{
				continue;
			}
			for (new m = 0; m < ncodes; m++)
			{
				cm[cn][m] = codes[m];
			}
			cl[cn] = ncodes;
			cv[cn] = value;
			cu[cn] = used;
			cn++;
		}
	}
	if (cn > 0)
	{
		for (new c = 0; c < cn; c++)
		{
			for (new m = 0; m < cl[c]; m++)
			{
				moves[c][m] = cm[c][m];
			}
			lens[c] = cl[c];
			values[c] = cv[c];
			uses[c] = cu[c];
		}
		count = cn;
	}
	return version;
}

// The whole file into text (bytes, as String::from_utf8_lossy sees ASCII): the length, -1 if
// it can't be read.
stock kfc_read_file(const path[], text[], maxlen)
{
	new f = fopen(path, "rb");
	if (!f)
	{
		text[0] = 0;
		return -1;
	}
	new n = fread_blocks(f, text, maxlen - 1, BLOCK_CHAR);
	fclose(f);
	if (n < 0)
	{
		n = 0;
	}
	text[n] = 0;
	return n;
}

// The host's State::write: atomically, a temporary file renamed over the old one ("wb": no
// newline translation on Windows). AMX Mod X's rename_file is MoveFileA on Windows, which won't
// replace a file: there the old one is deleted first.
stock bool:kfc_write_file(const path[], const text[], len)
{
	new tmp[192];
	formatex(tmp, charsmax(tmp), "%s.tmp", path);
	new f = fopen(tmp, "wb");
	if (!f)
	{
		return false;
	}
	new written = len > 0 ? fwrite_blocks(f, text, len, BLOCK_CHAR) : 0;
	fclose(f);
	if (written != len)
	{
		delete_file(tmp);
		return false;
	}
	if (rename_file(tmp, path, 1))
	{
		return true;
	}
	delete_file(path);
	if (rename_file(tmp, path, 1))
	{
		return true;
	}
	delete_file(tmp);
	return false;
}

// kungfu.rs seed_combos: TS's own chains (jabs into the heavy punch, quick kicks into the spin
// kick) and a few openers; KungFu::new's combos. Returns the count.
stock kfc_seed_combos(moves[][KFC_MAX_COMBO_LEN], lens[], Float:values[], uses[])
{
	static const SEEDS[][KFC_MAX_COMBO_LEN] =
	{
		{KF_MOVE_PUNCH, KF_MOVE_JAB, KF_MOVE_JAB, KF_MOVE_JAB, -1},
		{KF_MOVE_KICK, KF_MOVE_QUICK_KICK, KF_MOVE_QUICK_KICK, KF_MOVE_QUICK_KICK, -1},
		{KF_MOVE_PUNCH, KF_MOVE_KICK, -1, -1, -1},
		{KF_MOVE_SWEEP, -1, -1, -1, -1},
		{KF_MOVE_HEAVY_PUNCH, KF_MOVE_KICK, -1, -1, -1},
		{KF_MOVE_JUMP_KICK, KF_MOVE_PUNCH, -1, -1, -1},
		{KF_MOVE_BACK_KICK, -1, -1, -1, -1},
		{KF_MOVE_JAB, KF_MOVE_JAB, KF_MOVE_SWEEP, -1, -1},
		{KF_MOVE_FLY_KICK, KF_MOVE_PUNCH, -1, -1, -1},
		{KF_MOVE_WALL_KICK, KF_MOVE_KICK, -1, -1, -1},
		{KF_MOVE_WALL_FLY_KICK, -1, -1, -1, -1}
	};
	new n = sizeof(SEEDS);
	for (new c = 0; c < n; c++)
	{
		new len = 0;
		while (len < KFC_MAX_COMBO_LEN && SEEDS[c][len] >= 0)
		{
			moves[c][len] = SEEDS[c][len];
			len++;
		}
		lens[c] = len;
		values[c] = 0.0;
		uses[c] = 0;
	}
	return n;
}

#if defined RC_KUNGFU_CODEC_ONLY
	#endinput
#endif

// ================================================================================================
// The fighter.
// ================================================================================================

// Seconds without a fight think after which the fight is over.
#define KF_FIGHT_GAP		0.5
// Distance within which the opponent's kung fu can reach the bot.
#define KF_THREAT_RANGE		96.0
// Where the defensive stance waits: just outside the longest kick.
#define KF_GUARD_DISTANCE	82.0
// How close the bot gets to attack.
#define KF_STRIKE_DISTANCE	52.0
// A gunman nearer than this is rushed even unarmed; farther, the bot takes cover.
#define KF_RUSH_GUNMAN		220.0
// CCombatBlock's power cost and window, and the power's regeneration per second.
#define KF_BLOCK_COST		33.0
#define KF_BLOCK_TIME		0.5
#define KF_POWER_REGEN		15.0
// How long after a reaction its outcome is judged.
#define KF_REACTION_EVAL	0.6
// How many combos finish between two new ones.
#define KF_BREED_EVERY		20
// A combo is culled only after this many runs, and the best few never are.
#define KF_CULL_MIN_USES	8
#define KF_PROTECTED_COMBOS	3
// UCB1's exploration weights: combos, then stances and answers.
#define KF_UCB_COMBO		0.7
#define KF_UCB_CHOICE		0.5
// How many samples of the wider estimate a narrower one is shrunk toward.
#define KF_SHRINK			3.0
// Counts decay by this a fight.
#define KF_FIGHT_DECAY		0.99
// A human fight's lessons weigh this many bot fights'.
#define KF_HUMAN_WEIGHT		2.5
// What a block costs beside its outcome.
#define KF_BLOCK_PENALTY	0.3
// Health a reward counts per unit.
#define KF_REWARD_HEALTH	30.0
// An opponent attacking at least this often (s) is aggressive.
#define KF_AGGRESSIVE_INTERVAL	1.2
// FlyKick's range's far end: the farthest a combo starts.
#define KF_FLY_KICK_MAX		260.0

#define KF_INF				(Float:0x7F800000)
#define KF_NEG_INF			(Float:0xFF800000)
#define KF_E				2.7182817

// ---- learn/bandit.rs ----------------------------------------------------------------------------

// Arm::update.
static stock kf_arm_update(&Float:value, &Float:n, Float:reward, Float:weight)
{
	if (weight <= 0.0 || reward != reward || reward == KF_INF || reward == KF_NEG_INF)
	{
		return;
	}
	n += weight;
	value += (reward - value) * weight / n;
}

// Arm::shrunk.
static stock Float:kf_shrunk(Float:value, Float:n, Float:prior, Float:m)
{
	if (n + m <= 0.0)
	{
		return prior;
	}
	return (n * value + m * prior) / (n + m);
}

// ucb: an option never tried scores infinity.
static stock Float:kf_ucb(Float:mean, Float:n, Float:total, Float:c)
{
	if (n <= 0.001)
	{
		return KF_INF;
	}
	return mean + c * floatsqroot(floatlog(fmax(total, 0.0) + 1.0, KF_E) / n);
}

// lcb.
static stock Float:kf_lcb(Float:mean, Float:n, Float:total, Float:c)
{
	if (n <= 0.001)
	{
		return KF_NEG_INF;
	}
	return mean - c * floatsqroot(floatlog(fmax(total, 0.0) + 1.0, KF_E) / n);
}

// argmax: the highest score (ties to the first), -1 for none or only -inf.
static stock kf_argmax(const Float:scores[], num)
{
	new best = -1;
	for (new i = 0; i < num; i++)
	{
		new Float:s = scores[i];
		if (s == KF_NEG_INF || s != s)
		{
			continue;
		}
		if (best < 0 || s > scores[best])
		{
			best = i;
		}
	}
	return best;
}

// ---- Move -----------------------------------------------------------------------------------------

// Move::recovery: CCombatAttack's delay.
static stock Float:kf_recovery(m)
{
	switch (m)
	{
		case KF_MOVE_PUNCH: return 0.35;
		case KF_MOVE_JAB: return 0.25;
		case KF_MOVE_HEAVY_PUNCH: return 0.65;
		case KF_MOVE_KICK: return 0.5;
		case KF_MOVE_QUICK_KICK: return 0.3;
		case KF_MOVE_SWEEP, KF_MOVE_BACK_KICK: return 1.0;
		case KF_MOVE_JUMP_KICK: return 0.75;
		case KF_MOVE_JUMP_PUNCH: return 0.5;
		case KF_MOVE_FLY_KICK, KF_MOVE_WALL_FLY_KICK: return 1.0;
		case KF_MOVE_WALL_KICK: return 1.35;
	}
	return 0.5;
}

// Move::range.
static stock kf_range(m, &Float:lo, &Float:hi)
{
	lo = 0.0;
	switch (m)
	{
		case KF_MOVE_JUMP_KICK, KF_MOVE_JUMP_PUNCH: hi = 110.0;
		case KF_MOVE_BACK_KICK: hi = 70.0;
		case KF_MOVE_FLY_KICK:
		{
			lo = 90.0;
			hi = KF_FLY_KICK_MAX;
		}
		case KF_MOVE_WALL_KICK, KF_MOVE_WALL_FLY_KICK:
		{
			lo = 40.0;
			hi = 200.0;
		}
		default: hi = KF_STRIKE_DISTANCE + 24.0;
	}
}

static stock bool:kf_needs_wall(m)
{
	return m == KF_MOVE_WALL_KICK || m == KF_MOVE_WALL_FLY_KICK;
}

static stock kf_attack_button(m)
{
	return (m == KF_MOVE_PUNCH || m == KF_MOVE_JAB || m == KF_MOVE_HEAVY_PUNCH || m == KF_MOVE_JUMP_PUNCH) ? IN_ATTACK : IN_ATTACK2;
}

static stock bool:kf_fresh(m)
{
	return m != KF_MOVE_JAB && m != KF_MOVE_QUICK_KICK;
}

static stock bool:kf_airborne_move(m)
{
	return m == KF_MOVE_JUMP_KICK || m == KF_MOVE_JUMP_PUNCH || m == KF_MOVE_FLY_KICK || m == KF_MOVE_WALL_KICK || m == KF_MOVE_WALL_FLY_KICK;
}

// AttackKind::of.
static stock kf_attack_kind(buttons, bool:on_ground)
{
	if (!on_ground)
	{
		return KF_ATTACK_AIR;
	}
	if (buttons & IN_DUCK)
	{
		return KF_ATTACK_LOW;
	}
	if (buttons & IN_BACK)
	{
		return KF_ATTACK_BACK;
	}
	if (buttons & IN_ATTACK2)
	{
		return KF_ATTACK_KICK;
	}
	return KF_ATTACK_PUNCH;
}

// opp_class: human * 2 + aggressive.
static stock kf_opp_class(bool:human, Float:interval)
{
	return (human ? 2 : 0) + (interval < KF_AGGRESSIVE_INTERVAL ? 1 : 0);
}

// kungfu.rs is_ranged: TS weapon ids (kung fu 0) that shoot: not kung fu (0, 38), the knives
// (25, 35) or the katana (34).
stock bool:kungfu_is_ranged(game_id)
{
	return game_id != 0 && game_id != 25 && game_id != 34 && game_id != 35 && game_id != RC_KUNGFU;
}

// ---- the opponent as an Entity (a player's snapshot, else its entvars) ------------------------

static stock bool:kf_is_player(e)
{
	return e >= 1 && e <= MAX_PLAYERS;
}

// Entity::button.
static stock kf_ent_buttons(e)
{
	return kf_is_player(e) ? world_field(e, TSPS_BUTTONS) : pev(e, pev_button);
}

// Entity::flags & ONGROUND.
static stock bool:kf_ent_on_ground(e)
{
	return kf_is_player(e) ? (world_flags(e) & TSPF_ON_GROUND) != 0 : (pev(e, pev_flags) & FL_ONGROUND) != 0;
}

// Entity::health.
static stock Float:kf_ent_health(e)
{
	if (kf_is_player(e))
	{
		return world_health(e);
	}
	new Float:h;
	pev(e, pev_health, h);
	return h;
}

// Entity::origin.
static stock kf_ent_origin(e, Float:out[3])
{
	if (kf_is_player(e))
	{
		world_origin(e, out);
	}
	else
	{
		pev(e, pev_origin, out);
	}
}

// Entity::v_angle.
static stock kf_ent_vangle(e, Float:out[3])
{
	if (kf_is_player(e))
	{
		world_vangle(e, out);
	}
	else
	{
		pev(e, pev_v_angle, out);
	}
}

// Entity::entity_origin: a player's box middle, else world_entity_origin.
static stock kf_ent_center(e, Float:out[3])
{
	if (kf_is_player(e))
	{
		world_player_center(e, out);
	}
	else if (!world_entity_origin(e, out))
	{
		kf_ent_origin(e, out);
	}
}

// The opponent faces the bot: its yaw's forward against the way to the bot, in the plane.
static stock Float:kf_facing_dot(id, e)
{
	new Float:ang[3], Float:fwd[3], Float:right[3], Float:me[3], Float:at[3], Float:to_me[3];
	kf_ent_vangle(e, ang);
	ang[0] = 0.0;
	ang[2] = 0.0;
	make_vectors(ang, fwd, right);
	world_origin(id, me);
	kf_ent_origin(e, at);
	vec_sub(me, at, to_me);
	vec_normalize(to_me, to_me);
	return fwd[0] * to_me[0] + fwd[1] * to_me[1];
}

// ---- KungFu: the learned tables --------------------------------------------------------------

static stock kf_seed_combos(id)
{
	g_kf_combo_count[id] = kfc_seed_combos(g_kf_combo_moves[id], g_kf_combo_len[id], g_kf_combo_value[id], g_kf_combo_uses[id]);
}

// KungFu::new: a fighter that knows only the seed combos.
stock kungfu_bot_init(id)
{
	for (new a = 0; a < KF_STANCES; a++)
	{
		g_kf_stance_all[id][a][KF_ARM_VALUE] = 0.0;
		g_kf_stance_all[id][a][KF_ARM_N] = 0.0;
	}
	for (new a = 0; a < KF_OPP_CLASSES * KF_STANCES; a++)
	{
		g_kf_stances[id][a][KF_ARM_VALUE] = 0.0;
		g_kf_stances[id][a][KF_ARM_N] = 0.0;
	}
	for (new a = 0; a < KF_ANSWERS; a++)
	{
		g_kf_reaction_all[id][a][KF_ARM_VALUE] = 0.0;
		g_kf_reaction_all[id][a][KF_ARM_N] = 0.0;
	}
	for (new a = 0; a < KF_ATTACK_KINDS * KF_ANSWERS; a++)
	{
		g_kf_reactions[id][a][KF_ARM_VALUE] = 0.0;
		g_kf_reactions[id][a][KF_ARM_N] = 0.0;
	}
	kf_seed_combos(id);
	g_kf_finished[id] = 0;
	g_kf_opp_rows[id] = 0;
	kf_reset_fight(id);
}

// KungFu::reset_fight: Fight::default().
static stock kf_reset_fight(id)
{
	for (new Kf_I:k; k < Kf_I; k++)
	{
		g_kf_i[id][k] = 0;
	}
	for (new Kf_F:k; k < Kf_F; k++)
	{
		g_kf_f[id][k] = 0.0;
	}
	for (new Kf_V:k; k < Kf_V; k++)
	{
		vec_zero(g_kf_v[id][k]);
	}
	for (new k = 0; k < KF_ATTACK_KINDS; k++)
	{
		g_kf_opp_attacks[id][k] = 0;
	}
	g_kf_i[id][KF_STANCE] = -1;
	g_kf_i[id][KF_COMBO] = -1;
	g_kf_i[id][KF_REACTING] = -1;
}

// The learned arms as the codec's flat array, and back.
static stock kf_arms_out(id, Float:arms[])
{
	for (new s = 0; s < KF_STANCES; s++)
	{
		arms[(KFC_STANCE_ALL + s) * 2] = g_kf_stance_all[id][s][KF_ARM_VALUE];
		arms[(KFC_STANCE_ALL + s) * 2 + 1] = g_kf_stance_all[id][s][KF_ARM_N];
	}
	for (new a = 0; a < KF_OPP_CLASSES * KF_STANCES; a++)
	{
		arms[(KFC_STANCES + a) * 2] = g_kf_stances[id][a][KF_ARM_VALUE];
		arms[(KFC_STANCES + a) * 2 + 1] = g_kf_stances[id][a][KF_ARM_N];
	}
	for (new a = 0; a < KF_ANSWERS; a++)
	{
		arms[(KFC_REACTION_ALL + a) * 2] = g_kf_reaction_all[id][a][KF_ARM_VALUE];
		arms[(KFC_REACTION_ALL + a) * 2 + 1] = g_kf_reaction_all[id][a][KF_ARM_N];
	}
	for (new a = 0; a < KF_ATTACK_KINDS * KF_ANSWERS; a++)
	{
		arms[(KFC_REACTIONS + a) * 2] = g_kf_reactions[id][a][KF_ARM_VALUE];
		arms[(KFC_REACTIONS + a) * 2 + 1] = g_kf_reactions[id][a][KF_ARM_N];
	}
}

static stock kf_arms_in(id, const Float:arms[])
{
	for (new s = 0; s < KF_STANCES; s++)
	{
		g_kf_stance_all[id][s][KF_ARM_VALUE] = arms[(KFC_STANCE_ALL + s) * 2];
		g_kf_stance_all[id][s][KF_ARM_N] = arms[(KFC_STANCE_ALL + s) * 2 + 1];
	}
	for (new a = 0; a < KF_OPP_CLASSES * KF_STANCES; a++)
	{
		g_kf_stances[id][a][KF_ARM_VALUE] = arms[(KFC_STANCES + a) * 2];
		g_kf_stances[id][a][KF_ARM_N] = arms[(KFC_STANCES + a) * 2 + 1];
	}
	for (new a = 0; a < KF_ANSWERS; a++)
	{
		g_kf_reaction_all[id][a][KF_ARM_VALUE] = arms[(KFC_REACTION_ALL + a) * 2];
		g_kf_reaction_all[id][a][KF_ARM_N] = arms[(KFC_REACTION_ALL + a) * 2 + 1];
	}
	for (new a = 0; a < KF_ATTACK_KINDS * KF_ANSWERS; a++)
	{
		g_kf_reactions[id][a][KF_ARM_VALUE] = arms[(KFC_REACTIONS + a) * 2];
		g_kf_reactions[id][a][KF_ARM_N] = arms[(KFC_REACTIONS + a) * 2 + 1];
	}
}

// host bots.rs learned_file(b, "rkf"): botprofiles/<key>.rkf in RCBot's folder, the key from
// rcbot_learn (store::profile_key). False without the bot's learned data.
static stock bool:kf_file_path(id, out[], len)
{
	new h = g_ln_i[id][LN_HANDLE];
	if (h == 0)
	{
		return false;
	}
	new key[64], rel[96];
	rl_bot_key(RcLearn:h, key, charsmax(key));
	formatex(rel, charsmax(rel), "botprofiles/%s.rkf", key);
	files_path(out, len, rel);
	return true;
}

// host bots.rs new_bot: KungFu::load from botprofiles/<key>.rkf, if it is there.
stock kungfu_load(id)
{
	new path[RC_PATH_LEN];
	if (!kf_file_path(id, path, charsmax(path)))
	{
		return;
	}
	static text[KFC_TEXT], Float:arms[KFC_ARM_FLOATS];
	if (kfc_read_file(path, text, sizeof(text)) < 0)
	{
		return;
	}
	kf_arms_out(id, arms);
	new count = g_kf_combo_count[id];
	if (kfc_parse(text, arms, g_kf_combo_moves[id], g_kf_combo_len[id], g_kf_combo_value[id], g_kf_combo_uses[id], count))
	{
		kf_arms_in(id, arms);
		g_kf_combo_count[id] = count;
		if (g_e2e)
		{
			new what[48], used = 0;
			for (new k = 0; k < count; k++)
			{
				used += g_kf_combo_uses[id][k] > 0 ? 1 : 0;
			}
			formatex(what, charsmax(what), "loaded: %d combos, %d used", count, used);
			report_file(path, what);
		}
	}
	else
	{
		report_file(path, "not parsed: the defaults kept");
	}
}

// host bots.rs save_bot: KungFu::save to botprofiles/<key>.rkf (atomic).
stock kungfu_save(id)
{
	new path[RC_PATH_LEN];
	if (!kf_file_path(id, path, charsmax(path)))
	{
		return;
	}
	new dir[RC_PATH_LEN];
	files_path(dir, charsmax(dir), "botprofiles");
	if (!dir_exists(dir))
	{
		mkdir(dir);
	}
	static text[KFC_TEXT], Float:arms[KFC_ARM_FLOATS];
	kf_arms_out(id, arms);
	new len = kfc_format(arms, g_kf_combo_moves[id], g_kf_combo_len[id], g_kf_combo_value[id], g_kf_combo_uses[id], g_kf_combo_count[id], text, charsmax(text));
	if (!kfc_write_file(path, text, len))
	{
		rc_log(TSLOG_WARNING, "can't write %s", path);
	}
}

// KungFu::combo_runs: runs of every combo together (UCB1's total).
static stock Float:kf_combo_runs(id)
{
	new Float:t = 0.0;
	for (new c = 0; c < g_kf_combo_count[id]; c++)
	{
		t += float(g_kf_combo_uses[id][c]);
	}
	return t;
}

// KungFu::combo_done: a combo finished with reward (per second): its mean moves (never slower
// than a 10% step), and every BREED_EVERY combos a child of the best is bred.
static stock kf_combo_done(id, index, Float:reward)
{
	if (index < 0 || index >= g_kf_combo_count[id])
	{
		return;
	}
	if (reward != reward || reward == KF_INF || reward == KF_NEG_INF)
	{
		return;
	}
	g_kf_combo_uses[id][index]++;
	new Float:rate = fmax(1.0 / float(g_kf_combo_uses[id][index]), 0.1);
	g_kf_combo_value[id][index] += rate * (reward - g_kf_combo_value[id][index]);
	g_kf_finished[id]++;
	if (g_kf_finished[id] % KF_BREED_EVERY == 0)
	{
		kf_breed(id);
	}
}

// KungFu::breed: a child of the two best (a crossover, or the best mutated: a move changed,
// added or taken out), at its parent's value, untried. In a full pool it replaces the combo with
// the lowest upper bound among those not in the best PROTECTED_COMBOS that ran CULL_MIN_USES
// times and whose upper bound is under the best's lower bound; else it is dropped.
static stock kf_breed(id)
{
	new count = g_kf_combo_count[id];
	if (count == 0)
	{
		kf_seed_combos(id);
		return;
	}
	// The order by value, best first (a stable sort, as sort_by).
	new order[KF_MAX_COMBOS];
	for (new i = 0; i < count; i++)
	{
		new j = i;
		while (j > 0 && g_kf_combo_value[id][order[j - 1]] < g_kf_combo_value[id][i])
		{
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}
	new best = order[0];
	new child[KF_MAX_COMBO_LEN * 2 + 1], len = g_kf_combo_len[id][best];
	for (new m = 0; m < len; m++)
	{
		child[m] = g_kf_combo_moves[id][best][m];
	}
	if (count > 1 && random_num(0, 1) == 0)
	{
		new other = order[1], olen = g_kf_combo_len[id][other];
		new cut_a = random_num(1, len);
		new cut_b = random_num(0, olen - 1);
		len = cut_a;
		for (new m = cut_b; m < olen; m++)
		{
			child[len++] = g_kf_combo_moves[id][other][m];
		}
	}
	else
	{
		new mv = random_num(0, KF_MOVES - 1);
		new at = random_num(0, len - 1);
		switch (random_num(0, 2))
		{
			case 0: child[at] = mv;
			case 1:
			{
				for (new m = len; m > at; m--)
				{
					child[m] = child[m - 1];
				}
				child[at] = mv;
				len++;
			}
			default:
			{
				if (len > 1)
				{
					for (new m = at; m < len - 1; m++)
					{
						child[m] = child[m + 1];
					}
					len--;
				}
				else
				{
					child[len++] = mv;
				}
			}
		}
	}
	if (len > KF_MAX_COMBO_LEN)
	{
		len = KF_MAX_COMBO_LEN;
	}
	for (new c = 0; c < count; c++)
	{
		if (g_kf_combo_len[id][c] != len)
		{
			continue;
		}
		new bool:same = true;
		for (new m = 0; m < len && same; m++)
		{
			same = g_kf_combo_moves[id][c][m] == child[m];
		}
		if (same)
		{
			return;
		}
	}
	new Float:value = g_kf_combo_value[id][best];
	new slot = -1;
	if (count < KF_MAX_COMBOS)
	{
		slot = count;
		g_kf_combo_count[id] = count + 1;
	}
	else
	{
		new Float:total = kf_combo_runs(id);
		new Float:best_lcb = kf_lcb(value, float(g_kf_combo_uses[id][best]), total, KF_UCB_COMBO);
		new Float:lowest;
		for (new o = KF_PROTECTED_COMBOS; o < count; o++)
		{
			new c = order[o];
			if (g_kf_combo_uses[id][c] < KF_CULL_MIN_USES)
			{
				continue;
			}
			new Float:upper = kf_ucb(g_kf_combo_value[id][c], float(g_kf_combo_uses[id][c]), total, KF_UCB_COMBO);
			if (upper < best_lcb && (slot < 0 || upper < lowest))
			{
				slot = c;
				lowest = upper;
			}
		}
	}
	if (slot < 0)
	{
		return;
	}
	for (new m = 0; m < len; m++)
	{
		g_kf_combo_moves[id][slot][m] = child[m];
	}
	g_kf_combo_len[id][slot] = len;
	g_kf_combo_value[id][slot] = value;
	g_kf_combo_uses[id][slot] = 0;
}

// The row of KungFu::opponents for the fight's opponent key: -1 when there is none (or, with
// create, no room).
static stock kf_opp_row(id, bool:create)
{
	new kind = g_kf_i[id][KF_OPP_KEY_KIND];
	if (kind == 0)
	{
		return -1;
	}
	new kid = g_kf_i[id][KF_OPP_KEY_ID];
	new rows = g_kf_opp_rows[id];
	for (new r = 0; r < rows; r++)
	{
		if (g_kf_opp_key_kind[id][r] == kind && g_kf_opp_key_id[id][r] == kid)
		{
			return r;
		}
	}
	if (!create || rows >= RC_SLOTS)
	{
		// The Rust map grows without bound; past RC_SLOTS keys a map the newest go unlearned.
		return -1;
	}
	g_kf_opp_key_kind[id][rows] = kind;
	g_kf_opp_key_id[id][rows] = kid;
	for (new k = 0; k < KF_STANCES * 2; k++)
	{
		g_kf_opp_stances[id][rows][k] = 0.0;
	}
	for (new k = 0; k < KF_ATTACK_KINDS * KF_ANSWERS * 2; k++)
	{
		g_kf_opp_reactions[id][rows][k] = 0.0;
	}
	g_kf_opp_rows[id] = rows + 1;
	return rows;
}

// KungFu::lesson_weight.
static stock Float:kf_lesson_weight(id)
{
	return g_kf_i[id][KF_OPP_HUMAN] ? KF_HUMAN_WEIGHT : 1.0;
}

// KungFu::stance_estimate: stance s's blended value (returned) and its count, for class.
static stock Float:kf_stance_estimate(id, class, s, &Float:n)
{
	new a = class * KF_STANCES + s;
	new Float:v = kf_shrunk(g_kf_stances[id][a][KF_ARM_VALUE], g_kf_stances[id][a][KF_ARM_N], g_kf_stance_all[id][s][KF_ARM_VALUE], KF_SHRINK);
	n = g_kf_stances[id][a][KF_ARM_N];
	new r = kf_opp_row(id, false);
	if (r >= 0)
	{
		new Float:on = g_kf_opp_stances[id][r][s * 2 + 1];
		v = kf_shrunk(g_kf_opp_stances[id][r][s * 2], on, v, KF_SHRINK);
		n += on;
	}
	return v;
}

// KungFu::answer_estimate: answer a's blended value (returned) and its count, against kind.
static stock Float:kf_answer_estimate(id, kind, a, &Float:n)
{
	new k = kind * KF_ANSWERS + a;
	new Float:v = kf_shrunk(g_kf_reactions[id][k][KF_ARM_VALUE], g_kf_reactions[id][k][KF_ARM_N], g_kf_reaction_all[id][a][KF_ARM_VALUE], KF_SHRINK);
	n = g_kf_reactions[id][k][KF_ARM_N];
	new r = kf_opp_row(id, false);
	if (r >= 0)
	{
		new Float:on = g_kf_opp_reactions[id][r][k * 2 + 1];
		v = kf_shrunk(g_kf_opp_reactions[id][r][k * 2], on, v, KF_SHRINK);
		n += on;
	}
	return v;
}

// KungFu::learn_stance: scores stance s for class at every level.
static stock kf_learn_stance(id, class, s, Float:reward)
{
	new Float:w = kf_lesson_weight(id);
	kf_arm_update(g_kf_stance_all[id][s][KF_ARM_VALUE], g_kf_stance_all[id][s][KF_ARM_N], reward, w);
	new a = class * KF_STANCES + s;
	kf_arm_update(g_kf_stances[id][a][KF_ARM_VALUE], g_kf_stances[id][a][KF_ARM_N], reward, w);
	new r = kf_opp_row(id, true);
	if (r >= 0)
	{
		kf_arm_update(g_kf_opp_stances[id][r][s * 2], g_kf_opp_stances[id][r][s * 2 + 1], reward, w);
	}
}

// KungFu::learn_answer: scores answer a to kind at every level.
static stock kf_learn_answer(id, kind, a, Float:reward)
{
	new Float:w = kf_lesson_weight(id);
	kf_arm_update(g_kf_reaction_all[id][a][KF_ARM_VALUE], g_kf_reaction_all[id][a][KF_ARM_N], reward, w);
	new k = kind * KF_ANSWERS + a;
	kf_arm_update(g_kf_reactions[id][k][KF_ARM_VALUE], g_kf_reactions[id][k][KF_ARM_N], reward, w);
	new r = kf_opp_row(id, true);
	if (r >= 0)
	{
		kf_arm_update(g_kf_opp_reactions[id][r][k * 2], g_kf_opp_reactions[id][r][k * 2 + 1], reward, w);
	}
}

// KungFu::decay: a new fight, every count fades a little.
static stock kf_decay(id)
{
	for (new a = 0; a < KF_STANCES; a++)
	{
		g_kf_stance_all[id][a][KF_ARM_N] *= KF_FIGHT_DECAY;
	}
	for (new a = 0; a < KF_OPP_CLASSES * KF_STANCES; a++)
	{
		g_kf_stances[id][a][KF_ARM_N] *= KF_FIGHT_DECAY;
	}
	for (new a = 0; a < KF_ANSWERS; a++)
	{
		g_kf_reaction_all[id][a][KF_ARM_N] *= KF_FIGHT_DECAY;
	}
	for (new a = 0; a < KF_ATTACK_KINDS * KF_ANSWERS; a++)
	{
		g_kf_reactions[id][a][KF_ARM_N] *= KF_FIGHT_DECAY;
	}
	for (new r = 0; r < g_kf_opp_rows[id]; r++)
	{
		for (new k = 1; k < KF_STANCES * 2; k += 2)
		{
			g_kf_opp_stances[id][r][k] *= KF_FIGHT_DECAY;
		}
		for (new k = 1; k < KF_ATTACK_KINDS * KF_ANSWERS * 2; k += 2)
		{
			g_kf_opp_reactions[id][r][k] *= KF_FIGHT_DECAY;
		}
	}
}

// ---- Fight ----------------------------------------------------------------------------------------

// KungFu::opponent: 0 none.
stock kungfu_opponent(id)
{
	return g_kf_i[id][KF_OPPONENT];
}

// KungFu::fighting: the fight ran within gap seconds.
stock bool:kungfu_fighting(id, Float:gap)
{
	return g_kf_i[id][KF_OPPONENT] != 0 && g_now - g_kf_f[id][KF_UPDATED] <= gap;
}

// think.rs spawn_init's kung fu part: what is open is settled (broken off), the fight forgotten.
stock kungfu_spawn_init(id)
{
	kungfu_settle(id, KF_END_BROKEN);
	kf_reset_fight(id);
}

// Bot::kf_power: the game's bar (CombatState::melee_power, always in tsx's snapshot); for an
// empty snapshot the estimate (100, +15 a second).
static stock Float:kf_power(id, Float:now)
{
	if (world_userid(id) != 0)
	{
		g_kf_f[id][KF_POWER] = fclamp(world_combat(id, TSCOMBAT_MELEE_POWER), 0.0, 100.0);
	}
	else if (g_kf_f[id][KF_POWER_TIME] == 0.0)
	{
		g_kf_f[id][KF_POWER] = 100.0;
	}
	else
	{
		g_kf_f[id][KF_POWER] = fmin(g_kf_f[id][KF_POWER] + fmax(now - g_kf_f[id][KF_POWER_TIME], 0.0) * KF_POWER_REGEN, 100.0);
	}
	g_kf_f[id][KF_POWER_TIME] = now;
	return g_kf_f[id][KF_POWER];
}

// Bot::kf_ready: the last move's recovery is over, and the game's next_attack_at.
static stock bool:kf_ready(id, Float:now)
{
	return g_kf_f[id][KF_RECOVER_UNTIL] <= now && world_combat(id, TSCOMBAT_NEXT_ATTACK) <= now;
}

// Bot::kf_reaction_time: the profile's, shorter for skilled bots (at least 0.08 s).
static stock Float:kf_reaction_time(id)
{
	new skill = g_pr_i[id][PR_SKILL];
	skill = skill < 0 ? 0 : (skill > 100 ? 100 : skill);
	return fmax(g_th_f[id][TH_REACTION_TIME] * (1.0 - 0.7 * (float(skill) / 100.0)), 0.08);
}

// Bot::kf_trade: damage dealt to and taken from the fight's opponent since since (the ledger).
static stock kf_trade(id, Float:since, &Float:dealt, &Float:taken)
{
	dealt = 0.0;
	taken = 0.0;
	new o = g_kf_i[id][KF_OPPONENT], h = g_ln_i[id][LN_HANDLE];
	if (o == 0 || h == 0)
	{
		return;
	}
	dealt = rl_ledger_dealt_since(RcLearn:h, since, o);
	taken = rl_ledger_taken_since(RcLearn:h, since, o);
}

// Bot::kf_combo_reward: the trade per 30 health per second (at least half a second).
static stock Float:kf_combo_reward(id, Float:start, Float:now)
{
	new Float:dealt, Float:taken;
	kf_trade(id, start, dealt, taken);
	return fclamp((dealt - taken) / KF_REWARD_HEALTH / fmax(now - start, 0.5), -4.0, 4.0);
}

// Bot::kf_stance_reward: the trade per 30 health.
static stock Float:kf_stance_reward(id, Float:start)
{
	new Float:dealt, Float:taken;
	kf_trade(id, start, dealt, taken);
	return fclamp((dealt - taken) / KF_REWARD_HEALTH, -2.0, 2.0);
}

// Bot::kf_settle: the fight ended (or broke off): the open combo, stance and answer are scored by
// how (a kill +2, a death -2, else what they earned so far), and what was seen of the opponent
// goes to the opponent model. Nothing is scored twice.
stock kungfu_settle(id, fight_end)
{
	if (g_kf_i[id][KF_OPPONENT] == 0)
	{
		return;
	}
	new Float:now = g_now;
	new index = g_kf_i[id][KF_COMBO];
	if (index >= 0)
	{
		g_kf_i[id][KF_COMBO] = -1;
		new Float:start = g_kf_f[id][KF_COMBO_START];
		switch (fight_end)
		{
			case KF_END_KILL: kf_combo_done(id, index, 2.0);
			case KF_END_DEATH: kf_combo_done(id, index, -2.0);
			default:
			{
				if (now - start >= 0.25)
				{
					kf_combo_done(id, index, kf_combo_reward(id, start, now));
				}
			}
		}
	}
	new st = g_kf_i[id][KF_STANCE];
	if (st >= 0)
	{
		g_kf_i[id][KF_STANCE] = -1;
		new Float:start = g_kf_f[id][KF_STANCE_START];
		new class = g_kf_i[id][KF_STANCE_CLASS];
		switch (fight_end)
		{
			case KF_END_KILL: kf_learn_stance(id, class, st, 2.0);
			case KF_END_DEATH: kf_learn_stance(id, class, st, -2.0);
			default:
			{
				if (now - start >= 0.5)
				{
					kf_learn_stance(id, class, st, kf_stance_reward(id, start));
				}
			}
		}
	}
	kf_judge_reaction(id, now, fight_end == KF_END_DEATH);
	// observe_kungfu, under the key the fight began with (the slot's player may have changed).
	new o = g_kf_i[id][KF_OPPONENT];
	new kind, kid;
	if (g_kf_i[id][KF_OPP_KEY_KIND] != 0 && learning_player_key(o, kind, kid) && kind == g_kf_i[id][KF_OPP_KEY_KIND] && kid == g_kf_i[id][KF_OPP_KEY_ID])
	{
		new Float:interval = g_kf_f[id][KF_OPP_LAST_ATTACK] > 0.0 ? g_kf_f[id][KF_OPP_INTERVAL] : -1.0;
		rl_opp_kungfu(o, g_kf_opp_attacks[id], KF_ATTACK_KINDS, g_kf_i[id][KF_OPP_BLOCKS], interval);
	}
	for (new k = 0; k < KF_ATTACK_KINDS; k++)
	{
		g_kf_opp_attacks[id][k] = 0;
	}
	g_kf_i[id][KF_OPP_BLOCKS] = 0;
	g_kf_i[id][KF_PENDING_SET] = 0;
}

// Bot::kf_watch: the opponent's new presses: an attack starts its threat (its kind noted), a
// block (+reload with kung fu or a blade) its block window.
static stock kf_watch(id, enemy, Float:dist, Float:now)
{
	new buttons = kf_ent_buttons(enemy);
	new pressed = buttons & ~g_kf_i[id][KF_OPP_BUTTONS];
	g_kf_i[id][KF_OPP_BUTTONS] = buttons;
	new bool:player = kf_is_player(enemy);
	new bool:melee = player && !kungfu_is_ranged(world_weapon(enemy));
	if (melee && (pressed & IN_RELOAD))
	{
		g_kf_f[id][KF_OPP_BLOCK_UNTIL] = now + KF_BLOCK_TIME;
		g_kf_i[id][KF_OPP_BLOCKS]++;
	}
	// A new hit window is an attack under way even without a fresh press (held jabs).
	new Float:window = player ? world_combat(enemy, TSCOMBAT_MELEE_WINDOW) : 0.0;
	new bool:new_window = melee && window > now && window != g_kf_f[id][KF_OPP_WINDOW];
	g_kf_f[id][KF_OPP_WINDOW] = window;
	new bool:attacking = (pressed & (IN_ATTACK | IN_ATTACK2)) != 0 || new_window;
	new kind = kf_attack_kind(buttons, kf_ent_on_ground(enemy));
	if (attacking)
	{
		if (g_kf_f[id][KF_OPP_LAST_ATTACK] > 0.0)
		{
			new Float:gap = fmin(now - g_kf_f[id][KF_OPP_LAST_ATTACK], 3.0);
			g_kf_f[id][KF_OPP_INTERVAL] += 0.3 * (gap - g_kf_f[id][KF_OPP_INTERVAL]);
		}
		g_kf_f[id][KF_OPP_LAST_ATTACK] = now;
		if (melee)
		{
			g_kf_opp_attacks[id][kind]++;
		}
	}
	// Facing the bot within reach and attacking: answer after the reaction time.
	new bool:facing = kf_facing_dot(id, enemy) > 0.7;
	if (melee && facing && dist < KF_THREAT_RANGE && g_kf_i[id][KF_REACTING] < 0 && !g_kf_i[id][KF_PENDING_SET])
	{
		new Float:rt = kf_reaction_time(id);
		if (attacking)
		{
			g_kf_i[id][KF_PENDING_SET] = 1;
			g_kf_f[id][KF_PENDING_AT] = now + rt;
			g_kf_i[id][KF_PENDING_SEEN] = 1;
			g_kf_i[id][KF_PENDING_KIND] = kind;
		}
		else if (g_kf_i[id][KF_STANCE] == KF_STANCE_DEFENSIVE && g_kf_f[id][KF_OPP_LAST_ATTACK] > 0.0
			&& now - g_kf_f[id][KF_OPP_LAST_ATTACK] > g_kf_f[id][KF_OPP_INTERVAL] * 0.8)
		{
			// Its usual pause is over: read the next attack, half the time (a guess, not scored).
			if (random_num(0, 1) == 0)
			{
				g_kf_i[id][KF_PENDING_SET] = 1;
				g_kf_f[id][KF_PENDING_AT] = now + rt;
				g_kf_i[id][KF_PENDING_SEEN] = 0;
				g_kf_i[id][KF_PENDING_KIND] = KF_ATTACK_PUNCH;
			}
		}
	}
}

// Bot::kf_choose_stance: a stance for the next 2-4 s by UCB1 over what each earned against this
// class of opponent (and this opponent), nudged by both healths and the power; the stance that
// ends is scored by the trade while it held.
static stock kf_choose_stance(id, enemy, Float:now)
{
	new Float:me = world_health(id);
	new Float:power = kf_power(id, now);
	new st = g_kf_i[id][KF_STANCE];
	if (st >= 0 && g_kf_f[id][KF_STANCE_UNTIL] > now)
	{
		return;
	}
	if (st >= 0)
	{
		kf_learn_stance(id, g_kf_i[id][KF_STANCE_CLASS], st, kf_stance_reward(id, g_kf_f[id][KF_STANCE_START]));
	}
	new class = kf_opp_class(g_kf_i[id][KF_OPP_HUMAN] != 0, g_kf_f[id][KF_OPP_INTERVAL]);
	new Float:n_def, Float:n_off;
	new Float:def = kf_stance_estimate(id, class, KF_STANCE_DEFENSIVE, n_def);
	new Float:off = kf_stance_estimate(id, class, KF_STANCE_OFFENSIVE, n_off);
	new Float:def_bonus = 0.0, Float:off_bonus = 0.0;
	new Float:enemy_health = kf_ent_health(enemy);
	if (me < enemy_health)
	{
		def_bonus += 0.3;
	}
	else
	{
		off_bonus += 0.2;
	}
	if (enemy_health < 35.0)
	{
		off_bonus += 0.3;
	}
	if (g_kf_f[id][KF_OPP_INTERVAL] < 0.6)
	{
		def_bonus += 0.2;
	}
	if (power < KF_BLOCK_COST + 1.0)
	{
		off_bonus += 0.25;
	}
	new Float:total = n_def + n_off;
	new Float:scores[2];
	scores[0] = kf_ucb(def, n_def, total, KF_UCB_CHOICE) + def_bonus;
	scores[1] = kf_ucb(off, n_off, total, KF_UCB_CHOICE) + off_bonus;
	g_kf_i[id][KF_STANCE] = kf_argmax(scores, 2) == 1 ? KF_STANCE_OFFENSIVE : KF_STANCE_DEFENSIVE;
	g_kf_f[id][KF_STANCE_UNTIL] = now + random_float(2.0, 4.0);
	g_kf_f[id][KF_STANCE_START] = now;
	g_kf_i[id][KF_STANCE_CLASS] = class;
}

// Bot::kf_judge_reaction: scores the answer whose time is up (or, at the bot's death, any open
// one): did the opponent hurt the bot meanwhile? A block also pays its power. Answers to
// predicted attacks aren't scored.
static stock kf_judge_reaction(id, Float:now, bool:died)
{
	new answer = g_kf_i[id][KF_REACTING];
	if (answer < 0)
	{
		return;
	}
	if (g_kf_f[id][KF_REACT_JUDGE_AT] > now && !died)
	{
		return;
	}
	g_kf_i[id][KF_REACTING] = -1;
	new Float:dealt, Float:lost;
	kf_trade(id, g_kf_f[id][KF_REACT_START], dealt, lost);
	new bool:avoided = lost <= 0.0 && !died;
	if (avoided && answer != KF_NO_REACTION)
	{
		// It worked: hit back while the opponent recovers.
		g_kf_f[id][KF_COUNTER_UNTIL] = now + 0.6;
	}
	if (!g_kf_i[id][KF_REACT_SEEN])
	{
		return;
	}
	new Float:reward = died ? -2.0 : (avoided ? 1.0 : -fmin(lost / KF_REWARD_HEALTH, 2.0));
	if (answer == KF_REACT_BLOCK)
	{
		reward -= KF_BLOCK_PENALTY;
	}
	kf_learn_answer(id, g_kf_i[id][KF_REACT_KIND], answer, reward);
}

// Bot::kf_pick_answer: by UCB1 over the answers' estimates for a seen attack (not reacting
// included), the best estimate for a predicted one; no block without the power for it.
static stock kf_pick_answer(id, kind, bool:seen, bool:can_block)
{
	new Float:v[KF_ANSWERS], Float:n[KF_ANSWERS], Float:total = 0.0;
	for (new a = 0; a < KF_ANSWERS; a++)
	{
		v[a] = kf_answer_estimate(id, kind, a, n[a]);
		total += n[a];
	}
	new Float:scores[KF_ANSWERS];
	for (new a = 0; a < KF_ANSWERS; a++)
	{
		if (a == KF_REACT_BLOCK && !can_block)
		{
			scores[a] = KF_NEG_INF;
		}
		else
		{
			scores[a] = seen ? kf_ucb(v[a], n[a], total, KF_UCB_CHOICE) : v[a];
		}
	}
	new best = kf_argmax(scores, KF_ANSWERS);
	return best < 0 ? KF_NO_REACTION : best;
}

// Bot::kf_react: runs a due answer and keeps a running one going. True while reacting.
static stock bool:kf_react(id, const Float:origin[3], Float:dist, Float:now, &press)
{
	if (g_kf_i[id][KF_PENDING_SET] && g_kf_f[id][KF_PENDING_AT] <= now)
	{
		g_kf_i[id][KF_PENDING_SET] = 0;
		new bool:seen = g_kf_i[id][KF_PENDING_SEEN] != 0, kind = g_kf_i[id][KF_PENDING_KIND];
		// An offensive bot in the middle of a combo doesn't answer: that is the baseline.
		new bool:offensive = g_kf_i[id][KF_STANCE] == KF_STANCE_OFFENSIVE;
		new bool:busy = g_kf_i[id][KF_COMBO] >= 0;
		if (dist < KF_THREAT_RANGE)
		{
			new Float:power = kf_power(id, now);
			new bool:can_block = power >= KF_BLOCK_COST && g_kf_f[id][KF_RECOVER_UNTIL] <= now;
			new answer = offensive && busy ? KF_NO_REACTION : kf_pick_answer(id, kind, seen, can_block);
			new Float:until = now + (answer == KF_REACT_BLOCK ? KF_BLOCK_TIME : (answer < KF_NO_REACTION ? 0.4 : 0.0));
			g_kf_i[id][KF_REACTING] = answer;
			g_kf_f[id][KF_REACT_UNTIL] = until;
			g_kf_f[id][KF_REACT_START] = now;
			g_kf_f[id][KF_REACT_JUDGE_AT] = now + KF_REACTION_EVAL;
			g_kf_i[id][KF_REACT_SEEN] = seen ? 1 : 0;
			g_kf_i[id][KF_REACT_KIND] = kind;
			if (answer < KF_NO_REACTION)
			{
				g_kf_i[id][KF_COMBO] = -1;
				g_kf_i[id][KF_FACE_SET] = 0;
			}
			if (answer == KF_REACT_BLOCK)
			{
				g_kf_f[id][KF_POWER] -= KF_BLOCK_COST;
				g_kf_f[id][KF_BLOCK_UNTIL] = until;
				press |= IN_RELOAD;
			}
			g_kf_f[id][KF_SIDE] = random_num(0, 1) == 0 ? 1.0 : -1.0;
		}
	}
	new r = g_kf_i[id][KF_REACTING];
	if (r < 0 || r >= KF_NO_REACTION || g_kf_f[id][KF_REACT_UNTIL] <= now)
	{
		return false;
	}
	new Float:me[3], Float:away[3], Float:to[3];
	world_origin(id, me);
	vec_sub(me, origin, away);
	vec_normalize(away, away);
	switch (r)
	{
		case KF_REACT_BLOCK: movement_stop_moving(id);
		case KF_REACT_DUCK:
		{
			// The duck timers are movement's; kungfu.rs sets them here.
			g_mv_f[id][MV_START_DUCK_TIME] = now;
			g_mv_f[id][MV_END_DUCK_TIME] = g_kf_f[id][KF_REACT_UNTIL];
			movement_stop_moving(id);
		}
		case KF_REACT_STEP_BACK:
		{
			vec_mad(me, away, 96.0, to);
			movement_set_move_vector(id, to);
		}
		default:
		{
			new Float:side[3];
			new Float:s = g_kf_f[id][KF_SIDE];
			side[0] = -away[1] * s;
			side[1] = away[0] * s;
			side[2] = 0.0;
			vec_mad(me, side, 96.0, to);
			movement_set_move_vector(id, to);
		}
	}
	return true;
}

// Bot::kf_find_wall: a wall behind the bot, away from the opponent and near enough to reach,
// solid at the feet and at the chest, to wall jump off.
static stock bool:kf_find_wall(id, const Float:opponent[3], Float:out[3])
{
	new Float:o[3], Float:away[3], Float:end[3], Float:pos[3], hit;
	world_origin(id, o);
	vec_sub(o, opponent, away);
	away[2] = 0.0;
	vec_normalize(away, away);
	vec_mad(o, away, 72.0, end);
	if (world_trace_line(o, end, true, false, id, out, hit) >= 1.0)
	{
		return false;
	}
	new Float:up[3];
	vec_copy(o, up);
	up[2] += 24.0;
	vec_mad(up, away, 80.0, end);
	return world_trace_line(up, end, true, false, id, pos, hit) < 1.0;
}

// Bot::kf_pick_combo: by UCB1 among the combos whose first move fits the distance (and the wall,
// for a wall jump); -1 when none fits.
static stock kf_pick_combo(id, Float:dist)
{
	// A wall jump is a stunt: none while one cools down.
	new bool:wall = g_kf_i[id][KF_WALL_SET] && movement_stunt_ready(id);
	new Float:total = kf_combo_runs(id);
	new Float:scores[KF_MAX_COMBOS];
	new count = g_kf_combo_count[id];
	for (new c = 0; c < count; c++)
	{
		new first = g_kf_combo_moves[id][c][0];
		new Float:lo, Float:hi;
		kf_range(first, lo, hi);
		new bool:fits = dist >= lo && dist <= hi && (wall || !kf_needs_wall(first));
		scores[c] = fits ? kf_ucb(g_kf_combo_value[id][c], float(g_kf_combo_uses[id][c]), total, KF_UCB_COMBO) : KF_NEG_INF;
	}
	return kf_argmax(scores, count);
}

// Bot::kf_close_in: keeps in reach while a ground move waits (the back kick steps away instead).
static stock kf_close_in(id, m, const Float:origin[3], Float:dist)
{
	if (m == KF_MOVE_BACK_KICK)
	{
		new Float:me[3], Float:away[3], Float:to[3];
		world_origin(id, me);
		vec_sub(me, origin, away);
		vec_normalize(away, away);
		vec_mad(me, away, 96.0, to);
		movement_set_move_vector(id, to);
	}
	else if (dist > KF_STRIKE_DISTANCE)
	{
		movement_set_move_vector(id, origin);
	}
	else
	{
		movement_stop_moving(id);
	}
}

// Bot::kf_next_step: the move went (or was given up): the next step after its recovery.
static stock kf_next_step(id, m, Float:now)
{
	new Float:rec = kf_recovery(m);
	g_kf_i[id][KF_PHASE] = 0;
	g_kf_f[id][KF_PHASE_TIME] = now;
	g_kf_i[id][KF_FACE_SET] = 0;
	g_kf_f[id][KF_RECOVER_UNTIL] = now + rec;
	g_kf_i[id][KF_COMBO_STEP]++;
	g_kf_f[id][KF_COMBO_AT] = now + rec;
}

// Bot::kf_phase.
static stock kf_phase(id, phase, Float:now)
{
	g_kf_i[id][KF_PHASE] = phase;
	g_kf_f[id][KF_PHASE_TIME] = now;
}

// Faces the wall at eye height (the fight's face).
static stock kf_face_wall(id)
{
	if (g_kf_i[id][KF_WALL_SET])
	{
		vec_copy(g_kf_v[id][KF_WALL], g_kf_v[id][KF_FACE]);
		g_kf_v[id][KF_FACE][2] += Float:world_field(id, TSPS_VIEWOFS + 2);
		g_kf_i[id][KF_FACE_SET] = 1;
	}
	else
	{
		g_kf_i[id][KF_FACE_SET] = 0;
	}
}

// Bot::kf_airborne: the jump moves, step by step over the thinks. True once the move is done or
// given up.
//  * Jump kick and punch: a crouch jump, then the attack in the air with +duck held.
//  * Fly kick: run at the opponent and crouch jump into it from up to 150 units, then +duck
//    +attack2 when in reach.
//  * Wall kick: face the wall behind, walk to it, +alt1 (the wall jump flies the bot backwards),
//    then +attack2: the back kick. The wall fly kick turns to the opponent in the air instead
//    and kicks with +duck.
static stock bool:kf_airborne(id, m, const Float:origin[3], Float:dist, Float:now, &press)
{
	new bool:on_ground = (world_flags(id) & TSPF_ON_GROUND) != 0;
	new button = kf_attack_button(m);
	new Float:since = now - g_kf_f[id][KF_PHASE_TIME];
	switch (g_kf_i[id][KF_PHASE])
	{
		case 0:
		{
			// Getting there.
			if (since > 1.5)
			{
				return true;
			}
			if (kf_needs_wall(m))
			{
				if (!g_kf_i[id][KF_WALL_SET])
				{
					return true;
				}
				kf_face_wall(id);
				movement_set_move_vector(id, g_kf_v[id][KF_WALL]);
				new Float:me[3];
				world_origin(id, me);
				new bool:near = vec_distance_2d(g_kf_v[id][KF_WALL], me) < 40.0;
				if (near && on_ground && look_dot_from_origin(id, g_kf_v[id][KF_FACE]) > 0.8)
				{
					// CheckForWallJump's 48 units ahead, from the origin, along the view.
					new Float:fwd[3], Float:right[3], Float:end[3], Float:pos[3], hit;
					make_vectors(g_co_v[id][CO_VANGLE], fwd, right);
					vec_mad(me, fwd, 48.0, end);
					if (world_trace_line(me, end, true, false, id, pos, hit) < 1.0 && !(g_kf_i[id][KF_LAST_BUTTONS] & IN_ALT1) && movement_stunt_ready(id))
					{
						press |= IN_ALT1;
						kf_phase(id, 1, now);
					}
				}
				return false;
			}
			movement_set_move_vector(id, origin);
			new Float:jump_from = m == KF_MOVE_FLY_KICK ? 150.0 : 110.0;
			if (on_ground && dist <= jump_from && !(g_kf_i[id][KF_LAST_BUTTONS] & IN_JUMP))
			{
				// A fly kick wants a run-up: full speed toward the opponent first.
				if (m == KF_MOVE_FLY_KICK && g_th_f[id][TH_VELOCITY_2D] < 200.0 && dist > 90.0)
				{
					return false;
				}
				press |= IN_JUMP | IN_DUCK;
				kf_phase(id, 1, now);
			}
			return false;
		}
		case 1:
		{
			// Waiting to leave the ground; a jump that never does is given up.
			if (kf_needs_wall(m))
			{
				kf_face_wall(id);
			}
			else
			{
				press |= IN_DUCK;
				movement_set_move_vector(id, origin);
			}
			if (!on_ground)
			{
				kf_phase(id, 2, now);
			}
			else if (since > 0.6)
			{
				return true;
			}
			return false;
		}
	}
	if (on_ground && since > 0.1)
	{
		// Landed without the kick.
		return true;
	}
	new Float:kick_at;
	if (m == KF_MOVE_WALL_KICK)
	{
		// Keep looking at the wall: flying backwards is what makes the back kick.
		kf_face_wall(id);
		kick_at = 70.0;
	}
	else if (m == KF_MOVE_WALL_FLY_KICK)
	{
		// Turn round to the opponent, then kick when facing it.
		g_kf_i[id][KF_FACE_SET] = 0;
		press |= IN_DUCK;
		if (look_dot_from_origin(id, origin) < 0.7)
		{
			return false;
		}
		kick_at = 90.0;
	}
	else
	{
		press |= IN_DUCK;
		movement_set_move_vector(id, origin);
		kick_at = m == KF_MOVE_FLY_KICK ? 80.0 : 70.0;
	}
	// In reach, or coming down: kick now.
	new Float:vel[3];
	world_velocity(id, vel);
	if (dist <= kick_at || vel[2] < -150.0)
	{
		press |= button;
		return true;
	}
	return false;
}

// Bot::kf_run_combo: one step of the running combo, when its time comes.
static stock kf_run_combo(id, const Float:origin[3], Float:dist, Float:now, &press)
{
	new index = g_kf_i[id][KF_COMBO];
	if (index < 0)
	{
		return;
	}
	if (index >= g_kf_combo_count[id])
	{
		g_kf_i[id][KF_COMBO] = -1;
		return;
	}
	new step = g_kf_i[id][KF_COMBO_STEP];
	new Float:at = g_kf_f[id][KF_COMBO_AT];
	if (step >= g_kf_combo_len[id][index])
	{
		// Done: judge it once the last hit had its chance.
		g_kf_i[id][KF_FACE_SET] = 0;
		if (at <= now)
		{
			new Float:reward = kf_combo_reward(id, g_kf_f[id][KF_COMBO_START], now);
			kf_combo_done(id, index, reward);
			g_kf_i[id][KF_COMBO] = -1;
		}
		return;
	}
	new m = g_kf_combo_moves[id][index][step];
	if (at > now)
	{
		// A held move keeps the button down while it waits; a fresh one lets go.
		if (!kf_fresh(m))
		{
			press |= kf_attack_button(m) & g_kf_i[id][KF_LAST_BUTTONS];
		}
		if (!kf_airborne_move(m))
		{
			kf_close_in(id, m, origin, dist);
		}
		return;
	}
	new button = kf_attack_button(m);
	if (kf_airborne_move(m))
	{
		if (kf_airborne(id, m, origin, dist, now, press))
		{
			kf_next_step(id, m, now);
		}
		return;
	}
	kf_close_in(id, m, origin, dist);
	if (dist > KF_STRIKE_DISTANCE + 24.0 && m != KF_MOVE_BACK_KICK)
	{
		// Out of reach: wait for the distance to close.
		return;
	}
	if (!kf_ready(id, now))
	{
		// The game won't take the attack yet: a press now is lost.
		return;
	}
	if (kf_fresh(m) && (g_kf_i[id][KF_LAST_BUTTONS] & button))
	{
		// Let go for a think so the game sees a fresh press.
		return;
	}
	switch (m)
	{
		case KF_MOVE_HEAVY_PUNCH, KF_MOVE_SWEEP: press |= IN_DUCK | button;
		case KF_MOVE_BACK_KICK: press |= IN_BACK | button;
		default: press |= button;
	}
	kf_next_step(id, m, now);
}

// Bot::kf_offence: stance movement, counter-attacks and combos.
static stock kf_offence(id, enemy, const Float:origin[3], Float:dist, Float:now, &press)
{
	new stance = g_kf_i[id][KF_STANCE] < 0 ? KF_STANCE_OFFENSIVE : g_kf_i[id][KF_STANCE];
	new bool:player = kf_is_player(enemy);
	// The opponent can't strike before its next_attack_at and has no hit coming: it is
	// recovering from a move, the moment to hit back.
	new bool:opp_recovering = player && world_combat(enemy, TSCOMBAT_NEXT_ATTACK) > now + 0.1 && world_combat(enemy, TSCOMBAT_MELEE_WINDOW) <= now;
	new bool:countering = g_kf_f[id][KF_COUNTER_UNTIL] > now || opp_recovering;
	// The opponent's block is up: the game's block window, else a guess from its +reload.
	new bool:opp_blocking = player ? world_combat(enemy, TSCOMBAT_BLOCK_KUNGFU) > now : g_kf_f[id][KF_OPP_BLOCK_UNTIL] > now;
	// A passive opponent (no attack for a while) is attacked even from the defensive stance.
	new Float:last = g_kf_f[id][KF_OPP_LAST_ATTACK];
	new bool:passive = last == 0.0 || now - last > 2.0;
	new bool:attack = (stance == KF_STANCE_OFFENSIVE || countering || passive) && !opp_blocking;

	if (g_kf_i[id][KF_COMBO] < 0)
	{
		g_kf_i[id][KF_FACE_SET] = 0;
		new Float:me[3], Float:away[3], Float:to[3];
		world_origin(id, me);
		vec_sub(me, origin, away);
		vec_normalize(away, away);
		if (attack)
		{
			movement_set_move_vector(id, origin);
		}
		else if (dist < KF_GUARD_DISTANCE - 16.0)
		{
			vec_mad(me, away, 64.0, to);
			movement_set_move_vector(id, to);
		}
		else if (dist > KF_GUARD_DISTANCE + 16.0)
		{
			movement_set_move_vector(id, origin);
		}
		else
		{
			// Circle at guard distance (the strafe fields are movement's; kungfu.rs sets them).
			if (g_mv_f[id][MV_STRAFE_TIME] < now)
			{
				g_kf_f[id][KF_SIDE] = random_num(0, 1) == 0 ? 1.0 : -1.0;
				g_mv_f[id][MV_STRAFE_TIME] = now + random_float(0.4, 0.9);
				g_mv_f[id][MV_STRAFE_SPEED] = g_mv_f[id][MV_MAX_SPEED] * 0.6 * g_kf_f[id][KF_SIDE];
			}
			movement_stop_moving(id);
		}
		if (attack && dist < KF_FLY_KICK_MAX && kf_ready(id, now))
		{
			g_kf_i[id][KF_WALL_SET] = kf_find_wall(id, origin, g_kf_v[id][KF_WALL]) ? 1 : 0;
			new index = kf_pick_combo(id, dist);
			if (index >= 0)
			{
				g_kf_i[id][KF_COMBO] = index;
				g_kf_i[id][KF_COMBO_STEP] = 0;
				g_kf_f[id][KF_COMBO_AT] = now;
				g_kf_f[id][KF_COMBO_START] = now;
				g_kf_i[id][KF_PHASE] = 0;
				g_kf_f[id][KF_PHASE_TIME] = now;
			}
		}
	}
	kf_run_combo(id, origin, dist, now, press);
}

// Bot::kungfu_fight: NORMAL_ATTACK with kung fu in hand and the enemy in sight.
stock kungfu_fight(id, enemy, Float:dist)
{
	new Float:now = g_now;
	new Float:origin[3];
	kf_ent_center(enemy, origin);
	// No used_melee: that breeds the personal genes, which kung fu doesn't use (design §4).
	// A new opponent, or the fight broke off: what is open is scored, and the fight starts
	// afresh, so nothing of a half-done move lingers.
	if (g_kf_i[id][KF_OPPONENT] != enemy || now - g_kf_f[id][KF_UPDATED] > KF_FIGHT_GAP)
	{
		kungfu_settle(id, KF_END_BROKEN);
		new opp_buttons = kf_ent_buttons(enemy);
		new kind, kid;
		new bool:keyed = learning_player_key(enemy, kind, kid);
		new bool:human = kf_is_player(enemy) && !world_is_bot(enemy);
		// Its rhythm from earlier fights (the opponent model), else a second.
		new Float:interval = 1.0;
		if (keyed)
		{
			new Float:prof[RL_OPP_FIELDS];
			if (rl_opp_profile(enemy, prof, RL_OPP_FIELDS) > 0 && prof[RL_OPP_KF_FIGHTS] > 0.0)
			{
				interval = prof[RL_OPP_KF_INTERVAL];
			}
		}
		kf_reset_fight(id);
		g_kf_i[id][KF_OPPONENT] = enemy;
		g_kf_i[id][KF_OPP_KEY_KIND] = keyed ? kind : 0;
		g_kf_i[id][KF_OPP_KEY_ID] = keyed ? kid : 0;
		g_kf_i[id][KF_OPP_HUMAN] = human ? 1 : 0;
		g_kf_i[id][KF_OPP_BUTTONS] = opp_buttons;
		g_kf_f[id][KF_OPP_INTERVAL] = interval;
		kf_decay(id);
	}
	g_kf_f[id][KF_UPDATED] = now;

	// Against a gun out of reach: a gun of its own with rounds, else cover; but a gunman who
	// looks away or has an empty clip is rushed.
	new bool:player = kf_is_player(enemy);
	new opp_weapon = player ? world_weapon(enemy) : 0;
	new bool:gunman_aims = kf_facing_dot(id, enemy) > 0.7 && player && world_clip(enemy) > 0;
	if (kungfu_is_ranged(opp_weapon) && dist > KF_RUSH_GUNMAN && gunman_aims)
	{
		g_kf_i[id][KF_COMBO] = -1;
		new bool:primary;
		new best = combat_best_weapon(id, enemy, primary);
		if (best > 0 && botweapons_has(id, best) && !botweapons_is_melee(id, best))
		{
			think_switch_weapon(id, best);
		}
		else
		{
			combat_run_for_cover(id, origin, false);
		}
		return;
	}

	kf_watch(id, enemy, dist, now);
	kf_choose_stance(id, enemy, now);
	kf_judge_reaction(id, now, false);

	new press = 0;
	if (!kf_react(id, origin, dist, now, press))
	{
		kf_offence(id, enemy, origin, dist, now, press);
	}
	g_co_i[id][CO_BUTTON] |= press;
	g_kf_i[id][KF_LAST_BUTTONS] = press;
}

// Bot::kungfu_face: where the look code faces instead of the enemy while a wall jump needs the
// wall: only with kung fu in hand, in a fight that ran just now.
stock bool:kungfu_face(id, Float:out[3])
{
	new bool:fighting = g_kf_i[id][KF_COMBO] >= 0 && g_now - g_kf_f[id][KF_UPDATED] <= KF_FIGHT_GAP && g_bw_i[id][BW_CURRENT] == RC_KUNGFU;
	if (fighting && g_kf_i[id][KF_FACE_SET])
	{
		vec_copy(g_kf_v[id][KF_FACE], out);
		return true;
	}
	vec_zero(out);
	return false;
}

// rcbot/math.inc: vectors and angles (Rust Vector → Float:[3]).
// Owner: core. Ports math.rs (Vector, length as pow(..., 0.5)) and the free helpers of
// bot/look.rs (fix_float_angle, fix_angles, angle_diff, vec_to_angles, make_vectors, dot,
// change_angles): upstream util.cpp UTIL_FixFloatAngle, UTIL_FixAngles, UTIL_AngleDiff, the
// engine's VecToAngles/MakeVectors and BotFunc_ChangeAngles (Whistler).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Conventions (docs/pawn.md): vectors are Float:[3] (x, y, z) passed by reference; out
// parameters come last; nothing here allocates or touches strings, so it is safe per frame.
// Note AMX Mod X's floatatan2(a, b) is C's atan2(a, b): the first argument is y.

#if defined _rcbot_math_included
	#endinput
#endif
#define _rcbot_math_included

stock const Float:VEC_ZERO[3] = {0.0, 0.0, 0.0};

stock vec_set(Float:v[3], Float:x, Float:y, Float:z)
{
	v[0] = x;
	v[1] = y;
	v[2] = z;
}

stock vec_copy(const Float:from[3], Float:to[3])
{
	to[0] = from[0];
	to[1] = from[1];
	to[2] = from[2];
}

stock vec_zero(Float:v[3])
{
	v[0] = 0.0;
	v[1] = 0.0;
	v[2] = 0.0;
}

stock bool:vec_is_zero(const Float:v[3])
{
	return v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0;
}

// out = a - b
stock vec_sub(const Float:a[3], const Float:b[3], Float:out[3])
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

// out = a + b
stock vec_add(const Float:a[3], const Float:b[3], Float:out[3])
{
	out[0] = a[0] + b[0];
	out[1] = a[1] + b[1];
	out[2] = a[2] + b[2];
}

// out = a + b * s
stock vec_mad(const Float:a[3], const Float:b[3], Float:s, Float:out[3])
{
	out[0] = a[0] + b[0] * s;
	out[1] = a[1] + b[1] * s;
	out[2] = a[2] + b[2] * s;
}

stock vec_scale(const Float:a[3], Float:s, Float:out[3])
{
	out[0] = a[0] * s;
	out[1] = a[1] * s;
	out[2] = a[2] * s;
}

// math.rs Vector::length: pow(x² + y² + z², 0.5), as upstream computes it (A* and distance
// comparisons are checked bit for bit against upstream there).
stock Float:vec_length(const Float:v[3])
{
	return floatpower(v[0] * v[0] + v[1] * v[1] + v[2] * v[2], 0.5);
}

stock Float:vec_length_2d(const Float:v[3])
{
	return floatpower(v[0] * v[0] + v[1] * v[1], 0.5);
}

stock Float:vec_distance(const Float:a[3], const Float:b[3])
{
	new Float:d[3];
	vec_sub(a, b, d);
	return vec_length(d);
}

stock Float:vec_distance_2d(const Float:a[3], const Float:b[3])
{
	new Float:dx = a[0] - b[0], Float:dy = a[1] - b[1];
	return floatpower(dx * dx + dy * dy, 0.5);
}

// math.rs Vector::normalize (Vector::Normalize): the zero vector becomes (0, 0, 1); otherwise
// scaled by 1 / length, as the Rust (not divided: the last bit can differ). out may be v.
stock vec_normalize(const Float:v[3], Float:out[3])
{
	new Float:l = vec_length(v);
	if (l == 0.0)
	{
		vec_set(out, 0.0, 0.0, 1.0);
		return;
	}
	vec_scale(v, 1.0 / l, out);
}

// look.rs dot: DotProduct.
stock Float:vec_dot(const Float:a[3], const Float:b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// look.rs fix_float_angle: UTIL_FixFloatAngle, into -180..180 in at most four turns (0 after).
stock Float:fix_float_angle(Float:a)
{
	new loops = 0;
	if (a < -180.0)
	{
		while (loops < 4 && a < -180.0)
		{
			a += 360.0;
			loops++;
		}
	}
	else if (a > 180.0)
	{
		while (loops < 4 && a > 180.0)
		{
			a -= 360.0;
			loops++;
		}
	}
	if (loops >= 4)
	{
		a = 0.0;
	}
	return a;
}

// look.rs fix_angles: UTIL_FixAngles.
stock fix_angles(Float:v[3])
{
	v[0] = fix_float_angle(v[0]);
	v[1] = fix_float_angle(v[1]);
	v[2] = fix_float_angle(v[2]);
}

// look.rs angle_diff: UTIL_AngleDiff.
stock Float:angle_diff(Float:dest, Float:src)
{
	new Float:d = dest - src;
	if (dest > src)
	{
		if (d >= 180.0)
		{
			d -= 360.0;
		}
	}
	else if (d <= -180.0)
	{
		d += 360.0;
	}
	return d;
}

// look.rs vec_to_angles: the engine's VecToAngles, pitch up positive, both in 0..360. The view's
// pitch (v_angle[0]) is the negated pitch: RCBot's set_view_angles uses -angles[0].
stock vec_to_angles(const Float:v[3], Float:out[3])
{
	new Float:yaw, Float:pitch;
	if (v[1] == 0.0 && v[0] == 0.0)
	{
		yaw = 0.0;
		pitch = v[2] > 0.0 ? 90.0 : 270.0;
	}
	else
	{
		yaw = floatatan2(v[1], v[0], degrees);
		if (yaw < 0.0)
		{
			yaw += 360.0;
		}
		new Float:flat = floatsqroot(v[0] * v[0] + v[1] * v[1]);
		pitch = floatatan2(v[2], flat, degrees);
		if (pitch < 0.0)
		{
			pitch += 360.0;
		}
	}
	out[0] = pitch;
	out[1] = yaw;
	out[2] = 0.0;
}

// look.rs make_vectors: the engine's MakeVectors (AngleVectors): forward and right.
stock make_vectors(const Float:angles[3], Float:fwd[3], Float:right[3])
{
	new Float:sy = floatsin(angles[1], degrees), Float:cy = floatcos(angles[1], degrees);
	new Float:sp = floatsin(angles[0], degrees), Float:cp = floatcos(angles[0], degrees);
	new Float:sr = floatsin(angles[2], degrees), Float:cr = floatcos(angles[2], degrees);
	fwd[0] = cp * cy;
	fwd[1] = cp * sy;
	fwd[2] = -sp;
	right[0] = -sr * sp * cy + cr * sy;
	right[1] = -sr * sp * sy - cr * cy;
	right[2] = -sr * cp;
}

// look.rs change_angles: BotFunc_ChangeAngles. Turns current toward ideal by at most speed
// degrees the short way round; speed is set to the step taken.
stock change_angles(&Float:speed, Float:ideal, &Float:current)
{
	new Float:diff = floatabs(current - ideal);
	if (diff <= 0.1)
	{
		speed = diff;
		return;
	}
	if (diff < speed)
	{
		speed = diff;
	}
	if (current >= 0.0 && ideal >= 0.0)
	{
		if (current > ideal) current -= speed; else current += speed;
	}
	else if (current >= 0.0 && ideal < 0.0)
	{
		if (current - 180.0 > ideal) current += speed; else current -= speed;
	}
	else if (current < 0.0 && ideal >= 0.0)
	{
		if (current + 180.0 > ideal) current += speed; else current -= speed;
	}
	else if (current > ideal)
	{
		current -= speed;
	}
	else
	{
		current += speed;
	}
	current = fix_float_angle(current);
}

stock Float:fclamp(Float:v, Float:lo, Float:hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

stock Float:fmin(Float:a, Float:b)
{
	return a < b ? a : b;
}

stock Float:fmax(Float:a, Float:b)
{
	return a > b ? a : b;
}

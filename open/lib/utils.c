// SPDX-License-Identifier: ISC
/*
 * Generic helpers of the driver (bcmutils.c): the lookup of the board
 * variables and of numbers and lists of numbers in them, the number parser
 * they use, a bit count and the CRC-8 the SROM is checked with.
 *
 * Written from docs/re/spec/utils.md.
 */
#include <bcm4360/utils.h>
#include <bcm4360/hw.h>

/*
 * docs/re/spec/utils.md, "Overview": an entry of the variables is
 * "name=value"; a list is numbers separated by commas.
 */
#define VAR_VALUE_SEP		'='
#define VAR_LIST_SEP		','

/* docs/re/spec/utils.md, "Character classes of bcm_strtoul": white space */
#define CH_SPACE_FIRST		0x09	/* horizontal tab .. */
#define CH_SPACE_LAST		0x0d	/* .. carriage return */
#define CH_SPACE		0x20
#define CH_NO_BREAK_SPACE	0xa0

/* docs/re/spec/utils.md, "bcm_strtoul", step 3: the bases */
#define BASE_AUTO		0	/* from the prefix of the number */
#define BASE_OCT		8
#define BASE_DEC		10
#define BASE_HEX		16
#define HEX_PREFIX_LEN		2	/* "0x", "0X" */
#define HEX_LETTER_VALUE	10	/* the value of 'a' and 'A' */

/* bcm_strtoul, step 4: not a digit (and not less than any base) */
#define DIGIT_NONE		0xffffffffU

/* bcm_strtoul, step 4: the result of an overflow */
#define STRTOUL_OVERFLOW	0xffffffffffffffffULL

/* docs/re/spec/utils.md, "CRC-8 of hndcrc8": reflected polynomial */
#define CRC8_POLY		0xab
#define BITS_PER_BYTE		8

/*
 * docs/re/spec/utils.md, "getvar", step 3: whether the entry `e` gives the
 * value of `name` (of length n > 0): its first n bytes are `name` and byte n
 * is '='. The comparison is the osl_memcmp(e, name, n) == 0 of the
 * specification; it stops at the first difference, so it does not read past
 * the NUL of an entry shorter than `name` (`name` has no NUL in its n bytes).
 */
static bool var_entry_matches(const char *e, const char *name, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (e[i] != name[i])
			return false;
	return e[n] == VAR_VALUE_SEP;
}

/* docs/re/spec/utils.md, "getvar", step 3: the entry after `e` */
static const char *var_next_entry(const char *e)
{
	while (*e != '\0')
		e++;
	return e + 1;
}

/*
 * docs/re/spec/utils.md, "getvar": the value of `name` (the byte after the
 * '=' of the first entry with that name), else what `fallback` (the
 * specification's nvram_get) gives; without a fallback NULL.
 */
const char *bcm4360_getvar(const char *vars, const char *name, bcm4360_var_fallback fallback)
{
	const char *e;
	u32 n;

	/* step 1: no name, or an empty one, has no value (no fallback) */
	if (!name)
		return NULL;
	n = (u32)hw_strlen(name);
	if (n == 0)
		return NULL;

	/* steps 2, 3: the entries up to the empty string; the first match wins */
	if (vars) {
		for (e = vars; *e != '\0'; e = var_next_entry(e))
			if (var_entry_matches(e, name, n))
				return e + n + 1;
	}

	/* step 4 */
	return fallback ? fallback(name) : NULL;
}

/*
 * docs/re/spec/utils.md, "getintvar": the value of `name` as a number
 * (truncated to 32 bit), 0 if it has no value.
 */
s32 bcm4360_getintvar(const char *vars, const char *name, bcm4360_var_fallback fallback)
{
	const char *v = bcm4360_getvar(vars, name, fallback);

	if (!v)
		return 0;
	return (s32)bcm4360_strtoul(v, NULL, BASE_AUTO);
}

/*
 * docs/re/spec/utils.md, "getintvararray" step 2c and "getintvararraysize"
 * step 2: where the next element of a list starts, after the byte `end` that
 * ended a number: a comma is skipped.
 */
static const char *var_list_next(const char *end)
{
	return *end == VAR_LIST_SEP ? end + 1 : end;
}

/*
 * docs/re/spec/utils.md, "getintvararray": element `index` (from 0) of the
 * list `name` (truncated to 32 bit), 0 if the list has fewer elements.
 *
 * An element the parser does not consume is repeated with `i` growing until
 * `i` reaches `index` (specification, "Note"). `i` counts modulo 2^32. When
 * the parser returns its overflow value without storing the end, `end` is
 * still `p`: the element repeats in the same way.
 */
s32 bcm4360_getintvararray(const char *vars, const char *name, s32 index,
			   bcm4360_var_fallback fallback)
{
	const char *p = bcm4360_getvar(vars, name, fallback);
	const char *end;
	u64 v;
	u32 i;

	/* step 1 */
	if (!p)
		return 0;

	/* step 2 */
	for (i = 0; *p != '\0'; i++) {
		end = p;
		v = bcm4360_strtoul(p, &end, BASE_AUTO);	/* a */
		if (i == (u32)index)				/* b */
			return (s32)v;
		p = var_list_next(end);				/* c */
	}

	/* step 3: fewer elements than index + 1 */
	return 0;
}

/*
 * docs/re/spec/utils.md, "getintvararraysize": the number of elements of the
 * list `name`, 0 if it has no value.
 *
 * Where the object loops for ever (an element that the parser does not
 * consume and that no comma ends, or that overflows: `p` stays where it is),
 * the loop ends after counting that element once, since every further round
 * would be the same.
 */
s32 bcm4360_getintvararraysize(const char *vars, const char *name,
			       bcm4360_var_fallback fallback)
{
	const char *p = bcm4360_getvar(vars, name, fallback);
	const char *start, *end;
	s32 count = 0;

	/* step 1 */
	if (!p)
		return 0;

	/* step 2 */
	while (*p != '\0') {
		start = p;
		end = p;
		(void)bcm4360_strtoul(p, &end, BASE_AUTO);
		p = var_list_next(end);
		count++;
		if (p == start)
			break;
	}

	/* step 3 */
	return count;
}

/* docs/re/spec/utils.md, "Character classes of bcm_strtoul": white space */
static bool strtoul_is_space(u8 c)
{
	return (c >= CH_SPACE_FIRST && c <= CH_SPACE_LAST) || c == CH_SPACE ||
	       c == CH_NO_BREAK_SPACE;
}

/*
 * docs/re/spec/utils.md, "bcm_strtoul" step 4 and "Character classes of
 * bcm_strtoul": the value of the digit `c`; DIGIT_NONE for any byte that is
 * neither a decimal digit nor a hexadecimal letter.
 */
static u32 strtoul_digit(u8 c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + HEX_LETTER_VALUE;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + HEX_LETTER_VALUE;
	return DIGIT_NONE;
}

/* docs/re/spec/utils.md, "bcm_strtoul", step 3: the text starts with "0x" or "0X" */
static bool strtoul_hex_prefix(const u8 *s)
{
	return s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

/*
 * docs/re/spec/utils.md, "bcm_strtoul": the number at `cp` (64 bit). *endp
 * (if endp) = the byte that ended the number, except on an overflow without
 * '-', which returns all ones at once and leaves *endp alone.
 */
u64 bcm4360_strtoul(const char *cp, const char **endp, u32 base)
{
	const u8 *s = (const u8 *)cp;
	bool negative = false;
	u64 result = 0;
	u64 last = 0;
	u32 digit;

	/* step 1 */
	while (strtoul_is_space(*s))
		s++;

	/* step 2: one sign character */
	if (*s == '+') {
		s++;
	} else if (*s == '-') {
		negative = true;
		s++;
	}

	/* step 3 */
	if (base == BASE_AUTO) {
		if (strtoul_hex_prefix(s)) {
			base = BASE_HEX;
			s += HEX_PREFIX_LEN;
		} else if (*s == '0') {
			base = BASE_OCT;
			s++;
		} else {
			base = BASE_DEC;
		}
	} else if (base == BASE_HEX && strtoul_hex_prefix(s)) {
		s += HEX_PREFIX_LEN;
	}

	/* step 4; a byte that is no digit of the base ends the number (step 5) */
	for (;; s++) {
		digit = strtoul_digit(*s);
		if (digit == DIGIT_NONE || digit >= base)
			break;
		result = result * base + digit;
		if (result < last && !negative)
			return STRTOUL_OVERFLOW;
		last = result;
	}

	/* step 6 */
	if (negative)
		result = -result;

	/* step 7 */
	if (endp)
		*endp = (const char *)s;
	return result;
}

/* docs/re/spec/utils.md, "bcm_bitcount": the bits set in `length` bytes */
u32 bcm4360_bitcount(const u8 *bitmap, u32 length)
{
	u32 count = 0;
	u32 i, v;

	for (i = 0; i < length; i++)
		for (v = bitmap[i]; v != 0; v &= v - 1)	/* clears the lowest bit set */
			count++;
	return count;
}

/*
 * docs/re/spec/utils.md, "CRC-8 of hndcrc8": entry `c` of the table, made from
 * `c` by eight steps of the reflected polynomial.
 */
static u8 crc8_table_entry(u8 c)
{
	u32 v = c;
	int step;

	for (step = 0; step < BITS_PER_BYTE; step++)
		v = (v & 1) ? (v >> 1) ^ CRC8_POLY : v >> 1;
	return (u8)v;
}

/*
 * docs/re/spec/utils.md, "hndcrc8": the CRC-8 of `nbytes` bytes from `crc`,
 * one table entry per byte, no final inversion.
 */
u8 bcm4360_crc8(const u8 *data, u32 nbytes, u8 crc)
{
	u32 i;

	for (i = 0; i < nbytes; i++)
		crc = crc8_table_entry((u8)(crc ^ data[i]));
	return crc;
}

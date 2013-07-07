/*
 * DiscoCheck, an UCI chess engine. Copyright (C) 2011-2013 Lucas Braesch.
 *
 * DiscoCheck is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * DiscoCheck is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If not,
 * see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include "types.h"

enum {
	NORMAL,
	EN_PASSANT,
	PROMOTION,
	CASTLING
};

struct move_t {
	move_t(): b(0) {}	// silence compiler warnings
	explicit move_t(std::uint16_t _b): b(_b) {}

	operator bool() const;
	bool operator== (move_t m) const;
	bool operator!= (move_t m) const;

	int fsq() const;
	int tsq() const;
	int flag() const;
	int prom() const;

	void fsq(int fsq);
	void tsq(int tsq);
	void flag(int flag);
	void prom(int piece);

private:
	/* a move is incoded in 16 bits, as follows:
	 * 0..5: fsq (from square)
	 * 6..11: tsq (to square)
	 * 12,13: prom (promotion). Uses unusual numbering for optimal compactness: Knight=0 ... Queen=3
	 * 14,15: flag. Flags are: NORMAL=0, EN_PASSANT=1, PROMOTION=2, CASTLING=3 */
	std::uint16_t b;
};

struct Board;

enum { NO_CHECK, NORMAL_CHECK, DISCO_CHECK };

extern int move_is_check(const Board& B, move_t m);
extern bool move_is_cop(const Board& B, move_t m);	// capture or promotion
extern bool move_is_pawn_threat(const Board& B, move_t m);

extern move_t string_to_move(const Board& B, const std::string& s);
extern std::string move_to_string(move_t m);

extern int calc_see(const Board& B, move_t m);
extern bool test_see();

extern int mvv_lva(const Board& B, move_t m);

extern bool refute(const Board& B, move_t m1, move_t m2);

/* move_t member function */

inline move_t::operator bool() const
{
	return b;
}

inline bool move_t::operator== (move_t m) const
{
	return b == m.b;
}

inline bool move_t::operator!= (move_t m) const
{
	return b != m.b;
}

inline int move_t::fsq() const
{
	return b & 0x3f;
}

inline int move_t::tsq() const
{
	return (b >> 6) & 0x3f;
}

inline int move_t::flag() const
{
	return (b >> 14) & 3;
}

inline int move_t::prom() const
{
	assert(flag() == PROMOTION);
	return ((b >> 12) & 3) + KNIGHT;
}

inline void move_t::fsq(int fsq)
{
	assert(square_ok(fsq));
	b &= 0xffc0;
	b ^= fsq;
}

inline void move_t::tsq(int tsq)
{
	assert(square_ok(tsq));
	b &= 0xf03f;
	b ^= (tsq << 6);
}

inline void move_t::flag(int flag)
{
	assert(flag < 4);
	b &= 0x3fff;
	b ^= (flag << 14);
}

inline void move_t::prom(int piece)
{
	assert(KNIGHT <= piece && piece <= QUEEN);
	b &= 0xcfff;
	b ^= (piece - KNIGHT) << 12;
}


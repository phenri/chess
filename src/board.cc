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
#include <sstream>
#include <cstring>
#include "board.h"

namespace board {

const std::string PieceLabel[NB_COLOR] = { "PNBRQK", "pnbrqk" };

void Position::clear()
{
	assert(bb::BitboardInitialized);

	turn = WHITE;
	all[WHITE] = all[BLACK] = 0;
	king_pos[WHITE] = king_pos[BLACK] = 0;

	for (int sq = A1; sq <= H8; piece_on[sq++] = NO_PIECE);
	std::memset(b, 0, sizeof(b));

	sp = sp0 = game_stack;
	std::memset(sp, 0, sizeof(UndoInfo));
	sp->epsq = NO_SQUARE;
	move_count = 1;

	initialized = true;
}

void Position::set_fen(const std::string& _fen)
{
	clear();

	std::istringstream fen(_fen);
	fen >> std::noskipws;
	int sq = A8;
	char c, r, f;

	// piece placement
	while ((fen >> c) && !isspace(c)) {
		if ('1' <= c && c <= '8')
			sq += c - '0';
		else if (c == '/')
			sq -= 16;
		else {
			int color = isupper(c) ? WHITE : BLACK;
			int piece = PieceLabel[color].find(c);
			if (piece_ok(piece)) {
				set_square(color, piece, sq);
				if (piece == KING)
					king_pos[color] = sq;
			}
			sq++;
		}
	}

	// turn of play
	fen >> c;
	turn = c == 'w' ? WHITE : BLACK;
	if (turn) {
		sp->key ^= bb::zob_turn;
		sp->kpkey ^= bb::zob_turn;
	}
	fen >> c;

	// castling rights
	while ((fen >> c) && !isspace(c)) {
		int color = isupper(c) ? WHITE : BLACK;
		c = toupper(c);
		if (c == 'K')
			sp->crights |= OO << (2 * color);
		else if (c == 'Q')
			sp->crights |= OOO << (2 * color);
	}

	if ( (fen >> f) && ('a' <= f && f <= 'h')
		 && (fen >> r) && ('1' <= r && r <= '8') )
		sp->epsq = square(r - '1', f - 'a');

	fen >> std::skipws >> sp->rule50 >> move_count;

	const int us = turn, them = opp_color(us);
	sp->pinned = hidden_checkers(1, us);
	sp->dcheckers = hidden_checkers(0, us);

	calc_attacks(us);
	sp->attacked = calc_attacks(them);

	sp->checkers = bb::test_bit(st().attacked, king_pos[us]) ? calc_checkers(us) : 0ULL;

	assert(verify_keys());
	assert(verify_psq());
}

std::string Position::get_fen() const
{
	assert(initialized);
	std::ostringstream fen;

	// write the board
	for (int r = RANK_8; r >= RANK_1; --r) {
		int empty_cnt = 0;
		for (int f = FILE_A; f <= FILE_H; ++f) {
			int sq = square(r, f);
			if (piece_on[sq] == NO_PIECE)
				empty_cnt++;
			else {
				if (empty_cnt) {
					fen << empty_cnt;
					empty_cnt = 0;
				}
				fen << PieceLabel[get_color_on(sq)][piece_on[sq]];
			}
		}
		if (empty_cnt)
			fen << empty_cnt;
		if (r > RANK_1)
			fen << '/';
	}

	// turn of play
	fen << (turn ? " b " : " w ");

	// castling rights
	int crights = st().crights;
	if (crights) {
		if (crights & OO)
			fen << 'K';
		if (crights & OOO)
			fen << 'Q';
		if (crights & (OO << 2))
			fen << 'k';
		if (crights & (OOO << 2))
			fen << 'q';
	} else
		fen << '-';
	fen << ' ';

	// en passant square
	int epsq = st().epsq;
	if (square_ok(epsq)) {
		fen << char(file(epsq) + 'a');
		fen << char(rank(epsq) + '1');
	} else
		fen << '-';

	fen << ' ' << st().rule50 << ' ' << move_count;

	return fen.str();
}

std::ostream& operator<< (std::ostream& ostrm, const Position& B)
{
	for (int r = RANK_8; r >= RANK_1; --r) {
		for (int f = FILE_A; f <= FILE_H; ++f) {
			int sq = square(r, f);
			int color = B.get_color_on(sq);
			char c = color != NO_COLOR
					 ? PieceLabel[color][B.get_piece_on(sq)]
					 : (sq == B.st().epsq ? '*' : '.');
			ostrm << ' ' << c;
		}
		ostrm << std::endl;
	}

	return ostrm << B.get_fen() << std::endl;
}

void Position::play(move::move_t m)
{
	assert(initialized);
	++sp;
	memcpy(sp, sp - 1, sizeof(UndoInfo));
	sp->last_move = m;
	sp->rule50++;

	const int us = turn, them = opp_color(us);
	const int fsq = m.fsq(), tsq = m.tsq();
	const int piece = piece_on[fsq], capture = piece_on[tsq];

	// Null move
	if (!m) {
		assert(!is_check());
		sp->epsq = NO_SQUARE;
		goto move_played;
	}

	// normal capture: remove captured piece
	if (piece_ok(capture)) {
		sp->rule50 = 0;
		clear_square(them, capture, tsq);
	}

	// move our piece
	clear_square(us, piece, fsq);
	set_square(us, m.flag() == move::PROMOTION ? m.prom() : piece, tsq);

	if (piece == PAWN) {
		sp->rule50 = 0;
		const int inc_pp = us ? -8 : 8;
		// set the epsq if double push, and ep square is attacked by enemy pawns
		sp->epsq = tsq == fsq + 2 * inc_pp && bb::test_bit(st().attacks[them][PAWN], fsq + inc_pp)
				   ? fsq + inc_pp : NO_SQUARE;
		// capture en passant
		if (m.flag() == move::EN_PASSANT)
			clear_square(them, PAWN, tsq - inc_pp);
	} else {
		sp->epsq = NO_SQUARE;

		if (piece == ROOK) {
			// a rook move can alter castling rights
			if (fsq == (us ? H8 : H1))
				sp->crights &= ~(OO << (2 * us));
			else if (fsq == (us ? A8 : A1))
				sp->crights &= ~(OOO << (2 * us));
		} else if (piece == KING) {
			// update king_pos and clear crights
			king_pos[us] = tsq;
			sp->crights &= ~((OO | OOO) << (2 * us));

			if (m.flag() == move::CASTLING) {
				// rook jump
				if (tsq == fsq + 2) {			// OO
					clear_square(us, ROOK, us ? H8 : H1);
					set_square(us, ROOK, us ? F8 : F1);
				} else if (tsq == fsq - 2) {	// OOO
					clear_square(us, ROOK, us ? A8 : A1);
					set_square(us, ROOK, us ? D8 : D1);
				}
			}
		}
	}

	if (capture == ROOK) {
		// Rook captures can alter opponent's castling rights
		if (tsq == (us ? H1 : H8))
			sp->crights &= ~(OO << (2 * them));
		else if (tsq == (us ? A1 : A8))
			sp->crights &= ~(OOO << (2 * them));
	}

move_played:
	turn = them;
	if (turn == WHITE)
		++move_count;

	sp->key ^= bb::zob_turn;
	sp->kpkey ^= bb::zob_turn;

	sp->capture = capture;
	sp->pinned = hidden_checkers(1, them);
	sp->dcheckers = hidden_checkers(0, them);

	sp->attacked = calc_attacks(us);
	calc_attacks(them);

	sp->checkers = bb::test_bit(st().attacked, king_pos[them]) ? calc_checkers(them) : 0ULL;

	assert(verify_keys());
	assert(verify_psq());
}

void Position::undo()
{
	assert(initialized);
	const move::move_t m = st().last_move;
	const int us = opp_color(turn), them = turn;

	const int fsq = m.fsq(), tsq = m.tsq();
	const int piece = m.flag() == move::PROMOTION ? PAWN : piece_on[tsq];
	const int capture = st().capture;

	if (!m) {
		assert(!is_check());
		goto move_undone;
	}

	// move our piece back
	clear_square(us, get_piece_on(tsq), tsq, false);	// get_piece_on() is to handle a promotion
	set_square(us, piece, fsq, false);

	// restore the captured piece (if any)
	if (piece_ok(capture))
		set_square(them, capture, tsq, false);

	if (piece == KING) {
		// update king_pos
		king_pos[us] = fsq;

		if (m.flag() == move::CASTLING) {
			// undo rook jump
			if (tsq == fsq + 2) {			// OO
				clear_square(us, ROOK, us ? F8 : F1, false);
				set_square(us, ROOK, us ? H8 : H1, false);
			} else if (tsq == fsq - 2) {	// OOO
				clear_square(us, ROOK, us ? D8 : D1, false);
				set_square(us, ROOK, us ? A8 : A1, false);
			}
		}
	} else if (m.flag() == move::EN_PASSANT)	// restore the en passant captured pawn
		set_square(them, PAWN, tsq + (us ? 8 : -8), false);

move_undone:
	turn = us;
	if (turn == BLACK)
		--move_count;

	--sp;
}

Bitboard Position::calc_attacks(int color) const
{
	assert(initialized);
	Bitboard fss, r = 0;

	// Pawn
	fss = get_pieces(color, PAWN);
	r |= sp->attacks[color][PAWN] = bb::shift_bit((fss & ~bb::FileA_bb), color ? -NB_FILE - 1 : +NB_FILE - 1)
									| bb::shift_bit((fss & ~bb::FileH_bb), color ? -NB_FILE + 1 : +NB_FILE + 1);

	// Knight
	sp->attacks[color][KNIGHT] = 0;
	fss = get_pieces(color, KNIGHT);
	while (fss)
		r |= sp->attacks[color][KNIGHT] |= bb::NAttacks[bb::pop_lsb(&fss)];

	// Bishop + Queen (diagonal)
	sp->attacks[color][BISHOP] = 0;
	fss = get_BQ(color);
	while (fss)
		r |= sp->attacks[color][BISHOP] |= bb::bishop_attack(bb::pop_lsb(&fss), st().occ);

	// Rook + Queen (lateral)
	sp->attacks[color][ROOK] = 0;
	fss = get_RQ(color);
	while (fss)
		r |= sp->attacks[color][ROOK] |= bb::rook_attack(bb::pop_lsb(&fss), st().occ);

	// King
	r |= sp->attacks[color][KING] = bb::KAttacks[get_king_pos(color)];

	//All
	return sp->attacks[color][NO_PIECE] = r;
}

Bitboard Position::hidden_checkers(bool find_pins, int color) const
{
	assert(initialized && color_ok(color) && (find_pins == 0 || find_pins == 1));
	const int aside = color ^ find_pins, kside = opp_color(aside);
	Bitboard result = 0ULL, pinners;

	// Pinned pieces protect our king, dicovery checks attack the enemy king.
	const int ksq = king_pos[kside];

	// Pinners are only sliders with X-ray attacks to ksq
	pinners = (get_RQ(aside) & bb::RPseudoAttacks[ksq]) | (get_BQ(aside) & bb::BPseudoAttacks[ksq]);

	while (pinners) {
		int sq = bb::pop_lsb(&pinners);
		Bitboard b = bb::Between[ksq][sq] & ~(1ULL << sq) & st().occ;
		// NB: if b == 0 then we're in check

		if (!bb::several_bits(b) && (b & all[color]))
			result |= b;
	}
	return result;
}

Bitboard Position::calc_checkers(int kcolor) const
{
	assert(initialized && color_ok(kcolor));
	const int kpos = king_pos[kcolor];
	const int them = opp_color(kcolor);

	const Bitboard RQ = get_RQ(them) & bb::RPseudoAttacks[kpos];
	const Bitboard BQ = get_BQ(them) & bb::BPseudoAttacks[kpos];

	return (RQ & bb::rook_attack(kpos, st().occ))
		   | (BQ & bb::bishop_attack(kpos, st().occ))
		   | (get_pieces(them, KNIGHT) & bb::NAttacks[kpos])
		   | (get_pieces(them, PAWN) & bb::PAttacks[kcolor][kpos]);
}

void Position::set_square(int color, int piece, int sq, bool play)
{
	assert(initialized);
	assert(square_ok(sq) && color_ok(color) && piece_ok(piece));
	assert(get_piece_on(sq) == NO_PIECE);

	bb::set_bit(&b[piece], sq);
	bb::set_bit(&all[color], sq);
	piece_on[sq] = piece;

	if (play) {
		bb::set_bit(&sp->occ, sq);

		const Eval& e = get_psq(color, piece, sq);
		sp->psq[color] += e;
		if (KNIGHT <= piece && piece <= QUEEN)
			sp->piece_psq[color] += e.op;
		else
			sp->kpkey ^= bb::zob[color][piece][sq];

		sp->key ^= bb::zob[color][piece][sq];
		sp->mat_key += 1ULL << (8 * piece + 4 * color);
	}
}

void Position::clear_square(int color, int piece, int sq, bool play)
{
	assert(initialized);
	assert(square_ok(sq) && color_ok(color) && piece_ok(piece));
	assert(get_piece_on(sq) == piece);

	bb::clear_bit(&b[piece], sq);
	bb::clear_bit(&all[color], sq);
	piece_on[sq] = NO_PIECE;

	if (play) {
		bb::clear_bit(&sp->occ, sq);

		const Eval& e = get_psq(color, piece, sq);
		sp->psq[color] -= e;
		if (KNIGHT <= piece && piece <= QUEEN)
			sp->piece_psq[color] -= e.op;
		else
			sp->kpkey ^= bb::zob[color][piece][sq];

		sp->key ^= bb::zob[color][piece][sq];
		sp->mat_key -= 1ULL << (8 * piece + 4 * color);
	}
}

bool Position::verify_keys() const
{
	const Key base = get_turn() ? bb::zob_turn : 0;
	Key key = base, kpkey = base, mat_key = 0;

	for (int color = WHITE; color <= BLACK; ++color)
		for (int piece = PAWN; piece <= KING; ++piece) {
			Bitboard sqs = get_pieces(color, piece);
			mat_key += (std::uint64_t)bb::count_bit(sqs) << (8 * piece + 4 * color);
			while (sqs) {
				const int sq = bb::pop_lsb(&sqs);
				key ^= bb::zob[color][piece][sq];
				if (piece == PAWN || piece == KING)
					kpkey ^= bb::zob[color][piece][sq];
			}
		}

	return key == st().key && kpkey == st().kpkey && mat_key == st().mat_key;
}

bool Position::verify_psq() const
{
	Eval psq[NB_COLOR];
	int piece_psq[NB_COLOR];

	for (int color = WHITE; color <= BLACK; ++color) {
		psq[color] = {0, 0};
		piece_psq[color] = 0;

		for (int piece = PAWN; piece <= KING; ++piece) {
			Bitboard sqs = get_pieces(color, piece);
			while (sqs) {
				const Eval& e = get_psq(color, piece, bb::pop_lsb(&sqs));
				psq[color] += e;
				if (KNIGHT <= piece && piece <= QUEEN)
					piece_psq[color] += e.op;
			}
		}

		if (psq[color] != st().psq[color] || piece_psq[color] != st().piece_psq[color])
			return false;
	}

	return true;
}

bool Position::is_draw() const
{
	// 3-fold repetition
	for (int i = 4, rep = 1; i <= std::min(st().rule50, int(sp - game_stack)); i += 2) {
		// If the keys match, increment rep
		// Stop when rep >= 2 or 3 once we've traversed the root
		if ( (sp - i)->key == sp->key
			 && ++rep >= 2 + (sp - i < sp0) )
			return true;
	}

	// 50 move
	if (st().rule50 >= 100)
		return true;

	// insufficient material
	if ( get_pieces(WHITE) == (get_NB(WHITE) | get_pieces(WHITE, KING))
		 && get_pieces(BLACK) == (get_NB(BLACK) | get_pieces(BLACK, KING))
		 && !bb::several_bits(get_NB(WHITE)) && !bb::several_bits(get_NB(BLACK)) )
		return true;

	return false;
}

Bitboard hanging_pieces(const Position& B, int us)
{
	const int them = opp_color(us);

	const Bitboard our_pawns = B.get_pieces(us, PAWN);
	const Bitboard our_pieces = B.get_pieces(us) & ~our_pawns;

	const Bitboard attacked = B.st().attacks[them][NO_PIECE];
	const Bitboard defended = B.st().attacks[us][NO_PIECE];

	return ((our_pawns ^ our_pieces) & attacked & ~defended)
		   | (our_pieces & B.st().attacks[them][PAWN]);
}

Bitboard calc_attackers(const Position& B, int sq, Bitboard occ)
{
	assert(square_ok(sq));

	return (B.get_RQ() & bb::RPseudoAttacks[sq] & bb::rook_attack(sq, occ))
		   | (B.get_BQ() & bb::BPseudoAttacks[sq] & bb::bishop_attack(sq, occ))
		   | (bb::NAttacks[sq] & B.get_N())
		   | (bb::KAttacks[sq] & B.get_K())
		   | (bb::PAttacks[WHITE][sq] & B.get_pieces(BLACK, PAWN))
		   | (bb::PAttacks[BLACK][sq] & B.get_pieces(WHITE, PAWN));
}

}	// namespace board


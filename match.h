/*
 * Zinc, an UCI chess interface. Copyright (C) 2012 Lucas Braesch.
 * 
 * Zinc is free software: you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Zinc is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with this program. If not,
 * see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include <map>
#include "board.h"
#include "engine.h"
#include "epd.h"

struct GameResult
{
	Color winner;
	Result result;
};

extern GameResult game(const Engine E[NB_COLOR], Color color, const std::string& fen,
	const Engine::SearchParam& sp);

struct MatchResult
{
	int win, draw, loss;
	MatchResult();
};

extern MatchResult match(const Engine E[2], const EPD& epd, const Engine::SearchParam& sp,
	size_t nb_games);
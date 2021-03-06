/*! \file GameCVars.h
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup world
*/

#pragma once
#include "../Types.h"
#include "../CVars.h"
#include <Runtime/Container/ZoneSet.h>
#include <Runtime/PushPack.h>

class Game;
class GameCVars {
public:
	CVarBool r_showtris;
	CVarBool r_showportals;
	CVarBool r_showentitybboxes;
	CVarBool r_showworldbboxes;
	CVarBool r_showactorbboxes;
	CVarBool r_showwaypoints;
	CVarBool r_showmovecmds;
	CVarBool r_frustumcull;
	CVarBool r_lightscissor;
	CVarBool r_showlightscissor;
	CVarBool r_throttle;
	CVarBool r_showlightpasses;
	CVarBool r_showlightcounts;
	CVarBool r_fly;
	CVarBool r_lockvis;
	CVarBool r_viewunifiedlighttexturematrix;
	CVarBool r_viewunifiedlightprojectionmatrix;
	CVarBool r_showfrustum;
	CVarInt r_maxLightsPerPass;
	CVarBool r_showlights;
	CVarBool r_showunifiedlights;
	CVarBool r_showunifiedlightbboxprojection;
	CVarBool r_enablelights;
	CVarBool r_unifiedshadows;
	CVarBool r_showviewcontroller;
	CVarBool r_drawworld;
	CVarBool r_drawentities;
	CVarBool r_drawoccupants;
	CVarBool r_drawfog;

	void AddLuaVar(const CVar::Ref &cvar) {
		m_vec.push_back(cvar);
		m_set.insert(cvar.get());
	}

	bool IsLuaVar(CVar *cvar) {
		return m_set.find(cvar) != m_set.end();
	}

private:
	typedef zone_vector<CVar::Ref, ZEngineT>::type CVarVec;
	typedef zone_set<CVar*, ZEngineT>::type CVarSet;
	friend class Game;

	GameCVars(Game &game, CVarZone &zone);

	CVarVec m_vec;
	CVarSet m_set;
};

#include <Runtime/PopPack.h>

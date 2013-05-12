/*! \file GameCVars.cpp
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup world
*/

#include RADPCH
#include "GameCVars.h"

// cvar defaults
GameCVars::GameCVars(Game &game, CVarZone &zone) : 
r_showtris(zone, "r_showtris", false, false),
r_showportals(zone, "r_showportals", false, false),
r_showentitybboxes(zone, "r_showentitybboxes", false, false),
r_showworldbboxes(zone, "r_showworldbboxes", false, false),
r_showwaypoints(zone, "r_showwaypoints", false, false),
r_showmovecmds(zone, "r_showmovecmds", false, false),
r_frustumcull(zone, "r_frustumcull", false, false),
r_lightscissor(zone, "r_lightscissor", true, false),
r_showlightscissor(zone, "r_showlightscissor", false, false),
r_throttle(zone, "r_throttle", true, false){
}


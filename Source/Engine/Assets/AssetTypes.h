/*! \file AssetTypes.h
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup assets
*/


#if !defined(__ASSET_STRINGS_H__) || (defined(RAD_OPT_TOOLS) && defined(ASSET_STRINGS))

#if !defined(__ASSET_STRINGS_H__)

#include "../Types.h"
#include <Runtime/Container/HashMap.h>
#include <Runtime/PushCAWarnings.h>
#include <bitset>
#include <Runtime/PopCAWarnings.h>

namespace asset {

enum Type {
	AT_Texture,
	AT_Material,
	AT_Map,
	AT_SkModel,
	AT_SkAnimSet,
	AT_SkAnimStates,
	AT_VtModel,
	AT_Mesh,
	AT_Sound,
	AT_Music,
	AT_Font,
	AT_Typeface,
	AT_StringTable,
	AT_ConversationTree,
	AT_Particle,
	AT_Max
};

#if defined(RAD_OPT_TOOLS)
RADENG_API const char * RADENG_CALL TypeString(Type type);
RADENG_API Type RADENG_CALL TypeFromName(const char *str);
typedef std::bitset<AT_Max> TypeBits;

inline TypeBits TypeMask(Type type) {
	TypeBits b;
	b[type] = 1;
	return b;
}

#endif

} // asset

RAD_HASH_INT_TYPE(asset::Type);

#else

namespace {
const char *s_strings[asset::AT_Max] = {
	"Texture",
	"Material",
	"Map",
	"SkModel",
	"SkAnimSet",
	"SkAnimStates",
	"VtModel",
	"Mesh",
	"Sound",
	"Music",
	"Font",
	"Typeface",
	"StringTable",
	"ConversationTree",
	"Particle"
};
}

#endif

#if !defined(__ASSET_STRINGS_H__)
     #define __ASSET_STRINGS_H__
#endif

#endif

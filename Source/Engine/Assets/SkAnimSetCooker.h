/*! \file SkAnimSetCooker.h
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup assets
*/

#pragma once

#include "AssetTypes.h"
#include "../Packages/Packages.h"
#include <Runtime/PushPack.h>

namespace asset {

class RADENG_CLASS SkAnimSetCooker : public pkg::Cooker {
public:

	static void Register(Engine &engine);

	enum {
		AssetType = AT_SkAnimSet
	};

	SkAnimSetCooker();
	virtual ~SkAnimSetCooker();

	virtual pkg::CookStatus Status(int flags);
	virtual int Compile(int flags);

private:

	pkg::CookStatus CheckRebuildFiles(int flags);
};

} // asset


#include <Runtime/PopPack.h>

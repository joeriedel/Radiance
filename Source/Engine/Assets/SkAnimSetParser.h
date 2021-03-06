/*! \file SkAnimSetParser.h
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup assets
*/

#pragma once

#include "AssetTypes.h"
#include "../Packages/Packages.h"
#include "../SkAnim/SkAnim.h"

#if defined(RAD_OPT_TOOLS)
#include "../SkAnim/SkBuilder.h"
#include <Runtime/Container/ZoneVector.h>
#endif

#include <Runtime/File.h>
#include <Runtime/PushPack.h>

namespace asset  {

class SkAnimSetCooker;

class RADENG_CLASS SkAnimSetParser : public pkg::Sink<SkAnimSetParser> {
public:

	static void Register(Engine &engine);

	enum {
		SinkStage = pkg::SS_Parser,
		AssetType = AT_SkAnimSet
	};

	SkAnimSetParser();
	virtual ~SkAnimSetParser();

	RAD_DECLARE_READONLY_PROPERTY(SkAnimSetParser, dska, const ska::DSka*);
	RAD_DECLARE_READONLY_PROPERTY(SkAnimSetParser, valid, bool);

#if defined(RAD_OPT_TOOLS)
	static int LoadToolsFile(
		const char *path,
		Engine &engine,
		StringVec *sources,
		tools::SkaCompressionMap *compression
	);
#endif

protected:

	virtual int Process(
		const xtime::TimeSlice &time,
		Engine &engine,
		const pkg::Asset::Ref &asset,
		int flags
	);

#if defined(RAD_OPT_TOOLS)
	int Load(
		const xtime::TimeSlice &time,
		Engine &engine,
		const pkg::Asset::Ref &asset,
		int flags
	);
#endif

	int LoadCooked(
		const xtime::TimeSlice &time,
		Engine &engine,
		const pkg::Asset::Ref &asset,
		int flags
	);

private:

	friend class SkAnimSetCooker;

#if defined(RAD_OPT_TOOLS)
	RAD_DECLARE_GET(valid, bool) { 
		return m_skad||m_load; 
	}
	RAD_DECLARE_GET(dska, const ska::DSka*) { 
		return m_skad ? &m_skad->dska : &m_ska; 
	}
	tools::SkaData::Ref m_skad;
#else
	RAD_DECLARE_GET(valid, bool) { 
		return m_load; 
	}
	RAD_DECLARE_GET(dska, const ska::DSka*) { 
		return &m_ska; 
	}
#endif

	bool m_load;
	ska::DSka m_ska;
	file::MMapping::Ref m_mm;
};

} // asset

#include <Runtime/PopPack.h>


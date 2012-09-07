// MapParser.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH

#if defined(RAD_OPT_TOOLS)

#include "MapParser.h"
#include "../World/World.h"
#include "../Engine.h"

using namespace pkg;

namespace asset {

MapParser::MapParser()
: m_state(S_None) {
}

MapParser::~MapParser() {
}

int MapParser::Process(
	const xtime::TimeSlice &time,
	Engine &engine,
	const AssetRef &asset,
	int flags
) {
	if (flags&(P_Unload|P_Trim|P_Cancel))
	{
		m_state = S_None;
		m_script.Reset();
		return SR_Success;
	}

	if (asset->cooked || (flags&P_FastPath))
		return SR_Success;

	if (!(flags&(P_Load)))
		return SR_Success;

	if (m_state == S_Done)
		return SR_Success;

	int r = Load(
		time,
		engine,
		asset,
		flags
	);

	if (r < SR_Success) {
		m_state = S_None;
		m_script.Reset();
	}

	return r;
}

int MapParser::ParseEntity(world::EntSpawn &spawn) {
	spawn.keys.pairs.clear();
	return ParseScript(spawn);
}

int MapParser::ParseScript(world::EntSpawn &spawn) {
	String token, value, temp;

	if (!m_script.GetToken(token, Tokenizer::kTokenMode_CrossLine))
		return SR_End;
	if (token != "{")
		return SR_ParseError;

	for (;;) {
		if (!m_script.GetToken(token, Tokenizer::kTokenMode_CrossLine))
			return SR_ParseError;
		if (token == "}")
			break;
		if (!m_script.GetToken(value, Tokenizer::kTokenMode_CrossLine))
			return SR_ParseError;

		// turn "\n" into '\n'
		const char *sz = value.c_str;
		temp.Clear();

		while (*sz) {
			if (sz[0] == '\\' && sz[1] == 'n') {
				temp += '\n';
				++sz;
			} else {
				temp += *sz;
			}
			++sz;
		}

		spawn.keys.pairs[token] = temp;
	}

	return SR_Success;
}

int MapParser::Load(
	const xtime::TimeSlice &time,
	Engine &engine,
	const AssetRef &asset,
	int flags
) {
	if (m_state == S_None) {
		const String *name = asset->entry->KeyValue<String>("Source.File", P_TARGET_FLAGS(flags));
		if (!name || name->empty)
			return SR_MetaError;

		file::MMFileInputBuffer::Ref ib = engine.sys->files->OpenInputBuffer(name->c_str, ZWorld);
		if (!ib)
			return SR_FileNotFound;

		m_script.Bind(ib);

		m_state = S_Done;
	}

	return SR_Success;
}

void MapParser::Register(Engine &engine) {
	static pkg::Binding::Ref r = engine.sys->packages->Bind<MapParser>();
}

} // asset

#endif // RAD_OPT_TOOLS

/*! \file Console.cpp
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup engine
*/

#include RADPCH
#include "COut.h"
#include "Console.h"
#include "CVars.h"
#include <Runtime/Stream/Stream.h>
#include <Runtime/Stream/MemoryStream.h>
#include <Runtime/Tokenizer.h>

void Console::Exec(const char *sz, CVarZone *cvars) {
	RAD_ASSERT(sz);

	stream::MemInputBuffer ib(sz, string::len(sz));
	stream::InputStream is(ib);

	Tokenizer cmdline(is);

	String cmd;
	
	if (!cmdline.GetToken(cmd)) {
		COut(C_Error) << "Malformed exec command!" << std::endl;
		return;
	}

	if (cmd == "cvarlist") {
		ListCVars(cvars);
		return;
	}

	CVar *cvar = 0;
	if (cvars)
		cvar = cvars->Find(cmd.c_str, CVarZone::kFindScope_IncludingGlobals);

	if (!cvar) {
		COut(C_Error) << "There is no cvar with that name." << std::endl;
		return;
	}

	if (cvar->type == CVar::kType_Func) {
		// ECHO
		COut(C_Info) << sz << std::endl;
		cmdline.GetRemaining(cmd, Tokenizer::kFetchMode_RestOfLine);
		static_cast<CVarFunc*>(cvar)->Execute(cmd.c_str);
		return;
	}

	if (!cmdline.GetToken(cmd)) {
		// dump console value to output
		COut(C_Info) << cvar->name.get() << " is \"" << cvar->ToString().c_str.get() << "\"" << std::endl;
		return;
	}

	// set cvar
	cvar->Parse(cmd.c_str);

	// ECHO
	COut(C_Info) << sz << std::endl;
}

void Console::ListCVars(CVarZone *cvars) {

	COut(C_Info) << "*************************************************************" << std::endl;

	if (cvars) {
		for (CVarMap::const_iterator it = cvars->cvars->begin(); it != cvars->cvars->end(); ++it) {
			COut(C_Info) << it->second->name.get() << std::endl;
		}
	}

	if (cvars != &CVarZone::Globals()) {
		for (CVarMap::const_iterator it = CVarZone::Globals().cvars->begin(); it != CVarZone::Globals().cvars->end(); ++it) {
			COut(C_Info) << it->second->name.get() << std::endl;
		}
	}
}
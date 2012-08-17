/*! \file NativeApp.cpp
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup Main
*/

#include RADPCH
#include "NativeApp.h"
#include <Engine/COut.h>
#include <Runtime/StringBase.h>
#include <limits>

bool DisplayDevice::IsVidModeSupported(const r::VidMode &mode) {
	const r::VidModeVec &list = *this->vidModes.get();

	for (r::VidModeVec::const_iterator it = list.begin(); it != list.end(); ++it) {
		const r::VidMode &m = *it;

		if (m.w != mode.w)
			continue;
		if (m.h != mode.h)
			continue;
		if (m.bpp != mode.bpp)
			continue;
		if (mode.hz && m.hz != mode.hz)
			continue;

		return true;
	}

	return false;
}

bool DisplayDevice::MatchVidMode(
	r::VidMode &mode,
	MatchDisposition disposition
) {
	const r::VidModeVec &list = *this->vidModes.get();

	if (disposition&(
		kMatchDisposition_AllowAspect4x3|
		kMatchDisposition_AllowAspect16x9|
		kMatchDisposition_AllowAspect16x10
	)) {
		disposition |= kMatchDisposition_AllowAspectChange;
	}

	size_t bestX = std::numeric_limits<size_t>::max();
	int bestDiff = std::numeric_limits<int>::max();
	for (size_t i = 0; i < list.size(); ++i) {
		const r::VidMode &m = list[i];

		if (!(disposition&kMatchDisposition_AllowAspectChange)) {
			if (!m.SameAspect(mode))
				continue;
		} else if(m.Is4x3()) {
			if (!(disposition&kMatchDisposition_AllowAspect4x3))
				continue;
		} else if (m.Is16x9()) {
			if (!(disposition&kMatchDisposition_AllowAspect16x9))
				continue;
		} else if (m.Is16x10()) {
			if (!(disposition&kMatchDisposition_AllowAspect16x10))
				continue;
		} else {
			COut(C_Warn) << "WARNING: Video mode " << m.w << "x" << m.h << " has non-standard aspect ratio, mode ignored." << std::endl;
			continue;
		}

		if (disposition&kMatchDisposition_Upsize) {
			if (m.w < mode.w)
				continue;
		} else if(disposition&kMatchDisposition_Downsize) {
			if (m.w > mode.w)
				continue;
		} else if (m.w != mode.w) {
			continue;
		}

		int diff = m.w - mode.w;
		if (diff < 0)
			diff = -diff;

		if (diff < bestDiff) {
			bestDiff = diff;
			bestX = i;
		}
	}

	if (bestX == std::numeric_limits<size_t>::max())
		return false; // no matching on X.

	bool exact = list[bestX].w == mode.w;

	// found best match on X, find match on Y
	size_t bestY = std::numeric_limits<size_t>::max();
	bestDiff = std::numeric_limits<int>::max();

	for (size_t i = 0; i < list.size(); ++i) {
		const r::VidMode &m = list[i];

		if (!(disposition&kMatchDisposition_AllowAspectChange)) {
			if (!m.SameAspect(mode))
				continue;
		} else if(m.Is4x3()) {
			if (!(disposition&kMatchDisposition_AllowAspect4x3))
				continue;
		} else if (m.Is16x9()) {
			if (!(disposition&kMatchDisposition_AllowAspect16x9))
				continue;
		} else if (m.Is16x10()) {
			if (!(disposition&kMatchDisposition_AllowAspect16x10))
				continue;
		} else {
			COut(C_Warn) << "WARNING: Video mode " << m.w << "x" << m.h << " has non-standard aspect ratio, mode ignored." << std::endl;
			continue;
		}

		if (exact) {
			if (m.w != list[bestX].w)
				continue;
				
			if (disposition&kMatchDisposition_Upsize) {
				if (m.h < mode.h)
					continue;
			} else if(disposition&kMatchDisposition_Downsize) {
				if (m.h > mode.h)
					continue;
			} else if (m.h != mode.h) {
				continue;
			}
		}

		int diff = m.h - mode.h;
		if (diff < 0)
			diff = -diff;

		if (diff < bestDiff) {
			bestDiff = diff;
			bestY = i;
		}
	}

	if (bestY == std::numeric_limits<size_t>::max())
		return false;

	bool fullscreen = mode.fullscreen;
	mode = list[bestY];
	mode.fullscreen = fullscreen;

	return true;
}

///////////////////////////////////////////////////////////////////////////////

NativeApp::NativeApp(int argc, const char **argv) : m_argc(argc), m_argv(argv) {
	m_mainThreadId = thread::ThreadId();
}

bool NativeApp::FindArg(const char *arg) {
	for (int i = 0; i < m_argc; ++i) {
		if (!string::icmp(arg, m_argv[i])) 
			return true;
	}
	return false;
}

const char *NativeApp::ArgArg(const char *arg) {
	for (int i = 0; i < m_argc-1; ++i) {
		if (!string::icmp(arg, m_argv[i]))
			return m_argv[i+1];
	}
	return 0;
}

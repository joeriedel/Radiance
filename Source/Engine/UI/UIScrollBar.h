/*! \file UIScrollBar.h
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup ui
*/

#pragma once

#include "UIWidget.h"
#include "../Assets/MaterialParser.h"
#include <Runtime/PushPack.h>

namespace ui {

class VScrollBar;
struct VScrollBarMovedEventData {
	VScrollBar *instigator;
};

typedef Event<VScrollBarMovedEventData, EventNoAccess> VScrollBarMovedEvent;

//! Manages and renders a scroll bar (not a widget and doesn't fit in a widget hierarchy)
class VScrollBar {
public:
	typedef boost::shared_ptr<VScrollBar> Ref;
	typedef boost::weak_ptr<VScrollBar> WRef;

	VScrollBar();

	void Initialize(
		float width,
		float arrowHeight,
		const pkg::AssetRef &arrow,
		const pkg::AssetRef &arrowPressed,
		const pkg::AssetRef &track,
		const pkg::AssetRef &thumbTop,
		const pkg::AssetRef &thumbTopPressed,
		const pkg::AssetRef &thumbMiddle,
		const pkg::AssetRef &thumbMiddlePressed,
		const pkg::AssetRef &thumbBottom,
		const pkg::AssetRef &thumbBottomPressed
	);

	RAD_DECLARE_PROPERTY(VScrollBar, contentSize, float, float);
	RAD_DECLARE_PROPERTY(VScrollBar, rect, const Rect&, const Rect&);
	RAD_DECLARE_PROPERTY(VScrollBar, thumbPos, float, float);
	RAD_DECLARE_READONLY_PROPERTY(VScrollBar, thumbSize, float);
	RAD_DECLARE_READONLY_PROPERTY(VScrollBar, thumbMaxPos, float);

	void Draw(Widget &self, const Rect *clip);
	bool HandleInputEvent(Widget &self, const InputEvent &e, const TouchState *touch, const InputState &is);

	VScrollBarMovedEvent onMoved;

private:

	void RecalcBar();
	void ClampThumb();

	RAD_DECLARE_GET(contentSize, float) {
		return m_contentSize;
	}

	RAD_DECLARE_SET(contentSize, float) {
		m_contentSize = value;
		RecalcBar();
	}

	RAD_DECLARE_GET(rect, const Rect&) {
		return m_rect;
	}

	RAD_DECLARE_SET(rect, const Rect&) {
		m_rect = value;
		RecalcBar();
	}

	RAD_DECLARE_GET(thumbPos, float) {
		return m_thumbPos;
	}

	RAD_DECLARE_SET(thumbPos, float) {
		m_thumbPos = value;
	}

	RAD_DECLARE_GET(thumbSize, float) {
		return m_thumbSize;
	}

	RAD_DECLARE_GET(thumbMaxPos, float) {
		return m_thumbMaxPos;
	}

	struct Materials {
		asset::MaterialBundle arrow;
		asset::MaterialBundle arrowPressed;
		asset::MaterialBundle track;
		asset::MaterialBundle thumbTop;
		asset::MaterialBundle thumbTopPressed;
		asset::MaterialBundle thumbMiddle;
		asset::MaterialBundle thumbMiddlePressed;
		asset::MaterialBundle thumbBottom;
		asset::MaterialBundle thumbBottomPressed;
	};

	Materials m_materials;
	Rect m_rect;
	float m_contentSize;
	float m_thumbPos;
	float m_thumbSize;
	float m_thumbMaxPos;
	bool m_drag;
};

} // ui

#include <Runtime/PopPack.h>

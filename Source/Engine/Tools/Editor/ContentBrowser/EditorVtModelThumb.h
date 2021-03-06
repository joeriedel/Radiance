/*! \file EditorVtModelThumb.h
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup assets
*/

#pragma once

#include "EditorContentBrowserView.h"
#include <Runtime/PushPack.h>

namespace tools {
namespace editor {

class VtModelThumb : public ContentAssetThumb {
	Q_OBJECT
public:
	VtModelThumb(ContentBrowserView &view);
	virtual void OpenEditor(const pkg::Package::Entry::Ref &entry, bool editable, bool modal);
	static void New(ContentBrowserView &view);
};

} // editor
} // tools

#include <Runtime/PopPack.h>

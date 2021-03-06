/*! \file EditorPathfindingDebugWidget.h
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup editor
*/

#pragma once

#include "EditorTypes.h"
#include "EditorGLNavWidget.h"
#include "EditorProgressDialog.h"
#include "../../Packages/Packages.h"
#include "../../Assets/MapAsset.h"
#include "../../World/MapBuilder/MapBuilderDebugUI.h"
#include "../../World/Floors.h"
#include <Runtime/PushPack.h>

namespace tools {
namespace editor {

class RADENG_CLASS PathfindingDebugWidget : public GLNavWidget {
	Q_OBJECT
public:

	PathfindingDebugWidget(QWidget *parent = 0, Qt::WindowFlags f = 0);
	virtual ~PathfindingDebugWidget();

	void DebugMap(int id);

protected:

	virtual void renderGL();
	virtual void mousePressEvent(QMouseEvent *e);

private slots:

	void OnTick(float dt);

private:

	void LoadPlayerStart();

	int FindEntityByClass(const char *classname);

	const char *StringForKey(int entityNum, const char *key);

	void DrawFloors();
	void DrawFloor(int floor, const Vec4 &color);
	void DrawFloorTri(int tri, const Vec4 &color);
	void DrawMove(const ::world::FloorMove::Ref &move, const Vec4 &color);
	void DrawSpline(const ::world::FloorMove::Spline &spline);
	void Project(int x, int y, Vec3 &start, Vec3 &end);

	::world::FloorPosition m_pos[2];
	::world::FloorMove::Ref m_move;
	pkg::Asset::Ref m_asset;
	asset::MapAsset *m_map;
	world::Floors m_floors;
	world::bsp_file::BSPFile::Ref m_bsp;
	bool m_loaded;
	bool m_validPos[2];
	ProgressDialog *m_progress;
};

} // editor
} // tools

#include <Runtime/PopPack.h>

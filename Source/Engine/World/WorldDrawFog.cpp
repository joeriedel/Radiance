/*! \file WorldDrawFog.cpp
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup world
*/

#include RADPCH
#include "World.h"
#include "../Game/GameCVars.h"

namespace world {

void WorldDraw::DrawFog(ViewDef &view) {

	if (!m_world->cvars->r_drawfog.value)
		return;

	if (view.area < 0)
		return;
		
	CountFogs(view);
	++m_markFrame; // reset counter

	m_rb->BeginFog();

	if (m_world->m_nodes.empty()) {
		DrawFogNode(view, -1);
	} else {
		DrawFogNode(view, 0);
	}

	m_rb->EndFog();
}

void WorldDraw::CountFogs(ViewDef &view) {
	view.numFogs = 0;
	
	if (m_world->m_nodes.empty()) {
		CountFogNode(view, -1);
	} else {
		CountFogNode(view, 0);
	}
}

void WorldDraw::CountFogNode(ViewDef &view, int nodeNum) {
	if (nodeNum < 0) {
		nodeNum = -(nodeNum + 1);
		const dBSPLeaf &leaf = m_world->m_leafs[nodeNum];
		if (m_world->cvars->r_frustumcull.value && !ClipBounds(view.frustumVolume, view.frustumBounds, leaf.bounds))
			return; // node bounds not in view
		view.numFogs += leaf.numFogs;
		return;
	}

	const dBSPNode &node = m_world->m_nodes[nodeNum];

	if (m_world->cvars->r_frustumcull.value && !ClipBounds(view.frustumVolume, view.frustumBounds, node.bounds))
		return; // node bounds not in view

	CountFogNode(view, node.children[0]);
	CountFogNode(view, node.children[1]);
}

void WorldDraw::DrawFogNode(ViewDef &view, int nodeNum) {
	if (nodeNum < 0) {
		nodeNum = -(nodeNum + 1);
		DrawFogLeaf(view, nodeNum);
		return;
	}

	const dBSPNode &node = m_world->m_nodes[nodeNum];

	if (m_world->cvars->r_frustumcull.value && !ClipBounds(view.frustumVolume, view.frustumBounds, node.bounds))
		return; // node bounds not in view

	const Plane &p = m_world->m_planes[node.planenum];

	Plane::SideType side = p.Side(view.camera.pos, 0.1f);
	if (side == Plane::Front) {
		// back->front
		DrawFogNode(view, node.children[1]);
		DrawFogNode(view, node.children[0]);
		return;
	}

	// back->front
	DrawFogNode(view, node.children[0]);
	DrawFogNode(view, node.children[1]);
}

void WorldDraw::DrawFogLeaf(ViewDef &view, int leafNum) {
	const dBSPLeaf &leaf = m_world->m_leafs[leafNum];

	if ((leaf.numFogs < 1) || (leaf.area < 0) || !view.areas.test(leaf.area))
		return; // not in a visible area

	if (m_world->cvars->r_frustumcull.value && !ClipBounds(view.frustumVolume, view.frustumBounds, leaf.bounds))
		return; // node bounds not in view

	for (int i = 0; i < leaf.numFogs; ++i) {
		int index = *(m_world->m_bsp->ModelIndices() + leaf.firstFog + i);
		DrawFogNum(view, index);
	}
}

void WorldDraw::DrawFogNum(ViewDef &view, int num) {
	const MStaticWorldMeshBatch::Ref &fog = m_worldModels[num];
	if (fog->m_markFrame == m_markFrame)
		return; // drawn this frame
	fog->m_markFrame = m_markFrame;

	++m_counters.drawnFogs;
	
	MBatchDraw *draw = fog.get();

	const details::MatRef *matRef = fog->m_matRef;
	r::Shader::Uniforms u;
	u.blendColor = Vec4(1.f, 1.f, 1.f, 1.f);
	u.lights.numLights = 0;
	u.tcGen = m_rb->GetModelViewProjectionMatrix(true);
	
	// render fog backfaces into z
	m_fogZ_M.material->BindTextures(m_fogZ_M.loader);
	m_rb->BeginFogDepthWrite(*m_fogZ_M.material, false);

	m_fogZ_M.material->shader->Begin(r::Shader::kPass_Default, *m_fogZ_M.material);
	draw->Bind(m_fogZ_M.material->shader.get().get());
	m_fogZ_M.material->shader->BindStates(u);
	m_rb->CommitStates();
	draw->CompileArrayStates(*m_fogZ_M.material->shader.get());
	draw->Draw();
	m_fogZ_M.material->shader->End();

	// render fog opacity based on front face z
	matRef->mat->BindTextures(matRef->loader);
	m_rb->BeginFogDraw(*matRef->mat);
	matRef->mat->shader->Begin(r::Shader::kPass_Default, *matRef->mat);
	draw->Bind(matRef->mat->shader.get().get());
	matRef->mat->shader->BindStates(u);
	m_rb->CommitStates();
	draw->CompileArrayStates(*matRef->mat->shader.get());
	draw->Draw();
	matRef->mat->shader->End();

	// render fog front faces into z (for next fog)
	if (--view.numFogs > 0) { // skip this, if we are the last drawn fog we don't care.
		m_fogZ_M.material->BindTextures(m_fogZ_M.loader);
		m_rb->BeginFogDepthWrite(*m_fogZ_M.material, true);

		m_fogZ_M.material->shader->Begin(r::Shader::kPass_Default, *m_fogZ_M.material);
		draw->Bind(m_fogZ_M.material->shader.get().get());
		m_fogZ_M.material->shader->BindStates(u);
		m_rb->CommitStates();
		draw->CompileArrayStates(*m_fogZ_M.material->shader.get());
		draw->Draw();
		m_fogZ_M.material->shader->End();
	}
}

}

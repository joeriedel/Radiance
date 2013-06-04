// WorldDebugDraw.cpp
// Copyright (c) 2012 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "World.h"
#if defined(WORLD_DEBUG_DRAW)
#include "../Game/Game.h"
#include "../Game/GameCVars.h"
#include "../Renderer/Shader.h"

using namespace r;

namespace world {

int WorldDraw::LoadDebugMaterials() {
	
	int r = LoadMaterial("Sys/DebugWireframe_M", m_dbgVars.wireframe_M);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugPortalEdge_M", m_dbgVars.portal_M[0]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugPortal_M", m_dbgVars.portal_M[1]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugWorldBBox_M", m_dbgVars.worldBBox_M);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugEntityBBox_M", m_dbgVars.entityBBox_M);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugActorBBox_M", m_dbgVars.actorBBox_M);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugWaypoint_M", m_dbgVars.waypoint_M);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugLight0_M", m_dbgVars.lightPasses_M[0]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugLight1_M", m_dbgVars.lightPasses_M[1]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugLight2_M", m_dbgVars.lightPasses_M[2]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugLight3_M", m_dbgVars.lightPasses_M[3]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugLight4_M", m_dbgVars.lightPasses_M[4]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugLight5_M", m_dbgVars.lightPasses_M[5]);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/EditorLightSphere_M", m_dbgVars.lightSphere_M);
	if (r != pkg::SR_Success)
		return r;

	r = LoadMaterial("Sys/DebugSphere_M", m_dbgVars.sphere_M);
	if (r != pkg::SR_Success)
		return r;

	m_dbgVars.lightMesh = r::Mesh::MakeSphere(ZWorld, false);

	return m_rb->LoadMaterials();
}

void WorldDraw::DebugDrawPortals(ViewDef &view) {
	for (int i = 0; i < kMaxAreas; ++i) {
		if (view.areas.test(i))
			DebugDrawAreaportals(i);
	}
}

void WorldDraw::DebugDrawAreaportals(int areaNum) { 

	RAD_ASSERT(areaNum < (int)m_world->m_areas.size());

	for (int style = 0; style < 2; ++style) {
		m_dbgVars.portal_M[style].mat->BindStates();
		m_dbgVars.portal_M[style].mat->BindTextures(m_dbgVars.portal_M[style].loader);
		m_dbgVars.portal_M[style].mat->shader->Begin(r::Shader::kPass_Default, *m_dbgVars.portal_M[style].mat);


		const dBSPArea &area = m_world->m_areas[areaNum];

		for(int i = 0; i < area.numPortals; ++i) {

			int areaportalNum = (int)*(m_world->m_bsp->AreaportalIndices() + area.firstPortal + i);
			const dAreaportal &portal = m_world->m_areaportals[areaportalNum];

			m_rb->DebugUploadVerts(
				&portal.winding.Vertices()[0],
				portal.winding.NumVertices()
			);

			int numIndices = 0;
			if (style == 1)
				numIndices = m_rb->DebugTesselateVerts(portal.winding.NumVertices());

			m_dbgVars.portal_M[style].mat->shader->BindStates();
			m_rb->CommitStates();

			if (style == 0) {
				m_rb->DebugDrawLineLoop(portal.winding.NumVertices());
			} else {
				m_rb->DebugDrawIndexedTris(numIndices);
			}
		}

		m_dbgVars.portal_M[style].mat->shader->End();
	}
}

void WorldDraw::DebugDrawLightScissors() {
	m_rb->SetScreenLocalMatrix();
	DebugDrawRects(m_dbgVars.wireframe_M, m_dbgVars.lightScissors);
}

void WorldDraw::DebugDrawRects(const LocalMaterial &material, const Vec4Vec &rects) {
	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);

	for (Vec4Vec::const_iterator it = rects.begin(); it != rects.end(); ++it)
		DebugDrawRectBatch(material, *it);

	material.mat->shader->End();
}

void WorldDraw::DebugDrawRect(const LocalMaterial &material, const Vec4 &rect) {
	m_rb->SetScreenLocalMatrix();
	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);
	DebugDrawRectBatch(material, rect);
	material.mat->shader->End();
}

void WorldDraw::DebugDrawRectBatch(const LocalMaterial &material, const Vec4 &rect) {
	Vec3 verts[4];

	verts[0] = Vec3(rect[0], rect[1], 0.f);
	verts[1] = Vec3(rect[0], rect[3], 0.f);
	verts[2] = Vec3(rect[2], rect[3], 0.f);
	verts[3] = Vec3(rect[2], rect[1], 0.f);

	m_rb->DebugUploadVerts(verts, 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	m_rb->DebugDrawLineLoop(4);
}

void WorldDraw::DebugDrawBBoxes(const LocalMaterial &material, const BBoxVec &bboxes, bool wireframe) {
	if (bboxes.empty())
		return;

	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);

	for (BBoxVec::const_iterator it = bboxes.begin(); it != bboxes.end(); ++it)
		DebugDrawBBoxBatch(material, *it, wireframe);

	material.mat->shader->End();
}

void WorldDraw::DebugDrawBBox(const LocalMaterial &material, const BBox &bbox, bool wireframe) {
	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);
	DebugDrawBBoxBatch(material, bbox, wireframe);
	material.mat->shader->End();
}

void WorldDraw::DebugDrawBBoxBatch(const LocalMaterial &material, const BBox &bbox, bool wireframe) {
	typedef stackify<zone_vector<Vec3, ZWorldT>::type, 4> StackVec;
	StackVec v;

	const Vec3 &mins = bbox.Mins();
	const Vec3 &maxs = bbox.Maxs();

	// +X
	v->push_back(maxs);
	v->push_back(Vec3(maxs[0], mins[1], maxs[2]));
	v->push_back(Vec3(maxs[0], mins[1], mins[2]));
	v->push_back(Vec3(maxs[0], maxs[1], mins[2]));

	m_rb->DebugUploadVerts(&v[0], 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	if (wireframe) {
		m_rb->DebugDrawLineLoop(4);
	} else {
		m_rb->DebugDrawPoly(4);
	}
	v->clear();
	
	// -X
	v->push_back(Vec3(mins[0], maxs[1], maxs[2]));
	v->push_back(Vec3(mins[0], maxs[1], mins[2]));
	v->push_back(mins);
	v->push_back(Vec3(mins[0], mins[1], maxs[2]));

	m_rb->DebugUploadVerts(&v[0], 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	if (wireframe) {
		m_rb->DebugDrawLineLoop(4);
	} else {
		m_rb->DebugDrawPoly(4);
	}
	v->clear();

	// +Y
	v->push_back(maxs);
	v->push_back(Vec3(maxs[0], maxs[1], mins[2]));
	v->push_back(Vec3(mins[0], maxs[1], mins[2]));
	v->push_back(Vec3(mins[0], maxs[1], maxs[2]));

	m_rb->DebugUploadVerts(&v[0], 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	if (wireframe) {
		m_rb->DebugDrawLineLoop(4);
	} else {
		m_rb->DebugDrawPoly(4);
	}
	v->clear();

	// -Y
	v->push_back(Vec3(maxs[0], mins[1], maxs[2]));
	v->push_back(Vec3(mins[0], mins[1], maxs[2]));
	v->push_back(mins);
	v->push_back(Vec3(maxs[0], mins[1], mins[2]));
	
	m_rb->DebugUploadVerts(&v[0], 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	if (wireframe) {
		m_rb->DebugDrawLineLoop(4);
	} else {
		m_rb->DebugDrawPoly(4);
	}
	v->clear();

	// +Z
	v->push_back(maxs);
	v->push_back(Vec3(mins[0], maxs[1], maxs[2]));
	v->push_back(Vec3(mins[0], mins[1], maxs[2]));
	v->push_back(Vec3(maxs[0], mins[1], maxs[2]));

	m_rb->DebugUploadVerts(&v[0], 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	if (wireframe) {
		m_rb->DebugDrawLineLoop(4);
	} else {
		m_rb->DebugDrawPoly(4);
	}
	v->clear();
	
	// -Z
	v->push_back(mins);
	v->push_back(Vec3(maxs[0], mins[1], mins[2]));
	v->push_back(Vec3(maxs[0], maxs[1], mins[2]));
	v->push_back(Vec3(mins[0], maxs[1], mins[2]));

	m_rb->DebugUploadVerts(&v[0], 4);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	if (wireframe) {
		m_rb->DebugDrawLineLoop(4);
	} else {
		m_rb->DebugDrawPoly(4);
	}
	v->clear();

}

void WorldDraw::DebugDrawActiveWaypoints() {
	bool begin = false;

	for (U32 i = 0; i < m_world->bspFile->numWaypoints; ++i) {
		if (!(m_world->floors->WaypointState((int)i) & Floors::kWaypointState_Enabled))
			continue;

		if (!begin) {
			begin = true;
			m_dbgVars.waypoint_M.mat->BindStates();
			m_dbgVars.waypoint_M.mat->BindTextures(m_dbgVars.waypoint_M.loader);
			m_dbgVars.waypoint_M.mat->shader->Begin(r::Shader::kPass_Default, *m_dbgVars.waypoint_M.mat);
		}

		const bsp_file::BSPWaypoint *waypoint = m_world->bspFile->Waypoints() + i;
		const Vec3 kPos(waypoint->pos[0], waypoint->pos[1], waypoint->pos[2]);
		const BBox kBox(kPos - Vec3(12, 12, 12), kPos + Vec3(12, 12, 12));
		DebugDrawBBoxBatch(m_dbgVars.waypoint_M, kBox, false);
	}

	if (begin) {
		m_dbgVars.waypoint_M.mat->shader->End();
	}
}

void WorldDraw::DebugDrawFloorMoves() {
	LocalMaterial &material = m_dbgVars.worldBBox_M;

	bool begin = false;

	for (Entity::IdMap::const_iterator it = m_world->m_ents.begin(); it != m_world->m_ents.end(); ++it) {
		const Entity::Ref &entity = it->second;

		if (entity->ps->activeMove) {
			if (!begin) {
				begin = true;
				material.mat->BindStates();
				material.mat->BindTextures(material.loader);
				material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);
			}

			DebugDrawFloorMoveBatch(material, *entity->ps->activeMove);
		}
	}

	if (begin)
		material.mat->shader->End();
}

void WorldDraw::DebugDrawFloorMoveBatch(const LocalMaterial &material, const FloorMove &move) {
	enum {
		kSplineTess = 8
	};

	const float kStep = 1.f / (kSplineTess-1);
	Vec3 pts[kSplineTess];

	if (move.route->steps->empty())
		return;

	for (FloorMove::Step::Vec::const_iterator it = move.route->steps->begin(); it != move.route->steps->end(); ++it) {
		const FloorMove::Step &step = *it;

		float t = 0.f;

		for (int i = 0; i < kSplineTess; ++i, t += kStep) {
			step.path.Eval(t, pts[i], 0);
		}

		BBox bbox(pts[kSplineTess-1] - Vec3(16, 16, 16), pts[kSplineTess-1] + Vec3(16, 16, 16));
		DebugDrawBBoxBatch(material, bbox, true);

		m_rb->DebugUploadVerts(&pts[0], kSplineTess);
		material.mat->shader->BindStates();
		m_rb->CommitStates();
		m_rb->DebugDrawLineStrip(kSplineTess);
	}
}

void WorldDraw::DebugDrawLightPasses(ViewDef &view) {
	for (int i = 0; i < r::Material::kNumSorts; ++i) {
		for (details::MBatchIdMap::const_iterator it = view.batches.begin(); it != view.batches.end(); ++it) {
			const details::MBatch &batch = *it->second;

			if (batch.matRef->mat->sort != (r::Material::Sort)i) {
				continue;
			}

			if (batch.matRef->mat->maxLights > 0) {
				DebugDrawLightPass(batch);
			} else {
				DrawUnlitBatch(view, batch, false);
			}
		}
	}

	m_rb->ReleaseArrayStates(); // important! keeps pipeline changes from being recorded into VAO's
}

void WorldDraw::DebugDrawLightPass(const details::MBatch &batch) {

	r::Material *mat = batch.matRef->mat;
		
	Vec3 pos;
	Vec3 angles;
	
	bool diffuse = mat->shader->HasPass(r::Shader::kPass_Diffuse1);
	bool diffuseSpecular = mat->shader->HasPass(r::Shader::kPass_DiffuseSpecular1);

	RAD_ASSERT_MSG(diffuse, "All lighting shaders must have diffuse pass!");

	const int kMaxLights = std::min(mat->maxLights.get(), m_world->cvars->r_maxLightsPerPass.value.get());

	for (details::MBatchDrawLink *link = batch.head; link; link = link->next) {
		MBatchDraw *draw = link->draw;

		const bool tx = draw->GetTransform(pos, angles);
		if (tx) {
			m_rb->PushMatrix(pos, draw->scale, angles);
		}

		int numPasses = 0;
		
		details::LightInteraction *interaction = draw->m_interactions;
		while (interaction) {
			if (interaction->light->m_visFrame != m_markFrame) {
				interaction = interaction->nextOnBatch;
				continue; // not visible this frame.
			}
			const Light::LightStyle kStyle = interaction->light->style;
			if (kStyle & Light::kStyle_CastShadows) {
				++numPasses; // shadow casters cannot be batched.
			}
			interaction = interaction->nextOnBatch;
		}

		bool didDiffuse = false;
		bool didDiffuseSpecular = false;

		// count the # of lighting passes required to render this object.
		for (;;) {
			interaction = draw->m_interactions;

			while (interaction) {

				// batch up to kNumLights
				int numLights = 0;
	
				while (interaction && (numLights < kMaxLights)) {
					if (interaction->light->m_visFrame != m_markFrame) {
						interaction = interaction->nextOnBatch;
						continue; // not visible this frame.
					}

					const Light::LightStyle kStyle = interaction->light->style;

					if (!(kStyle & Light::kStyle_CastShadows)) {
						bool add = false;

						if ((kStyle&Light::kStyle_DiffuseSpecular) == Light::kStyle_DiffuseSpecular) {
							add = !didDiffuseSpecular;
						} else if (kStyle&Light::kStyle_Diffuse) {
							add = diffuse && (!diffuseSpecular || didDiffuseSpecular);
						}
					
						if (add) {
							++numLights;
						}
					}

					interaction = interaction->nextOnBatch;
				}

				if (numLights > 0) {
					++numPasses;
				}

				if (numPasses >= 5)
					break; // debug display doesn't do more than this.
			}

			if (numPasses >= 5)
				break; // debug display doesn't do more than this.

			if (diffuseSpecular && !didDiffuseSpecular) {
				didDiffuseSpecular = true;
			} else {
				break;
			}
		}

		const LocalMaterial &debugMat = m_dbgVars.lightPasses_M[numPasses];
		
		debugMat.mat->BindStates();
		debugMat.mat->BindTextures(debugMat.loader);
		debugMat.mat->shader->Begin(r::Shader::kPass_Default, *debugMat.mat);
		draw->Bind(debugMat.mat->shader.get().get());
		debugMat.mat->shader->BindStates();
		m_rb->CommitStates();
		draw->CompileArrayStates(*debugMat.mat->shader.get());
		draw->Draw();

		if (tx)
			m_rb->PopMatrix();

		debugMat.mat->shader->End();
	}
}

void WorldDraw::DebugDrawLightCounts(ViewDef &view) {
	for (int i = 0; i < r::Material::kNumSorts; ++i) {
		for (details::MBatchIdMap::const_iterator it = view.batches.begin(); it != view.batches.end(); ++it) {
			const details::MBatch &batch = *it->second;

			if (batch.matRef->mat->sort != (r::Material::Sort)i) {
				continue;
			}

			if (batch.matRef->mat->maxLights > 0) {
				DebugDrawLightCounts(batch);
			} else {
				DrawUnlitBatch(view, batch, false);
			}
		}
	}

	m_rb->ReleaseArrayStates(); // important! keeps pipeline changes from being recorded into VAO's
}

void WorldDraw::DebugDrawLightCounts(const details::MBatch &batch) {

	r::Material *mat = batch.matRef->mat;
		
	Vec3 pos;
	Vec3 angles;
	
	bool diffuse = mat->shader->HasPass(r::Shader::kPass_Diffuse1);
	bool diffuseSpecular = mat->shader->HasPass(r::Shader::kPass_DiffuseSpecular1);

	RAD_ASSERT_MSG(diffuse, "All lighting shaders must have diffuse pass!");

	const int kMaxLights = std::min(mat->maxLights.get(), m_world->cvars->r_maxLightsPerPass.value.get());

	for (details::MBatchDrawLink *link = batch.head; link; link = link->next) {
		MBatchDraw *draw = link->draw;

		const bool tx = draw->GetTransform(pos, angles);
		if (tx) {
			m_rb->PushMatrix(pos, draw->scale, angles);
		}

		int numLights = 0;
		
		details::LightInteraction *interaction = draw->m_interactions;
		while (interaction) {
			if (interaction->light->m_visFrame == m_markFrame) {
				++numLights;
				if (numLights >= 5) // max for debug display
					break;
			}
			interaction = interaction->nextOnBatch;
		}

		const LocalMaterial &debugMat = m_dbgVars.lightPasses_M[numLights];
		
		debugMat.mat->BindStates();
		debugMat.mat->BindTextures(debugMat.loader);
		debugMat.mat->shader->Begin(r::Shader::kPass_Default, *debugMat.mat);
		draw->Bind(debugMat.mat->shader.get().get());
		debugMat.mat->shader->BindStates();
		m_rb->CommitStates();
		draw->CompileArrayStates(*debugMat.mat->shader.get());
		draw->Draw();

		if (tx)
			m_rb->PopMatrix();

		debugMat.mat->shader->End();
	}
}

void WorldDraw::DebugDrawFrustumVolumes(ViewDef &view) {

	m_dbgVars.wireframe_M.mat->BindStates();
	m_dbgVars.wireframe_M.mat->BindTextures(m_dbgVars.wireframe_M.loader);
	m_dbgVars.wireframe_M.mat->shader->Begin(r::Shader::kPass_Default, *m_dbgVars.wireframe_M.mat);

	const r::Shader::Uniforms white(Vec4(1,1,1,1));
	const r::Shader::Uniforms green(Vec4(0,1,0,1));
	const r::Shader::Uniforms blue(Vec4(0,1,0,1));

	for (int x = m_dbgVars.frustum->size(); x > 0; --x) {
		const StackWinding &w = m_dbgVars.frustum[x-1];
			
		m_rb->DebugUploadVerts(
			&w.Vertices()[0],
			w.NumVertices()
		);

		if (x == 1) {
			m_dbgVars.wireframe_M.mat->shader->BindStates(green);
		} else {
			m_dbgVars.wireframe_M.mat->shader->BindStates(white);
		}

		m_rb->CommitStates();
		m_rb->DebugDrawLineLoop(w.NumVertices());
	}

	for (ClippedAreaVolumeStackVec::const_iterator it = m_dbgVars.frustumAreas->begin(); it != m_dbgVars.frustumAreas->end(); ++it) {
		const ClippedAreaVolume &v = *it;

		for (int x = v.volume->size(); x > 0; --x) {
			const StackWinding &w = v.volume[x-1];
			
			m_rb->DebugUploadVerts(
				&w.Vertices()[0],
				w.NumVertices()
			);

			if (x == 1) {
				m_dbgVars.wireframe_M.mat->shader->BindStates(green);
			} else {
				m_dbgVars.wireframe_M.mat->shader->BindStates(blue);
			}

			m_rb->CommitStates();
			m_rb->DebugDrawLineLoop(w.NumVertices());
		}
	}

	m_dbgVars.wireframe_M.mat->shader->End();
}

void WorldDraw::DebugDrawLights() {

	LocalMaterial &material = m_dbgVars.lightSphere_M;
	
	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);

	for (Vec3Vec::const_iterator it = m_dbgVars.lights.begin(); it != m_dbgVars.lights.end(); ++it) {
		const Vec3 &pos = *it;
		m_rb->PushMatrix(pos, Vec3(4,4,4), Vec3::Zero);
		m_dbgVars.lightMesh->BindAll(material.mat->shader.get().get());
		material.mat->shader->BindStates();
		m_rb->CommitStates();
		m_dbgVars.lightMesh->CompileArrayStates(*material.mat->shader.get());
		m_dbgVars.lightMesh->Draw();
		m_rb->PopMatrix();
	}
		
	material.mat->shader->End();

	m_rb->ReleaseArrayStates(); // important! keeps pipeline changes from being recorded into VAO's
}

void WorldDraw::DebugDrawUnifiedLights() {
	r::Shader::Uniforms u(r::Shader::Uniforms::kDefault);
	u.blendColor = Vec4(1.f, 0.f, 1.f, 1.f);

	LocalMaterial &material = m_dbgVars.lightSphere_M;
	
	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);

	for (Vec3Vec::const_iterator it = m_dbgVars.unifiedLights.begin(); it != m_dbgVars.unifiedLights.end(); ++it) {
		const Vec3 &pos = *it;
		m_rb->PushMatrix(pos, Vec3(4,4,4), Vec3::Zero);
		m_dbgVars.lightMesh->BindAll(material.mat->shader.get().get());
		material.mat->shader->BindStates(u);
		m_rb->CommitStates();
		m_dbgVars.lightMesh->CompileArrayStates(*material.mat->shader.get());
		m_dbgVars.lightMesh->Draw();
		m_rb->PopMatrix();
	}
		
	material.mat->shader->End();

	m_rb->ReleaseArrayStates(); // important! keeps pipeline changes from being recorded into VAO's
}

bool WorldDraw::DebugSetupUnifiedLightMatrixView(ViewDef &view) {

	const Entity::Ref &player = m_world->playerPawn;
	if (!(player && player->m_lightInteractions)) {
		return false; // player has no shadowing lights.
	}

	float radius;
	Vec3 lightPos;

	BBox bounds(player->ps->bbox);
	bounds.Translate(player->ps->worldPos);
	CalcUnifiedShadowPosAndSize(bounds, player->m_lightInteractions, lightPos, radius);

	Camera lightCam;
	lightCam.pos = lightPos;
	lightCam.fov = 45.f;
	lightCam.farClip = 500;//m_world->camera->farClip;
	lightCam.LookAt(bounds.Origin());
	m_dbgVars.unifiedLightAxis[0] = lightCam.fwd;
	m_dbgVars.unifiedLightAxis[1] = lightCam.left;
	m_dbgVars.unifiedLightAxis[2] = lightCam.up;
	m_dbgVars.unifiedLightAxis[3] = lightCam.pos;
	m_dbgVars.unifiedLightAxis[4] = bounds.Origin();

	if (m_world->cvars->r_lockvis.value) {
		if (!m_dbgVars.lockVis) {
			m_dbgVars.lockVisCamera = lightCam;
			m_dbgVars.lockVis = true;
		}
	}

	view.camera = lightCam;
	m_rb->RotateForCamera(view.camera);
	view.mv = m_rb->GetModelViewMatrix();
	m_dbgVars.unifiedLightMatrix = view.mv;

	Vec4 viewplanes;
	Vec2 zplanes;

	Vec3 radial(lightCam.pos.get() + (lightCam.fwd.get() * radius));

	CalcViewplaneBounds(view, bounds, &radial, viewplanes, zplanes);
	
	SetOrthoMatrix(viewplanes, zplanes);
	view.mvp = m_rb->GetModelViewProjectionMatrix();

	SetupOrthoFrustumPlanes(view, viewplanes, zplanes);
	FindViewArea(view);

	return true;
}

void WorldDraw::DebugDrawProjectedBBoxPoints() {
	r::Shader::Uniforms u(r::Shader::Uniforms::kDefault);
	u.blendColor = Vec4(0.f, 0.f, 1.f, 1.f);

	LocalMaterial &material = m_dbgVars.sphere_M;
	
	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);

	for (Vec3Vec::const_iterator it = m_dbgVars.projectedBoxPoints.begin(); it != m_dbgVars.projectedBoxPoints.end(); ++it) {
		const Vec3 &pos = *it;
		m_rb->PushMatrix(pos, Vec3(2,2,2), Vec3::Zero);
		m_dbgVars.lightMesh->BindAll(material.mat->shader.get().get());
		material.mat->shader->BindStates(u);
		m_rb->CommitStates();
		m_dbgVars.lightMesh->CompileArrayStates(*material.mat->shader.get());
		m_dbgVars.lightMesh->Draw();
		m_rb->PopMatrix();
	}
		
	material.mat->shader->End();

	m_rb->ReleaseArrayStates(); // important! keeps pipeline changes from being recorded into VAO's
}

void WorldDraw::DebugDrawUnifiedLightAxis() {
	const LocalMaterial &material = m_dbgVars.wireframe_M;
	const r::Shader::Uniforms kRed(Vec4(1.f, 0.f, 0.f, 1.f));
	const r::Shader::Uniforms kGreen(Vec4(0.f, 1.f, 0.f, 1.f));
	const r::Shader::Uniforms kBlue(Vec4(0.f, 0.f, 1.f, 1.f));
	const float kAxisLen = 16.f;

	const Vec3 &cameraPos = m_dbgVars.unifiedLightAxis[3];

	material.mat->BindStates();
	material.mat->BindTextures(material.loader);
	material.mat->shader->Begin(r::Shader::kPass_Default, *material.mat);

	Vec3 v[2];

	v[0] = cameraPos;
	v[1] = m_dbgVars.unifiedLightAxis[4];
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates();
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);

	// camera-space Z
	v[1] = cameraPos + m_dbgVars.unifiedLightAxis[0] * kAxisLen;
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates(kBlue);
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);

	// camera-space X
	v[1] = cameraPos + m_dbgVars.unifiedLightAxis[1] * kAxisLen;
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates(kRed);
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);

	// camera space Y
	v[1] = cameraPos + m_dbgVars.unifiedLightAxis[2] * kAxisLen;
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates(kGreen);
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);

	// debug axis
	// untransformed camera faces down +X, +Z up, +Y left

	// debug-space Z
	Mat4 m = m_dbgVars.unifiedLightMatrix;
	m.Invert();

	Vec3 axis = m.Transform(Vec3(1,0,0) * kAxisLen);
	/*Vec4 waxis = m.Transform(Vec4(0,0,kAxisLen,1));
	Vec3 orgZero = m.Transform(cameraPos);
	Vec3 orgZero2 = m_dbgVars.unifiedLightMatrix.Transform(cameraPos);
	Vec3 z = axis - cameraPos;*/

	v[1] = axis;
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates(kBlue);
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);

	// debug-space X

	axis = m.Transform(Vec3(0,1,0) * kAxisLen);

	v[1] = axis;
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates(kRed);
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);

	// debug space Y

	axis = m.Transform(Vec3(0,0,1) * kAxisLen);

	v[1] = axis;
	m_rb->DebugUploadVerts(&v[0], 2);
	material.mat->shader->BindStates(kGreen);
	m_rb->CommitStates();
	m_rb->DebugDrawLineStrip(2);
	
	material.mat->shader->End();
}

} // world

#endif // defined(WORLD_DEBUG_DRAW)

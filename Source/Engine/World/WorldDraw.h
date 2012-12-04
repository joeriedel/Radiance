// WorldDraw.h
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#pragma once

#include "../Renderer/Mesh.h"
#include "../Renderer/Material.h"
#include "../Assets/MaterialParser.h"
#include "../Camera.h"
#include "BSPFile.h"
#include "WorldDef.h"
#include "WorldDrawDef.h"
#include "MBatchDraw.h"
#include "DrawModel.h"
#include "ScreenOverlay.h"
#include "PostProcess.h"
#include <Runtime/Container/ZoneMap.h>
#include <Runtime/Base/ObjectPool.h>
#include <bitset>
#include <Runtime/PushPack.h>


namespace world {

///////////////////////////////////////////////////////////////////////////////

class ViewDef {
public:
	enum {
		kNumFrustumPlanes = 6
	};
	
	ViewDef() : mirror(false) {
	}

	Camera camera;
	PlaneVec frustum;

	int area;
	bool mirror;
	AreaBits areas;

	details::MBatchIdMap batches;
};

///////////////////////////////////////////////////////////////////////////////

class RADENG_CLASS RB_WorldDraw {
public:
	typedef boost::shared_ptr<RB_WorldDraw> Ref;

	static Ref New(World *w);

	RB_WorldDraw(World *w) : m_world(w) {}
	virtual ~RB_WorldDraw() {}

	RAD_DECLARE_READONLY_PROPERTY(RB_WorldDraw, world, World*);
	RAD_DECLARE_PROPERTY(RB_WorldDraw, wireframe, bool, bool);
	RAD_DECLARE_PROPERTY(RB_WorldDraw, numTris, int, int);

	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual int LoadMaterials() = 0;
	virtual int Precache() = 0;
	virtual void BindRenderTarget() = 0;
    virtual void ClearBackBuffer() = 0;
	virtual void ClearDepthBuffer() = 0;
	virtual void SetWorldStates() = 0;
	virtual void SetPerspectiveMatrix() = 0;
	virtual void SetScreenLocalMatrix() = 0;
	virtual void RotateForCamera(const Camera &camera) = 0;
	virtual void RotateForCameraBasis() = 0;
	virtual void PushMatrix(const Vec3 &pos, const Vec3 &scale, const Vec3 &angles) = 0;
	virtual void PopMatrix() = 0;
	virtual void ReleaseArrayStates() = 0;

#if !defined(RAD_OPT_SHIP)
	virtual void DebugUploadVerts(
		const Vec3 *verts, 
		int numVerts
	) = 0;

	virtual int DebugUploadAutoTessTriIndices(int numVerts) = 0;

	virtual void DebugDrawLineLoop(int numVerts) = 0;
	virtual void DebugDrawIndexedTris(int numIndices) = 0;

#endif

	// Post Process FX
	virtual void BindPostFXTargets(bool chain) = 0;
	virtual void BindPostFXQuad() = 0;
	virtual void DrawPostFXQuad() = 0;

	virtual void BindOverlay() = 0;
	
	virtual void DrawOverlay() = 0;
	virtual void CommitStates() = 0;
	virtual void Finish() = 0;

	virtual bool Project(const Vec3 &p, Vec3 &out) = 0;
	virtual Vec3 Unproject(const Vec3 &p) = 0;

protected:

	virtual RAD_DECLARE_GET(wireframe, bool) = 0;
	virtual RAD_DECLARE_SET(wireframe, bool) = 0;
	virtual RAD_DECLARE_GET(numTris, int) = 0; 
	virtual RAD_DECLARE_SET(numTris, int) = 0;

private:

	RAD_DECLARE_GET(world, World*) { return m_world; }

	World *m_world;
};

///////////////////////////////////////////////////////////////////////////////

class RADENG_CLASS WorldDraw {
public:
	typedef boost::shared_ptr<WorldDraw> Ref;
	WorldDraw(World *w);
	~WorldDraw();

	struct Counters {
		Counters() {
			Clear();
		}

		void Clear();

		int drawnLeafs;
		int testedLeafs;
		int drawnNodes;
		int testedNodes;
		int numModels;
		int numTris;
		int numMaterials;
	};

	int LoadMaterials();
	void Init(const bsp_file::BSPFile::Ref &bsp);
	int Precache();
	void AddEffect(int id, const PostProcessEffect::Ref &fx);
	bool AddMaterial(int matId);
		
	ScreenOverlay::Ref CreateScreenOverlay(int matId);
	
	void Tick(float dt);
	void Draw(Counters *counters = 0);
	
	bool Project(const Vec3 &p, Vec3 &out) { 
		return m_rb->Project(p, out); 
	}

	Vec3 Unproject(const Vec3 &p) { 
		return m_rb->Unproject(p); 
	}

	const PostProcessEffect::Ref &PostFX(int idx) {
		return m_postFX[idx];
	}

	RAD_DECLARE_READONLY_PROPERTY(WorldDraw, rb, const RB_WorldDraw::Ref&);
	RAD_DECLARE_PROPERTY(WorldDraw, wireframe, bool, bool);
	
private:

	friend class ScreenOverlay;
	friend class World;
	friend struct details::MBatch;

	class MStaticWorldMeshBatch : public MBatchDraw
	{
	public:
		typedef boost::shared_ptr<MStaticWorldMeshBatch> Ref;
		typedef zone_vector<Ref, ZWorldT>::type RefVec;

		MStaticWorldMeshBatch(
			const r::Mesh::Ref &m,
			const BBox &bounds,
			int matId
		);

	protected:
		virtual void Bind(r::Shader *shader);
		virtual void CompileArrayStates(r::Shader &shader);
		virtual void FlushArrayStates(r::Shader *shader);
		virtual void Draw();

		virtual RAD_DECLARE_GET(entity, Entity*) {
			return 0;
		}

		virtual RAD_DECLARE_GET(visible, bool) { 
			return true; 
		}

		virtual RAD_DECLARE_GET(rgba, const Vec4&) { 
			return s_rgba; 
		}

		virtual RAD_DECLARE_GET(scale, const Vec3&) { 
			return s_scale; 
		}

		virtual RAD_DECLARE_GET(xform, bool) { 
			return true; 
		}

		virtual RAD_DECLARE_GET(bounds, const BBox&) {
			return m_bounds;
		}

	private:
		r::Mesh::Ref m_m;
		BBox m_bounds;

		static Vec4 s_rgba;
		static Vec3 s_scale;
	};

	RAD_DECLARE_GET(rb, const RB_WorldDraw::Ref&) { return m_rb; }
	RAD_DECLARE_GET(wireframe, bool) { return m_wireframe; }
	RAD_DECLARE_SET(wireframe, bool) { m_wireframe = value; }

	static void DeleteBatch(details::MBatch *batch);

	void AddStaticWorldMesh(const r::Mesh::Ref &m, const BBox &bounds, int matId);

	details::MBatchRef AllocateBatch();
	details::MatRef *AddMaterialRef(int id);
	details::MBatchRef AddViewBatch(ViewDef &view, int id);

	void FindViewArea(ViewDef &view);
	void SetupFrustumPlanes(ViewDef &view);
	void VisMarkAreas(ViewDef &view);

	void VisMarkArea(
		ViewDef &view, 
		int area, 
		const StackWindingStackVec &volume, 
		const BBox &volumeBounds
	);
		
	bool ClipBounds(const StackWindingStackVec &volume, const BBox &volumeBounds, const BBox &bounds);
	void DrawView();
	void DrawView(ViewDef &view);
	void DrawUI();
	void DrawOverlays();
	void DrawViewBatches(ViewDef &view, bool wireframe);
	void PostProcess();
	void DrawViewBatches(ViewDef &view, r::Material::Sort sort, bool wireframe);
	void DrawBatch(const details::MBatch &batch, bool wireframe);
	void DrawOverlay(ScreenOverlay &overlay);
	void AddScreenOverlay(ScreenOverlay &overlay);
	void RemoveScreenOverlay(ScreenOverlay &overlay);
	void LinkEntity(Entity *entity, const BBox &bounds);
	void UnlinkEntity(Entity *entity);
	void LinkEntity(Entity *entity, const BBox &bounds, int nodeNum);

#if !defined(RAD_OPT_SHIP)
	void DebugDrawPortals(ViewDef &view);
	void DebugDrawAreaportals(int area);
#endif

	struct LocalMaterial {
		pkg::Asset::Ref asset;
		r::Material *mat;
	};

	int LoadMaterial(const char *name, LocalMaterial &mat);
	
	int m_frame;
	int m_markFrame;
	bool m_init;
	bool m_wireframe;
	Counters m_counters;
	PostProcessEffect::Map m_postFX;
	MStaticWorldMeshBatch::RefVec m_worldModels;
	LocalMaterial m_debugWireframe;
	LocalMaterial m_debugPortal[2];
	ScreenOverlay::List m_overlays;
	RB_WorldDraw::Ref m_rb;
	details::MatRefMap m_refMats;
	ObjectPool<details::MBatch> m_batchPool;
	ObjectPool<details::MBatchDrawLink> m_linkPool;
	World *m_world;
};

} // world

#include <Runtime/PopPack.h>

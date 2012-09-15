/*! \file SolidBSP.h
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup map_builder
*/

#pragma once

#include "../../../Types.h"
#include "../../../COut.h"
#include "../../../Tools/SceneFile.h"
#include "../MapBuilderDebugUI.h"
#include "../../BSPFile.h"
#include "MapTypes.h"
#include "VecHash.h"
#include "PlaneHash.h"
#include <Runtime/Container/ZoneVector.h>
#include <Runtime/Container/ZoneSet.h>
#include <Runtime/Thread.h>
#include <QtCore/QVariant>
#include <vector>

#include <Runtime/PushPack.h>

namespace tools {
namespace solid_bsp {

///////////////////////////////////////////////////////////////////////////////

Vec3 RandomColor();

///////////////////////////////////////////////////////////////////////////////

#define SOLID_BSP_ICE() RAD_FAIL("SolidBSP Internal Compiler Error: file: "__FILE__" function: "__FUNCTION__" line: " RAD_STRINGIZE(__LINE__))

///////////////////////////////////////////////////////////////////////////////

//! Builds a solid sectorized BSP.
/*! This is pretty similiar in function to Quake style BSP, a solid skin mesh
	is created by the artist, although any arbitrary enclosed shape bounded
	by triangles will work as a brush primitive (non-convexity is supported).

	The primary goal here is to automatically divide the map into areas seperated
	by portals for rendering. Each area is then further divided into sectors, which
	are square MxMxM in dimension and contain triangle lists for any geometry contained.

	The BSP iteself is used for collision, but not really for rendering. At rendering time
	the BSP is used to locate the camera's area and the sector list is rendered with
	frustum clipping.
 */
class RADENG_CLASS BSPBuilder : protected thread::Thread {
public:
	typedef boost::shared_ptr<BSPBuilder> Ref;

	BSPBuilder();
	~BSPBuilder();

	bool SpawnCompile(
		SceneFile &map, 
		tools::UIProgress *ui = 0, 
		MapBuilderDebugUI *debugUI = 0, // if non-null debugging is enabled.
		std::ostream *cout = 0
	);

	RAD_DECLARE_READONLY_PROPERTY(BSPBuilder, bspFile, world::bsp_file::BSPFile::Ref);
	RAD_DECLARE_READONLY_PROPERTY(BSPBuilder, bspFileBuilder, world::bsp_file::BSPFileBuilder::Ref);
	RAD_DECLARE_READONLY_PROPERTY(BSPBuilder, result, int); // SR_ result

	void DebugDraw(float time, float dt);
	void OnDebugMenu(const QVariant &data);

	void WaitForCompletion() const;

private:

	RAD_DECLARE_GET(bspFile, world::bsp_file::BSPFile::Ref) { 
		return boost::static_pointer_cast<world::bsp_file::BSPFile>(m_bspFile); 
	}

	RAD_DECLARE_GET(bspFileBuilder, world::bsp_file::BSPFileBuilder::Ref) { 
		return m_bspFile; 
	}

	///////////////////////////////////////////////////////////////////////////////

	enum ContentsFlags {
		RAD_FLAG(kContentsFlag_Solid), // solid splits first, all other contents are detail splitters
		RAD_FLAG(kContentsFlag_Areaportal),
		RAD_FLAG(kContentsFlag_Detail), // never in the BSP.
		RAD_FLAG(kContentsFlag_Clip),
		RAD_FLAG(kContentsFlag_Fog),
		RAD_FLAG(kContentsFlag_Water),
		kContentsFlag_VisibleContents = kContentsFlag_Solid|kContentsFlag_Detail|kContentsFlag_Clip|kContentsFlag_Fog|kContentsFlag_Water|kContentsFlag_Areaportal,
		kContentsFlag_FirstVisibleContents = kContentsFlag_Solid,
		kContentsFlag_LastVisibleContents = kContentsFlag_Areaportal,
		kContentsFlag_Structural = kContentsFlag_Solid|kContentsFlag_Areaportal, // just used for classification
		kContentsFlag_SolidContents = kContentsFlag_Solid, // blocks portal flood
		kContentsFlag_BSPContents = 0xffffffff & ~kContentsFlag_Detail
	};

	enum SurfaceFlags {
		RAD_FLAG(kSurfaceFlag_NoDraw)
	};

	///////////////////////////////////////////////////////////////////////////////

	struct Poly {
		Poly() {
			original = 0;
			color = RandomColor();
			++s_num;
		}

		Poly(const Poly &p) {
			original = p.original;
			planenum = p.planenum;
			contents = p.contents;
			onNode = p.onNode;
			color = p.color;
			++s_num;
		}

		~Poly() {
			--s_num;
		}

		SceneFile::TriFace *original;
		WindingRef winding;
		Vec3 color;
		int planenum;
		int contents;
		bool onNode;
	
		static int s_num;
	};

	typedef boost::shared_ptr<Poly> PolyRef;
	typedef zone_vector<PolyRef, world::bsp_file::ZBSPBuilderT>::type PolyVec;

	///////////////////////////////////////////////////////////////////////////////

	struct TriModelFrag {
		TriModelFrag() {
			original = 0;
			bounds.Initialize();
			++s_num;
		}

		~TriModelFrag() {
			--s_num;
		}

		PolyVec    polys;
		BBox       bounds;
		SceneFile::TriModel *original;

		static int s_num;
	};

	typedef boost::shared_ptr<TriModelFrag> TriModelFragRef;
	typedef zone_vector<TriModelFragRef, world::bsp_file::ZBSPBuilderT>::type TriModelFragVec;

	///////////////////////////////////////////////////////////////////////////////

	struct WindingPlane {
		WindingPlane() {
			++s_num;
		}

		~WindingPlane() {
			--s_num;
		}

		Winding winding;
		int planenum;

		static int s_num;
	};

	typedef boost::shared_ptr<WindingPlane> WindingPlaneRef;
	typedef zone_vector<WindingPlaneRef, world::bsp_file::ZBSPBuilderT>::type WindingPlaneVec;

	enum {
		kPlaneNumLeaf = -1
	};

	struct Node;
	typedef boost::shared_ptr<Node> NodeRef;
	struct Portal;
	typedef boost::shared_ptr<Portal> PortalRef;

	typedef zone_vector<SceneFile::TriFace*, world::bsp_file::ZBSPBuilderT>::type TriFacePtrVec;

	///////////////////////////////////////////////////////////////////////////////

	struct Portal {
		Portal() {
			emitId = -1;
			onNode = 0;
			areas[0] = areas[1] = -1;
			nodes[0] = nodes[1] = 0;
			contents = 0;
			poly = 0;
			bounding = false;
			color = RandomColor();
			++s_num;
		}

		Portal (const Portal &p) {
			original = p.original;
			onNode = p.onNode;
			plane.planenum = p.plane.planenum;
			emitId = -1;
			areas[0] = areas[1] = -1;
			nodes[0] = nodes[1] = 0;
			contents = 0;
			poly = 0;
			bounding = p.bounding;
			color = p.color;
			++s_num;
		}

		~Portal() {
			--s_num;
		}

		WindingPlane plane;
		int contents; // bounded contents.
		int areas[2];
		int emitId;
		TriFacePtrVec original;
		Poly *poly;
		Node *onNode; // null means outside node
		PortalRef next[2];
		Node   *nodes[2];
		Vec3 color;
		bool bounding;

		static int s_num;
	};

	typedef math::ConvexPolygon<SceneFileD::TriVert, Plane> SectorWinding;

	///////////////////////////////////////////////////////////////////////////////

	struct AreaPoly {
		AreaPoly() {}
		AreaPoly(const AreaPoly &p) {
			tri = p.tri;
			planenum = p.planenum;
		}

		int planenum;
		Winding winding;
		SceneFile::TriFace *tri;
	};

	///////////////////////////////////////////////////////////////////////////////

	struct SectorPoly {
		SectorPoly() {}
		SectorPoly(const SectorPoly &p) {
			tri = p.tri;
		}

		SectorWinding  winding;
		SceneFile::TriFace  *tri;
	};

	typedef boost::shared_ptr<SectorPoly> SectorPolyRef;
	typedef zone_vector<SectorPolyRef, world::bsp_file::ZBSPBuilderT>::type SectorPolyVec;
	typedef zone_vector<int, world::bsp_file::ZBSPBuilderT>::type AreaNumVec;

	///////////////////////////////////////////////////////////////////////////////

	struct Sector {
		Sector() : emitId(-1) {}
		int emitId;
		BBox bounds;
		SectorPolyVec polys;
		AreaNumVec areas;
	};

	typedef boost::shared_ptr<Sector> SectorRef;
	typedef zone_vector<SectorRef, world::bsp_file::ZBSPBuilderT>::type SectorVec;
	typedef zone_vector<SceneFile::TriFace*, world::bsp_file::ZBSPBuilderT>::type TriFacePtrVec;

	///////////////////////////////////////////////////////////////////////////////

	struct Area {
		Area() : area(-1) {
		}

		int area;
	
		BBox bounds;
		TriFacePtrVec tris;
		SectorVec  sectors;
	};

	typedef boost::shared_ptr<Area> AreaRef;
	typedef zone_vector<AreaRef, world::bsp_file::ZBSPBuilderT>::type AreaVec;

	//typedef container::hash_set<int>::type PlaneNumHash;

	///////////////////////////////////////////////////////////////////////////////

	struct Node {
		Node() {
			area = 0;
			parent = 0;
			contents = 0;
			planenum = 0;
			numPolys = 0;
			occupied = 0;
			bounds.Initialize();
			++s_num;
		}

		~Node() {
			--s_num;
		}

		TriModelFragVec models;
		BBox bounds;
		Node *parent;
		PortalRef portals;
		NodeRef children[2];
		int numPolys;
		int contents;
		int planenum;
		int occupied;
		Area *area;
		static int s_num;
	};

	///////////////////////////////////////////////////////////////////////////////

	struct EmitTriModel
	{
		typedef boost::shared_ptr<EmitTriModel> Ref;
		typedef SceneFileD::NormalTriVert Vert;
		typedef SceneFileD::NormalTriVertVec VertVec;
		typedef zone_map<Vert, int, world::bsp_file::ZBSPBuilderT>::type VertMap;
		typedef zone_vector<Ref, world::bsp_file::ZBSPBuilderT>::type Vec;
		typedef zone_vector<int, world::bsp_file::ZBSPBuilderT>::type Indices;
		VertVec verts;
		Indices indices;
		VertMap vmap;
		int mat;
		int numChannels;

		void AddVertex(const Vert &vert);
		void Clear() {
			verts.clear();
			indices.clear();
			vmap.clear();
		}
	};

	///////////////////////////////////////////////////////////////////////////////

	std::ostream &COut() {
		if (m_cout)
			return *m_cout;
		return ::COut(C_Debug);
	}

	void Log(const char *fmt, ...);

	RAD_DECLARE_GET(result, int);

	SceneFile *m_map;
	SceneFile::Entity::Ref m_leakEnt;
	PlaneHash m_planes;
	world::bsp_file::BSPFileBuilder::Ref m_bspFile;
	NodeRef m_root;
	Node m_outside;
	Vec3Vec m_leakpts;
	AreaVec m_areas;
	std::ostream *m_cout;
	tools::UIProgress *m_ui;
	MapBuilderDebugUI *m_debugUI;
	int m_numStructural;
	int m_numDetail;
	int m_numNodes;
	int m_numLeafs;
	int m_numPortals;
	int m_progress;
	int m_numOutsideNodes;
	int m_numOutsideTris;
	int m_numOutsideModels;
	int m_numInsideTris;
	int m_numInsideNodes;
	int m_numInsideModels;
	int m_numSectors;
	int m_numSharedSectors;
	int m_work;
	int m_result;
	bool m_flood;

	void Build();
	bool LoadMaterials();
	void CreateRootNode();

	void DisplayTree(
		Node *node, 
		bool outside=true,
		const Node *target = 0
	);

	void DisplayPortals(const Node *node, Portal *a=0, Portal *b=0, bool leak=false, int contents=0);

	void LeafNode(Node *node);
	void Split(Node *node, int boxAxis);
	void Split(const TriModelFragRef &model, const Plane &p, int planenum, TriModelFragRef &front, TriModelFragRef &back);
	bool MarkNodePolys(int planenum, const TriModelFragRef &m);
	void Portalize();
	void SplitNodePortals(Node *node);
	void MakeNodePortal(Node *node);
	void MakeTreePortals(Node *node);
	void AddPortalToNodes(const PortalRef &p, Node *front, Node *back);
	void RemovePortalFromNode(const PortalRef &p, Node *node);
	void FindPortalNodeFaces(Node *node);
	Node *LeafForPoint(const Vec3 &pos, Node *node=0);
	bool FloodFill();
	void PortalFlood(Node *leaf, int depth);
	void MarkLeakTrail();
	void DumpLeakFile();
	void FillOutside();
	void MarkOccupiedNodeFaces(Node *node);
	void AreaFlood();
	void AreaFlood(Node *leaf, Area *area);
	void FindAreas(Node *node);
	void BuildSectors();
	void DecomposeAreaModel(const SceneFile::TriModel &model);
	void DecomposeAreaPoly(Node *node, AreaPoly *poly);
	void BuildAreaSectors();
	void BuildAreaSectors(Area &area);
	void SubdivideSector(Sector *sector);
	void SplitSector(const Plane &p, Sector &sector, Sector &front, Sector &back);
	void BuildSharedSectors();
	int FindSplitPlane(Node *node, int &boxAxis);
	int BoxPlaneNum(Node *node, int &boxAxis);

	// Emit BSP
	void EmitBSPFile();
	void EmitBSPMaterials();
	void EmitBSPEntities();
	void EmitBSPEntity(const SceneFile::Entity::Ref &entity);
	void EmitBSPAreas();
	void EmitBSPAreaportals(Node *leaf, int areaNum, world::bsp_file::BSPArea &area);
	void EmitBSPSector(Sector &s);
	void EmitBSPModel(const EmitTriModel &model);
	S32 EmitBSPNodes(const Node *node, S32 parent);
	void EmitBSPClipSurfaces(const Node *node, world::bsp_file::BSPLeaf *leaf);
	void EmitBSPClipBevels(world::bsp_file::BSPLeaf *leaf);
	void EmitBSPPlanes();
	U32 FindBSPMaterial(const char *name);
	int EmitBSPCinematics();

	void ResetProgress();
	void EmitProgress();
	void SetResult(int result);

	virtual int ThreadProc();

	static void BBoxPlanes(const BBox &bounds, Plane *planes);
	static void BBoxWindings(const BBox &bounds, WindingVec &out);
	static void WindingBounds(const Winding &winding, BBox &bounds);
	static Vec3 WindingCenter(const Winding &winding);
	static int ContentsForString(const String &s);
	static int SurfaceForString(const String &s);

	// Converts to BSP precision types.
	static Vec2 ToBSPType(const SceneFile::Vec2 &vec);
	static Vec3 ToBSPType(const SceneFile::Vec3 &vec);
	static SceneFileD::TriVert ToBSPType(const SceneFile::TriVert &vec);
	static BBox ToBSPType(const SceneFile::BBox &bbox);
	static Plane ToBSPType(const SceneFile::Plane &plane);
	static SceneFile::Plane FromBSPType(const Plane &plane);
};

} // solid_bsp
} // tools

#include <Runtime/PopPack.h>
// WorldBSP.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "World.h"
#include "../MathUtils.h"

namespace world {

void World::SetAreaportalState(int areaportalNum, bool open, bool relinkOccupants) {
	RAD_ASSERT(areaportalNum < (int)m_bsp->numAreaportals.get());
	Areaportal &areaportal = m_areaportals[areaportalNum];
	areaportal.open = open;

	if (!relinkOccupants)
		return;

	EntityPtrSet occupants;

	for (dBSPLeaf::Vec::const_iterator it = m_leafs.begin(); it != m_leafs.end(); ++it) {
		const dBSPLeaf &leaf = *it;

		for (EntityPtrSet::iterator it2 = leaf.occupants.begin(); it2 != leaf.occupants.end(); ++it2) {
			Entity *entity = *it2;
			RAD_ASSERT(entity);

			if (leaf.area != entity->m_leaf->area) // foreign occupant?
				occupants.insert(entity);
		}
	}

	for (EntityPtrSet::const_iterator it = occupants.begin(); it != occupants.end(); ++it) {
		Entity *entity = *it;
		entity->Link();
	}
}

Entity::Vec World::BBoxTouching(const BBox &bbox, int stypes) const
{
	Entity::Vec touching;
	EntityBits touched;

	if (m_nodes.empty()) {
		BBoxTouching(bbox, stypes, -1, touching, touched);
	} else {
		BBoxTouching(bbox, stypes, 0, touching, touched);
	}

	return touching;
}

void World::BBoxTouching(
	const BBox &bbox,
	int stypes,
	int nodeNum,
	Entity::Vec &out,
	EntityBits &bits
) const {
	if (nodeNum < 0) {
		nodeNum = -(nodeNum + 1);
		RAD_ASSERT(nodeNum < (int)m_leafs.size());
		const dBSPLeaf &leaf = m_leafs[nodeNum];

		for (EntityPtrSet::const_iterator it = leaf.occupants.begin(); it != leaf.occupants.end(); ++it) {
			Entity *entity = *it;
			if (!(entity->ps->stype&stypes))
				continue;
			if (bits[entity->m_id])
				continue;
		
			switch (entity->ps->stype) {
			case ST_BBox: {
					BBox b(entity->ps->bbox);
					b.Translate(entity->ps->worldPos);
					bits.set(entity->m_id);
					if (bbox.Touches(b))
						out.push_back(entity->shared_from_this());
				}
				break;
			case ST_Brush:
				break;
			}
		}
	}
}

void World::LinkEntity(Entity *entity, const BBox &bounds) {
	UnlinkEntity(entity);

	entity->m_leaf = LeafForPoint(entity->ps->worldPos);
	RAD_ASSERT(entity->m_leaf);

	AreaBits tested;
	AreaBits visible;
	WindingVec bbox;

	BoundWindings(bounds, bbox);

	LinkEntityParms constArgs(entity, bounds, bbox, tested, visible);

	entity->m_bspLeafs.reserve(8);
	if (m_nodes.empty()) {
		LinkEntity(constArgs, -1);
	} else {
		LinkEntity(constArgs, 0);
	}

	m_draw->LinkEntity(entity, bounds);
}

void World::UnlinkEntity(Entity *entity) {
	m_draw->UnlinkEntity(entity);

	for (dBSPLeaf::PtrVec::const_iterator it = entity->m_bspLeafs.begin(); it != entity->m_bspLeafs.end(); ++it) {
		dBSPLeaf *leaf = *it;
		leaf->occupants.erase(entity);
	}

	entity->m_bspLeafs.clear();
	entity->m_leaf = 0;
}

void World::LinkEntity(const LinkEntityParms &constArgs, int nodeNum) {
	if (nodeNum < 0) {
		nodeNum = -(nodeNum + 1);
		RAD_ASSERT(nodeNum < (int)m_leafs.size());
		dBSPLeaf &leaf = m_leafs[nodeNum];

		bool canSee = true;

		if (leaf.area == constArgs.entity->m_leaf->area) {
			constArgs.visible.set(leaf.area);
		} else {
			if (constArgs.tested.test(leaf.area)) {
				canSee = constArgs.visible.test(leaf.area);
			} else {
				canSee = OccupantVolumeCanSeeArea(
					constArgs.entity->ps->worldPos,
					constArgs.bbox,
					0,
					constArgs.entity->m_leaf->area,
					leaf.area,
					constArgs.visible
				);
			}
		}

		constArgs.tested.set(leaf.area);

		if (canSee) {
			leaf.occupants.insert(constArgs.entity);
			constArgs.entity->m_bspLeafs.push_back(&leaf);
		}

		return;
	}

	RAD_ASSERT(nodeNum < (int)m_nodes.size());
	const dBSPNode &node = m_nodes[nodeNum];
	RAD_ASSERT(node.planenum < (int)m_planes.size());
	const Plane &p = m_planes[node.planenum];

	Plane::SideType side = p.Side(constArgs.bounds, 0.0f);
	
	if ((side == Plane::Cross) || (side == Plane::Front))
		LinkEntity(constArgs, node.children[0]);
	if ((side == Plane::Cross) || (side == Plane::Back))
		LinkEntity(constArgs, node.children[1]);
}

bool World::OccupantVolumeCanSeeArea(
	const Vec3 &pos,
	const WindingVec &volume,
	WindingVec *clipped,
	int fromArea,
	int toArea,
	AreaBits &visible
) {
	if (fromArea == toArea) {
		if (clipped)
			*clipped = volume;
		return true;
	}

	visible.set(fromArea);

	AreaBits stack;
	OccupantVolumeClipParms constArgs(pos, clipped, toArea, visible, stack);
	return OccupantVolumeCanSeeArea(constArgs, volume, fromArea);
}

bool World::OccupantVolumeCanSeeArea(
	const OccupantVolumeClipParms &constArgs,
	const WindingVec &volume,
	int fromArea
) {
	RAD_ASSERT(parms.fromArea < (int)m_bsp->numAreas.get());
	RAD_ASSERT(parms.toArea < (int)m_bsp->numAreas.get());

	constArgs.stack.set(fromArea);

	const world::bsp_file::BSPArea *area = m_bsp->Areas() + fromArea;

	bool canSee = false;

	// follow all portals into adjecent areas.
	for (U32 i = 0; i < area->numPortals; ++i) {

		U32 areaportalNum = *(m_bsp->AreaportalIndices() + area->firstPortal + i);
		RAD_ASSERT(areaportalNum < m_areaportals.size());
		const Areaportal &areaportal = m_areaportals[areaportalNum];

		if (!areaportal.open)
			continue;

		int side = areaportal.areas[1] == fromArea;
		RAD_ASSERT(areaportal.areas[side] == fromArea);
		int otherArea = areaportal.areas[!side];
		
		if (constArgs.stack.test(otherArea))
			continue; // came from here.

		// clip portal by volume.
		Winding winding(areaportal.winding);

		if (!ChopWindingToVolume(volume, winding))
			continue; // portal clipped away.

		// Generate planes that bound the portal.

		PlaneVec boundingPlanes;
		MakeBoundingPlanes(constArgs.pos, winding, boundingPlanes);

		// Constrain volume to portal bounding planes.
		WindingVec portalVolume(volume);
		if (!ChopVolume(portalVolume, boundingPlanes))
			continue; // volume clipped away.

		// Bound by portal.
		int planenum = areaportal.planenum ^ side; // put fromArea on front (we want to clip away volume in our area).
		const Plane &portalPlane = m_planes[planenum];
		if (!ChopVolume(portalVolume, portalPlane))
			continue;

		constArgs.visible.set(otherArea);

		canSee = otherArea == constArgs.toArea;

		if (canSee) {
			if (constArgs.clipped)
				*constArgs.clipped = portalVolume;
		} else {
			canSee = OccupantVolumeCanSeeArea(constArgs, portalVolume, otherArea);
		}
		
		if (canSee)
			break;
	}

	constArgs.stack.reset(fromArea);
	return canSee;
}

dBSPLeaf *World::LeafForPoint(const Vec3 &pos) {
	if (m_nodes.empty())
		return &m_leafs[0];
	return LeafForPoint(pos, 0);
}

dBSPLeaf *World::LeafForPoint(const Vec3 &pos, int nodeNum) {
	if (nodeNum < 0) {
		nodeNum = -(nodeNum + 1);
		RAD_ASSERT(nodeNum < (int)m_leafs.size());
		dBSPLeaf &leaf = m_leafs[nodeNum];
		return &leaf;
	}

	RAD_ASSERT(nodeNum < (int)m_nodes.size());
	const dBSPNode &node = m_nodes[nodeNum];
	RAD_ASSERT(node.planenum < (int)m_planes.size());
	const Plane &p = m_planes[node.planenum];

	Plane::SideType side = p.Side(pos, 0.f);
	if (side == Plane::Back)
		return LeafForPoint(pos, node.children[1]);
	return LeafForPoint(pos, node.children[0]);
}

void World::BoundWindings(const BBox &bounds, WindingVec &windings) {
	
	Plane planes[6];
	int planeNum = 0;

	for (int i = 0; i < 2; ++i) {
		for (int k = 0; k < 3; ++k) {

			Vec3 normal(Vec3::Zero);
			normal[k] = (i==0) ? -1.f : 1.f;
			float dist = (i==0) ? (-bounds.Mins()[k]) : (bounds.Maxs()[k]);

			planes[planeNum] = Plane(normal, dist);
			++planeNum;
		}
	}

	MakeVolume(planes, 6, windings);
}

bool World::ChopWindingToVolume(const WindingVec &volume, Winding &out) {

	Winding f;

	for (WindingVec::const_iterator it = volume.begin(); it != volume.end(); ++it) {
		const Winding &w = *it;

		f.Clear();
		out.Chop(w.Plane(), Plane::Back, f, 0.f);
		out.Swap(f);

		if (out.Empty())
			break;
	}

	return !out.Empty();
}

bool World::ChopVolume(WindingVec &volume, const Plane &p) {

	WindingVec src;
	src.swap(volume);

	volume.reserve(src.size());

	Winding f;
	for (WindingVec::const_iterator it = src.begin(); it != src.end(); ++it) {
		const Winding &w = *it;

		f.Clear();
		w.Chop(p, Plane::Back, f, 0.f);
		if (!f.Empty())
			volume.push_back(f);
	}

	Winding pw(p, 32767.f);

	for (WindingVec::const_iterator it = volume.begin(); it != volume.end(); ++it) {
		const Winding &w = *it;

		f.Clear();
		pw.Chop(w.Plane(), Plane::Back, f, 0.f);
		pw.Swap(f);
		if (pw.Empty())
			break;
	}

	if (!pw.Empty())
		volume.push_back(pw);

	return !volume.empty();
}

bool World::ChopVolume(WindingVec &volume, const PlaneVec &planes) {

	for (PlaneVec::const_iterator it = planes.begin(); it != planes.end(); ++it) {
		const Plane &p = *it;
		if (!ChopVolume(volume, p))
			return false;
	}

	return !volume.empty();
}

bool World::IntersectVolumes(const WindingVec &a, WindingVec &out) {

	for (WindingVec::const_iterator it = a.begin(); it != a.end(); ++it) {
		const Winding &w = *it;
		if (!ChopVolume(out, w.Plane()))
			return false;
	}

	return !out.empty();
}

void World::MakeVolume(const Plane *planes, int num, WindingVec &volume) {
	volume.clear();
	volume.reserve(num);

	Winding f;

	for (int i = 0; i < num; ++i) {
		Winding w(planes[i], 32767.f);
		for (int k = 0; k < num; ++k) {
			if (k == i)
				continue;
			
			f.Clear();
			w.Chop(planes[k], Plane::Back, f, 0.f);
			w.Swap(f);
			if (w.Empty())
				break;
		}

		if (!w.Empty())
			volume.push_back(w);
	}
}

void World::MakeBoundingPlanes(const Vec3 &pos, const Winding &portal, PlaneVec &planes) {

	RAD_ASSERT(portal.Vertices().size() >= 3);

	planes.clear();
	planes.reserve(portal.Vertices().size());

	for (Winding::VertexListType::const_iterator it = portal.Vertices().begin(); it != portal.Vertices().end(); ++it) {
		Winding::VertexListType::const_iterator it2 = it;
		++it2;
		if (it2 == portal.Vertices().end())
			it2 = portal.Vertices().begin();

		Winding::VertexListType::const_iterator it3 = it2;
		++it3;
		if (it3 == portal.Vertices().end())
			it3 = portal.Vertices().begin();

		const Vec3 &a = *it;
		const Vec3 &b = *it2;
		const Vec3 &c = *it3;

		Plane p(a, b, pos);
		if (p.Side(c, 0.f) == Plane::Front) {
			p = -p;
		}

		planes.push_back(p);
	}
}


} // world
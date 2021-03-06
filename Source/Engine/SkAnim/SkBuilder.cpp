// SkBuilder.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH

#if defined(RAD_OPT_TOOLS)

#include "SkBuilder.h"
#include "../COut.h"
#include "../Packages/PackagesDef.h"
#include "../Tools/Progress.h"
#include <Runtime/Container/ZoneVector.h>
#include <Runtime/Container/ZoneMap.h>
#include <Runtime/Container/ZoneSet.h>
#include <Runtime/Stream.h>
#include <Runtime/Stream/MemoryStream.h>
#include <Runtime/Endian/EndianStream.h>
#include <Runtime/Base/SIMD.h>
#include <Runtime/Thread/Thread.h>
#include <limits>
#undef min
#undef max

BOOST_STATIC_ASSERT((int)ska::kMaxUVChannels <= (int)tools::SceneFile::kMaxUVChannels);

namespace tools {

namespace {

// [0] = compression floor
// [1] = quat zero_children basis
// [2] = compression basis

// compression = max(compression_floor, compression_basis / (numchildren+1))

static const float kQuatCompressionBasis[3] = {0.01f, 2.0f, 1.0f};

// [0] = compression floor
// [1] = compression basis

// compression = max(compression_floor, compression_basis / (numchildren+1))

static const float kScaleCompressionBasis[2] = {0.1f*0.1f, 0.5f*0.5f}; // units squared
static const float kTranslationCompressionBasis[2] = {0.25f*0.25f, 1.f*1.f};

typedef zone_vector<int, ZToolsT>::type IntVec;
typedef zone_set<int, ZToolsT>::type IntSet;
struct BoneMap {
	typedef zone_vector<BoneMap, ZToolsT>::type Vec;
	int idx;
	IntVec children;
};

struct Tables;
struct TagTable;
struct AnimTables;

class SkaBuilder {
public:
	typedef SkaBuilder Ref;

	SkaBuilder();
	~SkaBuilder();

	bool Compile(
		const char *name, 
		const SceneFileVec &map, 
		int trimodel, 
		const SkaCompressionMap *compression,
		SkaData &sk
	);

	bool Compile(
		const char *name, 
		const SceneFile &map, 
		int trimodel, 
		const SkaCompressionMap *compression,
		SkaData &sk
	);

private:

	struct BoneTM {
		ska::BoneTM tm;
		String tag;
	};

	typedef zone_vector<BoneTM, ZToolsT>::type BoneTMVec;

	struct BoneDef {
		typedef zone_vector<BoneDef, ZToolsT>::type Vec;

		String name;
		int remap;
		int childDepth; // deepest child branch
		S16 parent;
		Mat4 invWorld;
	};

	struct Compression {
		typedef zone_vector<Compression, ZToolsT>::type Vec;
		float quatCompression;
		float scaleCompression;
		float translateCompression;
	};

	typedef zone_vector<BoneTMVec, ZToolsT>::type FrameVec;

	struct Anim {
		typedef zone_vector<Anim, ZToolsT>::type Vec;
		String name;
		float fps;
		float distance;
		bool removeMotion;
		FrameVec frames;
		Compression::Vec boneCompression;
	};

	class CompileAnimThread : public thread::Thread {
	public:
		typedef boost::shared_ptr<CompileAnimThread> Ref;
		typedef zone_vector<Ref, ZToolsT>::type Vec;

		CompileAnimThread(
			SkaBuilder &builder,
			const Anim &anim,
			AnimTables &tables
		) : m_builder(builder), m_anim(anim), m_tables(tables), m_done(false) {}

	protected:

		virtual int ThreadProc();

	private:

		friend class SkaBuilder;

		volatile bool m_done;
		SkaBuilder &m_builder;
		const Anim &m_anim;
		AnimTables &m_tables;
	};

	friend class CompileAnimThread;

	void CompileAnimation(
		const Anim &anim,
		AnimTables &tables
	);

	void WaitForThreads(int numFree);

	bool Compile(stream::IOutputBuffer &ob);
	
	void SetBones(const BoneDef::Vec &bones);

	void BeginAnim(
		const char *name,
		float fps,
		int numFrames,
		bool removeMotion,
		const SkaCompressionMap *compression
	);

	void AddFrame(const BoneTM *bones);

	void EndAnim(int motionBone);

	static bool EmitBones(
		const char *name, 
		int idx, 
		int parent, 
		BoneMap::Vec &map, 
		BoneDef::Vec &bones, 
		const SceneFile::BoneVec &skel
	);

	enum {
		kMaxThreads = 8
	};

	BoneDef::Vec m_bones;
	Anim::Vec m_anims;
	CompileAnimThread::Vec m_threads;
	volatile bool m_error;
};

///////////////////////////////////////////////////////////////////////////////

SkaBuilder::SkaBuilder() : m_error(false) {
}

SkaBuilder::~SkaBuilder() {
}

bool SkaBuilder::EmitBones(const char *name, int idx, int parent, BoneMap::Vec &map, BoneDef::Vec &bones, const SceneFile::BoneVec &skel) {
	BoneMap &bm = map[idx];
	const SceneFile::Bone &mb = skel[idx];

	if (bm.idx > -1) {
		COut(C_Error) << "EmitBones(\"" << name << "\"): \"" << skel[idx].name << "\" has multiple parent bones!" << std::endl;
		return false;
	}

	bm.idx = (int)bones.size();

	BoneDef bone;
	bone.name = mb.name;
	bone.parent = (S16)parent;
	bone.remap = idx;
	bone.childDepth = 0;
	bone.invWorld = mb.world.Inverted();
	bones.push_back(bone);

	// propogate depth.
	int depth = 1;

	while (parent > -1) {
		if (bones[parent].childDepth < depth) {
			bones[parent].childDepth = depth;
			++depth;
			parent = bones[parent].parent;
		} else {
			break;
		}
	}

	for (IntVec::const_iterator it = bm.children.begin(); it != bm.children.end(); ++it) {
		if (!EmitBones(name, *it, bm.idx, map, bones, skel))
			return false;
	}

	return true;
}

bool SkaBuilder::Compile(
	const char *name, 
	const SceneFileVec &maps, 
	int trimodel,
	const SkaCompressionMap *compression,
	SkaData &sk
) {
	RAD_ASSERT(maps.size() == 1 || trimodel == 0);

	SceneFile::Entity::Ref e = maps[0]->worldspawn;
	if (e->models[trimodel]->skel < 0)
		return false;

	const SceneFile::Skel &skel = *e->skels[e->models[trimodel]->skel];

	// apply constraint: bones[boneidx].parent < boneidx
	// since we do not walk the bone heirarchy in max from
	// root->children (and instead use skinops) the bone
	// array is not guaranteed to be sorted in this fashion.
	BoneMap::Vec bmap;

	bmap.resize(skel.bones.size());
	int root = -1;

	for (int i = 0; i < (int)skel.bones.size(); ++i) {
		const SceneFile::Bone &b = skel.bones[i];
		bmap[i].idx = -1;

		if (b.parent >= 0) {
			bmap[b.parent].children.push_back(i);
		} else {
			if (root != -1) {
				COut(C_Error) << "BuildAnimData(\"" << name << "\"): has multiple root bones!" << std::endl;
				return false;
			}
			root = i;
		}
	}

	if (root == -1) {
		COut(C_Error) << "BuildAnimData(\"" << name << "\"): no root bone found!" << std::endl;
		return false;
	}

	BoneDef::Vec bones;
	if (!EmitBones(name, root, -1, bmap, bones, skel.bones))
		return false;

	for (BoneDef::Vec::iterator it = bones.begin(); it != bones.end(); ++it) {
		BoneDef &bone = *it;
		if (bone.parent >= 0)
			bone.invWorld = bones[bone.parent].invWorld * bone.invWorld;
	}

	// bones is now a vector of bones sorted in parent->children order.
	root = 0;
	SetBones(bones);

	std::vector<bool> badList;
	badList.resize(maps.size());
	badList[0] = false;

	// ensure identical skels
	for (size_t i = 1; i < maps.size(); ++i) {
		badList[i] = true;

		SceneFile::Entity::Ref e = maps[i]->worldspawn;
		if (e->models[0]->skel < 0) {
			COut(C_Error) << "BuildAnimData(\"" << name << "\"): anim file " << i << " is missing skel data (skipping)!" << std::endl;
			continue;
		}

		const SceneFile::Skel &otherSkel = *e->skels[e->models[0]->skel];
		if (otherSkel.bones.size() != skel.bones.size()) {
			COut(C_Error) << "BuildAnimData(\"" << name << "\"): anim file " << i << " has mismatched skel (code 1, skipping)!" << std::endl;
			continue;
		}

		size_t k;
		for (k = 0; k < skel.bones.size(); ++k) {
			if (skel.bones[k].name != otherSkel.bones[k].name ||
				skel.bones[k].parent != otherSkel.bones[k].parent) {
				COut(C_Error) << "BuildAnimData(\"" << name << "\"): anim file " << i << " has mismatched skel (code 2, skipping)!" << std::endl;
				break;
			}
		}

		if (k != skel.bones.size())
			continue;

		badList[i] = false;
	}
	
	// build animation data
	for (size_t i = 0; i < maps.size(); ++i) {
		if (badList[i])
			continue;

		const SceneFile::TriModel::Ref &m = maps[i]->worldspawn->models[trimodel];

		for (SceneFile::AnimMap::const_iterator it = m->anims.begin(); it != m->anims.end(); ++it) {
			const SceneFile::Anim &anim = *(it->second.get());
			if (anim.boneFrames.empty())
				continue;

			BeginAnim(
				anim.name.c_str,
				(float)anim.frameRate,
				(int)anim.boneFrames.size(),
				false,
				compression
			);

			BoneTMVec tms;
			tms.resize(skel.bones.size());

			for (SceneFile::BoneFrames::const_iterator it = anim.boneFrames.begin(); it != anim.boneFrames.end(); ++it) {
				const SceneFile::BonePoseVec &frame = *it;

				// TODO: dynamic remap bones by name, would be pretty 
				// useful to decouple animation from a skeleton.

				if (frame.size() != skel.bones.size()) {
					COut(C_Error) << "BuildAnimData(\"" << name << "\"): animation \"" << anim.name << "\" frame " << 
						(it-anim.boneFrames.begin()) << " has mismatched bone count." << std::endl;
					return false;
				}

				for (SceneFile::BonePoseVec::const_iterator it = frame.begin(); it != frame.end(); ++it) {
					const SceneFile::BonePose &mtm = *it;
					BoneTM &tm = tms[bmap[it-frame.begin()].idx];

					tm.tm.r = mtm.m.r;
					tm.tm.s = mtm.m.s;
					tm.tm.t = mtm.m.t;
					tm.tag = mtm.tag;
				}

				AddFrame(&tms[0]);
			}

			EndAnim(root);
		}
	}

	// compile.
	stream::DynamicMemOutputBuffer ob(ska::ZSka);
	RAD_VERIFY(Compile(ob));
	sk.skaData = ob.OutputBuffer().Ptr();
	sk.skaSize = (AddrSize)ob.OutPos();
	ob.OutputBuffer().Set(0, 0); // so ob doesn't free the buffer.

	// trim
	sk.skaData = zone_realloc(ska::ZSka, sk.skaData, sk.skaSize);
	RAD_VERIFY(sk.dska.Parse(sk.skaData, sk.skaSize) == pkg::SR_Success);

	return true;
}

bool SkaBuilder::Compile(
	const char *name, 
	const SceneFile &map, 
	int trimodel, 
	const SkaCompressionMap *compression,
	SkaData &sk
) {
	SceneFile::Entity::Ref e = map.worldspawn;
	if (e->models[trimodel]->skel < 0)
		return false;

	const SceneFile::Skel &skel = *e->skels[e->models[trimodel]->skel];

	// apply constraint: bones[boneidx].parent < boneidx
	// since we do not walk the bone heirarchy in max from
	// root->children (and instead use skinops) the bone
	// array is not guaranteed to be sorted in this fashion.
	BoneMap::Vec bmap;

	bmap.resize(skel.bones.size());
	int root = -1;

	for (int i = 0; i < (int)skel.bones.size(); ++i) {
		const SceneFile::Bone &b = skel.bones[i];
		bmap[i].idx = -1;

		if (b.parent >= 0) {
			bmap[b.parent].children.push_back(i);
		} else {
			if (root != -1) {
				COut(C_Error) << "BuildAnimData(\"" << name << "\"): has multiple root bones!" << std::endl;
				return false;
			}
			root = i;
		}
	}

	if (root == -1) {
		COut(C_Error) << "BuildAnimData(\"" << name << "\"): no root bone found!" << std::endl;
		return false;
	}

	BoneDef::Vec bones;
	if (!EmitBones(name, root, -1, bmap, bones, skel.bones))
		return false;

	for (BoneDef::Vec::iterator it = bones.begin(); it != bones.end(); ++it) {
		BoneDef &bone = *it;
		if (bone.parent >= 0)
			bone.invWorld = bones[bone.parent].invWorld * bone.invWorld;
	}

	// bones is now a vector of bones sorted in parent->children order.
	SetBones(bones);


	const SceneFile::TriModel::Ref &m = map.worldspawn->models[trimodel];

	for (SceneFile::AnimMap::const_iterator it = m->anims.begin(); it != m->anims.end(); ++it) {
		const SceneFile::Anim &anim = *(it->second.get());
		if (anim.boneFrames.empty())
			continue;

		// do we already have this animation?
		for (Anim::Vec::const_iterator it = m_anims.begin(); it != m_anims.end(); ++it) {
			if ((*it).name.Comparei(anim.name.c_str) == 0) {
				COut(C_Error) << "ERROR: animation '" << anim.name << " has duplicates. One or more 3DX files have this animation exported." << std::endl;
				return false;
			}
		}

		BeginAnim(
			anim.name.c_str,
			(float)anim.frameRate,
			(int)anim.boneFrames.size(),
			false,
			compression
		);

		BoneTMVec tms;
		tms.resize(skel.bones.size());

		for (SceneFile::BoneFrames::const_iterator it = anim.boneFrames.begin(); it != anim.boneFrames.end(); ++it) {
			const SceneFile::BonePoseVec &frame = *it;

			// TODO: dynamic remap bones by name, would be pretty 
			// useful to decouple animation from a skeleton.

			if (frame.size() != skel.bones.size()) {
				COut(C_Error) << "BuildAnimData(\"" << name << "\"): animation \"" << anim.name << "\" frame " << 
					(it-anim.boneFrames.begin()) << " has mismatched bone count." << std::endl;
				return false;
			}

			for (SceneFile::BonePoseVec::const_iterator it = frame.begin(); it != frame.end(); ++it) {
				const SceneFile::BonePose &mtm = *it;
				BoneTM &tm = tms[bmap[it-frame.begin()].idx];

				tm.tm.r = mtm.m.r;
				tm.tm.s = mtm.m.s;
				tm.tm.t = mtm.m.t;
				tm.tag = mtm.tag;
			}

			AddFrame(&tms[0]);
		}

		EndAnim(root);
	}

	// compile.
	stream::DynamicMemOutputBuffer ob(ska::ZSka);
	RAD_VERIFY(Compile(ob));
	sk.skaData = ob.OutputBuffer().Ptr();
	sk.skaSize = (AddrSize)ob.OutPos();
	ob.OutputBuffer().Set(0, 0); // so ob doesn't free the buffer.

	// trim
	sk.skaData = zone_realloc(ska::ZSka, sk.skaData, sk.skaSize);
	RAD_VERIFY(sk.dska.Parse(sk.skaData, sk.skaSize) == pkg::SR_Success);

	return true;
}

typedef zone_vector<float, ZToolsT>::type FloatVec;

struct TagTable {
	StringVec strings;

	TagTable() {
		strings.reserve(64);
	}

	int AddString(const String &str) {
		if (str.empty)
			return -1;

		for (StringVec::const_iterator it = strings.begin(); it != strings.end(); ++it) {
			if (str == *it)
				return (int)(it-strings.begin());
		}

		int ofs = (int)strings.size();
		if (ofs > 254) {
			COut(C_Error) << "SkaBuilder::TagTable: string table exceeds 255 elements!" << std::endl;
			return -1;
		}
		strings.push_back(str);
		return ofs;
	}
};

struct Tables {
	FloatVec rTable;
	FloatVec sTable;
	FloatVec tTable;
	float tAbsMax;
	float sAbsMax;

	Tables() : tAbsMax(-1.0f), sAbsMax(-1.0f) {
	}

	// building these tables is all slow and linear, need to figure out how to speed this up.

	static S16 QuantFloat(float f, float absMax) {
		if (absMax <= 0.f)
			return 0;
		const int shortMax = std::numeric_limits<S16>::max();
		const int shortMin = std::numeric_limits<S16>::min();
		int val = (int)floor(((f*shortMax)/absMax)+0.5);
		if (val > shortMax)
			return (S16)shortMax;
		if (val < shortMin)
			return (S16)shortMin;
		return (S16)val;
	}

	bool AddRotate(const Quat &r, float eqDist, int &out) {
		
		int best = -1;
		float bestError = eqDist*3.f;

		for (int i = 0; i+4 <= (int)rTable.size(); i += 4) {
			if (QuatEq(*((const Quat*)&rTable[i]), r, eqDist, bestError)) {
				best = i;
			}
		}

		if (best != -1) {
			out = best / 4;
			return true;
		}

		if ((rTable.size()/4) == ska::kEncMask)
			return false;
		
		for (size_t i = 0; i < 4; ++i)
			rTable.push_back(r[(int)i]);
		out = (int)(rTable.size()/4)-1;
		return true;
	}

	bool AddScale(const Vec3 &s, float max, int &out) {
		return AddVec3(sTable, s, out, sAbsMax, max);
	}

	bool AddTranslate(const Vec3 &t, float max, int &out) {
		return AddVec3(tTable, t, out, tAbsMax, max);
	}

private:

	bool QuatEq(const Quat &a, const Quat &b, float max, float &error) {
		
		Mat4 ma = Mat4::Rotation(a);
		Mat4 mb = Mat4::Rotation(b);

		float e = 0.f;

		for (int row = 0; row < 3; ++row) {
			Vec3 d(
				(ma[row][0] - mb[row][0]), 
				(ma[row][1] - mb[row][1]), 
				(ma[row][2] - mb[row][2]) 
			);

			float m = d.MagnitudeSquared();
			e += m;

			if (m >= max)
				return false;
			if (e >= error)
				return false;
		}

		RAD_ASSERT(e < error);
		error = e;

		return true;
	}

	float VecEq(const Vec3 &a, const Vec3 &b) {
		return (a-b).MagnitudeSquared();
	}

	bool AddVec3(FloatVec &table, const Vec3 &v, int &out, float &outMax, float max) {

		int best = -1;
		
		for (int i = 0; i+3 <= (int)table.size(); i += 3) {
			const Vec3 &a = *((const Vec3*)&table[i]);
			float d = (a-v).MagnitudeSquared();

			if (d < max) {
				max = d;
				best = i;
			}
		}

		if (best != -1) {
			out = best / 3;
			return true;
		}

		if ((table.size()/3) == ska::kEncMask)
			return false;	

		for (size_t i = 0; i < 3; ++i) {
			table.push_back(v[(int)i]);
			outMax = std::max(math::Abs(v[(int)i]), outMax);
		}

		out = (int)(table.size()/3)-1;
		return true;
	}
};

typedef zone_vector<int, ZToolsT>::type IntVec;
struct AnimTables {
	typedef zone_vector<AnimTables, ZToolsT>::type Vec;

	AnimTables() : totalTags(0) {
	}

	Tables animTables;
	
	IntVec rFrames;
	IntVec sFrames;
	IntVec tFrames;
	IntVec tags;
	int totalTags;
};

int SkaBuilder::CompileAnimThread::ThreadProc() {

	COut(C_Info) << "SkaBuilder: compiling '" << m_anim.name << "' on thread 0x" << std::hex << id.get() << std::dec << std::endl;
	
	IntVec prevBoneRFrame;
	IntVec prevBoneSFrame;
	IntVec prevBoneTFrame;

	prevBoneRFrame.resize(m_builder.m_bones.size());
	prevBoneSFrame.resize(m_builder.m_bones.size());
	prevBoneTFrame.resize(m_builder.m_bones.size());
	
	m_tables.animTables.rTable.reserve(128);
	m_tables.animTables.sTable.reserve(128);
	m_tables.animTables.tTable.reserve(128);

	m_tables.rFrames.reserve(m_anim.frames.size()*m_builder.m_bones.size());
	m_tables.sFrames.reserve(m_anim.frames.size()*m_builder.m_bones.size());
	m_tables.tFrames.reserve(m_anim.frames.size()*m_builder.m_bones.size());
	m_tables.tags.reserve(m_anim.frames.size()*m_builder.m_bones.size());

	FrameVec::const_iterator last = m_anim.frames.end();
	for(FrameVec::const_iterator fit = m_anim.frames.begin(); fit != m_anim.frames.end(); ++fit) {
		const BoneTMVec &frame = *fit;

		bool tag = false;

		// apply progressive compression.
		// bones deeper in the hierarchy are compressed less
		// with the theory that don't contribute as much to noticable wobble.

		for (size_t boneIdx = 0; boneIdx < m_builder.m_bones.size(); ++boneIdx) {
			RAD_ASSERT(frame.size() == m_builder.m_bones.size());
			const BoneTM &tm = frame[boneIdx];
			const BoneTM *prevTM = 0;

			if (last != m_anim.frames.end())
				prevTM = &((*last)[boneIdx]);

			bool emitR = prevTM == 0;
			bool emitS = prevTM == 0;
			bool emitT = prevTM == 0;

			emitR = emitR || tm.tm.r != prevTM->tm.r;
			emitS = emitS || tm.tm.s != prevTM->tm.s;
			emitT = emitT || tm.tm.t != prevTM->tm.t;

			if (emitR) {
				int idx;
				if (!m_tables.animTables.AddRotate(tm.tm.r, m_anim.boneCompression[boneIdx].quatCompression, idx)) {
					COut(C_Error) << "Error compiling animation tables for '" << m_anim.name << "'" << std::endl;
					m_builder.m_error = true;
					m_done = true;
					return 0;
				}
				m_tables.rFrames.push_back(idx);
				prevBoneRFrame[boneIdx] = idx;
			} else {
				m_tables.rFrames.push_back(prevBoneRFrame[boneIdx]);
			}

			if (emitS) {
				int idx;
				if (!m_tables.animTables.AddScale(tm.tm.s, m_anim.boneCompression[boneIdx].scaleCompression, idx)) {
					COut(C_Error) << "Error compiling animation tables for '" << m_anim.name << "'" << std::endl;
					m_builder.m_error = true;
					m_done = true;
					return 0;
				}
				m_tables.sFrames.push_back(idx);
				prevBoneSFrame[boneIdx] = idx;
			} else {
				m_tables.sFrames.push_back(prevBoneSFrame[boneIdx]);
			}

			if (emitT) {
				int idx;
				if (!m_tables.animTables.AddTranslate(tm.tm.t, m_anim.boneCompression[boneIdx].translateCompression, idx)) {
					COut(C_Error) << "Error compiling animation tables for '" << m_anim.name << "'" << std::endl;
					m_builder.m_error = true;
					m_done = true;
					return 0;
				}
				m_tables.tFrames.push_back(idx);
				prevBoneTFrame[boneIdx] = idx;
			} else {
				m_tables.tFrames.push_back(prevBoneTFrame[boneIdx]);
			}
		}

		last = fit;
	}

	COut(C_Info) << "SkaBuilder: '" << m_anim.name << "' finished on thread 0x" << std::hex << id.get() << std::dec  << std::endl;

	m_done = true;
	return 0;
}

void SkaBuilder::CompileAnimation(
	const Anim &anim,
	AnimTables &tables
) {
	WaitForThreads(1);
	CompileAnimThread::Ref thread(new (ZTools) CompileAnimThread(*this, anim, tables));
	m_threads.push_back(thread);
	thread->Run();
}

void SkaBuilder::WaitForThreads(int numFree) {
	// reclaim
	for(;;) {
		for (CompileAnimThread::Vec::iterator it = m_threads.begin(); it != m_threads.end();) {
			const CompileAnimThread::Ref &thread = *it;
			if (thread->m_done) {
				thread->Join();
				it = m_threads.erase(it);
			} else {
				++it;
			}
		}

		if ((kMaxThreads-(int)m_threads.size()) >= numFree) {
			break;
		}

		thread::Sleep(100);
	}
}
	
bool SkaBuilder::Compile(stream::IOutputBuffer &ob) {
	
	stream::LittleOutputStream os(ob);

	{
		const U32 id  = ska::kSkaTag;
		const U32 ver = ska::kSkaVersion;
		if (!os.Write(id) || !os.Write(ver))
			return false;
	}

	if (!os.Write((U16)m_bones.size()) || !os.Write((U16)m_anims.size()))
		return false;

	for (BoneDef::Vec::const_iterator it = m_bones.begin(); it != m_bones.end(); ++it) {
		if ((*it).name.numBytes > ska::kDNameLen) {
			COut(C_ErrMsgBox) << "ska::DNameLen exceeded, contact a programmer to increase." << std::endl;
			return false;
		}

		char name[ska::kDNameLen+1];
		string::ncpy(name, (*it).name.c_str.get(), ska::kDNameLen+1);
		if (!os.Write(name, ska::kDNameLen+1, 0))
			return false;
	}

	for (BoneDef::Vec::const_iterator it = m_bones.begin(); it != m_bones.end(); ++it) {
		if (!os.Write((*it).parent))
			return false;
	}

	if (m_bones.size()&1) // align?
		if (!os.Write((S16)0xffff))
			return false;

	for (BoneDef::Vec::const_iterator it = m_bones.begin(); it != m_bones.end(); ++it) {
		const BoneDef &b = *it;
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 3; ++j)
				if (!os.Write(b.invWorld[i][j]))
					return false;
	}

	TagTable tagTable;
	AnimTables::Vec at;
	at.resize(m_anims.size());

	for (Anim::Vec::const_iterator it = m_anims.begin(); it != m_anims.end(); ++it) {
		AnimTables &ct = at[it-m_anims.begin()];
		const Anim &anim = *it;

		// build tags

		FrameVec::const_iterator last = anim.frames.end();
		for(FrameVec::const_iterator fit = anim.frames.begin(); fit != anim.frames.end(); ++fit) {
			const BoneTMVec &frame = *fit;

			bool tag = false;

			// apply progressive compression.
			// bones deeper in the hierarchy are compressed less
			// with the theory that don't contribute as much to noticable wobble.
			for (size_t boneIdx = 0; boneIdx < m_bones.size(); ++boneIdx) {
				RAD_ASSERT(frame.size() == m_bones.size());
				const BoneTM &tm = frame[boneIdx];
				
				int tagId = tagTable.AddString(tm.tag);
				ct.tags.push_back(tagId);
				if (!tag && tagId > -1) {
					tag = true;
					++ct.totalTags;
				}
			}

			last = fit;
		}

		// compile animation
		CompileAnimation(anim, ct);
	}

	WaitForThreads(kMaxThreads);

	if (m_error) {
		// error building animation data
		return false;
	}

	// Write animations.

	RAD_ASSERT(at.size() == m_anims.size());

	for (size_t i = 0; i < at.size(); ++i) {
		char name[ska::kDNameLen+1];
		string::ncpy(name, m_anims[i].name.c_str.get(), ska::kDNameLen+1);
		if (os.Write(name, ska::kDNameLen+1, 0) != (ska::kDNameLen+1))
			return false;
		if (!os.Write(m_anims[i].distance))
			return false;
		U16 fps = (U16)(floorf(m_anims[i].fps+0.5f));
		if (!os.Write(fps))
			return false;
		if (!os.Write((U16)m_anims[i].frames.size()))
			return false;
		if (!os.Write((U16)at[i].totalTags))
			return false;
		if (!os.Write((U16)0))
			return false; // padd bytes.
	}

	// Write animation tables
		
	for (size_t i = 0; i < at.size(); ++i) {
		const AnimTables &tables = at[i];

		RAD_ASSERT(tables.rFrames.size() == (m_bones.size()*m_anims[i].frames.size()));

		if (!os.Write((U32)(tables.animTables.rTable.size())) ||
			!os.Write((U32)(tables.animTables.sTable.size())) ||
			!os.Write((U32)(tables.animTables.tTable.size()))) {
			return false;
		}
	
		if (!os.Write(tables.animTables.sAbsMax/std::numeric_limits<S16>::max()))
			return false;
		if (!os.Write(tables.animTables.tAbsMax/std::numeric_limits<S16>::max()))
			return false;

		// Encode floats.
		for (size_t k = 0; k < tables.animTables.rTable.size(); ++k) {
			S16 enc = Tables::QuantFloat(tables.animTables.rTable[k], 1.0f);
			if (!os.Write(enc))
				return false;
		}

		for (size_t k = 0; k < tables.animTables.sTable.size(); ++k) {
			S16 enc = Tables::QuantFloat(tables.animTables.sTable[k], tables.animTables.sAbsMax);
			if (!os.Write(enc))
				return false;
		}

		for (size_t k = 0; k < tables.animTables.tTable.size(); ++k) {
			S16 enc = Tables::QuantFloat(tables.animTables.tTable[k], tables.animTables.tAbsMax);
			if (!os.Write(enc))
				return false;
		}

		int bytes = 
			(int)(tables.animTables.rTable.size()*2)+
			(int)(tables.animTables.sTable.size()*2)+
			(int)(tables.animTables.tTable.size()*2);

		if (bytes&3) {
			bytes &= 3;
			U8 pad[3] = { 0, 0, 0 };
			if (os.Write(pad, 4-bytes, 0) != (4-bytes))
				return false;
		}

		bytes = 0;
		
		for (IntVec::const_iterator it = tables.rFrames.begin(); it != tables.rFrames.end(); ++it) {
			int i = endian::SwapLittle((*it)&ska::kEncMask);
			if (os.Write(&i, ska::kEncBytes, 0) != ska::kEncBytes)
				return false;
			bytes += ska::kEncBytes;
		}

		RAD_ASSERT(tables.sFrames.size() == (m_bones.size()*m_anims[i].frames.size()));
		
		for (IntVec::const_iterator it = tables.sFrames.begin(); it != tables.sFrames.end(); ++it) {
			int i = endian::SwapLittle((*it)&ska::kEncMask);
			if (os.Write(&i, ska::kEncBytes, 0) != ska::kEncBytes)
				return false;
			bytes += ska::kEncBytes;
		}

		RAD_ASSERT(tables.tFrames.size() == (m_bones.size()*m_anims[i].frames.size()));
		
		for (IntVec::const_iterator it = tables.tFrames.begin(); it != tables.tFrames.end(); ++it) {
			int i = endian::SwapLittle((*it)&ska::kEncMask);
			if (os.Write(&i, ska::kEncBytes, 0) != ska::kEncBytes)
				return false;
			bytes += ska::kEncBytes;
		}

		RAD_ASSERT(tables.tags.size() == (m_bones.size()*m_anims[i].frames.size()));

		// Write DTags

		int boneTagOfs = 0;

		IntVec::const_iterator tagIt = tables.tags.begin();
		for (size_t j = 0; j < m_anims[i].frames.size(); ++j) {
			int numBones = 0;

			for (size_t k = 0; k < m_bones.size(); ++k, ++tagIt) {
				int tagIdx = *tagIt;
				if (tagIdx > -1)
					++numBones;
			}

			if (!numBones)
				continue;

			if (!os.Write((U16)j))
				return false;
			if (!os.Write((U16)numBones))
				return false;
			if (!os.Write((U16)boneTagOfs))
				return false;

			bytes += (int)sizeof(U16) * 3;

			boneTagOfs += numBones * 3;
			if (boneTagOfs > std::numeric_limits<U16>::max()) {
				COut(C_Error) << "SkaBuilder: Bone tag table exceeds 64k!" << std::endl;
				return false;
			}
		}

		if (!os.Write((U16)boneTagOfs))
			return false;
		bytes += (int)sizeof(U16);

		// build bone tag data.
		tagIt = tables.tags.begin();
		for (size_t j = 0; j < m_anims[i].frames.size(); ++j) {
			for (size_t k = 0; k < m_bones.size(); ++k, ++tagIt) {
				int tagIdx = *tagIt;
				if (tagIdx > -1) {
					if (!os.Write((U16)k))
						return false;
					if (!os.Write((U8)tagIdx))
						return false;
					bytes += 3;
				}
			}
		}

		if (bytes&3) { // padd to 4 byte alignment.
			bytes &= 3;
			U8 pad[3] = { 0, 0, 0 };
			if (os.Write(pad, 4-bytes, 0) != (4-bytes))
				return false;
		}
	}

	int bytes = 0;

	// write string table.
	if (!os.Write((U8)tagTable.strings.size()))
		return false;
	++bytes;

	// compile string indexes.
	int stringIdx = 0;
	for (StringVec::const_iterator it = tagTable.strings.begin(); it != tagTable.strings.end(); ++it) {
		if (!os.Write((U16)stringIdx))
			return false;
		bytes += 2;
		const String &str = *it;
		stringIdx += (int)str.numBytes+1;
		if (stringIdx > std::numeric_limits<U16>::max()) {
			COut(C_Error) << "SkaBuilder: String table exceeds 64k in size!" << std::endl;
			return false;
		}
	}

	// compile strings
	for (StringVec::const_iterator it = tagTable.strings.begin(); it != tagTable.strings.end(); ++it) {
		const String &str = *it;
		if (os.Write(str.c_str.get(), (stream::SPos)(str.numBytes+1), 0) != (stream::SPos)(str.numBytes+1))
			return false;
		bytes += (int)str.numBytes+1;
	}
	
	if (bytes&3) { // padd file to 4 byte alignment.
		bytes &= 3;
		U8 pad[3] = { 0, 0, 0 };
		if (os.Write(pad, 4-bytes, 0) != (4-bytes))
			return false;
	}

	return true;
}

void SkaBuilder::SetBones(const BoneDef::Vec &bones) {
	m_bones = bones;
}

void SkaBuilder::BeginAnim(
	const char *name,
	float fps,
	int numFrames,
	bool removeMotion,
	const SkaCompressionMap *compression
) {
	RAD_ASSERT(name);
	RAD_ASSERT(numFrames>0);

	m_anims.resize(m_anims.size()+1);
	Anim &a = m_anims.back();
	a.name = name;
	a.fps = fps;
	a.removeMotion = removeMotion;
	a.frames.reserve((size_t)numFrames);
	a.distance = 0.f;
	a.boneCompression.resize(m_bones.size());

	float compressionLevel = 1.f;
	if (compression) {
		SkaCompressionMap::const_iterator it = compression->find(CStr(name));
		if (it != compression->end())
			compressionLevel = it->second;
	}

	for (size_t i = 0; i < m_bones.size(); ++i) {
		Compression &c = a.boneCompression[i];
		const BoneDef &bone = m_bones[i];

		const float kFactor = math::Pow(0.7f, (float)bone.childDepth);

		c.quatCompression = std::max(
			kQuatCompressionBasis[0],
			kQuatCompressionBasis[std::min(1, bone.childDepth)+1] * kFactor * compressionLevel
		);

		float qsin = sin(c.quatCompression*3.1415926535f/180);
		c.quatCompression = qsin*qsin;

		c.scaleCompression = std::max(
			kScaleCompressionBasis[0],
			kScaleCompressionBasis[1] * kFactor * compressionLevel
		);

		c.translateCompression = std::max(
			kTranslationCompressionBasis[0],
			kTranslationCompressionBasis[1] * kFactor * compressionLevel
		);
	}
}

void SkaBuilder::AddFrame(const BoneTM *bones) {
	RAD_ASSERT(bones);
	Anim &a = m_anims.back();
	if (a.frames.empty())
		a.frames.reserve(256);
	a.frames.resize(a.frames.size()+1);
	a.frames.back().reserve(m_bones.size());
	for (size_t i = 0; i < m_bones.size(); ++i)
		a.frames.back().push_back(bones[i]);
}

void SkaBuilder::EndAnim(int motionBone) {
	if (motionBone < -1)
		return;
	Anim &a = m_anims.back();
	if (motionBone >= (int)a.frames.front().size())
		return;

	const BoneTMVec &start = a.frames.front();
	const BoneTMVec &end = a.frames.back();

	Vec3 d = end[motionBone].tm.t - start[motionBone].tm.t;
	a.distance = d.Magnitude();
}

///////////////////////////////////////////////////////////////////////////////

typedef SceneFile::WeightedNormalTriVert TriVert;
typedef zone_vector<TriVert, ZToolsT>::type TriVertVec;
typedef zone_map<TriVert, int, ZToolsT>::type TriVertMap;

struct SkTriModel {
	typedef boost::shared_ptr<SkTriModel> Ref;
	typedef zone_vector<Ref, ZToolsT>::type Vec;
	typedef zone_vector<Vec4, ZToolsT>::type Vec4Vec;
	typedef zone_vector<Vec3, ZToolsT>::type Vec3Vec;
	typedef zone_vector<Vec2, ZToolsT>::type Vec2Vec;

	struct VertIndex {
		typedef zone_vector<VertIndex, ZToolsT>::type Vec;

		VertIndex(int _numBones, int _idx) : numBones(_numBones), index(_idx) {}

		int numBones;
		int index;
	};

	int mat;
	int totalVerts;
	TriVertVec verts[ska::kBonesPerVert];
	TriVertMap vmap[ska::kBonesPerVert];
	VertIndex::Vec indices;

	Vec4Vec weightedVerts;
	Vec4Vec weightedNormals;
	Vec4Vec weightedTangents;
	Vec2Vec uvs[ska::kMaxUVChannels];
	IntVec sortedIndices;

	static TriVert CleanWeights(const TriVert &v) {
		TriVert z(v);
		SceneFile::BoneWeights w;

		// drop tiny weights
		for (size_t i = 0; i < z.weights.size(); ++i) {
			if (z.weights[i].weight < 0.009999999f)
				continue;
			w.push_back(z.weights[i]);
		}

		while (w.size() > ska::kBonesPerVert) { // drop smallest.
			size_t best = 0;
			float bestw = w[0].weight;

			for (size_t i = 1; i < w.size(); ++i) {
				if (w[i].weight < bestw) {
					bestw = w[i].weight;
					best = i;
				}
			}

			SceneFile::BoneWeights x;
			x.swap(w);

			for (size_t i = 0; i < x.size(); ++i) {
				if (i != best)
					w.push_back(x[i]);
			}
		}

		float total = 0.f;

		for (size_t i = 0; i < w.size(); ++i) {
			total += w[i].weight;
		}

		if (total <= 0.f)
			w.clear(); // bad

		// renormalize
		for (size_t i = 0; i < w.size(); ++i) {
			w[i].weight = w[i].weight / total;
		}

		if (w.empty()) {
			SceneFile::BoneWeight x;
			x.bone = 0;
			x.weight = 1.0f;
			w.push_back(x);
		}

		z.weights = w;
		return z;
	}

	void AddVertex(const TriVert &v) {
		TriVert z = CleanWeights(v);

		RAD_ASSERT(!z.weights.empty());
		RAD_ASSERT(z.weights.size() <= ska::kBonesPerVert);
		int mapIdx = (int)z.weights.size()-1;

		TriVertMap::iterator it = vmap[mapIdx].find(z);
		if (it != vmap[mapIdx].end()) {
#if defined(RAD_OPT_DEBUG)
			RAD_ASSERT(z.weights.size() == it->first.weights.size());
			RAD_ASSERT(z.pos == it->first.pos);
			for (int i = 0; i < SceneFile::kMaxUVChannels; ++i) {
				RAD_ASSERT(z.st[i] == it->first.st[i]);
				RAD_ASSERT(z.tangent[i] == it->first.tangent[i]);
			}
			RAD_ASSERT(z.st[0] == it->first.st[0]);
			for (size_t i = 0; i < z.weights.size(); ++i) {
				RAD_ASSERT(z.weights[i].weight == it->first.weights[i].weight);
				RAD_ASSERT(z.weights[i].bone == it->first.weights[i].bone);
			}
#endif
			indices.push_back(VertIndex(mapIdx, it->second));
			return;
		}

		int ofs = (int)verts[mapIdx].size();
		verts[mapIdx].push_back(z);
		vmap[mapIdx].insert(TriVertMap::value_type(z, ofs));
		indices.push_back(VertIndex(mapIdx, ofs));
	}

	void AddTriangles(const SceneFile::TriModel::Ref &m) {
		for (SceneFile::TriFaceVec::const_iterator it = m->tris.begin(); it != m->tris.end(); ++it) {
			const SceneFile::TriFace &tri = *it;
			if (tri.mat < 0)
				continue;
			if (tri.mat != mat)
				continue;
			for (int i = 0; i < 3; ++i)
				AddVertex(TriVert(m->verts[tri.v[i]]));
		}
	}

	void Compile() {
		// figure out how many final verts we'll have.
		int numWeightedVerts = 0;
		totalVerts = 0;

		for (int i = 0; i < ska::kBonesPerVert; ++i) {
			int c = (int)vmap[i].size();

			totalVerts += c;
			numWeightedVerts += c*i;
		}		

		sortedIndices.reserve(indices.size());
		weightedVerts.reserve(numWeightedVerts);
		weightedNormals.reserve(numWeightedVerts);
		weightedTangents.reserve(numWeightedVerts);

		for (int i = 0; i < ska::kMaxUVChannels; ++i) {
			uvs[i].reserve(totalVerts);
		}

		int vertIndex = 0;
		IntVec idxRemap[ska::kBonesPerVert];

		for (int i = 0; i < ska::kBonesPerVert; ++i)
			idxRemap[i].reserve(verts[i].size());

		// emit vertices premultiplied by bone weights
		for (int i = 0; i < ska::kBonesPerVert; ++i) {
			const TriVertVec &vec = verts[i];
			for (TriVertVec::const_iterator it = vec.begin(); it != vec.end(); ++it) {
				const TriVert &v = *it;
				RAD_ASSERT((i+1) == (int)v.weights.size());

				idxRemap[i].push_back(vertIndex++);

				for (int k = 0; k < ska::kMaxUVChannels; ++k) {
					uvs[k].push_back(v.st[k]);
				}

				for (int k = 0; k <= i; ++k) {
					Vec4 z(v.pos * v.weights[k].weight, v.weights[k].weight);
					weightedVerts.push_back(z);
				}

				for (int k = 0; k <= i; ++k) {
					Vec4 z(v.normal * v.weights[k].weight, 1.f);
					weightedNormals.push_back(z);
				}

				for (int k = 0; k <= i; ++k) {
					Vec4 z(Vec3(v.tangent[0]) * v.weights[k].weight, v.tangent[0][3]);
					weightedTangents.push_back(z);
				}
			}
		}

		for (VertIndex::Vec::const_iterator it = indices.begin(); it != indices.end(); ++it) {
			const VertIndex &idx = *it;
			sortedIndices.push_back(idxRemap[idx.numBones][idx.index]);
		}
	}
};

bool CompileCPUSkmData(const char *name, const SceneFile &map, int trimodel, SkmData &sk, const ska::DSka &ska) {
	SkTriModel::Ref m(new (ZTools) SkTriModel());
	SkTriModel::Vec models;

	SceneFile::Entity::Ref e = map.worldspawn;
	SceneFile::TriModel::Ref r = e->models[trimodel];

	if (r->skel < 0)
		return false;

	const SceneFile::Skel &skel = *e->skels[r->skel].get();

	for (int i = 0; i < (int)map.mats.size(); ++i){
		m->mat = i;
		m->AddTriangles(r);
		if (!m->indices.empty()) {
			m->Compile();
			models.push_back(m);
			m.reset(new (ZTools) SkTriModel());
		}
	}

	std::vector<int> remap;
	remap.reserve(skel.bones.size());

	// build bone remap table
	for (size_t i = 0; i < skel.bones.size(); ++i) {
		U16 k;
		for (k = 0; k < ska.numBones; ++k) {
			if (!string::cmp(skel.bones[i].name.c_str.get(), &ska.boneNames[k*(ska::kDNameLen+1)]))
				break;
		}

		remap.push_back((k==ska.numBones) ? 0 : (int)k);
	}

	m.reset();

	if (models.empty())
		return false;

	{ // file 1: non persistant data (material names, texCoords, tris)
		stream::DynamicMemOutputBuffer ob(ska::ZSka);
		stream::LittleOutputStream os(ob);

		if (!os.Write((U32)ska::kSkmxTag) || !os.Write((U32)ska::kSkmVersion))
			return false;

		// bounds
		for (int i = 0; i < 3; ++i) {
			if (!os.Write((float)r->bounds.Mins()[i]))
				return false;
		}
		for (int i = 0; i < 3; ++i) {
			if (!os.Write((float)r->bounds.Maxs()[i]))
				return false;
		}

		if (!os.Write((U16)models.size()))
			return false;
		if (!os.Write((U16)0))
			return false;

		for (SkTriModel::Vec::const_iterator it = models.begin(); it != models.end(); ++it) {
			const SkTriModel::Ref &m = *it;

			if (m->totalVerts > std::numeric_limits<U16>::max()) {
				COut(C_Error) << "object exceeds 32k verts." << std::endl;
				return false;
			}
			if (m->sortedIndices.size()/3 > std::numeric_limits<U16>::max()) {
				COut(C_Error) << "object exceeds 32k indices (this is not a vertex count issue, contact a programmer to remove this limit)." << std::endl;
				return false;
			}

			if (!os.Write((U16)m->totalVerts))
				return false;

			for (int i = 0; i < ska::kBonesPerVert; ++i) {
				if (m->verts[i].size() > std::numeric_limits<U16>::max())
					return false;
				if (!os.Write((U16)m->verts[i].size()))
					return false;
			}

			if (!os.Write((U16)(m->sortedIndices.size()/3)))
				return false;

			if (!os.Write((U16)(r->numChannels)))
				return false;

			if ((ska::kBonesPerVert+3)&1) // align?
				if (!os.Write((U16)0))
					return false;

			if (map.mats[m->mat].name.numBytes > ska::kDNameLen) {
				COut(C_Error) << "ska::kDNameLen exceeded, contact a programmer to increase." << std::endl;
				return false;
			}

			char name[ska::kDNameLen+1];
			string::ncpy(name, map.mats[m->mat].name.c_str.get(), ska::kDNameLen+1);
			if (!os.Write(name, ska::kDNameLen+1, 0))
				return false;

			// texcoords
			const int kNumTexCoords = (int)m->uvs[0].size();

			for (int i = 0; i < kNumTexCoords; ++i) {
				for (int k = 0; k < r->numChannels; ++k) {
					const SkTriModel::Vec2Vec &uvs = m->uvs[k];
					for (int j = 0; j < 2; ++j) {
						if (!os.Write(uvs[i][j]))
							return false;
					}
				}
			}

			// tris (indices)
			for (IntVec::const_iterator it = m->sortedIndices.begin(); it != m->sortedIndices.end(); ++it) {
				if (!os.Write((U16)*it))
					return false;
			}

			if (m->sortedIndices.size()&1) { // align
				RAD_ASSERT((m->sortedIndices.size()/3)&1);
				if (!os.Write((U16)0))
					return false;
			}
		}

		sk.skmData[0] = ob.OutputBuffer().Ptr();
		sk.skmSize[0] = (AddrSize)ob.OutPos();
		ob.OutputBuffer().Set(0, 0);
		sk.skmData[0] = zone_realloc(ska::ZSka, sk.skmData[0], sk.skmSize[0]);
	}
	{ // file 2: persisted data (prescaled vertices, prescaled normals, bone indices)
		stream::DynamicMemOutputBuffer ob(ska::ZSka, SIMDDriver::kAlignment);
		stream::LittleOutputStream os(ob);

		if (!os.Write((U32)ska::kSkmpTag) || !os.Write((U32)ska::kSkmVersion))
			return false;

		int bytes = 8;

		for (SkTriModel::Vec::const_iterator it = models.begin(); it != models.end(); ++it) {
			const SkTriModel::Ref &m = *it;

			if (bytes&(SIMDDriver::kAlignment-1))  { // SIMD padd
				U8 padd[(SIMDDriver::kAlignment-1)];
				if (os.Write(padd, SIMDDriver::kAlignment-(bytes&(SIMDDriver::kAlignment-1)), 0) != (SIMDDriver::kAlignment-(bytes&(SIMDDriver::kAlignment-1))))
					return false;
				bytes = Align(bytes, SIMDDriver::kAlignment);
			}

			RAD_ASSERT(m->weightedVerts.size() == m->weightedNormals.size());
			RAD_ASSERT(m->weightedVerts.size() == m->weightedTangents.size());

			int ofs = 0;
			for (int i = 0; i < ska::kBonesPerVert; ++i) {
				const int kBoneVertCount = (int)m->verts[i].size();
				
				for (int z = 0; z < kBoneVertCount; ++z) {
					for (int k = 0; k <= i; ++k) {
						const Vec4 *v = &m->weightedVerts[k+ofs];

						if (!os.Write((*v)[0]) ||
							!os.Write((*v)[1]) ||
							!os.Write((*v)[2]) ||
							!os.Write((*v)[3])) {
							return false;
						}

						bytes += 16;
					}

					for (int k = 0; k <= i; ++k) {
						const Vec4 *v = &m->weightedNormals[k+ofs];

						if (!os.Write((*v)[0]) ||
							!os.Write((*v)[1]) ||
							!os.Write((*v)[2]) ||
							!os.Write((*v)[3])) {
							return false;
						}

						bytes += 16;
					}

					for (int k = 0; k <= i; ++k) {
						const Vec4 *v = &m->weightedTangents[k+ofs];

						if (!os.Write((*v)[0]) ||
							!os.Write((*v)[1]) ||
							!os.Write((*v)[2]) ||
							!os.Write((*v)[3])) {
							return false;
						}

						bytes += 16;
					}
					
					ofs += (i+1);
				}
			}

			// bone indices

			for (int i = 0; i < ska::kBonesPerVert; ++i) {
				if (bytes&(SIMDDriver::kAlignment-1)) { // SIMD padd
					U8 padd[(SIMDDriver::kAlignment-1)];
					if (os.Write(padd, SIMDDriver::kAlignment-(bytes&(SIMDDriver::kAlignment-1)), 0) != (SIMDDriver::kAlignment-(bytes&(SIMDDriver::kAlignment-1))))
						return false;
					bytes = Align(bytes, SIMDDriver::kAlignment);
				}

				const TriVertVec &verts = m->verts[i];
				for (TriVertVec::const_iterator it = verts.begin(); it != verts.end(); ++it) {
					const TriVert &v = *it;
					for (int k = 0; k <= i; ++k) {
						RAD_ASSERT(k <= (int)v.weights.size());
						int b = remap[v.weights[k].bone];
						if (!os.Write((U16)b))
							return false;
						bytes += 2;
					}
				}
			}
		}

		sk.skmData[1] = ob.OutputBuffer().Ptr();
		sk.skmSize[1] = (AddrSize)ob.OutPos();
		ob.OutputBuffer().Set(0, 0);
		sk.skmData[1] = zone_realloc(
			ska::ZSka, 
			sk.skmData[1], 
			sk.skmSize[1],
			0,
			SIMDDriver::kAlignment
		);
	}

	RAD_VERIFY(sk.dskm.Parse(sk.skmData, sk.skmSize, ska::kSkinType_CPU) == pkg::SR_Success);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

typedef SceneFile::NormalTriVert VtmVert;
typedef zone_vector<VtmVert, ZToolsT>::type VtmVertVec;

struct VtmModel {
	typedef boost::shared_ptr<VtmModel> Ref;
	typedef zone_vector<Ref, ZToolsT>::type Vec;
	typedef zone_vector<Vec4, ZToolsT>::type Vec4Vec;
	typedef zone_vector<Vec3, ZToolsT>::type Vec3Vec;
	typedef zone_vector<Vec2, ZToolsT>::type Vec2Vec;
	typedef zone_vector<int, ZToolsT>::type IntVec;
	typedef zone_map<int, int, ZToolsT>::type VertMap;

	int mat;
	int vertOfs; // relative to file
	VtmVertVec verts;
	VertMap remap[2];
	IntVec indices;

	int AddVertex(const VtmVert &v, int idx) {

		VertMap::iterator it = remap[0].find(idx);
		if (it != remap[0].end()) {
			RAD_ASSERT(v == verts[it->second]);
			return it->second;
		}
		
		int ofs = (int)verts.size();
		verts.push_back(v);
		remap[0].insert(VertMap::value_type(idx, ofs));
		remap[1].insert(VertMap::value_type(ofs, idx));
		return ofs;
	}

	void AddTriangles(const SceneFile::TriModel::Ref &m) {

		remap[0].clear();
		remap[1].clear();
		verts.clear();

		indices.clear();
		indices.reserve(m->tris.size()*3);

		for (SceneFile::TriFaceVec::const_iterator it = m->tris.begin(); it != m->tris.end(); ++it) {
			const SceneFile::TriFace &tri = *it;
			if (tri.mat < 0)
				continue;
			if (tri.mat != mat)
				continue;
			for (int i = 0; i < 3; ++i)
				indices.push_back(AddVertex(VtmVert(m->verts[tri.v[i]]), tri.v[i]));
		}
	}
};

bool CompileVtmData(
	const char *name, 
	const SceneFile &mesh,
	const SceneFileVec *animVec, 
	const SceneFile *animFile,
	int trimodel, 
	VtmData &vtm
) {
	if (!animVec && !animFile)
		return false;

	VtmModel::Ref m(new (ZTools) VtmModel());
	VtmModel::Vec models;

	const SceneFile::Entity::Ref &e = mesh.worldspawn;
	const SceneFile::TriModel::Ref &r = e->models[trimodel];

	// validate all anim files for identical meshes
	if (animVec) {
		for (SceneFileVec::const_iterator it = animVec->begin(); it != animVec->end(); ++it) {
			const SceneFile::Entity::Ref w = (*it)->worldspawn;
			const SceneFile::TriModel::Ref &t = w->models[trimodel];
			if ((t->verts.size() != r->verts.size()) ||
				(t->tris.size() != r->tris.size())) {
				COut(C_Error) << "CompileVtmData: vertex animated model has animation files that do not match its mesh." << std::endl;
				return false;
			}
		}
	} else {
		RAD_ASSERT(animFile);
		const SceneFile::Entity::Ref w = animFile->worldspawn;
		const SceneFile::TriModel::Ref &t = w->models[trimodel];
		if ((t->verts.size() != r->verts.size()) ||
			(t->tris.size() != r->tris.size())) {
			COut(C_Error) << "CompileVtmData: vertex animated model has animation files that do not match its mesh." << std::endl;
			return false;
		}
	}

	int vertOfs = 0;

	for (int i = 0; i < (int)mesh.mats.size(); ++i){
		m->mat = i;
		m->vertOfs = vertOfs;
		m->AddTriangles(r);
		if (!m->indices.empty()) {
			models.push_back(m);
			vertOfs += (int)m->verts.size();
			m.reset(new (ZTools) VtmModel());
		}
	}

	// write file 1: non-persisted data: material names, texCoords, tris
	{
		stream::DynamicMemOutputBuffer ob(ska::ZSka);
		stream::LittleOutputStream os(ob);

		if (!os.Write((U32)ska::kVtmxTag) || !os.Write((U32)ska::kVtmVersion))
			return false;

		// bounds
		for (int i = 0; i < 3; ++i) {
			if (!os.Write((float)r->bounds.Mins()[i]))
				return false;
		}
		for (int i = 0; i < 3; ++i) {
			if (!os.Write((float)r->bounds.Maxs()[i]))
				return false;
		}

		if (!os.Write((U16)models.size()))
			return false;
		if (!os.Write((U16)0))
			return false; // padd
		
		for (VtmModel::Vec::const_iterator it = models.begin(); it != models.end(); ++it) {
			const VtmModel::Ref &m = *it;

			if (m->verts.size() > std::numeric_limits<U16>::max()) {
				COut(C_Error) << "object exceeds 32k verts." << std::endl;
				return false;
			}
			if (m->indices.size()/3 > std::numeric_limits<U16>::max()) {
				COut(C_Error) << "object exceeds 32k indices (this is not a vertex count issue, contact a programmer to remove this limit)." << std::endl;
				return false;
			}

			if (!os.Write((U32)m->vertOfs))
				return false;
			if (!os.Write((U16)m->verts.size()))
				return false;
			if (!os.Write((U16)(m->indices.size()/3)))
				return false;
			if (!os.Write((U16)r->numChannels))
				return false;
			if (!os.Write((U16)0))
				return false; // padd

			if (mesh.mats[m->mat].name.numBytes > ska::kDNameLen) {
				COut(C_Error) << "ska::kDNameLen exceeded, contact a programmer to increase." << std::endl;
				return false;
			}

			char name[ska::kDNameLen+1];
			string::ncpy(name, mesh.mats[m->mat].name.c_str.get(), ska::kDNameLen+1);
			if (!os.Write(name, ska::kDNameLen+1, 0))
				return false;

			for (VtmVertVec::const_iterator it = m->verts.begin(); it != m->verts.end(); ++it) {
				for (int i = 0; i < r->numChannels; ++i) {
					for (int k = 0; k < 2; ++k) {
						if (!os.Write((*it).st[i][k]))
							return false;
					}
				}
			}

			// tris (indices)
			for (IntVec::const_iterator it = m->indices.begin(); it != m->indices.end(); ++it) {
				if (!os.Write((U16)*it))
					return false;
			}

			if (m->indices.size()&1) { // align
				if (!os.Write((U16)0))
					return false;
			}
		}

		vtm.vtmData[0] = ob.OutputBuffer().Ptr();
		vtm.vtmSize[0] = (AddrSize)ob.OutPos();
		ob.OutputBuffer().Set(0, 0);
		vtm.vtmData[0] = zone_realloc(ska::ZSka, vtm.vtmData[0], vtm.vtmSize[0]);
	}

	typedef std::map<String, const SceneFile*> AnimMap;
	AnimMap animMap;
	
	// get animation list
	if (animVec) {
		for (SceneFileVec::const_iterator it = animVec->begin(); it != animVec->end(); ++it) {
			const SceneFile::Entity::Ref w = (*it)->worldspawn;
			const SceneFile::TriModel::Ref &t = w->models[trimodel];

			for (SceneFile::AnimMap::const_iterator animIt = t->anims.begin(); animIt != t->anims.end(); ++animIt) {
			
				AnimMap::iterator x = animMap.find(animIt->first);
				if (x != animMap.end()) {
					COut(C_Error) << "ERROR: animation '" << animIt->first << "' is duplicated in the model source files." << std::endl;
					return false;
				}

				const SceneFile::Anim::Ref &anim = animIt->second;
				if (anim->vertexFrames.empty())
					continue;

				animMap.insert(AnimMap::value_type(animIt->first, it->get()));
			}
		}
	} else {
		RAD_ASSERT(animFile);
		const SceneFile::Entity::Ref w = animFile->worldspawn;
		const SceneFile::TriModel::Ref &t = w->models[trimodel];

		for (SceneFile::AnimMap::const_iterator animIt = t->anims.begin(); animIt != t->anims.end(); ++animIt) {
			
			AnimMap::iterator x = animMap.find(animIt->first);
			if (x != animMap.end()) {
				COut(C_Error) << "ERROR: animation '" << animIt->first << "' is duplicated in the model source files." << std::endl;
				return false;
			}

			const SceneFile::Anim::Ref &anim = animIt->second;
			if (anim->vertexFrames.empty())
				continue;

			animMap.insert(AnimMap::value_type(animIt->first, animFile));
		}
	}

	if (animMap.empty()) {
		COut(C_Error) << "ERROR: no vertex animation data present." << std::endl;
		return false;
	}

	// write file 2: persisted data: vertices, normals, tangents
	{
		stream::DynamicMemOutputBuffer ob(ska::ZSka, SIMDDriver::kAlignment);
		stream::LittleOutputStream os(ob);

		if (!os.Write((U32)ska::kVtmpTag) || !os.Write((U32)ska::kVtmVersion))
			return false;

		if (!os.Write((U32)vertOfs))
			return false;
		if (!os.Write((U16)animMap.size()))
			return false;

		// padd to 16
		if (!os.Write((U16)0))
			return false; 

		BOOST_STATIC_ASSERT(SIMDDriver::kAlignment == 16); // this is coded to be 16 byte aligned.

		// write ref verts
		for (VtmModel::Vec::const_iterator it = models.begin(); it != models.end(); ++it) {
			const VtmModel::Ref &m = *it;

			for (VtmVertVec::const_iterator it = m->verts.begin(); it != m->verts.end(); ++it) {
				const VtmVert &v = *it;

				// vertex, normal, tangent
				for (int i = 0; i < 3; ++i) {
					if (!os.Write(v.pos[i]))
						return false;
				}

				if (!os.Write(1.f))
					return false;

				for (int i = 0; i < 3; ++i) {
					if (!os.Write(v.normal[i]))
						return false;
				}

				if (!os.Write(1.f))
					return false;

				for (int i = 0; i < 4; ++i) {
					if (!os.Write(v.tangent[0][i]))
						return false;
				}
			}
		}
		
		// write animations
		for (AnimMap::const_iterator it = animMap.begin(); it != animMap.end(); ++it) {
			const SceneFile &srcFile = *it->second;

			if (it->first.numBytes > ska::kDNameLen) {
				COut(C_Error) << "ska::kDNameLen exceeded, contact a programmer to increase." << std::endl;
				return false;
			}

			char name[ska::kDNameLen+1];
			string::ncpy(name, it->first.c_str.get(), ska::kDNameLen+1);
			if (!os.Write(name, ska::kDNameLen+1, 0))
				return false;

			const SceneFile::Anim::Ref &srcAnim = srcFile.worldspawn->models[trimodel]->anims[it->first];
			
			if (!os.Write((U16)srcAnim->frameRate))
				return false;
			if (!os.Write((U16)srcAnim->vertexFrames.size()))
				return false;

			// frame offsets
			for (SceneFile::VertexFrames::const_iterator it = srcAnim->vertexFrames.begin(); it != srcAnim->vertexFrames.end(); ++it) {
				int offsetFrame = (*it).frame - srcAnim->firstFrame;
				if (!os.Write((U16)offsetFrame))
					return false;
			}

			int numShortsToPadd = 8 - ((2+(int)srcAnim->vertexFrames.size())&7);
			if (numShortsToPadd < 8) {
				U16 shorts[7] = {0, 0, 0, 0, 0, 0, 0};
				if (os.Write(shorts, sizeof(U16)*numShortsToPadd, 0) != (stream::SPos)(sizeof(U16)*numShortsToPadd))
					return false;
			}

			// write verts, indexed by model.
			for (SceneFile::VertexFrames::const_iterator it = srcAnim->vertexFrames.begin(); it != srcAnim->vertexFrames.end(); ++it) {
				const SceneFile::VertexFrame &vframe = *it;

				for (VtmModel::Vec::const_iterator it = models.begin(); it != models.end(); ++it) {
					const VtmModel::Ref &m = *it;

					for (size_t i = 0; i < m->verts.size(); ++i) {
						const SceneFile::TriVert &vfv = vframe.verts[m->remap[1][i]];
						
						// vertex, normal, tangent
						for (int k = 0; k < 3; ++k) {
							if (!os.Write(vfv.pos[k]))
								return false;
						}

						if (!os.Write(1.f))
							return false;

						for (int k = 0; k < 3; ++k) {
							if (!os.Write(vfv.normal[k]))
								return false;
						}

						if (!os.Write(1.f))
							return false;


						// NOTE: pull the determinant from the original model, we can't blend a binary
						// -1 -> 1, binormal may not be right under severe distortion
						for (int k = 0; k < 3; ++k) {
							if (!os.Write(vfv.tangent[0][k]))
								return false;
						}

						const VtmVert &mv = m->verts[i];
						if (!os.Write(mv.tangent[0][3]))
							return false;
					}
				}
			}
		}

		vtm.vtmData[1] = ob.OutputBuffer().Ptr();
		vtm.vtmSize[1] = (AddrSize)ob.OutPos();
		ob.OutputBuffer().Set(0, 0);
		vtm.vtmData[1] = zone_realloc(
			ska::ZSka, 
			vtm.vtmData[1], 
			vtm.vtmSize[1],
			0,
			SIMDDriver::kAlignment
		);
	}

	RAD_VERIFY(vtm.dvtm.Parse(vtm.vtmData, vtm.vtmSize) == pkg::SR_Success);
	return true;
}

}

///////////////////////////////////////////////////////////////////////////////

RADENG_API SkaData::Ref RADENG_CALL CompileSkaData(
	const char *name, 
	const SceneFileVec &anims,
	int trimodel,
	const SkaCompressionMap *compression
) {
	SkaData::Ref sk(new (ZTools) SkaData());

	SkaBuilder b;
	if (!b.Compile(name, anims, trimodel, compression, *sk))
		return SkaData::Ref();

	SizeBuffer memSize;
	FormatSize(memSize, sk->skaSize);
	COut(C_Info) << "CompileSkaData(" << memSize << ")" << std::endl;
	
	return sk;
}

RADENG_API SkaData::Ref RADENG_CALL CompileSkaData(
	const char *name, 
	const SceneFile &anims,
	int trimodel,
	const SkaCompressionMap *compression
) {
	SkaData::Ref sk(new (ZTools) SkaData());

	SkaBuilder b;
	if (!b.Compile(name, anims, trimodel, compression, *sk))
		return SkaData::Ref();
	
	SizeBuffer memSize;
	FormatSize(memSize, sk->skaSize);
	COut(C_Info) << "CompileSkaData(" << memSize << ")" << std::endl;

	return sk;
}

RADENG_API SkmData::Ref RADENG_CALL CompileSkmData(
	const char *name, 
	const SceneFile &map, 
	int trimodel,
	ska::SkinType skinType,
	const ska::DSka &ska
) {
	SkmData::Ref sk(new (ZTools) SkmData());
	sk->skinType = skinType;

	if (!CompileCPUSkmData(name, map, trimodel, *sk, ska))
		return SkmData::Ref();

	SizeBuffer memSize[2];
	FormatSize(memSize[0], sk->skmSize[0]);
	FormatSize(memSize[1], sk->skmSize[1]);
	COut(C_Info) << "CompileSkmData(" << memSize[0] << ", " << memSize[1] << ")" << std::endl;

	return sk;
}

RADENG_API VtmData::Ref RADENG_CALL CompileVtmData(
	const char *name,
	const SceneFile &mesh,
	const SceneFileVec &anims,
	int trimodel
) {
	VtmData::Ref vtm(new (ZTools) VtmData());

	if (!CompileVtmData(name, mesh, &anims, 0, trimodel, *vtm))
		return VtmData::Ref();

	SizeBuffer memSize[2];
	FormatSize(memSize[0], vtm->vtmSize[0]);
	FormatSize(memSize[1], vtm->vtmSize[1]);
	COut(C_Info) << "CompileVtmData(" << memSize[0] << ", " << memSize[1] << ")" << std::endl;

	return vtm;
}

RADENG_API VtmData::Ref RADENG_CALL CompileVtmData(
	const char *name,
	const SceneFile &mesh,
	const SceneFile &anims,
	int trimodel
) {
	VtmData::Ref vtm(new (ZTools) VtmData());

	if (!CompileVtmData(name, mesh, 0, &anims, trimodel, *vtm))
		return VtmData::Ref();

	SizeBuffer memSize[2];
	FormatSize(memSize[0], vtm->vtmSize[0]);
	FormatSize(memSize[1], vtm->vtmSize[1]);
	COut(C_Info) << "CompileVtmData(" << memSize[0] << ", " << memSize[1] << ")" << std::endl;

	return vtm;
}

///////////////////////////////////////////////////////////////////////////////

SkaData::SkaData() :
skaData(0),
skaSize(0) {
	dska.Clear();
}

SkaData::~SkaData() {
	if (skaData)
		zone_free(skaData);
}

SkmData::SkmData() :
skinType(ska::kSkinType_CPU) {
	skmData[0] = skmData[1] = 0;
	skmSize[0] = skmSize[1] = 0;
	dskm.Clear();
}

SkmData::~SkmData() {
	if (skmData[0])
		zone_free(skmData[0]);
	if (skmData[1])
		zone_free(skmData[1]);
}

VtmData::VtmData() {
	vtmData[0] = vtmData[1] = 0;
	vtmSize[0] = vtmSize[1] = 0;
	dvtm.Clear();
}

VtmData::~VtmData() {
	if (vtmData[0])
		zone_free(vtmData[0]);
	if (vtmData[1])
		zone_free(vtmData[1]);
}

} // tools

#endif // RAD_OPT_TOOLS


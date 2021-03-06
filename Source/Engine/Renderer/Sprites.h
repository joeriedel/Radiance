/*! \file Sprites.h
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup renderer
*/

#pragma once
#include "../Types.h"
#include "Mesh.h"
#include <Runtime/Base/MemoryPool.h>
#include <Runtime/PushPack.h>

namespace r {

class SpriteBatch;
class Sprite {
public:
	Vec3 pos;
	Vec4 rgba;
	Vec2 size;
	float rot;
	float _padd0[2]; // padd to 48 bytes

private:
	friend class SpriteBatch;

	Sprite *next;
	Sprite *prev;
};

class SpriteBatch : public boost::noncopyable {
public:
	typedef boost::shared_ptr<SpriteBatch> Ref;
	enum {
		// we cannot index > than this number of sprites in GLES 2.0
		kMaxSprites = 0xffff / 4
	};

	SpriteBatch();
	SpriteBatch(
		int spriteSize,
		int minSprites, 
		int maxSprites = 0
	); // 0 == no-limit/kMaxSprites

	void Init(
		int spriteSize,
		int minSprites, 
		int maxSprites = kMaxSprites
	);

	RAD_DECLARE_READONLY_PROPERTY(SpriteBatch, mesh, Mesh*);
	RAD_DECLARE_READONLY_PROPERTY(SpriteBatch, numSprites, int);

	Sprite *AllocateSprite();
	void FreeSprite(Sprite *sprite);
	void Compact();

	void Skin();
	void Draw();

private:
	
	RAD_DECLARE_GET(mesh, Mesh*) {
		return &const_cast<SpriteBatch*>(this)->m_m;
	}

	RAD_DECLARE_GET(numSprites, int) {
		return m_numSprites;
	}

	void AllocateMesh();

	MemoryPool m_p;
	Mesh m_m;
	Sprite *m_head;
	Sprite *m_tail;
	int m_numSprites;
	int m_minSprites;
	int m_maxSprites;
	int m_meshSprites;
	int m_vertStream;
	RAD_DEBUG_ONLY(bool m_init);
};

} // r

#include <Runtime/PopPack.h>

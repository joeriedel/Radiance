// GLMesh.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "GLMesh.h"

namespace r {

int GLMesh::AllocateStream(StreamUsage usage, int vertexSize, int vertexCount, bool swapChain) {
	m_streams.resize(m_streams.size()+1);
	Stream &stream = m_streams.back();
	stream.size = vertexSize;
	stream.count = vertexCount;

	const int numVBs = swapChain ? m_numSwapChains : 1;
	
	stream.vbs.reserve(numVBs);

	for (int i = 0; i < numVBs; ++i) {
		GLVertexBuffer::Ref vb(
			new (ZRender) GLVertexBuffer(
				GL_ARRAY_BUFFER_ARB,
				(usage==kStreamUsage_Static) ? GL_STATIC_DRAW_ARB : (usage==kStreamUsage_Dynamic) ? GL_DYNAMIC_DRAW_ARB : GL_STREAM_DRAW_ARB,
				vertexSize*vertexCount
			)
		);
		if (usage != kStreamUsage_Static)
			vb->Map(); // make sure to allocate space.
		stream.vbs.push_back(vb);
	}

	return (int)(m_streams.size()-1);
}

GLMesh::StreamPtr::Ref GLMesh::Map(int s) {
	RAD_ASSERT(s >= 0 && s < (int)m_streams.size());
	Stream &stream = m_streams[s];
	int vb = std::min((int)(stream.vbs.size()-1), m_swapChain);
	RAD_ASSERT(vb >= 0);
	return stream.vbs[vb]->Map();
}

void GLMesh::MapSource(
	int stream, 
	MaterialGeometrySource s, 
	int index,
	int stride,
	int ofs,
	int count
) {
	Source &source = m_sources[s][index];

	source.stream = stream;
	source.type = GL_FLOAT;
	source.stride = stride;
	source.ofs = (GLuint)ofs;
	source.count = count;

}

GLMesh::StreamPtr::Ref GLMesh::MapIndices(StreamUsage _usage, int elemSize, int count) {
	RAD_ASSERT(elemSize==2||elemSize==4);
	GLenum usage = (_usage == kStreamUsage_Static) ? GL_STATIC_DRAW_ARB : GL_DYNAMIC_DRAW_ARB;
	GLenum type = (elemSize == 2) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

	// don't accidentally record this into an active VAO
	if (gl.vbos&&gl.vaos)
		gls.BindVertexArray(r::GLVertexArrayRef());

	if (m_i.vb && (m_i.count == count) && (m_i.usage == usage))
		return m_i.vb->Map();
	
	m_i.type = type;
	m_i.count = count;
	m_i.usage = usage;
	m_i.vb.reset(new (ZRender) GLVertexBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, usage, elemSize*count));

	return m_i.vb->Map();
}

void GLMesh::Bind(MaterialGeometrySource s, int index) {
	Source &source = m_sources[s][index];

	if (source.stream >= 0) {
		RAD_ASSERT(source.stream < (int)m_streams.size());

		const Stream &stream = m_streams[source.stream];

		int vb = std::min((int)(stream.vbs.size()-1), m_swapChain);
		RAD_ASSERT(vb >= 0);

		gls.SetMGSource(
			s,
			index,
			stream.vbs[vb],
			source.count,
			source.type,
			GL_FALSE,
			source.stride,
			source.ofs
		);
	}
}

void GLMesh::BindAll(Shader *shader) {

	// shader state guid must be unique per-shader-pass
	int guid = GenShaderGuid(shader);

	if (m_shaderGuid != guid) {
		m_shaderGuid = guid;
		if (shader) {
			m_va = m_shaderStates.Find(guid);
		} else {
			m_va = 0;
		}
	}

	if (m_va && !m_va->at(m_swapChain))
		m_va = 0;

	bool bindState = true;

	if (m_va) { 
		// we have compiled vertex states.
		const GLVertexArray::Ref &va = m_va->at(m_swapChain);
		gls.BindVertexArray(va);
		bindState = !va->initialized;
	} else if (shader && (gl.vbos&&gl.vaos)) {
		RAD_ASSERT(guid != -1);
		m_va = m_shaderStates.Create(guid, m_swapChain);
		RAD_ASSERT(m_va);
		gls.BindVertexArray(m_va->at(m_swapChain));
		BindIndices(true); // must force this binding to be set.
	} else {
		if (gl.vbos&&gl.vaos)
			gls.BindVertexArray(GLVertexArrayRef());
		BindIndices();
	}

	if (bindState) {
		ResetStreamState();
		for (int s = 0; s < kNumMaterialGeometrySources; ++s) {
			for (int i = 0; i < kMaterialGeometrySource_MaxIndices; ++i) {
				Bind((MaterialGeometrySource)s, i);
			}
		}
	}
}

void GLMesh::Draw(int firstTri, int numTris) {
	RAD_ASSERT(m_i.vb);
	firstTri = firstTri * 3;

	if (numTris < 0) {
		numTris = m_i.count - firstTri;
	} else {
		numTris = numTris * 3;
	}

	const int elemSize = (m_i.type == GL_UNSIGNED_SHORT) ? 2 : 4;

	gl.DrawElements(GL_TRIANGLES, numTris, m_i.type, ((const void*)(firstTri * elemSize)));
	CHECK_GL_ERRORS();
}

void GLMesh::Release() {
	m_va = 0;
	m_shaderGuid = -1;
	m_swapChain = 0;
	m_numSwapChains = 1;
	m_shaderStates.Clear();
	m_i.vb.reset();
	m_streams.clear();

	for (int s = 0; s < kNumMaterialGeometrySources; ++s) {
		for (int i = 0; i < kMaterialGeometrySource_MaxIndices; ++i) {
			m_sources[s][i].stream = -1;
		}
	}
}

} // r

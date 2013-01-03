// GLSLTool.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH

#include "GLSLTool.h"
#include "../../Engine.h"
#include "../../COut.h"
#include "../Material.h"
#include <Runtime/Stream/STLStream.h>
#include <Runtime/StringBase.h>
#include "../../../../../Extern/aras-p-glsl-optimizer-f2217b0/src/glsl/glsl_optimizer.h"
#include <sstream>
#include <Runtime/PushSystemMacros.h>

namespace tools {
namespace shader_utils {

bool GLSLTool::Assemble(
	Engine &engine,
	const r::Material &material,
	const Shader::Ref &shader,
	const r::MaterialInputMappings &mapping,
	const Shader::TexCoordMapping &tcMapping,
	r::Shader::Pass pass,
	AssembleFlags flags,
	std::ostream &out
) {
	std::stringstream ss;
	
	if (!(flags&(kAssemble_VertexShader|kAssemble_PixelShader)))
		flags |= kAssemble_VertexShader;

	bool vertexShader = (flags & kAssemble_VertexShader) ? true : false;

	if (vertexShader) {
		ss << "#define VERTEX" << "\r\n";
	} else {
		ss << "#define FRAGMENT" << "\r\n";
		ss << "#define MATERIAL" << "\r\n";
	}

	if (shader->MaterialSourceUsage(pass, Shader::kMaterialSource_Vertex) > 0)
		ss << "#define SHADER_POSITION\r\n";

	int numTextures = 0;
	for (int i = 0; i < r::kMaxTextures; ++i) {
		if (mapping.textures[i][0] == r::kInvalidMapping)
			break; // no more texture bindings
		switch (mapping.textures[i][0]) {
		case r::kMaterialTextureSource_Texture:
			ss << "#define T" << i << "TYPE sampler2D" << "\r\n";
			break;
		default:
			break;
		}
		++numTextures;
	}

	if (numTextures > 0)
		ss << "#define TEXTURES " << numTextures << "\r\n";
	
	const Shader::IntSet &tcUsage = shader->AttributeUsage(pass, Shader::kMaterialSource_TexCoord);
	int numTexCoords = (int)tcUsage.size();
	if (numTexCoords > 0) {
		// Shaders files can read from r::kMaxTexture unique texture coordinate slots.
		// The set of read texcoord registers may be sparse. Additionally the engine only supports
		// 2 UV channels in most model data. We do some work here to map the set of read texcoord
		// registers onto the smallest possible set:
		//
		// A tcMod may take an input UV channel and modify it, meaning that texture channel is unique,
		// however there are a lot of cases where the tcMod is identity and therefore a texture coordinate
		// register in a shader can be mapped to a commmon register.

		int numTCMods = 0;
		for (int i = 0; i < r::kMaterialTextureSource_MaxIndices; ++i) {
			if (mapping.tcMods[i] == r::kInvalidMapping)
				break;
			++numTCMods;
		}
		
		// numTCMods is the number of unique texture coordinates read by the pixel shader.
		// (which have to be generated by the vertex shader).

		ss << "#define TEXCOORDS " << numTCMods << "\r\n";

		RAD_VERIFY(tcUsage.size() == tcMapping.size());

		int ofs = 0;
		for (Shader::IntSet::const_iterator it = tcUsage.begin(); it != tcUsage.end(); ++it, ++ofs) {
			if (vertexShader) {
				ss << "#define TEXCOORD" << ofs << " tc" << material.TCUVIndex(*it) << "\r\n";
			} else {
				ss << "#define TEXCOORD" << ofs << " tc" << tcMapping[ofs].first << "\r\n";
			}
		}

		if (vertexShader) {
			Shader::IntSet tcInputs;
			
			for (int i = 0; i < r::kMaterialTextureSource_MaxIndices; ++i) {
				if (mapping.tcMods[i] == r::kInvalidMapping)
					break;
				int tcIndex = (int)mapping.tcMods[i];
				tcInputs.insert(material.TCUVIndex(tcIndex));

				int tcMod = material.TCModFlags(tcIndex);
				if (tcMod & r::Material::kTCModFlag_Rotate)
					ss << "#define TEXCOORD" << i << "_ROTATE\r\n";
				if (tcMod & r::Material::kTCModFlag_Scale)
					ss << "#define TEXCOORD" << i << "_SCALE\r\n";
				if (tcMod & r::Material::kTCModFlag_Shift)
					ss << "#define TEXCOORD" << i << "_SHIFT\r\n";
				if (tcMod & r::Material::kTCModFlag_Scroll)
					ss << "#define TEXCOORD" << i << "_SCROLL\r\n";
				if (tcMod & r::Material::kTCModFlag_Turb)
					ss << "#define TEXCOORD" << i << "_TURB\r\n";

				int tcGen = material.TCGen(tcIndex);
				if (tcGen == r::Material::kTCGen_EnvMap)
					ss << "#define TEXCOORD" << i << "_GENREFLECT\r\n";
			}

			ss << "#define TCINPUTS " << tcInputs.size() << "\r\n";
		}
	}
	
	int numColors = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_Color);

	if (numColors > 0)
		ss << "#define COLORS " << numColors << "\r\n";

	int numLightPos = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_LightPos);
	int numLightDir = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_LightDir);
	int numLightHalfDir = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_HalfLightDir);
	int numLightDiffuseColor = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_LightDiffuseColor);
	int numLightSpecularColor = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_LightSpecularColor);

	int numShaderNormals = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_Normal);
	int numShaderTangents = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_Tangent);
	int numShaderBitangents = shader->MaterialSourceUsage(pass, Shader::kMaterialSource_Bitangent);

	int numNormals = numShaderNormals;
	int numTangents = numShaderTangents;
	int numBitangents = numShaderBitangents;

	bool needTangents = vertexShader && (numLightDir || numLightHalfDir);
	if (needTangents) {
		numNormals = std::max(1, numShaderNormals);
		numTangents = std::max(1, numShaderTangents);
		numBitangents = std::max(1, numShaderBitangents);
	}

	if (numNormals > 0)
		ss << "#define NORMALS " << numNormals << "\r\n";
	if (numTangents > 0)
		ss << "#define TANGENTS " << numTangents << "\r\n";
	if (numBitangents > 0)
		ss << "#define BITANGENTS " << numBitangents << "\r\n";

	if (numShaderNormals > 0)
		ss << "#define NUM_SHADER_NORMALS " << numShaderNormals << "\r\n";
	if (numShaderTangents > 0)
		ss << "#define NUM_SHADER_TANGENTS " << numShaderTangents << "\r\n";
	if (numShaderBitangents > 0)
		ss << "#define NUM_SHADER_BITANGENTS " << numShaderBitangents << "\r\n";

	int numLights = std::max(
		numLightPos, 
		std::max(
			numLightDir, 
			std::max(
				numLightHalfDir,
				std::max(numLightDiffuseColor, numLightSpecularColor)
			)
		)
	);

	if (numLights > 0) {
		ss << "#define LIGHTS " << numLights << "\r\n";
		if (numLightDir > 0)
			ss << "#define SHADER_LIGHT_DIR " << numLightDir << "\r\n";
		if (numLightHalfDir > 0)
			ss << "#define SHADER_LIGHT_HALFDIR " << numLightHalfDir << "\r\n";
	}

	const bool GLES = (flags & kAssemble_GLES) ? true : false;

	if (GLES)
		ss << "#define _GLES\r\n";
	
	if (!Inject(engine, "@r:/Source/Shaders/Nodes/GLSL.c", ss))
		return false;
	if (!Inject(engine, "@r:/Source/Shaders/Nodes/Common.c", ss))
		return false;
	if (!Inject(engine, "@r:/Source/Shaders/Nodes/Shader.c", ss))
		return false;

	std::stringstream ex;
	if (!ExpandIncludes(*this, ss, ex))
		return false;

	if (flags & kAssemble_Optimize) {
		String in(ex.str());

		glslopt_shader *opt_shader = glslopt_optimize(
			GLES ? r::gl.glslopt_es : r::gl.glslopt,
			vertexShader ? kGlslOptShaderVertex : kGlslOptShaderFragment,
			in.c_str, 
			0
		);
		
		{
			engine.sys->files->CreateDirectory("@r:/Temp/Shaders/Logs");
			String path(CStr("@r:/Temp/Shaders/Logs/"));
			path += shader->name;
			path += "_unoptimized";
			if (vertexShader) {
				path += ".vert.glsl";
			} else {
				path += ".frag.glsl";
			}
			tools::shader_utils::SaveText(engine, path.c_str, in.c_str);
		}

		if (!glslopt_get_status(opt_shader)) {
			COut(C_Error) << "Error optimizing shader: " << std::endl << in << std::endl << glslopt_get_log(opt_shader) << std::endl;
			engine.sys->files->CreateDirectory("@r:/Temp/Shaders/Logs");

			String path;
			for (int i = 0;; ++i) {
				path.PrintfASCII("@r:/Temp/Shaders/Logs/%s_error_%d.log", shader->name.get(), i);
				if (!engine.sys->files->FileExists(path.c_str)) {
					SaveText(engine, path.c_str, in.c_str);
					break;
				}
			}
			glslopt_shader_delete(opt_shader);
			return false;
		}

		std::stringstream z;
		if (GLES) {
			if (vertexShader)
				z << "precision mediump float;\r\n";
			else
				z << "precision lowp float;\r\n";
		}
		z << glslopt_get_output(opt_shader);
		glslopt_shader_delete(opt_shader);

		Copy(z, out);
	} else {
		Copy(ex, out);
	}

	return true;
}

} // shader_utils
} // tools


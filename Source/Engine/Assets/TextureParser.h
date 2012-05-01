// TextureParser.h
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#pragma once

#include "AssetTypes.h"
#include "../Packages/Packages.h"
#include "../FileSystem/FileSystem.h"
#include <Runtime/ImageCodec/ImageCodec.h>
#include <Runtime/Container/ZoneVector.h>
#include <Runtime/PushPack.h>

class Engine;

#if defined(RAD_OPT_PC_TOOLS)
namespace pvrtexlib {
	class CPVRTexture;	
}
#endif

namespace asset {

struct TextureTag
{
	enum
	{
		RAD_FLAG(WrapS),
		RAD_FLAG(WrapT),
		RAD_FLAG(WrapR),
		RAD_FLAG(Mipmap),
		RAD_FLAG(Filter)
	};

	U8 flags;
};

class RADENG_CLASS TextureParser : public pkg::Sink<TextureParser>
{
public:

	static void Register(Engine &engine);

	enum 
	{ 
		SinkStage = pkg::SS_Parser,
		AssetType = AT_Texture
	};

	typedef boost::shared_ptr<TextureParser> Ref;

	struct Header
	{
		UReg format;
		int width;
		int height;
		int numMips;
	};

	TextureParser();
	virtual ~TextureParser();

	const image_codec::Image *Image(int idx) const;

	RAD_DECLARE_READONLY_PROPERTY(TextureParser, numImages, int);
	RAD_DECLARE_READONLY_PROPERTY(TextureParser, imgValid, bool);
	RAD_DECLARE_READONLY_PROPERTY(TextureParser, header, const Header*);
	RAD_DECLARE_READONLY_PROPERTY(TextureParser, headerValid, bool);
	RAD_DECLARE_READONLY_PROPERTY(TextureParser, tag, const TextureTag*);

protected:

	virtual int Process(
		const xtime::TimeSlice &time,
		Engine &engine,
		const pkg::Asset::Ref &asset,
		int flags
	);

private:

	enum
	{
		S_None,
		S_Loading,
		S_Parsing,
		S_Done,
		S_Header,
		F_Tga = 0,
		F_Dds,
		F_Bmp,
		F_Png
	};

#if defined(RAD_OPT_TOOLS)
	int Load(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);

	int Loading(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);

	int Parsing(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);

	int Format(const wchar_t *name);

	int Resize(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);

	int Mipmap(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);

	int Compress(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);
	
#if defined(RAD_OPT_PC_TOOLS)
	static int ExtractPVR(
		Engine &engine,
		int frame,
		pvrtexlib::CPVRTexture &src,
		image_codec::Image &img
	);
#endif

	int m_fmt;

#endif

	int LoadCooked(
		Engine &engine,
		const xtime::TimeSlice &time,
		const pkg::Asset::Ref &asset,
		int flags
	);

	RAD_DECLARE_GET(numImages, int) { return (int)m_images.size(); }
	RAD_DECLARE_GET(header, const Header *) { return &m_header; }
	RAD_DECLARE_GET(imgValid, bool) { return m_state == S_Done; }
	RAD_DECLARE_GET(headerValid, bool) { return m_state == S_Done || m_state == S_Header; }
	RAD_DECLARE_GET(tag, const TextureTag*) { return &m_tag; }

	typedef zone_vector<image_codec::Image::Ref, ZEngineT>::type ImageVec;
	typedef zone_vector<file::HBufferedAsyncIO, ZEngineT>::type IOVec;

	int m_state;
	bool m_load;
	Header m_header;
	ImageVec m_images;
#if defined(RAD_OPT_TOOLS)
	IOVec m_bufs;
	pkg::Cooker::Ref m_cooker;
#endif
	file::HBufferedAsyncIO m_buf;
	TextureTag m_tag;
};

} // asset

#include <Runtime/PopPack.h>

// PackageCooker.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "../App.h"
#include "../Engine.h"
#include "Packages.h"
#include <Runtime/File.h>
#include <Runtime/Endian/EndianStream.h>
#include <Runtime/DataCodec/LmpWriter.h>
#include <Runtime/DataCodec/ZLib.h>
#include <Runtime/Stream/MemoryStream.h>
#include <Runtime/Tokenizer.h>
#include "../Renderer/Material.h"
#include "../Renderer/GL/GLState.h"
#include "../Tools/Editor/EditorGLWidget.h"
#include <algorithm>
#include <iomanip>
#include <Runtime/PushSystemMacros.h>

using namespace xtime;

namespace pkg {

namespace {

const char *ErrorString(int r) {
	switch (r) {
	case SR_ErrorGeneric:
		return "SR_ErrorGeneric";
	case SR_FileNotFound:
		return "SR_FileNotFound";
	case SR_ParseError:
		return "SR_ParseError";
	case SR_BadVersion:
		return "SR_BadVersion";
	case SR_MetaError:
		return "SR_MetaError";
	case SR_InvalidFormat:
		return "SR_InvalidFormat";
	case SR_CorruptFile:
		return "SR_CorruptFile";
	case SR_IOError:
		return "SR_IOError";
	case SR_CompilerError:
		return "SR_CompilerError";
	case SR_ScriptError:
		return "SR_ScriptError";
	}

	return "Unknown Error";
}

}

#if defined(RAD_OPT_PC_TOOLS)

PackageMan::CookThread::CookThread(
		int _threadNum,
		PackageMan *_pkgMan,
		std::ostream &_out,
		tools::editor::GLWidget &_glw
) : threadNum(_threadNum), pkgMan(_pkgMan), out(&_out), glw(&_glw) {
}

PackageMan::CookThread::~CookThread() {
	Join();
}

int PackageMan::CookThread::ThreadProc() {

	glw->bindGL(true);

	for (;;) {

		pkgMan->m_cookState->waiting.Wait();

		if (pkgMan->m_cookState->done || pkgMan->m_cookState->error || pkgMan->m_cancelCook) {
			Lock L(pkgMan->m_cookState->mutex);
			pkgMan->m_cookState->finished.Open();
			break;
		}

		CookCommand *cmd = 0;

		{
			Lock L(pkgMan->m_cookState->mutex);
			cmd = pkgMan->m_cookState->pending;
			if (cmd) {
				pkgMan->m_cookState->pending = cmd->next;
			} else {
				pkgMan->m_cookState->waiting.Close();
			}
		}

		if (cmd) {
			{
				Lock L(pkgMan->m_cookState->ioMutex);
				(*out) << cmd->name << ": Cooking on thread " << threadNum << "..." << std::endl;
			}

			try {
				cmd->result = cmd->cooker->Cook(cmd->flags);
			} catch (exception &e) {
				Lock L(pkgMan->m_cookState->ioMutex);
				(*out) << "ERROR: exception '" << e.type() << "' msg: '" << (e.what()?e.what():"no message") << "' occured!" << std::endl;
				cmd->result = SR_IOError;
			}

			pkgMan->ResetCooker(cmd->cooker);

			if (cmd->result == SR_Success) {
				Lock L(pkgMan->m_cookState->ioMutex);
				(*out) << cmd->name << ": Finished on thread " << threadNum << "." << std::endl;
			} else {
				Lock L(pkgMan->m_cookState->ioMutex);
				(*out) << cmd->name << ": " << ErrorString(cmd->result) <<  " on thread " << threadNum << "." << std::endl;
			}

			Lock L(pkgMan->m_cookState->mutex);
			cmd->next = pkgMan->m_cookState->complete;
			pkgMan->m_cookState->complete = cmd;
			RAD_ASSERT(pkgMan->m_cookState->numPending > 0);
			if (--pkgMan->m_cookState->numPending == 0) {
				pkgMan->m_cookState->finished.Open();
			}

			if (cmd->result != SR_Success) {
				if (pkgMan->m_cookState->error == SR_Success) {
					pkgMan->m_cookState->error = cmd->result;
					pkgMan->m_cookState->finished.Open();
				}
				break;
			}
		}
	}

	pkgMan->m_engine.sys->r->ctx = r::HContext();

	return 0;
}

void PackageMan::CreateCookThreads(tools::editor::GLWidget &glContext, std::ostream &out) {
	if (m_cookState->numThreads > 0) {
		out << "Starting multithreaded cook with " << m_cookState->numThreads << " thread(s)." << std::endl;

		m_cookThreads.reserve(m_cookState->numThreads);

		for (int i = 0; i < m_cookState->numThreads; ++i) {
			CookThread::Ref thread(new (ZPackages) CookThread(i+1, this, out, glContext));
			thread->Run();
			m_cookThreads.push_back(thread);
		}
	}
}

void PackageMan::ResetCooker(const Cooker::Ref &cooker) {
	cooker->m_asset.reset();
}

bool PackageMan::MakeBuildDirs(int flags, std::ostream &out) {

	const char *platformName = PlatformNameForFlags(flags);
	if (!platformName)
		return false;

	m_cookState->targetPath = CStr("@r:/Cooked/") + CStr(platformName);

	if (flags&P_Clean) {
		out << "(CLEAN): Removing Output Directories..." << std::endl;
		m_engine.sys->files->DeleteDirectory(m_cookState->targetPath.c_str);
	}

	out << "Creating Output Directories..." << std::endl;
	
	bool done = false;
	int maxTries = 1;

	if (flags&P_Clean)
		maxTries = 2;

	const String kPakDir(m_cookState->targetPath + CStr("/Pak/"));

	for (int i = 0; i < maxTries && !done; ++i) {
		if (flags&P_Clean) {
			// wait for file system to settle.
			thread::Sleep(2000);
		}

		if (!m_engine.sys->files->CreateDirectory((m_cookState->targetPath + CStr("/Base")).c_str))
			continue;
		if (!m_engine.sys->files->CreateDirectory((m_cookState->targetPath + CStr("/Shaders")).c_str))
			continue;
		if (!m_engine.sys->files->CreateDirectory((m_cookState->targetPath + CStr("/Packages")).c_str))
			continue;
		if (!MakePackageDirs((m_cookState->targetPath + CStr("/Tags/")).c_str))
			continue;
		if (!MakePackageDirs((m_cookState->targetPath + CStr("/Globals/")).c_str))
			continue;

		for (CookPakFile::Map::const_iterator it = m_cookState->pakfiles.begin(); it != m_cookState->pakfiles.end(); ++it) {
			const CookPakFile::Ref &pakfile = it->second;
			if (!MakePackageDirs((kPakDir + pakfile->name + CStr("/")).c_str))
				continue;
		}

		done = true;
	}

	return done;
}

int PackageMan::Cook(
	int flags,
	int languages,
	int compression,
	int numThreads,
	tools::editor::GLWidget &glContext,
	std::ostream &out
) {
	bool scriptsOnly = (flags&P_ScriptsOnly) ? true : false;

	if (!languages) {
		out << "ERROR: no valid languages specified." << std::endl;
		return SR_ErrorGeneric;
	}

	int ptargets = flags&P_AllTargets;
	if (!ptargets) {
		out << "ERROR: no platforms specified for cook!" << std::endl;
		return SR_ErrorGeneric;
	}

	if (ptargets != LowBitVal(ptargets)) {
		out << "ERROR: multiple platforms specified for cook!" << std::endl;
		return SR_ErrorGeneric;
	}

	flags &= ~ptargets;
	
	m_cookState.reset(new (ZPackages) CookState());
	m_cookState->cout = &out;
	m_cookState->flags = flags|ptargets;
	m_cookState->ptargets = ptargets;
	m_cookState->languages = languages;
	m_cookState->waiting.Close();
	m_cookState->finished.Open();
	m_cookState->pending = 0;
	m_cookState->complete = 0;
	m_cookState->numPending = 0;
	m_cookState->numThreads = numThreads;
	m_cookState->done = false;
	m_cookState->error = SR_Success;

	int r = LoadCookTxt(flags|ptargets, out);

	if (r != SR_Success)
		return r;

	if (!MakeBuildDirs(flags|ptargets, out)) {
		out << "ERROR: failed to create output directories!" << std::endl;
		return SR_ErrorGeneric;
	}

	{ // show languages
		bool sep = false;

		out << "Languages: ";
		for (int i = StringTable::LangId_EN; i < StringTable::LangId_MAX; ++i) {
			if (!((1<<i)&languages))
				continue;
			if (sep) {
				sep = false;
				out << ", ";
			}
			out << CStr(StringTable::Langs[i]).Upper();
			sep = true;
		}
		out << std::endl;
	}

	xtime::TimeVal startTime = xtime::ReadMilliseconds();

	r = CompileScripts();

	if (r == SR_Success) {
		if (scriptsOnly) {
			r = BuildPakFiles(ptargets, compression);
		} else {
			CreateCookThreads(glContext, out);

			r::Material::BeginCook();

			for (CookPakFile::Map::const_iterator it = m_cookState->pakfiles.begin(); it != m_cookState->pakfiles.end(); ++it) {
				const CookPakFile::Ref &pakfile = it->second;
				out << "Cooking " << PlatformNameForFlags(ptargets) << " " << pakfile->name << "..." << std::endl;
				r = CookPakFileSources(pakfile, flags|ptargets, out);
				if (r != SR_Success) {
					out << "ERROR: cooking " << PlatformNameForFlags(ptargets) << " " << pakfile->name << "!" << std::endl;
					break;
				}
			}

			m_cookState->done = true;
			m_cookState->waiting.Open();
			{
				Lock L(m_cookState->mutex);
				while (m_cookState->complete) {
					CookCommand *next = m_cookState->complete->next;
					delete m_cookState->complete;
					m_cookState->complete = next;
				}
				while (m_cookState->pending) {
					CookCommand *next = m_cookState->pending->next;
					delete m_cookState->pending;
					m_cookState->pending = next;
				}
			}
			m_cookThreads.clear(); // destroy cook threads.

			r::Material::EndCook();

			if (r == SR_Success)
				r = BuildPakFiles(ptargets, compression);
		}
	}

	xtime::TimeVal totalTime = xtime::ReadMilliseconds() - startTime;
	UReg days, hours, minutes;
	FReg seconds;

	xtime::MilliToDayHourSecond(totalTime, &days, &hours, &minutes, &seconds);

	if (r == SR_Success) {
		out << "Finished in " << (days*24+hours) << " hour(s), " << minutes << " minute(s), " << seconds << " second(s)" << std::endl;
	} else {
		out << "Finished WITH ERRORS in " << (days*24+hours) << " hour(s), " << minutes << " minute(s), " << seconds << " second(s)" << std::endl;
	}

	m_cookState.reset();

	return r;
}

int PackageMan::CookPakFileSources(
	const CookPakFile::Ref &pakfile,
	int flags,
	std::ostream &out
) {
	int pflags = flags&P_AllTargets;
	std::set<int> cooked;

	for (StringVec::const_iterator it = pakfile->roots.begin(); it != pakfile->roots.end(); ++it) {
		if (m_cancelCook) {
			Lock L(m_cookState->ioMutex);
			*m_cookState->cout << "ERROR: cook cancelled..." << std::endl;
			return SR_ErrorGeneric;
		}

		Asset::Ref asset = Resolve((*it).c_str, Z_Cooker);
		if (!asset) {
			Lock L(m_cookState->ioMutex);
			out << "ERROR: resolving " << *it << "!" << std::endl;
			return SR_FileNotFound;
		}

		if (m_cookState->cooked.find(asset->id) != m_cookState->cooked.end())
			continue; // already cooked

		m_cookState->cooked.insert(asset->id);
		cooked.insert(asset->id);

		Cooker::Ref cooker = CookerForAsset(asset, pakfile);
		if (!cooker)
			return SR_ErrorGeneric; 

		CookStatus status = cooker->Status(flags);
		if (status == CS_Error) {
			out << "Error: cooker status for " << *it << " returned error!" << std::endl;
			return SR_CompilerError;
		}

		if (status == CS_NeedRebuild) {

			bool queued = false;

			if (m_cookState->numThreads > 0) {
				if (asset->type != asset::AT_Material) {
					queued = true;
					Lock L(m_cookState->mutex);
					if (m_cookState->error != SR_Success)
						return m_cookState->error;

					CookCommand *cmd = new (ZPackages) CookCommand();
					cmd->flags = flags;
					cmd->result = 0;
					cmd->name = *it;
					cmd->cooker = cooker;

					cmd->next = m_cookState->pending;
					m_cookState->pending = cmd;
					++m_cookState->numPending;
					m_cookState->waiting.Open();
					m_cookState->finished.Close();
				}
			} 

			if (!queued) {
				{
					Lock L(m_cookState->ioMutex);
					out << (*it) << ": Cooking..." << std::endl;
				}

				int r;

				try {
					r = cooker->Cook(flags);
				} catch (exception &e) {
					Lock L(m_cookState->ioMutex);
					out << "ERROR: exception '" << e.type() << "' msg: '" << (e.what()?e.what():"no message") << "' occured!" << std::endl;
					r = SR_IOError;
				}

				cooker->m_asset.reset();

				if (r != SR_Success) {
					Lock L(m_cookState->ioMutex);
					out << "ERROR: " << ErrorString(r) << std::endl;
					return r;
				}
			}

		} else if (status == CS_UpToDate) {
			Lock L(m_cookState->mutex);
			if (m_cookState->error != SR_Success)
				return m_cookState->error;

			cooker->LoadImports();
			Lock L2(m_cookState->ioMutex);
			out << (*it) << ": Up to date." << std::endl;
		} else {
			Lock L(m_cookState->mutex);
			if (m_cookState->error != SR_Success)
				return m_cookState->error;
		}
	}

	if (m_cookState->numThreads > 0) {
		m_cookState->finished.Wait();
	
		if (m_cookState->error != SR_Success)
			return m_cookState->error;
	
		if (m_cancelCook) {
			*m_cookState->cout << "ERROR: cook cancelled..." << std::endl;
			return SR_ErrorGeneric;
		}

		RAD_VERIFY(m_cookState->numPending == 0);
		RAD_VERIFY(m_cookState->pending == 0);
		while (m_cookState->complete) {
			CookCommand *next = m_cookState->complete->next;
			delete m_cookState->complete;
			m_cookState->complete = next;
		}
	}

	// follow imports...
	std::set<int> imported(cooked);
	bool imports = true;

	while (imports) {
		if (m_cancelCook) {
			Lock L(m_cookState->ioMutex);
			*m_cookState->cout << "ERROR: cook cancelled..." << std::endl;
			return SR_ErrorGeneric;
		}

		imports = false;

		std::set<int> added;

		for (std::set<int>::const_iterator it = imported.begin(); it != imported.end(); ++it) {
			if (m_cancelCook) {
				Lock L(m_cookState->ioMutex);
				*m_cookState->cout << "ERROR: cook cancelled..." << std::endl;
				return SR_ErrorGeneric;
			}

			const Cooker::Ref &cooker = m_cookState->cookers[*it];
			RAD_ASSERT(cooker);
			
			for (Cooker::ImportVec::const_iterator it2 = cooker->m_imports.begin(); it2 != cooker->m_imports.end(); ++it2) {
				
				if (m_cancelCook) {
					Lock L(m_cookState->ioMutex);
					*m_cookState->cout << "ERROR: cook cancelled..." << std::endl;
					return SR_ErrorGeneric;
				}

				const Cooker::Import &imp = *it2;
				
				Asset::Ref asset = Resolve(imp.path.c_str, Z_Cooker);
				if (!asset) {
					Lock L(m_cookState->ioMutex);
					out << "ERROR: resolving import " << imp.path << " referenced from " << cooker->m_assetPath << "!" << std::endl;
					return SR_FileNotFound;
				}

				if (m_cookState->cooked.find(asset->id)==m_cookState->cooked.end()) {
					imports = true;

					added.insert(asset->id);
					cooked.insert(asset->id);
					m_cookState->cooked.insert(asset->id);

					CookPakFile::Ref importPakFile;
					{
						CookPakFile::IdMap::iterator it = m_cookState->assetPakFiles.find(asset->id);
						if (it != m_cookState->assetPakFiles.end()) {
							importPakFile = it->second;
						} else { // inherited
							importPakFile = pakfile;
							m_cookState->assetPakFiles[asset->id] = pakfile;
						}
					}

					Cooker::Ref impCooker = CookerForAsset(asset, importPakFile);

					if (!impCooker)
						return SR_ErrorGeneric;

					CookStatus status = impCooker->Status(flags);
					if (status == CS_Error) {
						out << "Error: cooker status for " << imp.path << " returned error!" << std::endl;
						return SR_CompilerError;
					}

					if (status == CS_NeedRebuild) {
						bool queued = false;

						if (m_cookState->numThreads > 0) {
							if (asset->type != asset::AT_Material) {
								queued = true;
								Lock L(m_cookState->mutex);
								if (m_cookState->error != SR_Success)
									return m_cookState->error;

								CookCommand *cmd = new (ZPackages) CookCommand();
								cmd->flags = flags;
								cmd->result = 0;
								cmd->name = imp.path;
								cmd->cooker = impCooker;

								cmd->next = m_cookState->pending;
								m_cookState->pending = cmd;
								++m_cookState->numPending;
								m_cookState->waiting.Open();
								m_cookState->finished.Close();
							}
						}
							
						if (!queued) {
							{
								Lock L(m_cookState->ioMutex);
								out << imp.path << ": Cooking..." << std::endl;
							}

							int r;
							try {
								r = impCooker->Cook(flags);
							} catch (exception &e) {
								Lock L(m_cookState->ioMutex);
								out << "ERROR: exception '" << e.type() << "' msg: '" <<(e.what()?e.what():"no message") << "' occured!" << std::endl;
								r = SR_IOError;
							}

							impCooker->m_asset.reset();

							if (r != SR_Success) {
								Lock L(m_cookState->ioMutex);
								out << "ERROR: " << ErrorString(r) << std::endl;
								return r;
							}
						}
					}
					else if (status == CS_UpToDate) {
						Lock L(m_cookState->mutex);
						if (m_cookState->error != SR_Success)
							return m_cookState->error;
						impCooker->LoadImports();
						Lock L2(m_cookState->ioMutex);
						out << imp.path << ": Up to date." << std::endl;
					} else {
						Lock L(m_cookState->mutex);
						if (m_cookState->error != SR_Success)
							return m_cookState->error;
					}
				}
			}
		}

		if (m_cookState->numThreads > 0) {
			m_cookState->finished.Wait();
	
			if (m_cookState->error != SR_Success)
				return m_cookState->error;
	
			if (m_cancelCook) {
				*m_cookState->cout << "ERROR: cook cancelled..." << std::endl;
				return SR_ErrorGeneric;
			}

			RAD_VERIFY(m_cookState->numPending == 0);
			RAD_VERIFY(m_cookState->pending == 0);
	
			while (m_cookState->complete) {
				CookCommand *next = m_cookState->complete->next;
				delete m_cookState->complete;
				m_cookState->complete = next;
			}
		}

		// add each level of imports.
		imported.swap(added);
	}

	return SR_Success;
}

int PackageMan::BuildPakFiles(int flags, int compression) {
	int r = BuildPackageData();
	if (r != SR_Success)
		return r;
	r = BuildManifest(compression);
	if (r != SR_Success)
		return r;

	for (int i = P_FirstTarget; i <= P_LastTarget; i <<= 1) {
		if (!(i&flags))
			continue;
		for (CookPakFile::Map::const_iterator it = m_cookState->pakfiles.begin(); it != m_cookState->pakfiles.end(); ++it) {
			r = BuildPakFile(it->second->name.c_str, compression);
			if (r != SR_Success)
				return r;
		}
	}

	return SR_Success;
}

int PackageMan::BuildPakFile(const char *name, int compression) {
	const String filename(CStr(name) + CStr(".pak"));
	const String filepath(m_cookState->targetPath + CStr("/Base/") + filename);
	*m_cookState->cout << "------ Packaging '" << filepath << "' ------" << std::endl;

	String nativePath;
		
	if (!m_engine.sys->files->ExpandToNativePath(
		filepath.c_str,
		nativePath
	)) {
		*m_cookState->cout << "ERROR expanding path '" << filepath << "'" << std::endl;
		return SR_IOError;
	}

	FILE *fp = fopen(nativePath.c_str, "wb");

	if (!fp) {
		*m_cookState->cout << "ERROR opening '" << nativePath << "'" << std::endl;
		return SR_IOError;
	}

	file::FILEOutputBuffer ob(fp);
	stream::LittleOutputStream os(ob);
	data_codec::lmp::Writer lumpWriter;

	lumpWriter.Begin(file::kDPakSig, file::kDPakMagic, os);

	const String kPakDir(m_cookState->targetPath + CStr("/Pak/") + name);
	
	int r = PakDirectory(kPakDir.c_str, "Cooked/", compression, lumpWriter);
	if (r != SR_Success) {
		fclose(fp);
		return r;
	}

	U32 numLumps = lumpWriter.NumLumps();
	lumpWriter.SortLumps();
	lumpWriter.End();
	fclose(fp);

	*m_cookState->cout << "Wrote " << numLumps << " file(s)." << std::endl;

	if (numLumps < 1) {
		*m_cookState->cout << "WARNING: removing empty pak file '" << nativePath << "'" << std::endl;
		m_engine.sys->files->DeleteFile(nativePath.c_str, file::kFileOption_NativePath);
	}

	return SR_Success;
}

int PackageMan::PakDirectory(
	const char *_path, 
	const char *prefix,
	int compression, 
	data_codec::lmp::Writer &lumpWriter
) {
	RAD_ASSERT(_path);

	String path(CStr(_path));

	file::FileSearch::Ref s = m_engine.sys->files->OpenSearch(
		(path + "/*.*").c_str,
		file::kSearchOption_Recursive,
		file::kFileOptions_None,
		file::kFileMask_Base
	);
	
	if (!s)
		return SR_Success;

	String filename;
	while (s->NextFile(filename)) {
		if ((filename.StrStr(".svn/") != -1) || (filename.StrStr(".cvs/") != -1) || (filename.StrStr(".git/") != -1))
			continue;
				
		String fpath(path + "/" + filename);
		
		String lumpName(CStr(prefix));
		lumpName += filename;

		*m_cookState->cout << lumpName << "... " << std::flush;

		String nativePath;
		m_engine.sys->files->ExpandToNativePath(fpath.c_str, nativePath, file::kFileMask_Base);

		FILE *fp = fopen(nativePath.c_str, "rb");
		if (!fp) {
			*m_cookState->cout << std::endl << "ERROR failed to load '" << nativePath << "'!" << std::endl;
			return SR_IOError;
		}

		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		if (size < 1) {
			fclose(fp);
			*m_cookState->cout << "(SKIPPING ZERO LENGTH FILE)!" << std::endl;
			continue;
		}

		void *data = safe_zone_malloc(ZPackages, size);
		if (fread(data, 1, size, fp) != size) {
			zone_free(data);
			fclose(fp);
			*m_cookState->cout << std::endl << "ERROR failed to read " << size << " byte(s) from '" << nativePath << "'!" << std::endl;
			return SR_IOError;
		}

		fclose(fp);

		bool compressed = false;

		if (compression) {
			AddrSize zipSize = data_codec::zlib::PredictEncodeSize(size);
			void *zipData = safe_zone_malloc(ZPackages, zipSize);

			if (!data_codec::zlib::Encode(data, size, compression, zipData, &zipSize)) {
				zone_free(data);
				zone_free(zipData);
				*m_cookState->cout << "ERROR compression failure!" << std::endl;
				return SR_IOError;
			}

			if (zipSize < size) {
				// compression is good
				data_codec::lmp::Writer::Lump *l = 
					lumpWriter.WriteLump(lumpName.c_str, zipData, zipSize, 16);
				data_codec::lmp::LOfs *uncSize = (data_codec::lmp::LOfs*)l->AllocateTagData(sizeof(data_codec::lmp::LOfs));
				*uncSize = (data_codec::lmp::LOfs)size;

				float ratio = (1.0f - ((float)zipSize / (float)size)) * 100.0f;
				char nums[16];
				sprintf(nums, "%.1f", ratio);
				*m_cookState->cout << "(" << nums << "%)" << std::endl;

				compressed = true;
			}

			zone_free(zipData);
		}

		if (!compressed) {
			lumpWriter.WriteLump(lumpName.c_str, data, size, 16);
			*m_cookState->cout << "(0%)" << std::endl;
		}

		zone_free(data);
	}

	return SR_Success;
}

int PackageMan::LoadCookTxt(int flags, std::ostream &out) {
	file::MMFileInputBuffer::Ref ib = m_engine.sys->files->OpenInputBuffer("@r:/cook.txt", ZPackages);
	if (!ib) {
		out << "ERROR: cannot open @r:/cook.txt" << std::endl;
		return SR_FileNotFound;
	}
				
	const String kOpenBrace(CStr("{"));
	const String kCloseBrace(CStr("}"));
	const String kManifest(CStr("manifest"));

	Tokenizer script(ib);
	for (;;) {

		String name;

		if (!script.GetToken(name))
			break;

		String lowerName(name);
		lowerName.Lower();

		if (m_cookState->pakfiles.find(lowerName) != m_cookState->pakfiles.end()) {
			out << "ERROR: cook.txt pak file '" << name << "' has already been defined, line: " << script.line.get() << std::endl;
			return SR_ParseError;
		}

		if (lowerName == kManifest) {
			out << "ERROR: cook.txt pak file named 'manifest' is reserved, line: " << script.line.get() << std::endl;
			return SR_ParseError;
		}

		String arg;
		if (!script.GetToken(arg, Tokenizer::kTokenMode_SameLine)) {
			out << "ERROR: cook.txt expected targets or '{' on line: " << script.line.get() << std::endl;
			return SR_ParseError;
		}

		int plats = 0;

		if (arg != kOpenBrace) {
			// semi-colon delimited targets
			StringVec platStrings = arg.Split(";");
			for (StringVec::const_iterator it = platStrings.begin(); it != platStrings.end(); ++it) {
				const String &platform = *it;
				int platFlag = PlatformFlagsForName(platform.c_str);
				if (platFlag == 0) {
					out << "ERROR: cook.txt unrecognized platform '" << platform << "' specified on line: " << script.line.get() << std::endl;
					return SR_ParseError;
				}
				plats |= platFlag;
			}
		} else {
			plats = flags&P_AllTargets;
		}

		if (plats&flags) {

			CookPakFile::Ref pakfile(new (ZPackages) CookPakFile());
			pakfile->name = name;

			for (;;) {
				String token;
				if (!script.GetToken(token)) {
					out << "ERROR: unexpected end of file in cook.txt while looking for '}' in package '" << name << "'" << std::endl;
					return SR_ParseError;
				}
				if (token == kCloseBrace)
					break;
				pakfile->roots.push_back(token);
			}

			m_cookState->pakfiles[lowerName] = pakfile;

		} else {
			// skip this package, it's not used.
			String token;
			for (;;) {
				if (!script.GetToken(token)) {
					out << "ERROR: unexpected end of file in cook.txt while looking for '}' in package '" << name << "'" << std::endl;
					return SR_ParseError;
				}

				if (token == kCloseBrace)
					break;
			}
		}
	}

	return SR_Success;
}

int PackageMan::LuaChunkWrite(lua_State *L, const void *p, size_t size, void *ud) {
	return reinterpret_cast<stream::OutputStream*>(ud)->Write(p, (stream::SPos)size, 0) == false;
}

namespace {
struct ScriptBinFile {
	typedef boost::shared_ptr<ScriptBinFile> Ref;
	typedef zone_vector<Ref, ZToolsT>::type Vec;

	ScriptBinFile() : ob(ZTools) {}

	String filename;
	String cname;
	stream::DynamicMemOutputBuffer ob;
	int len;
};
}

int PackageMan::CompileScripts() {
	const String basePath(CStr("@r:/Source/Scripts"));
	file::FileSearch::Ref s = m_engine.sys->files->OpenSearch(
		(basePath + "/*.lua").c_str,
		file::kSearchOption_Recursive,
		file::kFileOptions_None,
		file::kFileMask_Base
	);
	
	if (!s)
		return SR_Success;

	ScriptBinFile::Vec files;

	lua::State::Ref L(new lua::State("scripts"));

	String filename;
	while (s->NextFile(filename)) {
		*m_cookState->cout << "Compiling " << filename << "..." << std::endl;

		String filePath = basePath + "/" + filename;
		file::MMapping::Ref mm = m_engine.sys->files->MapFile(filePath.c_str, ZTools);
		if (!mm) {
			*m_cookState->cout << "ERROR reading \"" << filePath << "\"" << std::endl;
			return SR_FileNotFound;
		}

		if (luaL_loadbuffer(L->L, (const char*)mm->data.get(), mm->size, filename.c_str) != 0) {
			*m_cookState->cout << "ERROR compiling \"" << filePath << "\"" << lua_tostring(L->L, -1) << std::endl;
			return SR_CompilerError;
		}

		ScriptBinFile::Ref binFile(new ScriptBinFile());
		binFile->filename = file::SetFileExtension(filename.c_str, 0);
		binFile->cname = "s_luaBinFile_" + binFile->filename;
		binFile->cname.Replace('/', '_');
		stream::OutputStream os(binFile->ob);
		
		if (lua_dump(L->L, LuaChunkWrite, &os) != 0) {
			*m_cookState->cout << "ERROR converting \"" << filePath << "\" to binfile." << std::endl;
			return SR_CompilerError;
		}

		files.push_back(binFile);
		lua_pop(L->L, 1); // pop function off the stack.
	}

	L.reset(); // don't need lua anymore.

	// Dump binary to C++ code to be included in golden & ship builds (i.e. things that run from pak data).
	FILE *fp = m_engine.sys->files->fopen("@r:/Source/Scripts/CompiledScripts.cpp", "wt");
	if (!fp) {
		*m_cookState->cout << "ERROR opening @r:/Source/Scripts/CompiledScripts.cpp for write." << std::endl;
		return SR_IOError;
	}

	xtime::TimeDate curTime = xtime::TimeDate::Now(xtime::TimeDate::local_time_tag);

	fprintf(fp, "// @r:/Source/Scripts/CompiledScripts.cpp\n");
	fprintf(fp, "// Auto-generated by Radiance package cooker on %d/%d/%d @ %02d:%02d:%02d\n",
		curTime.month, curTime.dayOfMonth, curTime.year,
		curTime.hour, curTime.minute, curTime.second
	);
	
	fprintf(fp, "// Contents:\n");

	for (ScriptBinFile::Vec::const_iterator it = files.begin(); it != files.end(); ++it) {
		const ScriptBinFile::Ref &binFile = *it;
		fprintf(fp, "// %s.lua\n", binFile->filename.c_str.get());
	}

	fprintf(fp, "\n\n");
	fprintf(fp, "namespace {\n\n");

	for (ScriptBinFile::Vec::const_iterator it = files.begin(); it != files.end(); ++it) {
		const ScriptBinFile::Ref &binFile = *it;

		const U8 *data = (const U8*)binFile->ob.OutputBuffer().Ptr();
		binFile->len  = (int)binFile->ob.OutputBuffer().OutPos();

		fprintf(fp, "static const U8 %s[%d] = {", binFile->cname.c_str.get(), binFile->len);

		for (int i = 0; i < binFile->len; ++i) {
			// newline?
			if ((i%16) == 0) {
				fprintf(fp, "\n\t");
			}
			
			fprintf(fp, "0x%02x", data[i]);
			if (i < binFile->len-1)
				fprintf(fp, ", ");
		}

		binFile->ob.FreeMemory();
		fprintf(fp, "\n};\n\n");
	}

	fprintf(fp, "} // namespace\n\n");
	fprintf(fp, "lua::SrcBuffer::Ref WorldLua::ImportLoader::Load(lua_State *L, const char *name) {\n\n");

	for (ScriptBinFile::Vec::const_iterator it = files.begin(); it != files.end(); ++it) {
		const ScriptBinFile::Ref &binFile = *it;

		fprintf(fp, "\tif (!string::cmp(name, \"%s\")) {\n", binFile->filename.c_str.get());
		fprintf(fp, "\t\treturn lua::SrcBuffer::Ref(new lua::BlobSrcBuffer(\"%s.lua\", %s, %d));\n", 
			binFile->filename.c_str.get(), 
			binFile->cname.c_str.get(),
			(int)binFile->len
		);
		fprintf(fp, "\t}\n");
	}

	fprintf(fp, "\n\treturn lua::SrcBuffer::Ref();\n}\n");
	fclose(fp);

	return SR_Success;
}

bool PackageMan::LoadTagFile(const Cooker::Ref &cooker, void *&data, AddrSize &size) {
	String path = cooker->TagPath();
	String nativePath;

	m_engine.sys->files->ExpandToNativePath(path.c_str, nativePath, file::kFileMask_Base);
	FILE *fp = fopen(nativePath.c_str, "rb");
	if (!fp)
		return false;

	fseek(fp, 0, SEEK_END);
	size = (AddrSize)ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	if (size < 1) {
		fclose(fp);
		return false;
	}

	data = safe_zone_malloc(ZPackages, size);
	if (fread(data, 1, (size_t)size, fp) != (AddrSize)size) {
		fclose(fp);
		zone_free(data);
		return false;
	}

	fclose(fp);
	return true;
}

bool PackageMan::CreateTagData(const Cooker::Ref &cooker, TagData *&data, AddrSize &size) {
	RAD_ASSERT(cooker);

	AddrSize ofs = sizeof(TagData);
	if (!cooker->m_imports.empty())
		ofs += sizeof(U16)*(cooker->m_imports.size()-1);
	
	TagData *tag = (TagData*)safe_zone_calloc(ZPackages, 1, ofs);
	void *file;
	AddrSize fileSize;

	tag->type = (U16)cooker->m_type;

	if (LoadTagFile(cooker, file, fileSize)) {
		tag = (TagData*)safe_zone_realloc(ZPackages, tag, ofs+fileSize);
		tag->ofs = (U32)ofs;
		memcpy(((U8*)tag)+tag->ofs, file, fileSize);
		ofs += fileSize;
		zone_free(file);
	}

	data = tag;
	size = ofs;

	return true;
}

int PackageMan::CookPackage::AddImport(const char *path) {
	RAD_ASSERT(path);

	for (StringVec::const_iterator it = imports.begin(); it != imports.end(); ++it) {
		if ((*it) == path)
			return (int)(it-imports.begin());
	}

	imports.push_back(String(path));
	return (int)(imports.size()-1);
}

int PackageMan::BuildPackageData() {

	CookPackage::Map packages;

	// gather packages.
	for (CookerMap::const_iterator it = m_cookState->cookers.begin(); it != m_cookState->cookers.end(); ++it) {
		if (m_cancelCook) {
			*m_cookState->cout << "ERROR cook cancelled..." << std::endl;
			return SR_ErrorGeneric;
		}

		const Package::Ref &pkg = it->second->m_pkg;

		CookPackage::Map::iterator mit = packages.find(pkg.get());
		if (mit == packages.end()) {
			CookPackage cp;
			cp.pkg = pkg;
			cp.cookers.push_back(it->second);
			packages.insert(CookPackage::Map::value_type(pkg.get(), cp));
		} else {
			mit->second.cookers.push_back(it->second);
		}
	}

	// generate a LuMP file for each package which contains the
	// asset directory and tags
	for (CookPackage::Map::iterator it = packages.begin(); it != packages.end(); ++it) {
		
		if (m_cancelCook) {
			*m_cookState->cout << "ERROR cook cancelled..." << std::endl;
			return SR_ErrorGeneric;
		}

		CookPackage &pkg = it->second;
		data_codec::lmp::Writer lmpWriter;

		String path(m_cookState->targetPath + CStr("/Packages/"));
		path += pkg.pkg->name;
		path += ".lump";

		String nativePath;
		m_engine.sys->files->ExpandToNativePath(path.c_str, nativePath, file::kFileMask_Base);
		
		FILE *fp = fopen(nativePath.c_str, "wb");
		if (!fp) {
			*m_cookState->cout << "ERROR failed to open '" << path << "!" << std::endl;
			return SR_ErrorGeneric;
		}

		file::FILEOutputBuffer ob(fp);
		stream::LittleOutputStream os(ob);

		lmpWriter.Begin(LumpSig, LumpId, os);

		for (Cooker::Vec::const_iterator ait = pkg.cookers.begin(); ait != pkg.cookers.end(); ++ait) {
			
			if (m_cancelCook) {
				fclose(fp);
				*m_cookState->cout << "ERROR cook cancelled..." << std::endl;
				return SR_ErrorGeneric;
			}

			TagData *data;
			AddrSize size;
			const Cooker::Ref &cooker = *ait;

			if (!CreateTagData(cooker, data, size)) {
				fclose(fp);
				return SR_ErrorGeneric;
			}

			// write zero length lump
			data_codec::lmp::Writer::Lump *l = lmpWriter.WriteLump(cooker->m_assetName.c_str, 0, 0, 4);
			if (!l) {
				*m_cookState->cout << "ERROR writing lump '" << cooker->m_assetName << "' to '" << path << "!" << std::endl;
				fclose(fp);
				return SR_ErrorGeneric;
			}

			if (size) {
				// map imports.
				int importNum = 0;
				data->numImports = (U16)cooker->m_imports.size();
				for (Cooker::ImportVec::const_iterator impIt = cooker->m_imports.begin(); impIt != cooker->m_imports.end(); ++impIt, ++importNum) {
					data->imports[importNum] = (U16)pkg.AddImport((*impIt).path.c_str);
				}

				void *tag = l->AllocateTagData((data_codec::lmp::LOfs)size);
				if (!tag) {
					*m_cookState->cout << "ERROR allocating " << size << " byte lump for '" << cooker->m_assetPath << "' in file '" << path << "!" << std::endl;
					fclose(fp);
					return SR_ErrorGeneric;
				}
				memcpy(tag, data, size);
				zone_free(data);
			}
		}

		// import table
		{
			AddrSize importTagSize = sizeof(U16);
			for (StringVec::const_iterator it = pkg.imports.begin(); it != pkg.imports.end(); ++it) {
				importTagSize += (*it).numBytes + 1 + sizeof(U16);
			}

			data_codec::lmp::Writer::Lump *l = lmpWriter.WriteLump("@imports", 0, 0, 4);

			if (!l) {
				*m_cookState->cout << "ERROR writing lump '@imports' to '" << path << "!" << std::endl;
				fclose(fp);
				return SR_ErrorGeneric;
			}

			U8 *tag = (U8*)l->AllocateTagData((data_codec::lmp::LOfs)importTagSize);
			if (!tag) {
				*m_cookState->cout << "ERROR allocating " << importTagSize << " byte lump for '@imports' in file '" << path << "!" << std::endl;
				fclose(fp);
				return SR_ErrorGeneric;
			}

			*reinterpret_cast<U16*>(tag) = (U16)pkg.imports.size();
			tag += sizeof(U16);

			for (StringVec::const_iterator it = pkg.imports.begin(); it != pkg.imports.end(); ++it) {
				*reinterpret_cast<U16*>(tag) = (U16)(*it).numBytes+1;
				tag += sizeof(U16);
				memcpy(tag, (*it).c_str, (*it).numBytes+1);
				tag += (*it).numBytes+1;
			}
		}

		lmpWriter.SortLumps();
		lmpWriter.End();
		fclose(fp);
	}

	return SR_Success;
}

int PackageMan::BuildManifest(int compression) {
	const String filepath(m_cookState->targetPath + CStr("/Base/manifest.pak"));
	*m_cookState->cout << "------ Packaging '" << filepath << "' ------" << std::endl;

	String nativePath;
		
	if (!m_engine.sys->files->ExpandToNativePath(
		filepath.c_str,
		nativePath
	)) {
		*m_cookState->cout << "ERROR expanding path '" << filepath << "'" << std::endl;
		return SR_IOError;
	}

	FILE *fp = fopen(nativePath.c_str, "wb");

	if (!fp) {
		*m_cookState->cout << "ERROR opening '" << nativePath << "'" << std::endl;
		return SR_IOError;
	}

	file::FILEOutputBuffer ob(fp);
	stream::LittleOutputStream os(ob);
	data_codec::lmp::Writer lumpWriter;

	lumpWriter.Begin(file::kDPakSig, file::kDPakMagic, os);

	const String kPakDir(m_cookState->targetPath + CStr("/Packages/"));
	const String kShaderDir(m_cookState->targetPath + CStr("/Shaders/"));
	
	int r = PakDirectory(kPakDir.c_str, "Packages/", compression, lumpWriter);
	if (r != SR_Success) {
		fclose(fp);
		return r;
	}
	
	r = PakDirectory(kShaderDir.c_str, "Shaders/", compression, lumpWriter);
	if (r != SR_Success) {
		fclose(fp);
		return r;
	}

	U32 numLumps = lumpWriter.NumLumps();
	lumpWriter.SortLumps();
	lumpWriter.End();
	fclose(fp);

	*m_cookState->cout << "Wrote " << numLumps << " file(s)." << std::endl;

	if (numLumps < 1) {
		*m_cookState->cout << "ERROR: manifest.pak is empty!'" << std::endl;
		return SR_CompilerError;
	}

	return SR_Success;
}

void PackageMan::CancelCook() {
	m_cancelCook = true;
}

void PackageMan::ResetCancelCook() {
	m_cancelCook = false;
}

Cooker::Ref PackageMan::CookerForAsset(const Asset::Ref &asset, const CookPakFile::Ref &pakfile) {
	{
		CookerMap::iterator it = m_cookState->cookers.find(asset->id);
		if (it != m_cookState->cookers.end()) {
			it->second->m_asset = asset; // re-attach
			return it->second;
		}
	}

	TypeCookerFactoryMap::iterator it = m_cookerFactoryMap.find(asset->type);
	if (it == m_cookerFactoryMap.end()) {
		*m_cookState->cout << "ERROR: No cooker for asset type: " << asset::TypeString(asset->type) << std::endl;
		return Cooker::Ref();
	}

	Cooker::Ref cooker(it->second->New());
	m_cookState->cookers[asset->id] = cooker;
	pakfile->cookers[asset->id] = cooker;

	// We cache alot of this information because the
	// asset may get free'd during cook.
	cooker->m_cout = m_cookState->cout;
	cooker->m_languages = m_cookState->languages;
	cooker->m_asset = asset;
	cooker->m_pkgMan = this;
	cooker->m_assetPath = asset->path.get();
	cooker->m_assetName = asset->name.get();
	cooker->m_pkg = asset->pkg;
	cooker->m_type = asset->type;
	cooker->m_cooking = true;
	cooker->m_basePath = m_cookState->targetPath;
	cooker->m_globalsPath = m_cookState->targetPath + CStr("/Globals/");
	cooker->m_tagsPath = m_cookState->targetPath + CStr("/Tags/");
	cooker->m_filePath = m_cookState->targetPath + CStr("/Pak/") + pakfile->name + CStr("/");
	cooker->m_pakfile = pakfile->name;
	cooker->m_originalPakfile = pakfile->name;
	cooker->LoadGlobals();

	return cooker;
}

#endif

bool PackageMan::MakeIntermediateDirs() {

	MakePackageDirs("@r:/Temp/Files/");
	MakePackageDirs("@r:/Temp/Tags/");
	MakePackageDirs("@r:/Temp/Globals/");

	return true;
}

bool PackageMan::MakePackageDirs(const char *prefix) {
	const String kPrefix(CStr(prefix));

	for(Package::Map::const_iterator it = m_packages.begin(); it != m_packages.end(); ++it) {
		String path(kPrefix);
		path += it->second->name;
		if (!m_engine.sys->files->CreateDirectory(path.c_str))
			return false;
	}

	return true;
}

Cooker::Ref PackageMan::AllocateIntermediateCooker(const Asset::Ref &_asset) {
	// clone this asset so we can mess with it.
	Asset::Ref asset = Asset(_asset->id, Z_Unique);
	if (!asset)
		return Cooker::Ref();

	TypeCookerFactoryMap::iterator it = m_cookerFactoryMap.find(asset->type);
	if (it == m_cookerFactoryMap.end()) {
		COut(C_Info) << "ERROR: No cooker for asset type: " << asset::TypeString(asset->type) << std::endl;
		return Cooker::Ref();
	}

	Cooker::Ref cooker(it->second->New());

	cooker->m_cout = &COut(C_Info);
	cooker->m_languages = (1<<(App::Get()->langId.get()));
	cooker->m_asset = asset;
	cooker->m_pkgMan = this;
	cooker->m_assetPath = asset->path.get();
	cooker->m_assetName = asset->name.get();
	cooker->m_pkg = asset->pkg;
	cooker->m_type = asset->type;
	cooker->m_cooking = false;
	cooker->m_globalsPath = CStr("@r:/Temp/Globals/");
	cooker->m_tagsPath = CStr("@r:/Temp/Tags/");
	cooker->m_filePath = CStr("@r:/Temp/Files/");
	cooker->LoadGlobals();

	return cooker;
}

void Cooker::ResetPakFile() {
	this->pakfile = m_originalPakfile.c_str;
}

void Cooker::RAD_IMPLEMENT_SET(pakfile) (const char *value) {
	RAD_ASSERT(value);
	m_pakfile = value;
	m_filePath = m_basePath + CStr("/Pak/") + m_pakfile + CStr("/");
}

int Cooker::Cook(int flags) {
	RAD_ASSERT(flags&P_AllTargets); // must specify a target
	int r = Compile(flags);
	if (r == SR_Success)
		SaveState();
	return r;
}

void Cooker::LoadGlobals() {
	String spath(m_globalsPath);

	spath += m_assetPath;

	String nativePath;
	if (!engine->sys->files->ExpandToNativePath(spath.c_str, nativePath)) {
		m_globals = Persistence::Load(0);
		return;
	}

	FILE *fp = fopen(nativePath.c_str, "rb");
	if (fp) {
		file::FILEInputBuffer ib(fp);
		stream::InputStream is(ib);
		m_globals = Persistence::Load(is);
		fclose(fp);
	} else {
		m_globals = Persistence::Load(0);
	}
}

void Cooker::SaveGlobals() {

	String spath(m_globalsPath);
	spath += m_assetPath;

	String nativePath;
	if (!engine->sys->files->ExpandToNativePath(spath.c_str, nativePath))
		return;

	FILE *fp = fopen(nativePath.c_str, "wb");
	if (fp) {
		file::FILEOutputBuffer ob(fp);
		stream::OutputStream os(ob);
		m_globals->Save(os);
		fclose(fp);
	}
}

void Cooker::LoadImports() {

	if (!m_cooking)
		return;

	m_imports.clear();

	String spath(m_globalsPath);
	spath += m_assetPath;
	spath += ".imports";

	String nativePath;
	if (!engine->sys->files->ExpandToNativePath(spath.c_str, nativePath))
		return;

	FILE *fp = fopen(nativePath.c_str, "rb");
	if (fp) {
		file::FILEInputBuffer ib(fp);
		stream::InputStream is(ib);
		
		U32 tag;
		if (!is.Read(&tag))
			return;
		if (tag != ImportsTag)
			return;
		U32 numStrings;
		if (!is.Read(&numStrings))
			return;

		for (U32 i = 0; i < numStrings; ++i) {
			char buf[256];
			U32 size;

			if (!is.Read(&size))
				return;
			if (is.Read(buf, (stream::SPos)size, 0) != (stream::SPos)size)
				return;
			buf[size] = 0;

			Import imp;
			imp.path = buf;

			m_imports.push_back(imp);
		}

		fclose(fp);
	}
}

void Cooker::SaveImports() {
	if (!m_cooking)
		return;

	String spath(m_globalsPath);
	spath += m_assetPath;
	spath += ".imports";

	String nativePath;
	if (!engine->sys->files->ExpandToNativePath(spath.c_str, nativePath))
		return;

	FILE *fp = fopen(nativePath.c_str, "wb");
	if (fp) {
		file::FILEOutputBuffer ob(fp);
		stream::OutputStream os(ob);
		
		if (!os.Write((U32)ImportsTag))
			return;
		if (!os.Write((U32)m_imports.size()))
			return;

		for (ImportVec::const_iterator it = m_imports.begin(); it != m_imports.end(); ++it) {
			const Import &i = *it;
			if (!os.Write((U32)i.path.numBytes.get()))
				return;
			if (os.Write(i.path.c_str, (stream::SPos)i.path.numBytes.get(), 0) != (stream::SPos)i.path.numBytes.get())
				return;
		}

		fclose(fp);
	}
}

void Cooker::SaveState() {
	SaveGlobals();
	SaveImports();
}

String Cooker::FilePath(const char *path) {
	RAD_ASSERT(path);
	String spath(CStr(path));
	return m_filePath + spath;
}

bool Cooker::FileTime(const char *path, xtime::TimeDate &time) {
	String s = FilePath(path);
	return engine->sys->files->GetFileTime(s.c_str, time);
}

bool Cooker::CopyOutputFile(const char *src, const char *dst) {
	file::MMFileInputBuffer::Ref ib = engine->sys->files->OpenInputBuffer(
		src,
		ZPackages,
		8*kMeg,
		file::kFileOptions_None,
		~file::kFileMask_PakFiles
	);

	if (!ib)
		return false;

	stream::InputStream is(*ib);

	BinFile::Ref of = OpenWrite(dst);
	if (!of)
		return false;

	stream::OutputStream os(of->ob);
	return is.PipeToStream(os, 0, 0, stream::PipeAll) == stream::Success;
}

bool Cooker::CopyOutputBinFile(const char *src) {
	String dst(asset->path);
	dst += ".bin";
	return CopyOutputFile(src, dst.c_str);
}

BinFile::Ref Cooker::OpenWrite(const char *path) {
	String spath = FilePath(path);

	FILE *fp = engine->sys->files->fopen(spath.c_str, "wb");

	if (!fp)
		return BinFile::Ref();
	return BinFile::Ref(new (ZPackages) BinFile(fp));
}

BinFile::Ref Cooker::OpenRead(const char *path) {
	String spath = FilePath(path);
	FILE *fp = engine->sys->files->fopen(spath.c_str, "rb");
	if (!fp)
		return BinFile::Ref();
	return BinFile::Ref(new (ZPackages) BinFile(fp));
}

file::MMapping::Ref Cooker::MapFile(
	const char *path, 
	::Zone &zone
) {
	String spath = FilePath(path);
	return engine->sys->files->MapFile(spath.c_str, zone);
}

String Cooker::TagPath() {

	String spath(m_tagsPath);
	spath += m_assetPath;
	spath += ".tag";
	return spath;
}

BinFile::Ref Cooker::OpenTagWrite() {

	String spath = TagPath();

	FILE *fp = engine->sys->files->fopen(spath.c_str, "wb");
	if (!fp)
		return BinFile::Ref();
	return BinFile::Ref(new (ZPackages) BinFile(fp));
}

BinFile::Ref Cooker::OpenTagRead()
{
	String spath = TagPath();

	FILE *fp = engine->sys->files->fopen(spath.c_str, "rb");
	if (!fp)
		return BinFile::Ref();
	return BinFile::Ref(new (ZPackages) BinFile(fp));
}

file::MMapping::Ref Cooker::LoadTag() {
	String spath = TagPath();
	return engine->sys->files->MapFile(spath.c_str, ZPackages);
}

bool Cooker::HasTag() {
	String spath = TagPath();
	return engine->sys->files->FileExists(spath.c_str);
}

int Cooker::AddImport(const char *path) {
	
	if (!m_cooking)
		return 0;

	RAD_ASSERT(path);
	for (ImportVec::iterator it = m_imports.begin(); it != m_imports.end(); ++it) {
		if (!string::cmp((*it).path.c_str.get(), path)) {
			return (int)(it-m_imports.begin());
		}
	}

	Import imp;
	imp.path = path;
	m_imports.push_back(imp);
	return (int)(m_imports.size()-1);
}

Engine *Cooker::RAD_IMPLEMENT_GET(engine) {
	return App::Get()->engine;
}

void Cooker::UpdateModifiedTime(int target) {
	target &= P_AllTargets;
	String key(TargetPath(target)+"__cookerModifiedTime");
	
	Persistence::KeyValue::Map::iterator it = globals->find(key);
	if (it == globals->end()) {
		(*globals.get())[key].sVal = asset->entry->modifiedTime->ToString();
	} else {
		it->second.sVal = asset->entry->modifiedTime->ToString();
	}
}

int Cooker::CompareVersion(int target, bool updateIfNewer) {
	target &= P_AllTargets;
	String key(TargetPath(target)+"__cookerVersion");

	Persistence::KeyValue::Map::iterator it = globals->find(key);
	if (it == globals->end()) {
		char sz[64];
		string::itoa(version, sz);
		(*globals.get())[key].sVal = String(sz);

		return -1; // we have no version on disk, so the source data has to be newer
	}

	int v = string::atoi(it->second.sVal.c_str.get());

	// this logic may appear backwards but what we are saying here is that
	// if the cached version is less than our current version then the source
	// data on disk must be newer and should be recooked.

	int r = 0;
	if (version > -1) {
		r = (v != version) ? -1 : 0;
	} else { // version of -1 means always rebuild.
		r = -1;
	}
	
	if (r < 0 && updateIfNewer) {
		char sz[64];
		string::itoa(version, sz);
		(*globals.get())[key].sVal = String(sz);
	}

	return r;
}

int Cooker::CompareModifiedTime(int target, bool updateIfNewer) {
	target &= P_AllTargets;
	String key(TargetPath(target)+"__cookerModifiedTime");

	Persistence::KeyValue::Map::iterator it = globals->find(key);
	if (it == globals->end()) {
		(*globals.get())[key].sVal = asset->entry->modifiedTime->ToString();
		return -1; // always older
	}

	TimeDate td = TimeDate::FromString(it->second.sVal.c_str);
	int r = td.Compare(*asset->entry->modifiedTime.get());
	if ((r < 0 && updateIfNewer) || (r > 0)) {
		(*globals.get())[key].sVal = asset->entry->modifiedTime->ToString();
	}

	return r;
}

int Cooker::CompareCachedFileTime(int target, const char *key, const char *path, bool updateIfNewer, bool optional) {
	RAD_ASSERT(key);
	RAD_ASSERT(path);

	target &= P_AllTargets;
	
	String skey(TargetPath(target)+key);
	String skeyFile(skey+"_file");
	String spath(path);
	bool force = false;

	Persistence::KeyValue::Map::const_iterator it = globals->find(skeyFile);
	if (it != globals->end()) {
		if (it->second.sVal != spath)
			force = true; // filename changed!
	}

	(*globals.get())[skeyFile].sVal = spath;

	TimeDate selfTime;
	bool m = TimeForKey(target, key, selfTime);

	TimeDate fileTime;

	engine->sys->files->GetAbsolutePath(path, spath, file::kFileMask_Base);

	if (!engine->sys->files->GetFileTime(spath.c_str, fileTime)) {
		if (optional && !m) // we don't have a record of this either
			return 0;
		if (m) // remove from cache, no longer a record of this file.
			globals->erase(skeyFile);
		return -1; // always older
	}

	int c = m ? selfTime.Compare(fileTime) : -1;
	if (force)
		c = -1;

	if ((c < 0 && updateIfNewer) || (c > 0)) { 
		// this can happen from DST or someone reverted a file version.
		(*globals.get())[skey].sVal = fileTime.ToString();
	}

	return c;
}

int Cooker::CompareCachedFileTimeKey(int target, const char *key, const char *localized, bool updateIfNewer, bool optional) {
	RAD_ASSERT(key);
	target &= P_AllTargets;

	const String *s = asset->entry->KeyValue<String>(key, target);
	if (!s) {
		cout.get() << "WARNING: Cooker(" << asset->path.get() << ") is asking for key '" << key << "' which does not exist or is not a string in target '" << target << "'" << std::endl;
		return -1; // always older (error condition)
	}

	if (!localized)
		return CompareCachedFileTime(target, key, s->c_str, updateIfNewer, optional);

	String sKey(CStr(key));
	String sPath(*s);
	
	// create localized file path.
	String ext = file::GetFileExtension(sPath.c_str);
	String sBasePath = file::SetFileExtension(sPath.c_str, 0);

	int r = 0;

	for (int i = StringTable::LangId_EN; i < StringTable::LangId_MAX; ++i) {
		if (!((1<<i)&languages))
			continue;

		String x, k;

		if (i != StringTable::LangId_EN) {
			x = sBasePath;
			x += "_";
			x += StringTable::Langs[i];
			x += ext;
			k = sKey;
			k += "_cookerLang_";
			k += StringTable::Langs[i];
		} else {
			x = sPath;
			k = sKey;
		}

		// don't early out here just record the newest value. we need to cache all file times for all versions.
		int z = CompareCachedFileTime(target, k.c_str, x.c_str, updateIfNewer, optional);
		r = ((r<z)&&(r!=0)) ? r : z;
	}

	int lr = CompareCachedLocalizeKey(target, localized);
	return ((r<lr)&&(r!=0)) ? r : lr;
}

int Cooker::CompareCachedStringKey(int target, const char *key) {
	RAD_ASSERT(key);
	target &= P_AllTargets;

	const String *s = asset->entry->KeyValue<String>(key, target);
	if (!s) {
		cout.get() << "WARNING: Cooker(" << asset->path.get() << ") is asking for key '" << key << "' which does not exist or is not a string in target '" << target << "'" << std::endl;
		return -1; // key is missing!
	}
	
	return CompareCachedString(target, key, s->c_str);
}

int Cooker::CompareCachedString(int target, const char *key, const char *string) {
	RAD_ASSERT(key);

	target &= P_AllTargets;

	const String skey(TargetPath(target)+key);
	const String sstring(CStr(string));

	Persistence::KeyValue::Map::const_iterator it = globals->find(skey);
	if (it != globals->end()) {
		if (it->second.sVal != sstring) {
			(*globals.get())[skey].sVal = sstring;
			return -1; // changed!
		}
	} else {
		(*globals.get())[skey].sVal = sstring;
		return -1; // doesn't exist!
	}

	return 0;
}

int Cooker::CompareCachedLocalizeKey(int target, const char *key) {
	RAD_ASSERT(key);

	target &= P_AllTargets;

	const bool *b = asset->entry->KeyValue<bool>(key, target);
	if (!b) {
		cout.get() << "WARNING: Cooker(" << asset->path.get() << ") is asking for key '" << key << "' which does not exist or is not a bool in target '" << target << "'" << std::endl;
		return -1; // key is missing!
	}

	if (!(*b))
		return 0; // not localized ignore it.

	String localizedString = LocalizedString(languages);
	String skey(TargetPath(target)+"__cookerLocalizedVersion");
	Persistence::KeyValue::Map::const_iterator it = globals->find(skey);

	if (it != globals->end()) {
		if (it->second.sVal != localizedString) {
			(*globals.get())[skey].sVal = localizedString;
			return -1; // changed!
		}
	} else {
		(*globals.get())[skey].sVal = localizedString;
		return -1; // doesn't exist!
	}

	return 0; // same languages
}

const char *Cooker::TargetString(int target, const char *key) {
	RAD_ASSERT(key);

	target &= P_AllTargets;

	const String skey(TargetPath(target)+key);
	Persistence::KeyValue::Map::const_iterator it = globals->find(skey);
	if (it != globals->end())
		return it->second.sVal.c_str;
	return 0;
}

void Cooker::SetTargetString(int target, const char *key, const char *string) {
	RAD_ASSERT(key);
	RAD_ASSERT(string);

	target &= P_AllTargets;

	const String skey(TargetPath(target)+key);
	(*globals.get())[skey].sVal = String(string);
}

bool Cooker::ModifiedTime(int target, xtime::TimeDate &td) const {
	target &= P_AllTargets;
	return TimeForKey(target, "__cookerModifiedTime", td);
}

bool Cooker::TimeForKey(int target, const char *key, xtime::TimeDate &td) const {
	RAD_ASSERT(key);
	target &= P_AllTargets;
	String skey(TargetPath(target)+key);
	Persistence::KeyValue::Map::const_iterator it = globals->find(skey);
	if (it == globals->end())
		return false;
	td = TimeDate::FromString(it->second.sVal.c_str);
	return true;
}

String Cooker::TargetPath(int target) {
	target &= P_AllTargets;
	const char *sz = pkg::PlatformNameForFlags(target);
	if (!sz)
		return CStr("Generic/");
	return CStr(sz)+"/";
}

String Cooker::LocalizedString(int languages) {
	String s;
	bool sep = false;

	for (int i = StringTable::LangId_EN; i < StringTable::LangId_MAX; ++i) {
		if (!((1<<i)&languages))
			continue;
		if (sep) {
			sep = false;
			s += ";";
		}
		s += CStr(StringTable::Langs[i]).Upper();
		sep = true;
	}

	return s;
}

} // pkg

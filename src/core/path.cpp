#include "core/lumix.h"
#include "core/path.h"

#include "core/blob.h"
#include "core/crc32.h"
#include "core/mt/spin_mutex.h"
#include "core/path_utils.h"
#include "core/string.h"

#include <string>

namespace Lumix
{

	PathManager LUMIX_ENGINE_API g_path_manager;


	PathManager::PathManager()
		: m_paths(m_allocator)
		, m_mutex(false)
	{
		getPath(0, "");
	}


	PathManager::~PathManager()
	{
		ASSERT(m_paths.size() == 1 && m_paths.at(0)->m_ref_count == 1);
	}


	void PathManager::serialize(OutputBlob& serializer)
	{
		MT::SpinLock lock(m_mutex);
		serializer.write((int32_t)m_paths.size());
		for (int i = 0; i < m_paths.size(); ++i)
		{
			serializer.writeString(m_paths.at(i)->m_path);
		}
	}


	void PathManager::deserialize(InputBlob& serializer)
	{
		MT::SpinLock lock(m_mutex);
		int32_t size;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			char path[LUMIX_MAX_PATH];
			serializer.readString(path, sizeof(path));
			uint32_t hash = crc32(path);
			PathInternal* internal = getPathMultithreadUnsafe(hash, path);
			--internal->m_ref_count;
		}
	}


	Path::Path()
	{
		m_data = g_path_manager.getPath(0, "");
	}


	Path::Path(uint32_t hash)
	{
		m_data = g_path_manager.getPath(hash);
		ASSERT(m_data);
	}


	PathInternal* PathManager::getPath(uint32_t hash)
	{
		MT::SpinLock lock(m_mutex);
		int index = m_paths.find(hash);
		if (index < 0)
		{
			return NULL;
		}
		++m_paths.at(index)->m_ref_count;
		return m_paths.at(index);
	}


	PathInternal* PathManager::getPath(uint32_t hash, const char* path)
	{
		MT::SpinLock lock(m_mutex);
		return getPathMultithreadUnsafe(hash, path);
	}


	PathInternal* PathManager::getPathMultithreadUnsafe(uint32_t hash, const char* path)
	{
		int index = m_paths.find(hash);
		if (index < 0)
		{
			PathInternal* internal = m_allocator.newObject<PathInternal>();
			internal->m_ref_count = 1;
			internal->m_id = hash;
			copyString(internal->m_path, sizeof(internal->m_path), path);
			m_paths.insert(hash, internal);
			return internal;
		}
		else
		{
			++m_paths.at(index)->m_ref_count;
			return m_paths.at(index);
		}
	}


	void PathManager::incrementRefCount(PathInternal* path)
	{
		MT::SpinLock lock(m_mutex);
		++path->m_ref_count;
	}


	void PathManager::decrementRefCount(PathInternal* path)
	{
		MT::SpinLock lock(m_mutex);
		--path->m_ref_count;
		if (path->m_ref_count == 0)
		{
			m_paths.erase(path->m_id);
			m_allocator.deleteObject(path);
		}
	}


	Path::Path(const Path& rhs)
		: m_data(rhs.m_data)
	{
		g_path_manager.incrementRefCount(m_data);
	}
	

	Path::Path(const char* path)
	{
		char tmp[LUMIX_MAX_PATH];
		size_t len = strlen(path);
		ASSERT(len < LUMIX_MAX_PATH);
		PathUtils::normalize(path, tmp, (uint32_t)len + 1);
		uint32_t hash = crc32(tmp);
		m_data = g_path_manager.getPath(hash, tmp);
	}


	Path::~Path()
	{
		g_path_manager.decrementRefCount(m_data);
	}


	void Path::operator =(const Path& rhs)
	{
		g_path_manager.decrementRefCount(m_data);
		m_data = rhs.m_data;
		g_path_manager.incrementRefCount(m_data);
	}


	void Path::operator =(const char* rhs)
	{
		char tmp[LUMIX_MAX_PATH];
		size_t len = strlen(rhs);
		ASSERT(len < LUMIX_MAX_PATH);
		PathUtils::normalize(rhs, tmp, (uint32_t)len + 1);
		uint32_t hash = crc32(tmp);

		g_path_manager.decrementRefCount(m_data);
		m_data = g_path_manager.getPath(hash, tmp);
	}


	void Path::operator =(const string& rhs)
	{
		*this = rhs.c_str();
	}
}
#pragma once
#include <cstddef>
#include <memory>
/* Taken from multiblend.  */

#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

#define _MAPALLOC_
namespace colorscreen
{
class MapAlloc {
private:
	MapAlloc();
	~MapAlloc();
	class MapAllocObject {
	public:
		MapAllocObject(size_t _size, const char *reason, int alignment);
		~MapAllocObject();
		void* GetPointer();
		size_t GetSize() { return size; }
		bool IsFile();

	private:
#ifdef _WIN32
		HANDLE file = NULL;
		HANDLE map = NULL;
#else
		int file = 0;
#endif
		void* pointer = NULL;
		size_t size;
	};
	struct DestructionGuard
	{
	  ~DestructionGuard ();
	};
	static DestructionGuard Guard;
	static std::vector<std::unique_ptr<MapAllocObject>> objects;
	static char tmpdir[256];
	static char filename[512];
	static int suffix;
	static size_t cache_threshold;
	static size_t min_file_size;
	static size_t total_allocated;

public:
	static void* Alloc(size_t size, const char *reason, int alignment = 128);
	static void Free(void* p);
	static size_t GetSize(void* p);
	static void CacheThreshold(size_t threshold);
	static void SetTmpdir(const char* _tmpdir);
	static bool LastFile() { return objects.back()->IsFile(); }
//	static bool last_mapped;
};
}

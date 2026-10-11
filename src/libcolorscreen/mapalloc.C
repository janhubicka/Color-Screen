/* Taken from multiblend.  */
#include "mapalloc.h"
#include "include/cache-stats.h"
#ifdef __APPLE__
#define memalign(a,b) malloc((b))
#else
#include <malloc.h>
#endif
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <cstdlib>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#define strcpy_s(a,b) strcpy(a,b)
#endif

#include "include/base.h"
namespace colorscreen
{
static const bool debug = false;
static bool destroyed = false;

MapAlloc::DestructionGuard MapAlloc::Guard;
std::vector<std::unique_ptr<MapAlloc::MapAllocObject>> MapAlloc::objects;
char MapAlloc::tmpdir[256] = "";
char MapAlloc::filename[512];
int MapAlloc::suffix = 0;
size_t MapAlloc::cache_threshold = 1024*1024*1024;
size_t MapAlloc::min_file_size = 1024*1024;
size_t MapAlloc::total_allocated = 0;
std::mutex MapAlloc::mapalloc_lock;

MapAlloc::DestructionGuard::~DestructionGuard ()
{
  destroyed = true;
}

/***********************************************************************
* MapAlloc
***********************************************************************/
void MapAlloc::CacheThreshold(size_t limit) {
	std::lock_guard<std::mutex> guard (mapalloc_lock);
	cache_threshold = limit;
}

void* MapAlloc::Alloc(size_t size, const char *reason, int alignment) {
	std::lock_guard<std::mutex> guard (mapalloc_lock);
	/* A temporary-file failure may throw. Always release the mutex. */
	objects.push_back(std::make_unique <MapAllocObject>(size, reason, alignment));
	return objects.back ()->GetPointer();
}

void MapAlloc::Free(void* p) {
	/* If static destruction already ran; do nothing.  */
	if (destroyed)
	  return;
	std::lock_guard<std::mutex> guard (mapalloc_lock);
	for (auto it = objects.begin(); it < objects.end(); ++it) {
		if ((*it)->GetPointer() == p) {
			objects.erase(it);
			break;
		}
	}
}

size_t MapAlloc::GetSize(void* p) {
	std::lock_guard<std::mutex> guard (mapalloc_lock);
	for (auto it = objects.begin(); it < objects.end(); ++it) {
		if ((*it)->GetPointer() == p) {
			return (*it)->GetSize();
		}
	}
	return 0;
}

/* Change the scratch directory used for FUTURE mmap allocations. PATH is
   an OS-native encoded folder. Empty restores platform default. The
   historic fixed-size array must never truncate a selected path. */
bool
MapAlloc::SetTmpdir (const char *path)
{
  if (!path)
    return false;
  std::string directory (path);
  while (directory.size () > 1
         && (directory.back () == '/' || directory.back () == '\\')
         && !(directory.size () == 3 && directory[1] == ':'))
    directory.pop_back ();
  if (directory.size () >= sizeof (tmpdir))
    return false;
  if (!directory.empty ())
    {
      std::error_code error;
      if (!std::filesystem::is_directory (
              std::filesystem::path (directory), error))
        return false;
    }
  std::lock_guard<std::mutex> guard (mapalloc_lock);
  std::memcpy (tmpdir, directory.c_str (), directory.size () + 1);
  return true;
}

/* Read the configured directory without racing a new mmap allocation. */
std::string
MapAlloc::GetTmpdir ()
{
  std::lock_guard<std::mutex> guard (mapalloc_lock);
  return std::string (tmpdir);
}

/* Frontend-neutral scratch configuration exposed by libcolorscreen. */
bool
set_cache_scratch_directory (const std::string &path)
{
  return MapAlloc::SetTmpdir (path.c_str ());
}

std::string
cache_scratch_directory ()
{
  return MapAlloc::GetTmpdir ();
}

/***********************************************************************
* MapAllocObject
***********************************************************************/
MapAlloc::MapAllocObject::MapAllocObject(size_t _size, const char *reason, int alignment) : size(_size) {
	if (total_allocated + size < cache_threshold || size < min_file_size) {
		if (debug)
		  printf ("Allocating %li bytes for %s, overall allocation: %li\n", (long)_size, reason, (long)(total_allocated + size));
#ifdef _WIN32
		pointer = _aligned_malloc(size, alignment);
#else
		pointer = memalign(alignment, size);
#endif
	}

	if (!pointer) {
#ifdef _WIN32
		if (!tmpdir[0]) {
			GetTempPath(256, tmpdir);
			size_t l = strlen(tmpdir);
			while (tmpdir[l - 1] == '\\' || tmpdir[l - 1] == '/' && l > 0) tmpdir[--l] = 0;
		}

		while (true) {
			sprintf_s(filename, "%s\\_mb%05d.tmp", tmpdir, suffix++);
			file = CreateFile(filename, GENERIC_ALL, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if (file != INVALID_HANDLE_VALUE) break;
			if (GetLastError() != 80) {
				char buf[256];
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					buf, sizeof(buf), NULL);
				sprintf_s(filename, "Could not create temp file in %s\\: %s", tmpdir, buf);
				throw(filename);
			}
			if (suffix == 65536) {
				sprintf_s(filename, "Could not create temp file in %s\\: suffixes exhausted", tmpdir);
				throw(filename);
			}
		}

		map = CreateFileMapping(file, NULL, PAGE_READWRITE, size >> 32, size & 0xffffffff, NULL);
		if (!map) {
			sprintf_s(filename, "Could not allocate %zu temporary bytes in %s", size, tmpdir);
			throw(filename);
		}

		pointer = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (!pointer) {
			sprintf_s(filename, "Could not map view of temporary file");
			throw(filename);
		}
#else
		if (!tmpdir[0]) {
			const char* td = getenv("TMPDIR");
			if (!td || !*td || strlen(td) >= sizeof (tmpdir))
			  td = "/tmp";
			snprintf(tmpdir, sizeof (tmpdir), "%s", td);
		}

		sprintf(filename, "%s/.mbXXXXXX", tmpdir);
		file = mkstemp(filename);

		if (file < 0) {
			sprintf(filename, "Could not create temp file in %s/: %s", tmpdir, strerror(errno));
			throw(filename);
		}

		if (ftruncate(file, size)) {
			const int failure = errno;
			unlink(filename);
			close(file);
			file = -1;
			snprintf(filename, sizeof (filename),
			         "Could not allocate %zu temporary bytes in %s: %s",
			         size, tmpdir, strerror(failure));
			throw(filename);
		}

		if (debug)
		  printf ("Producing tmp file %s for %s of size %liMB\n", filename, reason, (long)size/1024/1024);

		/* These arrays are intentionally DISK-BACKED scratch. MAP_PRIVATE
		   would turn writes into anonymous copy-on-write RAM and could OOM
		   the process during large scans, despite the temporary file. */
		pointer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
		if (pointer == MAP_FAILED) {
			unlink(filename);
			close(file);
			file = -1;
			pointer = NULL;
			snprintf(filename, sizeof (filename),
			         "Could not mmap temporary file: %s", strerror(errno));
			throw(filename);
		}

		unlink(filename);
#endif
	} else {
		total_allocated += size;
	}
}

MapAlloc::MapAllocObject::~MapAllocObject() {
#ifdef _WIN32
	if (!file) {
		if (debug)
		  printf ("Freeing %li bytes\n", (long)size);
		_aligned_free(pointer);
		total_allocated -= size;
	} else {
		if (debug)
		  printf ("Removing file %s of size %liMB\n", filename, (long)size/1024/1024);
		UnmapViewOfFile(pointer);
		CloseHandle(map);
		CloseHandle(file);
	}
#else
	if (file < 0) {
		if (debug)
		  printf ("Freeing %li bytes\n", (long)size);
		free(pointer);
	} else {
		if (debug)
		  printf ("Unmapping file of size %liMB\n", (long)size/1024/1024);
		munmap(pointer, size);
		close(file);
	}
#endif
}

void* MapAlloc::MapAllocObject::GetPointer() {
	return pointer;
}

bool MapAlloc::MapAllocObject::IsFile() {
#ifdef _WIN32
	return !!file;
#else
	return file >= 0;
#endif
}

MapAlloc::~MapAlloc ()
{
	if (debug)
	  printf ("Global destruction\n");
	if (colorscreen_checking)
	  for (auto it = objects.begin(); it < objects.end(); ++it) 
	    printf ("Mapallocated %li bytes during global destruction\n", (long)(*it)->GetSize());
}
}

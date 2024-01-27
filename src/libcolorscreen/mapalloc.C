/* Taken from multiblend.  */
#include "mapalloc.h"
#ifdef __APPLE__
#define memalign(a,b) malloc((b))
#else
#include <malloc.h>
#endif
#include <cstring>
#include <cstdio>

#ifndef _WIN32
#include <cstdlib>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#define strcpy_s(a,b) strcpy(a,b)
#endif

static const int debug = 0;

std::vector<MapAlloc::MapAllocObject*> MapAlloc::objects;
char MapAlloc::tmpdir[256] = "";
char MapAlloc::filename[512];
int MapAlloc::suffix = 0;
size_t MapAlloc::cache_threshold = 1024*1024*10;
size_t MapAlloc::total_allocated = 0;

/***********************************************************************
* MapAlloc
***********************************************************************/
void MapAlloc::CacheThreshold(size_t limit) {
	cache_threshold = limit;
}

void* MapAlloc::Alloc(size_t size, const char *reason, int alignment) {
	MapAllocObject* m = new MapAllocObject(size, reason, alignment);
	objects.push_back(m);
	return m->GetPointer();
}

void MapAlloc::Free(void* p) {
	for (auto it = objects.begin(); it < objects.end(); ++it) {
		if ((*it)->GetPointer() == p) {
			delete (*it);
			objects.erase(it);
			break;
		}
	}
}

size_t MapAlloc::GetSize(void* p) {
	for (auto it = objects.begin(); it < objects.end(); ++it) {
		if ((*it)->GetPointer() == p) {
			return (*it)->GetSize();
		}
	}

	return 0;
}

void MapAlloc::SetTmpdir(const char* _tmpdir) {
	strcpy_s(tmpdir, _tmpdir);
	size_t l = strlen(tmpdir);
	while ((tmpdir[l - 1] == '\\' || tmpdir[l - 1] == '/') && l > 0) tmpdir[--l] = 0;
}

/***********************************************************************
* MapAllocObject
***********************************************************************/
MapAlloc::MapAllocObject::MapAllocObject(size_t _size, const char *reason, int alignment) : size(_size) {
	if (total_allocated + size < cache_threshold) {
		if (debug)
		  printf ("Allocating %li bytes for %s, overall allocation: %li", (long)_size, reason, (long)(total_allocated + size));
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
			char* td = getenv("TMPDIR");
			if (td) {
				strcpy(tmpdir, td);
			} else {
				strcpy(tmpdir, "/tmp");
			}
		}

		sprintf(filename, "%s/.mbXXXXXX", tmpdir);
		file = mkstemp(filename);

		if (file <= 0) {
			sprintf(filename, "Could not create temp file in %s/: %s", tmpdir, strerror(errno));
			throw(filename);
		}

		if (ftruncate(file, size)) {
			unlink(filename);
			sprintf(filename, "Could not allocate %zu temporary bytes in %s: %s", size, tmpdir, strerror(errno));
			throw(filename);
		}

		if (debug)
		  printf ("Producing tmp file %s for %s of size %liMB\n", filename, reason, (long)size/1024/1024);

		pointer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, file, 0);
		if (pointer == MAP_FAILED) {
			unlink(filename);
			pointer = NULL;
			sprintf(filename, "Could not mmap temporary file");
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
	if (!file) {
		if (debug)
		  printf ("Freeing %li bytes\n", (long)size);
		free(pointer);
	} else {
		if (debug)
		  printf ("Unmapping file %s of size %liMB\n", filename, (long)size/1024/1024);
		munmap(pointer, size);
		close(file);
	}
#endif
}

void* MapAlloc::MapAllocObject::GetPointer() {
	return pointer;
}

bool MapAlloc::MapAllocObject::IsFile() {
	return !!file;
}

/*
Copyright (c) 2018 tevador

This file is part of RandomX.

RandomX is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RandomX is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RandomX.  If not, see<http://www.gnu.org/licenses/>.
*/

#include <new>
#include <algorithm>
#include <stdexcept>
#include <cstring>

#include "common.hpp"
#include "dataset.hpp"
#include "Pcg32.hpp"
#include "Cache.hpp"
#include "virtualMemory.hpp"
#include "softAes.h"

#if defined(__SSE2__)
#include <wmmintrin.h>
#define PREFETCH(memory) _mm_prefetch((const char *)((memory).ds.dataset + (memory).ma), _MM_HINT_NTA)
#else
#define PREFETCH(memory)
#endif

namespace RandomX {

	template<typename T>
	static inline void shuffle(T* buffer, size_t bytes, Pcg32& gen) {
		auto count = bytes / sizeof(T);
		for (auto i = count - 1; i >= 1; --i) {
			int j = gen.getUniform(0, i);
			std::swap(buffer[j], buffer[i]);
		}
	}

	template<bool soft>
	void initBlock(const uint8_t* intermediate, uint8_t* out, uint32_t blockNumber, const KeysContainer& keys) {
		__m128i x0, x1, x2, x3;

		__m128i* xit = (__m128i*)intermediate;
		__m128i* xout = (__m128i*)out;

		x0 = _mm_cvtsi32_si128(blockNumber);
		constexpr int mask = (CacheSize / CacheLineSize) - 1;

		for (auto i = 0; i < DatasetIterations; ++i) {
			x0 = aesenc<soft>(x0, keys[0]);
			//x0 = aesenc<soft>(x0, keys[1]);
			x1 = aesenc<soft>(x0, keys[2]);
			//x1 = aesenc<soft>(x1, keys[3]);
			x2 = aesenc<soft>(x1, keys[4]);
			//x2 = aesenc<soft>(x2, keys[5]);
			x3 = aesenc<soft>(x2, keys[6]);
			//x3 = aesenc<soft>(x3, keys[7]);

			int index = _mm_cvtsi128_si32(x3);
			index &= mask;

			__m128i t0 = _mm_load_si128(xit + 4 * index + 0);
			__m128i t1 = _mm_load_si128(xit + 4 * index + 1);
			__m128i t2 = _mm_load_si128(xit + 4 * index + 2);
			__m128i t3 = _mm_load_si128(xit + 4 * index + 3);

			x0 = _mm_xor_si128(x0, t0);
			x1 = _mm_xor_si128(x1, t1);
			x2 = _mm_xor_si128(x2, t2);
			x3 = _mm_xor_si128(x3, t3);
		}

		_mm_store_si128(xout + 0, x0);
		_mm_store_si128(xout + 1, x1);
		_mm_store_si128(xout + 2, x2);
		_mm_store_si128(xout + 3, x3);
	}

	template
		void initBlock<true>(const uint8_t*, uint8_t*, uint32_t, const KeysContainer&);

	template
		void initBlock<false>(const uint8_t*, uint8_t*, uint32_t, const KeysContainer&);

	void datasetRead(addr_t addr, MemoryRegisters& memory, RegisterFile& reg) {
		uint64_t* datasetLine = (uint64_t*)(memory.ds.dataset + memory.ma);
		memory.mx ^= addr;
		memory.mx &= -64; //align to cache line
		std::swap(memory.mx, memory.ma);
		PREFETCH(memory);
		for (int i = 0; i < RegistersCount; ++i)
			reg.r[i].u64 ^= datasetLine[i];
	}

	template<bool softAes>
	void datasetReadLight(addr_t addr, MemoryRegisters& memory, RegisterFile& reg) {
		Cache* cache = memory.ds.cache;
		uint64_t datasetLine[CacheLineSize / sizeof(uint64_t)];
		initBlock<softAes>(cache->getCache(), (uint8_t*)datasetLine, memory.ma / CacheLineSize, cache->getKeys());
		for (int i = 0; i < RegistersCount; ++i)
			reg.r[i].u64 ^= datasetLine[i];
		memory.mx ^= addr;
		memory.mx &= -64; //align to cache line
		std::swap(memory.mx, memory.ma);
	}

	template
		void datasetReadLight<false>(addr_t addr, MemoryRegisters& memory, RegisterFile& reg);

	template
		void datasetReadLight<true>(addr_t addr, MemoryRegisters& memory, RegisterFile& reg);

	void datasetReadLightAsync(addr_t addr, MemoryRegisters& memory, RegisterFile& reg) {
		ILightClientAsyncWorker* aw = memory.ds.asyncWorker;
		const uint64_t* datasetLine = aw->getBlock(memory.ma);
		for (int i = 0; i < RegistersCount; ++i)
			reg.r[i].u64 ^= datasetLine[i];
		memory.mx ^= addr;
		memory.mx &= -64; //align to cache line
		std::swap(memory.mx, memory.ma);
		aw->prepareBlock(memory.ma);
	}

	void datasetAlloc(dataset_t& ds, bool largePages) {
		if (sizeof(size_t) <= 4)
			throw std::runtime_error("Platform doesn't support enough memory for the dataset");
		if (largePages) {
			ds.dataset = (uint8_t*)allocLargePagesMemory(DatasetSize);
		}
		else {
			ds.dataset = (uint8_t*)_mm_malloc(DatasetSize, 64);
			if (ds.dataset == nullptr) {
				throw std::runtime_error("Dataset memory allocation failed. >4 GiB of free virtual memory is needed.");
			}
		}
	}

	template<bool softAes>
	void datasetInit(Cache* cache, dataset_t ds, uint32_t startBlock, uint32_t blockCount) {
		for (uint32_t i = startBlock; i < startBlock + blockCount; ++i) {
			initBlock<softAes>(cache->getCache(), ds.dataset + i * CacheLineSize, i, cache->getKeys());
		}
	}

	template
		void datasetInit<false>(Cache*, dataset_t, uint32_t, uint32_t);

	template
		void datasetInit<true>(Cache*, dataset_t, uint32_t, uint32_t);

	template<bool softAes>
	void datasetInitCache(const void* seed, dataset_t& ds, bool largePages) {
		ds.cache = new(Cache::alloc(largePages)) Cache();
		ds.cache->initialize<softAes>(seed, SeedSize);
	}

	template
		void datasetInitCache<false>(const void*, dataset_t&, bool);

	template
		void datasetInitCache<true>(const void*, dataset_t&, bool);

	template<bool softAes>
	void aesBench(uint32_t blockCount) {
		alignas(16) KeysContainer keys;
		alignas(16) uint8_t buffer[CacheLineSize];
		for (uint32_t block = 0; block < blockCount; ++block) {
			initBlock<softAes>(buffer, buffer, 0, keys);
		}
	}

	template void aesBench<false>(uint32_t blockCount);
	template void aesBench<true>(uint32_t blockCount);
}

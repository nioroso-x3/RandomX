/*
Copyright (c) 2019 tevador

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

#include "softAes.h"

template<bool softAes>
void hashAes1Rx4(const void *input, size_t inputSize, void *hash) {
	const uint8_t* inptr = (uint8_t*)input;
	const uint8_t* inputEnd = inptr + inputSize;

	__m128i state0, state1, state2, state3;
	__m128i in0, in1, in2, in3;

	//intial state
	state0 = _mm_set_epi32(0x9d04b0ae, 0x59943385, 0x30ac8d93, 0x3fe49f5d);
	state1 = _mm_set_epi32(0x8a39ebf1, 0xddc10935, 0xa724ecd3, 0x7b0c6064);
	state2 = _mm_set_epi32(0x7ec70420, 0xdf01edda, 0x7c12ecf7, 0xfb5382e3);
	state3 = _mm_set_epi32(0x94a9d201, 0x5082d1c8, 0xb2e74109, 0x7728b705);

	//process 64 bytes at a time in 4 lanes
	while (inptr < inputEnd) {
		in0 = _mm_load_si128((__m128i*)inptr + 0);
		in1 = _mm_load_si128((__m128i*)inptr + 1);
		in2 = _mm_load_si128((__m128i*)inptr + 2);
		in3 = _mm_load_si128((__m128i*)inptr + 3);

		state0 = aesenc<softAes>(state0, in0);
		state1 = aesdec<softAes>(state1, in1);
		state2 = aesenc<softAes>(state2, in2);
		state3 = aesdec<softAes>(state3, in3);

		inptr += 64;
	}

	//two extra rounds to achieve full diffusion
	__m128i xkey0 = _mm_set_epi32(0x4ff637c5, 0x053bd705, 0x8231a744, 0xc3767b17);
	__m128i xkey1 = _mm_set_epi32(0x6594a1a6, 0xa8879d58, 0xb01da200, 0x8a8fae2e);

	state0 = aesenc<softAes>(state0, xkey0);
	state1 = aesdec<softAes>(state1, xkey0);
	state2 = aesenc<softAes>(state2, xkey0);
	state3 = aesdec<softAes>(state3, xkey0);

	state0 = aesenc<softAes>(state0, xkey1);
	state1 = aesdec<softAes>(state1, xkey1);
	state2 = aesenc<softAes>(state2, xkey1);
	state3 = aesdec<softAes>(state3, xkey1);

	//output hash
	_mm_store_si128((__m128i*)hash + 0, state0);
	_mm_store_si128((__m128i*)hash + 1, state1);
	_mm_store_si128((__m128i*)hash + 2, state2);
	_mm_store_si128((__m128i*)hash + 3, state3);
}

template void hashAes1Rx4<false>(const void *input, size_t inputSize, void *hash);
template void hashAes1Rx4<true>(const void *input, size_t inputSize, void *hash);

/*
 *  Copyright (C) 2017, Konstantinos Agiannis
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


//TODO
//improve header encoding
//sym_num constant?
//support big endian
//corrupt input
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

//config
#define NDEBUG
#define NUM_STREAMS 3
#define MAX_BIT_LEN 11
#define MAX_SYM_NUM 256

#define AMAX_BIT_LEN 14
#define HEADER_SIZE (2*(NUM_STREAMS-1))
#define MAX_HEADER_STAT_SIZE 128
#define MAX_OUTPUT(x) ((x) + ((x)/(63 << 10)*4) + 64)//TODO
#define MAGIC_NUM 0xf1f2
#define BLOCK_READ (64 << 20)

#if UINTPTR_MAX > 0x100000000ULL
#	define ARCH64
#else
#	define ARCH32
#endif

#ifdef ARCH64
#	if MAX_BIT_LEN <= 11
#		define RENORM_NUM 5
#	else
#		define RENORM_NUM 4
#	endif
#else
#	if MAX_BIT_LEN <= 12
#		define RENORM_NUM 2
#	else
#		define RENORM_NUM 1
#	endif
#endif

//macros
#define MIN(A,B) ((A) < (B)?(A):(B))
#define CAT(a, ...) XCAT(a, __VA_ARGS__)
#define XCAT(a, ...) a##__VA_ARGS__

#define REPEAT_ARG(N,X) CAT(REPA_,N)(X)
#define REPA_0(X) 
#define REPA_1(X) X(0)
#define REPA_2(X) REPA_1(X) X(1)
#define REPA_3(X) REPA_2(X) X(2)
#define REPA_4(X) REPA_3(X) X(3)
#define REPA_5(X) REPA_4(X) X(4)

#define REPEAT(N,...) CAT(REP_,N)(__VA_ARGS__)
#define REP_0(...) 
#define REP_1(...) __VA_ARGS__
#define REP_2(...) REP_1(__VA_ARGS__) __VA_ARGS__
#define REP_3(...) REP_2(__VA_ARGS__) __VA_ARGS__
#define REP_4(...) REP_3(__VA_ARGS__) __VA_ARGS__
#define REP_5(...) REP_4(__VA_ARGS__) __VA_ARGS__

#define DEC(X) CAT(DEC_,X)
#define DEC_1 0
#define DEC_2 1
#define DEC_3 2
#define DEC_4 3
#define DEC_5 4

//compiler specific stuff
#if defined(__GNUC__) || defined(__clang__)
#	define INLINE inline __attribute__ ((always_inline))
#	define likely(x) (__builtin_expect((x) != 0,1))
#	define unlikely(x) (__builtin_expect((x) != 0,0))
#	define BSWAP32(x) __builtin_bswap32(x)
#	define BSWAP64(x) __builtin_bswap64(x)
#else
#	define INLINE inline
#	define likely(x) x
#	define unlikely(x) x
#	define BSWAP32(x)\
			(((x) << 24) & 0xff000000 ) |\
			(((x) <<  8) & 0x00ff0000 ) |\
			(((x) >>  8) & 0x0000ff00 ) |\
			(((x) >> 24) & 0x000000ff )
#	define BSWAP64(x) \
			(((x) << 56) & 0xff00000000000000ULL) |\
            (((x) << 40) & 0x00ff000000000000ULL) |\
            (((x) << 24) & 0x0000ff0000000000ULL) |\
            (((x) << 8)  & 0x000000ff00000000ULL) |\
            (((x) >> 8)  & 0x00000000ff000000ULL) |\
            (((x) >> 24) & 0x0000000000ff0000ULL) |\
            (((x) >> 40) & 0x000000000000ff00ULL) |\
            (((x) >> 56) & 0x00000000000000ffULL)
#endif

//types
#define U16MAX 65535
typedef uint64_t U64;
typedef uint32_t U32;
typedef uint16_t U16;
typedef uint8_t U8;

typedef struct{
	U8 len,sym;
}Dnode;

typedef struct{
	U16 val,len;
}Enode;

typedef struct{
	U16 freq,sym;
}Fsym;

//memory stuff
INLINE U16 L16(const void* ptr)
{
	U16 result;
	memcpy(&result, ptr, sizeof(result));
	return result;
}

INLINE U32 L32(const void* ptr)
{
	U32 result;
	memcpy(&result, ptr, sizeof(result));
	return result;
}

INLINE U64 L64(const void* ptr)
{
	U64 result;
	memcpy(&result, ptr, sizeof(result));
	return result;
}

INLINE size_t LARCH(const void *ptr)
{
	size_t result;
	memcpy(&result, ptr, sizeof(size_t));
	return result;
}

INLINE void W16(void *ptr,U16 data)
{
	memcpy(ptr,&data,sizeof(data));
}

INLINE void W32(void *ptr,U32 data)
{
	memcpy(ptr,&data,sizeof(data));
}

INLINE void W64(void *ptr,U64 data)
{
	memcpy(ptr,&data,sizeof(data));
}

INLINE void WARCH(void *ptr,size_t data)
{
	memcpy(ptr,&data,sizeof(size_t));
}

/* compute table
#include <stdio.h>
int brev(int num,int len)
{
	int a,tmp0,tmp1;
	for(a = 0;a < len/2;a++){
		tmp0 = (num >> a) & 1;
		tmp1 = (num >> (len - a-1)) & 1;
		num &= ~((1 << a) | (1 << (len-a-1)));
		num |= tmp1 << a;
		num |= tmp0 << (len-a-1);
	}
	return num;
}

int main()
{
	int a;
	for(a = 0;a < 128;a++)
		printf(" %d,",brev(a,7));
}
*/

const U8 trev[128] = {
	0, 64, 32, 96, 16, 80, 48, 112, 8, 72, 40, 104, 24, 88, 56, 120,
	4, 68, 36, 100, 20, 84, 52, 116, 12, 76, 44, 108, 28, 92, 60, 124,
	2, 66, 34, 98, 18, 82, 50, 114, 10, 74, 42, 106, 26, 90, 58, 122,
	6, 70, 38, 102, 22, 86, 54, 118, 14, 78, 46, 110, 30, 94, 62, 126,
	1, 65, 33, 97, 17, 81, 49, 113, 9, 73, 41, 105, 25, 89, 57, 121,
	5, 69, 37, 101, 21, 85, 53, 117, 13, 77, 45, 109, 29, 93, 61, 125,
	3, 67, 35, 99, 19, 83, 51, 115, 11, 75, 43, 107, 27, 91, 59, 123,
	7, 71, 39, 103, 23, 87, 55, 119, 15, 79, 47, 111, 31, 95, 63, 127};

//assume num is 14 bits long
INLINE U32 brev(U32 num)
{
	return ((U32)trev[num >> 7]) | (((U32)trev[num & 127]) << 7);
}

/* compute table
#include <stdio.h>
#include <math.h>
int main(void)
{
	printf("{0");
	for(int a = 0;a < 64;a++){
		for(int b = 0;b < 16;b++){
			if(a == 0 && b == 0)
				continue;
			if(b != 0)
				printf(", ");
			else
				printf(",\n");
			printf("%.0lf",round((16*log2(a*16+b))));
		}
	}
	printf("};\n");
	return 0;
}
*/
const U8 lookup_log2[1024] = 
{0, 0, 16, 25, 32, 37, 41, 45, 48, 51, 53, 55, 57, 59, 61, 63,
64, 65, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 79,
80, 81, 81, 82, 83, 83, 84, 85, 85, 86, 86, 87, 87, 88, 88, 89,
89, 90, 90, 91, 91, 92, 92, 93, 93, 93, 94, 94, 95, 95, 95, 96,
96, 96, 97, 97, 97, 98, 98, 98, 99, 99, 99, 100, 100, 100, 101, 101,
101, 101, 102, 102, 102, 103, 103, 103, 103, 104, 104, 104, 104, 105, 105, 105,
105, 106, 106, 106, 106, 107, 107, 107, 107, 107, 108, 108, 108, 108, 109, 109,
109, 109, 109, 110, 110, 110, 110, 110, 111, 111, 111, 111, 111, 111, 112, 112,
112, 112, 112, 113, 113, 113, 113, 113, 113, 114, 114, 114, 114, 114, 114, 115,
115, 115, 115, 115, 115, 116, 116, 116, 116, 116, 116, 116, 117, 117, 117, 117,
117, 117, 117, 118, 118, 118, 118, 118, 118, 118, 119, 119, 119, 119, 119, 119,
119, 119, 120, 120, 120, 120, 120, 120, 120, 121, 121, 121, 121, 121, 121, 121,
121, 121, 122, 122, 122, 122, 122, 122, 122, 122, 123, 123, 123, 123, 123, 123,
123, 123, 123, 124, 124, 124, 124, 124, 124, 124, 124, 124, 125, 125, 125, 125,
125, 125, 125, 125, 125, 125, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126,
127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 128, 128, 128, 128, 128,
128, 128, 128, 128, 128, 128, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129,
129, 129, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 131, 131,
131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 132, 132, 132, 132, 132, 132,
132, 132, 132, 132, 132, 132, 132, 132, 133, 133, 133, 133, 133, 133, 133, 133,
133, 133, 133, 133, 133, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134,
134, 134, 134, 134, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135,
135, 135, 135, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136,
136, 136, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,
137, 137, 137, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138,
138, 138, 138, 138, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139,
139, 139, 139, 139, 139, 139, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140,
140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 141, 141, 141, 141, 141,
141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 142, 142, 142, 142,
142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142,
143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143,
143, 143, 143, 143, 143, 143, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144,
144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 145, 145, 145, 145,
145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145,
145, 145, 145, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146,
146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 147, 147, 147, 147, 147,
147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147,
147, 147, 147, 147, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148,
148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 149,
149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149,
149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 150, 150, 150, 150, 150, 150,
150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,
150, 150, 150, 150, 150, 150, 150, 151, 151, 151, 151, 151, 151, 151, 151, 151,
151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151,
151, 151, 151, 151, 151, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152,
152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152,
152, 152, 152, 152, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153,
153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153,
153, 153, 153, 153, 153, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154,
154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154,
154, 154, 154, 154, 154, 154, 154, 155, 155, 155, 155, 155, 155, 155, 155, 155,
155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155,
155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 156, 156, 156, 156, 156,
156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156,
156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156,
157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157,
157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157,
157, 157, 157, 157, 157, 157, 157, 158, 158, 158, 158, 158, 158, 158, 158, 158,
158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158,
158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158,
159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159,
159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159,
159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 160, 160, 160, 160, 160,
160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160};

INLINE int log_int(int a)
{
	if(a >= 1024)
		return (16-10)*16 + lookup_log2[a >> 6];
	else
		return lookup_log2[a];
}

//0 <= symbols <= sym_num-1 < 256
void byte_count(U8 *s,int len,U32 *res,int sym_num)
{
	U32 a,b1[3*MAX_SYM_NUM]={0};//avoid 3 memsets
	U32 *b2 = b1 + MAX_SYM_NUM,*b3 = b2 + MAX_SYM_NUM;
	U8 tmp0,tmp1,tmp2,tmp3;

	for(a = 0;a < (len & (~7));a += 4){
		tmp0 = s[a];
		tmp1 = s[a+1];
		tmp2 = s[a+2];
		tmp3 = s[a+3];
		res[tmp0]++;
		b1[tmp1]++;
		b2[tmp2]++;
		b3[tmp3]++;
	}
	for(;a < len;)
		res[s[a++]]++;
	for(a = 0;a < sym_num;a++)
		res[a] += b1[a] + b2[a] + b3[a];
}

//freq should be strictly less than 2^16
void sort_inc(Fsym *input,int num)
{
	U32 a,b,c,sum0,sum1,f0[256] = {0},f1[256] = {0};
	Fsym tmp[MAX_SYM_NUM],s;
	for(a = 0;a < num;a++){
		b = input[a].freq;
		f0[b & 255]++;
		f1[b >> 8]++;
	}
	sum0 = f0[0];
	f0[0] = 0;
	sum1 = f1[0];
	f1[0] = 0;
	for(a = 1;a <= 255;a++){
		b = f0[a];
		c = f1[a];
		f0[a] = sum0;
		f1[a] = sum1;
		sum0 += b;
		sum1 += c;
	}
	//sort
	for(a = 0;a < num;a++){
		s = input[a];
		b = s.freq & 255;
		tmp[f0[b]] = s;
		f0[b]++;
	}
	for(a = 0;a < num;a++){
		s = tmp[a];
		b = s.freq >> 8;
		input[f1[b]] = s;
		f1[b]++;
	}
}

void construct_dec_table(U8 *header_len,Dnode *lookup,int sym_num)
{
	//TODO check invalid input
	U32 a,b,prev_cum = 0,prev_num = 0,d,base;
	U32 count_bit[AMAX_BIT_LEN+1] = {0};
	Dnode tmp;

#ifndef NDEBUG
	for(a = 0,b = 0;a < sym_num;a++){
		if(header_len[a] == 0)
			continue;
		b += 1 << (MAX_BIT_LEN - header_len[a]);
	}
	assert(b == (1 << MAX_BIT_LEN));
#endif
	
	//count sort
	for(a = 0;a < sym_num;a++)
		count_bit[header_len[a]]++;
	for(a = MAX_BIT_LEN;a != 0;a--){
		prev_cum += prev_num;
		prev_num = count_bit[a] << (AMAX_BIT_LEN - a);
		count_bit[a] = prev_cum;
	}
	
	//place
	for(a = 0;a < sym_num;a++){
		b = header_len[a];
		if(b == 0)
			continue;
		d = 1 << b;
		tmp = (Dnode){b,a};
		base = brev(count_bit[b]);
		count_bit[b] += 1 << (AMAX_BIT_LEN - b);
		for(;base < (1 << MAX_BIT_LEN);base += d)
			lookup[base] = tmp;
	}
}

//assumes it does not contain non existant symbols
//assume sort not stable
void construct_enc_table(Enode *lookup,Fsym *s,int num)
{
	U32 a,count[AMAX_BIT_LEN+1] = {0},prev_num = 0,prev_cum = 0;
	for(a = 0;a < num;a++){
		count[s[a].freq]++;
		lookup[s[a].sym].len = s[a].freq;
	}
	for(a = MAX_BIT_LEN;a != 0;a--){
		prev_cum += prev_num;
		prev_num = count[a] << (AMAX_BIT_LEN - a);
		count[a] = prev_cum;
	}
	for(a = 0;a < num;a++){
		if(lookup[a].len == 0)
			continue;
		lookup[a].val = brev(count[lookup[a].len]);
		count[lookup[a].len] += 1 << (AMAX_BIT_LEN - lookup[a].len);
	}
}

//output to Fsym.freq bit_len
//WARNING!!!:input must have 1 additional free space not counted in num
//packages[L] conctains elements of L in packages
void build_prefix_codes(Fsym *input,int num)
{
	int packages[2][MAX_SYM_NUM+1],L,leaf_pos,package_pos,package_num = 0;
	int tmp,len,a,b,pnum;
	U8 M[MAX_BIT_LEN][2*MAX_SYM_NUM];//number of leafs up to there - 1
	input[num].freq = U16MAX;
	
	//L = 0
	package_num = num / 2;
	for(a = 0;a < (package_num << 1);a++)
		M[0][a] = a;
	for(a = 0,b = 0;a < package_num;a++,b += 2)
		packages[0][a] = input[b].freq + input[b+1].freq;

	for(L = 1;L < MAX_BIT_LEN;L++){
		len = (package_num + num) / 2;
		packages[(L-1)%2][package_num] = U16MAX;
		leaf_pos = package_pos = 0;
		//M[L][0] is always a leaf
		for(pnum = 0,a = 0;pnum < len;pnum++,a++){
			assert(package_pos <= package_num);
			if(input[leaf_pos].freq < packages[(L-1)%2][package_pos]){
				tmp = input[leaf_pos].freq;
				M[L][a] = leaf_pos++;
			}else{
				tmp = packages[(L-1)%2][package_pos];
				package_pos++;
				M[L][a] = leaf_pos-1;
			}
			a++;
			assert(package_pos <= package_num);
			if(input[leaf_pos].freq < packages[(L-1)%2][package_pos]){
				packages[L%2][pnum] = tmp + input[leaf_pos].freq;
				M[L][a] = leaf_pos++;
			}else{
				packages[L%2][pnum] = tmp + packages[(L-1)%2][package_pos];
				package_pos++;
				M[L][a] = leaf_pos-1;
			}
		}
		package_num = pnum;
	}
	
	for(a = 0;a < num;a++)
		input[a].freq = 0;

	//calculate result,take 2*n-2 first
	//bit lenght is the number of leaf occurences for each symbol
	len = 2*num-2;
	for(a = L-1;a >= 0 && len > 0;a--){
		leaf_pos = M[a][len-1];//garbage???
		input[leaf_pos].freq++;
		//printf("len = %d,leaf_pos = %d\n",len,leaf_pos);
		len = 2*(len -1 -leaf_pos );
	}
	for(b = input[num-1].freq,a = num-2;a >= 0;a--){
		b += input[a].freq;
		input[a].freq = b;
	}
}

U8 *byte_pos,*init_pos,c;
U32 nibble_count;

void init_nibble(U8 *pos)
{
	byte_pos = pos;
	init_pos = pos;
	nibble_count = 0;
}

INLINE U8 get_nibble()
{
	//branchless
	/*byte_pos += nibble_count%2;
	int res = ((*byte_pos)>>((nibble_count%2)<<3))&15;
	nibble_count++;
	return res;*/
	//with branch
	if(nibble_count++%2 == 0){
		c = *byte_pos++;
		return c & 15;
	}else{
		return c >> 4;
	}
}

INLINE void put_nibble(U8 n)
{
	if(nibble_count++%2 == 0){
		c = n;
	}else{
		c |= n << 4;
		*byte_pos++ = (U8)c;
	}
}

//returns number of bytes writen
U32 flush_nibbles()
{
	if(nibble_count%2 == 1)
		*byte_pos++ = c;
	return byte_pos - init_pos;
}

U32 get_input_nibbles()
{
	return byte_pos - init_pos;
}

//return bytes written
U32 write_prefix_descr(Enode *lookup,U32 sym_num,U8 *res)
{
	U32 previous,count,a;
	init_nibble(res);
	for(a = 0;a < sym_num;a++){
		count = 1;
		previous = lookup[a].len;
		
		while(a+1 < sym_num && previous == lookup[a+1].len)
			count++,a++;
		
		if(count == 1){
			put_nibble(previous);
		}else if(count == 2){
			put_nibble(previous);
			put_nibble(previous);
		}else{
			if(previous == 0 && count == a+1)
				count++;
			else
				put_nibble(previous);
			if(count <= 16-MAX_BIT_LEN)
				put_nibble(MAX_BIT_LEN+count-2);
			else{
				put_nibble(15);
				count -= 17-MAX_BIT_LEN;
				while(count >= 15){
					put_nibble(15);
					count -= 15;
				}
				put_nibble(count);
			}
		}
	}
	return flush_nibbles();
}

U32 read_prefix_descr(U8 *len,U32 sym_num,U8 *in)
{
	//TODO check invalid
	int bl,previous = 0,a = 0,c;
	init_nibble(in);
	while(a < sym_num){
		bl = get_nibble();
		if(bl <= MAX_BIT_LEN){
			previous = bl;
			len[a++] = bl;
		}else if(bl < 15){
			c = 1+bl-MAX_BIT_LEN;
			while(c-- > 0)
				len[a++] = previous;
		}else{
			c = 16-MAX_BIT_LEN;
			while(c-- > 0)
				len[a++] = previous;
			do{
				c = bl = get_nibble();
				while(c-->0)
					len[a++] = previous;
			}while(bl == 15);
		}
	}
	return get_input_nibbles();
}
 
void write_header(U16 *pos,U32 *stream_size)
{
	for(U32 a = 0;a < NUM_STREAMS-1;a++)
		W16(pos+a,stream_size[a]);
		//pos[a] = stream_size[a];//misalligned ??????
}

//return uncompressed size
int read_header(U16 *pos,U32 *stream_size)
{
	for(U32 a = 0;a < NUM_STREAMS;a++)
		stream_size[a] = pos[a+1];
	return pos[0];
}

//1 stream a time
//dest should have some 8? more free bytes
int prefix_codes_encode(U8 *dest,U8 *src,U32 sym_num,const Enode *lookup)
{
	U8 *src_end = src + sym_num - (sym_num%(RENORM_NUM*NUM_STREAMS));
	U8 *dest_start = dest,sym,bl;
	U32 bits_av = 0,tmp;
	size_t bits = 0,code;
	
	while(src < src_end){//??????
		REPEAT(RENORM_NUM,
			sym = *src;
			code = lookup[sym].val;
			bl = lookup[sym].len;
			src += NUM_STREAMS;
			bits |= code << bits_av;
			bits_av += bl;
		)
		WARCH(dest,bits);
		tmp = bits_av >> 3;
		bits_av &= 7;
		bits >>= tmp << 3;
		dest += tmp;
	}
	
	//at most RENORM_NUM-1 times
	src_end += sym_num %(RENORM_NUM * NUM_STREAMS);
	while(src < src_end){
		sym = *src;
		src += NUM_STREAMS;
		code = lookup[sym].val;
		bl = lookup[sym].len;
		bits |= code << bits_av;
		bits_av += bl;
	}
	//renormalise
	WARCH(dest,bits);
	dest += (bits_av+7) >> 3;
	
	return dest - dest_start;
}


#ifdef ARCH64
#	define RENORM_DEC(A){\
		bits##A |= L64(stream_pos##A) << bits_av##A;\
		stream_pos##A += (63 - bits_av##A) >> 3;\
		bits_av##A |= 56;\
	}
#else
#	define RENORM_DEC(A){\
		bits##A |= L32(stream_pos##A) << bits_av##A;\
		stream_pos##A += (31 - bits_av##A) >> 3;\
		bits_av##A |= 24;\
	}
#endif

#define RENORM_DEC_END(A)\
	bits##A |= LARCH(stream_pos##A) << bits_av##A;

#define DEC_INIT(A){\
	stream_pos##A = other;\
	other += L16(src);\
	src += 2;\
}

#define PREFIX_DEC(A){\
	code = bits##A & ((1 << MAX_BIT_LEN)-1);\
	bl = lookup[code].len;\
	bits##A >>= bl;\
	bits_av##A -= bl;\
	*dest++ = lookup[code].sym;\
}

#define PREFIX_DEC_END(A){\
	if(dest >= dest_end)return;\
	code = bits##A & ((1 << MAX_BIT_LEN)-1);\
	bl = lookup[code].len;\
	*dest++ = lookup[code].sym;\
	bits##A >>= bl;\
}

#define DEC_DECLARE(A)\
	U32 bits_av##A = 0;\
	size_t bits##A = 0;\
	U8 *stream_pos##A;

//src stream should have 8 additional free bytes
void prefix_codes_decode(U8 *dest,U32 dest_size,U8 *src,U32 src_size,const Dnode *lookup)
{
	REPEAT_ARG(NUM_STREAMS,DEC_DECLARE);
	U32 code,bl;
	U8 *dest_end = dest + dest_size - (dest_size%(RENORM_NUM * NUM_STREAMS));
	U8 *other = src + HEADER_SIZE;
	
	src_size -= HEADER_SIZE;
	REPEAT_ARG(DEC(NUM_STREAMS),DEC_INIT)
	CAT(stream_pos,DEC(NUM_STREAMS)) = other;
	//stream_end = src + src_size;
	
	//TODO check invalid
	while(dest < dest_end){//processes RENORM_NUM*NUM_STREAMS bytes a time
		//renormalise
		REPEAT_ARG(NUM_STREAMS,RENORM_DEC)
		//dec
		REPEAT(RENORM_NUM,
			REPEAT_ARG(NUM_STREAMS,PREFIX_DEC))
	}
	
	REPEAT_ARG(NUM_STREAMS,RENORM_DEC_END);
	//decode one by one
	dest_end += dest_size%(RENORM_NUM * NUM_STREAMS);
	for(;;){
		REPEAT_ARG(NUM_STREAMS,PREFIX_DEC_END);
	}
	return;
}

//encode bytes,return bytes written
//size < 64Kb
U32 prefix_encode(void *output,const void *in,U32 size,U32 sym_num)
{
	U32 a,b,count[MAX_SYM_NUM] = {0},stream_size[NUM_STREAMS],compressed_size;
	U8 *out_start = (U8 *)output,*header_start,*out = (U8 *)output;
	Fsym s[MAX_SYM_NUM+1];
	Enode lookup[MAX_SYM_NUM];

	byte_count((U8 *)in,size,count,256);
	for(a = 0;a < sym_num;a++)
		s[a] = (Fsym){count[a],a};
	sort_inc(s,sym_num);
	if(s[sym_num - 1].freq == size){
		*out = s[sym_num -1].sym;
		return 1;
	}
	if(s[0].freq == 8){//fastpath for uncompressed???needed????
		memcpy(output,in,size);
		return size;
	}
	//cut 0 freq
	for(a = 0;a < sym_num && s[a].freq == 0;a++);

	assert(sym_num - a != 1);

	build_prefix_codes(s+a,sym_num - a);
	construct_enc_table(lookup,s,sym_num);
		
	init_nibble((U8 *)out);
	//U32 t[17] = {0};
	//for(a = 0;a < 256;a++)t[lookup[a].len]++;//debug
	//for(a = 0;a <= 12;a++)printf("len %d = %.3lf\%\n",a,100*((double)t[a])/256);
	out += write_prefix_descr(lookup,sym_num,out);
	header_start = out;//misaligned
	out += HEADER_SIZE;
	
	for(a = 0;a < NUM_STREAMS;a++){
		b = prefix_codes_encode(out,((U8 *)in)+a,size-a,lookup);
		stream_size[a] = b;
		out += b;
	}
	
	compressed_size = (U32 )(out - out_start);
	if(compressed_size >= size){
		memcpy(output,in,size);
		return size;
	}
	write_header((U16 *)header_start,stream_size);
	return compressed_size;
}

void prefix_decode(void * output,U32 out_size,const void *input,U32 in_size,U32 sym_num)
{
	if(in_size == 1){
		memset(output,*((char*)input),out_size);
		return;
	}
	if(in_size == out_size){
		memcpy(output,input,in_size);
		return;
	}
	Dnode lookup[1 << MAX_BIT_LEN];
	U8 *in = (U8 *)input,*out = (U8 *)output;
	U8 bit_len[MAX_SYM_NUM];
	U32 bit_descr_size;
	bit_descr_size = read_prefix_descr(bit_len,sym_num,in);
	construct_dec_table(bit_len,lookup,sym_num);
	//decode
	prefix_codes_decode(out,out_size,in + bit_descr_size,in_size - bit_descr_size,lookup);
}

//simple adler32 checksum
//not the best just for testing
uint32_t hash(unsigned char *data, size_t len)
{

#define MOD_ADLER 65521
#define MAX_NONMOD 5552

	U32 a = 1,b = 0;
	size_t index;
again:
	for(index = 0;index < MIN(len,MAX_NONMOD); index++){
		assert(b <= b+a);
		assert(a <= a + data[index]);
		a = a + data[index];
		b = b + a;
	}
	if(index < len){
		len -= MAX_NONMOD;
		a = a % MOD_ADLER;
		b = b % MOD_ADLER;
		goto again;
	}
	return ((b % MOD_ADLER) << 16) | (a % MOD_ADLER);
}

INLINE void 
error(const char *s)
{
        fprintf(stderr,"%s\n",s);
        exit(EXIT_FAILURE);
}

INLINE
void * MALLOC(size_t size)
{
	void * ptr;
	ptr = malloc(size);
	
	if(ptr == 0 && size != 0)
		error("ERROR:could not allocate memory\n");
	
	return ptr;
}

U32 block_encode(void *output,void *input,U32 bsize)
{
	W16(output,bsize);//LE
	U32 tmp = prefix_encode(((char *)output) + 4,input,bsize,256);
	W16(((char*)output) + 2,tmp);//LE
	return 4 + tmp;
}

U32 comp_block_adaptive(void * output,void * input,U32 inlen)
{
	
#define ADAPT_MOD 64
#define BLOCK_OVERHEAD 100
#define STEP 1024
#define MBLOCK (((1 << 16)-1)/STEP)
#define LOG2(A) log_int(A)
//#define LOG2(A) (A == 0 ? 0 : round(16*log2(A))) 

	int Cfreq[MBLOCK+1][256],dp[64];
	U8 *block_size = MALLOC((inlen/STEP)+1);
	U8 *in = (U8 *)input,*out = (U8 *)output,*out_start = (U8 *) output;
	
	//init
	int block_end = (inlen-1) / STEP;//????
	for(int a = 0;a < 256;a++)
		dp[(block_end+1)%ADAPT_MOD] = Cfreq[(block_end+1)%ADAPT_MOD][a] = 0;

	//compute block sizes
	int b = inlen-1;
	for(int a = inlen - STEP;a >= 0;a -= STEP){
		int cur = (a / STEP) % ADAPT_MOD;
		int prev = (cur + 1)%ADAPT_MOD;
		for(int c = 0;c < 256;c++)
			Cfreq[cur][c] = Cfreq[prev][c];
		for(;b >= a;b--)
			Cfreq[cur][in[b]]++;
		int best = INT_MAX,bsize;//MAX
		for(int c = 1;c <= MBLOCK && (c*STEP) <= inlen - a;c++){//???
			//bits = -sum(ni*log2(ni) + total*log2(total))
			int res = 0;
			for(int d = 0;d < 256;d++){
				int n = Cfreq[cur][d] - Cfreq[(cur+c)%ADAPT_MOD][d];
				res -= n*LOG2(n);
			}
			res += c*STEP*LOG2(c*STEP);
			res = (res/16+7)/8 + BLOCK_OVERHEAD + dp[(cur + c)%ADAPT_MOD];
			//res = prefix_encode(out,in + a,c*STEP,256) + dp[(cur + c)%ADAPT_MOD];
			if(res <= best){
				best = res;
				bsize = c;
			}
		}
		block_size[a / STEP] = bsize;
		dp[cur] = best;
	}
	
	//now encode using block sizes
	int a = 0,res = 0;
	//if not max block possible merge remaining
	//if(block_size[0] != MBLOCK){//??????
		out += block_encode(out,in,(inlen % STEP) + (block_size[0] * STEP));
		in += block_size[0] * STEP;
		a = block_size[0];
	//}else{
	//	out += block_encode(out,in,inlen % STEP);
	//}
	in += inlen % STEP;
	for(;a < block_end;a += block_size[a]){
		out += block_encode(out,in,block_size[a] * STEP);
		in += block_size[a] * STEP;
		//printf("bsize = %d\n", block_size[a]);
	}
	free(block_size);
	return out - out_start;
}

//return compressed size
//bsize < 64KB
//if bsize == 0 then adaptive
U32 comp_block(void * output,void * input,U32 inlen,U32 bsize)
{
	if(bsize == 0)
		return comp_block_adaptive(output,input,inlen);
	char *in = (char *) input,*out = (char *)output,*out_start = (char *)output;
	while(inlen > 0){
		U32 step = MIN(inlen,bsize);
		out += block_encode(out,in,step);
		in += step;
		inlen -= step;
	}
	W16(out,0);
	return (U32)(out - out_start);
}

U32 dec_block(void * output,void * input,U32 inlen)
{
	char *in = (char *)input,*in_start = (char *)input,*out = (char *)output;
	while(inlen > 0){
		U32 d = L16(in);//LE
		U32 e = L16(in+2);//LE
		in += 4;
		//TODO invalid
		prefix_decode(out,d,in,e,256);
		out += d;
		in += e;
		inlen -= 4+e;
	}
	return (U32)(in-in_start);
}


void bench_file(FILE *in,U32 chunk_size,U32 bsize)
{
	U64 csize = 0,size = 0,a;
	char *input = (char *)MALLOC(chunk_size+128);
	char *output = (char *)MALLOC(MAX_OUTPUT(chunk_size));//TODO
	clock_t t0,t1,t2,t3,t4,compt = 0,dect = 0;

	//bench
	while ((a = fread(input,1,chunk_size,in)) > 0){
		U32 h1 = hash(input,a);
		
		t0 = clock();
		comp_block(output,input,a,bsize);
		t1 = clock();
		comp_block(output,input,a,bsize);
		t2 = clock();
		comp_block(output,input,a,bsize);
		t3 = clock();
		U32 tmp = comp_block(output,input,a,bsize);
		t4 = clock();
		
		compt += MIN(MIN(MIN(t4-t3,t3-t2),t2-t1),t1-t0);
		
		t0 = clock();
		dec_block(input,output,tmp);
		t1 = clock();
		dec_block(input,output,tmp);
		t2 = clock();
		dec_block(input,output,tmp);
		t3 = clock();
		dec_block(input,output,tmp);
		t4 = clock();
		dect += MIN(MIN(MIN(t4-t3,t3-t2),t2-t1),t1-t0);
		
		U32 h2 = hash(input,a);
		if(h1 != h2)
			error("ERROR:Input differs from output.");
		csize += tmp;
		size += a;
	}
	printf("%llu -> %llu, %.2lf%% of original,ratio = %.3lf\n"
			"compression speed %.2lf MB/s, decompression speed %.2lf MB/s\n",size,csize,
	((double)csize)/((double)size)*100,((double)size)/((double)csize),
	((double)size)/1024/1024/((double)compt / CLOCKS_PER_SEC),((double)size)/1024/1024/((double)dect / CLOCKS_PER_SEC));

	free(input);
	free(output);
}

//format bits
//magic 16
//for every block
// 16 dec size | 16 enc size| enc size bytes
//if enc size == 1 then read a byte and repeate dec size times
//if enc size == dec size then memcpy
//return compressed size on success else 0

//in and out must be open
U64 comp_file(FILE *in,FILE *out,U32 bsize)
{
	int block = (bsize == 0 ? BLOCK_READ : bsize );
	U8 *input = (U8 *) MALLOC(MAX_OUTPUT(block)),*output = (U8 *) MALLOC(MAX_OUTPUT(block));
	U64 res = 4;
	U32 a,c,magic = MAGIC_NUM;
	fwrite(&magic,2,1,out);
	while ((a = fread(input,1,block,in)) > 0){
		c = comp_block(output,input,a,bsize);
		fwrite(output,c,1,out);
		res += 4 + ((U64)c);
	}
	a = 0;
	fwrite(&a,2,1,out);
	free(input);
	free(output);
	return res;
}

//return decompressed size
U64 dec_file(FILE *in,FILE *out)
{
	U8 input[MAX_OUTPUT(1<<16)],output[MAX_OUTPUT(1<<16)];
	U64 res = 0;
	U32 a = 0,c;

	fread(&a,2,1,in);
	if(a != MAGIC_NUM)
		error("ERROR:File not compressed.");

	fread(&a,2,1,in);
	while(a != 0){
		fread(&c,2,1,in);
		U32 b = fread(input,1,c,in);
		if(b != c)
			error("ERROR:File corrupted.");
		prefix_decode(output,a,input,c,256);//TODO: bit_len
		res += (U64)a;
		fwrite(output,1,a,out);
		fread(&a,2,1,in);
	}
	return res;
}

void help(char **argv)
{
	//make decompress default
	printf("Fast Prefix Coder v0.1\n\n"
			"usage: %s [options] input [output]\n\n"
			"  -B           : benchmark file\n"
			"  -b num       : block size in KB, 1<= num <= 63, 0 for adaptive (default 16)\n"
			//"  -c           : compress\n"
			"  -d           : decompress\n",*argv);
	exit(EXIT_FAILURE);
}

int main(int argc,char **argv)
{
	int bsize = 16 * 1024;
	int count = 1,bench = 0,compress = 1;
	
	while(count < argc && argv[count][0] == '-'){
		if(argv[count][2] != 0)
			help(argv);
		switch(argv[count][1]){
		case 'B':
			bench = 1;
			break;
		case 'b':
			if(++count < argc)
				bsize = atoi(argv[count]) * 1024;
			else
				help(argv);
			break;
		case 'd':
			compress = 0;
			break;
		default:
			help(argv);
		}
		count++;
	}
	if(count >= argc || bsize < 0 || bsize > 63*1024)
		help(argv);
	FILE *in = fopen(argv[count++],"r");
	if(in == 0)
		error("ERROR:Unable to open input");
	if(bench == 0){
		FILE *out = fopen(argv[count++],"w");
		if(out == 0)
			error("ERROR:Unable to open output");
		if(count != argc)
			help(argv);
		if(compress == 1)
			comp_file(in,out,bsize);
		else
			dec_file(in,out);
	}else{
		if(count != argc)
			help(argv);
		bench_file(in,BLOCK_READ,bsize);
	}
	return 0;
}

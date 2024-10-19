// based on https://github.com/eliben/code-for-blog/blob/22821a6/2008/memmgr/memmgr.c (public domain)

#include "InvisiblePoolAllocator.h"

#include <cstdint>
#include <emulibc.h>

#define MIN_INVISIBLE_POOL_ALLOC_QUANTAS 16

union invisible_mem_header_t
{
	struct invisible_mem_header_s
	{
		// Pointer to the next block in the free list
		union invisible_mem_header_t* next;

		// Size of the block (in quantas of sizeof(invisible_mem_header_t))
		std::size_t size;
	} s;

	// Used to align headers in memory to the maximum alignment boundary
	std::max_align_t align_dummy;
};

// Initial empty list
ECL_INVISIBLE static invisible_mem_header_t invisible_mem_base;
// Start of free list
ECL_INVISIBLE static invisible_mem_header_t* invisible_free_p;

// Static invisible pool for new allocations
ECL_INVISIBLE static std::uint8_t invisible_pool[INVISIBLE_POOL_SIZE];
ECL_INVISIBLE static std::size_t invisible_pool_free_pos;

static invisible_mem_header_t* get_invisible_mem_from_pool(std::size_t num_quantas)
{
	std::size_t total_req_size;
	invisible_mem_header_t* h;

	if (num_quantas < MIN_INVISIBLE_POOL_ALLOC_QUANTAS)
		num_quantas = MIN_INVISIBLE_POOL_ALLOC_QUANTAS;

	total_req_size = num_quantas * sizeof(invisible_mem_header_t);

	if (invisible_pool_free_pos + total_req_size <= INVISIBLE_POOL_SIZE)
	{
		h = reinterpret_cast<invisible_mem_header_t*>(&invisible_pool[invisible_pool_free_pos]);
		h->s.size = num_quantas;
		invisible_pool_free(static_cast<void*>(h + 1));
		invisible_pool_free_pos += total_req_size;
	}
	else
	{
		return nullptr;
	}

	return invisible_free_p;
}

// Allocations are done in 'quantas' of header size.
// The search for a free block of adequate size begins at the point 'invisible_free_p' where the last block was found.
// If a too-big block is found, it is split and the tail is returned (this way the header of the original needs only to have its size adjusted).
// The pointer returned to the user points to the free space within the block, which begins one quanta after the header.
void* invisible_pool_alloc(std::size_t num_bytes)
{
	invisible_mem_header_t* p;
	invisible_mem_header_t* prev_p;

	// Calculate how many quantas are required: we need enough to house all the requested bytes, plus the header.
	// The -1 and +1 are there to make sure that if num_bytes is a multiple of num_quantas, we don't allocate too much
	std::size_t num_quantas = (num_bytes + sizeof(invisible_mem_header_t) - 1) / sizeof(invisible_mem_header_t) + 1;

	// First alloc call, and no free list yet?
	// Use 'invisible_mem_base' for an initial denegerate block of size 0, which points to itself
	if ((prev_p = invisible_free_p) == nullptr)
	{
		invisible_mem_base.s.next = invisible_free_p = prev_p = &invisible_mem_base;
		invisible_mem_base.s.size = 0;
	}

	for (p = prev_p->s.next; ; prev_p = p, p = p->s.next)
	{
		// big enough?
		if (p->s.size >= num_quantas)
		{
			// exactly?
			if (p->s.size == num_quantas)
			{
				// just eliminate this block from the free list by pointing its prev's next to its next
				prev_p->s.next = p->s.next;
			}
			else // too big
			{
				p->s.size -= num_quantas;
				p += p->s.size;
				p->s.size = num_quantas;
			}

			invisible_free_p = prev_p;
			return static_cast<void*>(p + 1);
		}
		// Reached end of free list?
		// Try to allocate the block from the pool.
		// If that succeeds, get_invisible_mem_from_pool adds the new block to the free list and it will be found in the following iterations.
		// If the call to get_invisible_mem_from_pool doesn't succeed, we've run out of memory
		else if (p == invisible_free_p)
		{
			if ((p = get_invisible_mem_from_pool(num_quantas)) == nullptr)
			{
				return nullptr;
			}
		}
	}
}

// Scans the free list, starting at invisible_free_p, looking the the place to insert the free block.
// This is either between two existing blocks or at the end of the list.
// In any case, if the block being freed is adjacent to either neighbor, the adjacent blocks are combined.
void invisible_pool_free(void* ap)
{
	invisible_mem_header_t* block;
	invisible_mem_header_t* p;

	// acquire pointer to block header
	block = static_cast<invisible_mem_header_t*>(ap) - 1;

	// Find the correct place to place the block in (the free list is sorted by address, increasing order)
	for (p = invisible_free_p; !(block > p && block < p->s.next); p = p->s.next)
	{
		// Since the free list is circular, there is one link where a higher-addressed block points to a lower-addressed block.
		// This condition checks if the block should be actually inserted between them
		if (p >= p->s.next && (block > p || block < p->s.next))
			break;
	}

	// Try to combine with the higher neighbor
	if (block + block->s.size == p->s.next)
	{
		block->s.size += p->s.next->s.size;
		block->s.next = p->s.next->s.next;
	}
	else
	{
		block->s.next = p->s.next;
	}

	// Try to combine with the lower neighbor
	if (p + p->s.size == block)
	{
		p->s.size += block->s.size;
		p->s.next = block->s.next;
	}
	else
	{
		p->s.next = block;
	}

	invisible_free_p = p;
}

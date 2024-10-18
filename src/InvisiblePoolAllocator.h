#include <cstddef>
#include <new>

// 128MiB is hopefully enough?
#define INVISIBLE_POOL_SIZE (128 * 1024 * 1024)

void* invisible_pool_alloc(std::size_t num_bytes);
void invisible_pool_free(void* ap);

template <class T>
struct InvisiblePoolAllocator
{
	typedef T value_type;

	InvisiblePoolAllocator() = default;

	template <class U>
	constexpr InvisiblePoolAllocator(const InvisiblePoolAllocator<U>&) noexcept {}

	[[nodiscard]] T* allocate(std::size_t n)
	{
		if (n > INVISIBLE_POOL_SIZE / sizeof(T))
		{
			throw std::bad_array_new_length();
		}

		auto p = static_cast<T*>(invisible_pool_alloc(n * sizeof(T)));
		if (!p)
		{
			throw std::bad_alloc();
		}

		return p;
	}

	void deallocate(T* p, [[maybe_unused]] std::size_t n) noexcept
	{
		invisible_pool_free(static_cast<void*>(p));
	}
};

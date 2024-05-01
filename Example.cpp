#include <iostream>
#include <iomanip>

#define DYNAMIC_ALLOCATOR_DEBUG

#define DYNAMIC_ALLOCATOR_STATS 1
#include "DynamicAllocator.h"

int main()
{
	harz::DynamicAllocator<> DynamicAllocator{ 1024 * 1024 };
	// Allocate memory for int array of 25 elements
	auto* NewAlloc = DynamicAllocator.Allocate(sizeof(int) * 25 * 8);
	int* IntArr = (int*)NewAlloc;
	// Assign integer for 18-th element of array
	IntArr[18] = 163456;
	std::cout << "Here our int[18]: " << IntArr[18] << '\n';

	// Make loop of allocations/deallocations(frees)
	const int DynAllocIters = 10000;
	for (int i = 0; i < DynAllocIters; i++)
	{
		if (i < 8)
			i = 8;

		void* Allocation = DynamicAllocator.Allocate(i * 10);
		*(int*)Allocation = 15;
		DynamicAllocator.Free(Allocation);
	}

	// Resize allocator
	DynamicAllocator.Resize(1024 * 1024 + 10000);
	// Make big allocation(which will be freed after calling resize with small size,
	// which should force dynamic allocator to free all "free space" if it's larger than input size and this memory blocks are suitable "primarly allocated" for deallocation)
	NewAlloc = DynamicAllocator.Allocate(1024 * 980);
	DynamicAllocator.Resize(1024 * 5);
	DynamicAllocator.Free(NewAlloc);
	std::cout << DynamicAllocator.GetAllocatorStats() << '\n';
	// Clear allocator memory
	DynamicAllocator.Clear();

	// Again allocate after clear array of 50 int's and assign value for last number in array
	NewAlloc = DynamicAllocator.Allocate(sizeof(int) * 50 * 8);
	int* IntArr1 = (int*)NewAlloc;
	IntArr1[49] = 637;
	std::cout << "Here our last int in allocated array: " << IntArr1[49] << '\n';
	DynamicAllocator.Free(IntArr);
	return 0;
};


/// -----------------------------------------------------------------------------
/// 
/// BSD 3-Clause License (see file: LICENSE)
/// Copyright(c) 2023-2024, (IHarzI) Maslianka Zakhar
/// 
/// -----------------------------------------------------------------------------

#pragma once

#include <vector>

#ifdef DYNAMIC_ALLOCATOR_DEBUG
#include <cassert>
#define DYNAMIC_ALLOCATOR_ASSERT(cond) assert(cond)
#define DYNAMIC_ALLOCATOR_REPORT(msg) std::cout << "| HARZ | DYNAMIC ALLOCATOR | REPORT: " << msg << '\n';
#else
#define DYNAMIC_ALLOCATOR_ASSERT(cond)
#define DYNAMIC_ALLOCATOR_REPORT(msg)
#endif

// define DYNAMIC_ALLOCATOR_USE_MALLOC 0, if you don't want to allocate directly from malloc
#ifndef DYNAMIC_ALLOCATOR_USE_MALLOC
#define DYNAMIC_ALLOCATOR_USE_MALLOC 1
#endif

// define 1 for calling GetAllocatorStats
#ifndef DYNAMIC_ALLOCATOR_STATS
#define DYNAMIC_ALLOCATOR_STATS 0
#endif

#if DYNAMIC_ALLOCATOR_STATS == 1
#include <string>
#include <sstream>
#endif

namespace harz
{
	namespace DynamicAllocatorDetails
	{
		using uint32 = unsigned int;
		using uint8 = unsigned char;

		constexpr static uint32 FreeIdsUseThreshold = 64;
		constexpr static uint32 MaxAllocationsDefault = 50 * 1024;
		constexpr static uint32 MinAllocSizeRequirement = 64;
		// Free list must not have such a big number of nodes
		constexpr static uint32 InvalidNodeID = 0xFFFFFFFF;

		struct MemoryHeaderBlockNode
		{
			MemoryHeaderBlockNode()
			{
				Size = 0;
				NodeMemory = nullptr;
				NextNodeIndex = InvalidNodeID;
				IsNextNodeAdjacent = 0;
				IsBlockFree = 1;
				IsPrimaryAllocated = 0;
				FreeSpace = 0;
			};

			uint32 Size = 0;
			void* NodeMemory = nullptr;
			uint32 NextNodeIndex = 0;

			// ==================== FLAGS

			// For future use
			uint8 FreeSpace : 5;
			// Adjacent memory flag(to know if next block of memory in List is "neighbor" to this node's block of memory)
			uint8 IsNextNodeAdjacent : 1;
			uint8 IsBlockFree : 1;
			// Is this block primarily allocated from internal allocator(used for deallocation of block)
			uint8 IsPrimaryAllocated : 1;
		};

#if DYNAMIC_ALLOCATOR_USE_MALLOC == 1
#include <malloc.h>

		class DYNAMIC_ALLOCATOR_MALLOC
		{
		public:
			using pointer = void*;
			static inline pointer Allocate(size_t allocationsize)
			{
				pointer allocation = malloc(allocationsize);
				DYNAMIC_ALLOCATOR_REPORT(" DYNAMIC ALLOCATOR: MALLOC: ALLOCATED AT ADDRESS: " << allocation);
				return allocation;
			};

			static inline bool Deallocate(pointer allocation)
			{
				DYNAMIC_ALLOCATOR_REPORT(" DYNAMIC ALLOCATOR: MALLOC: CALL TO DEALLOCATE AT ADDRESS: " << allocation);

				free(allocation);
				return true;
			};
		};
#endif
	}

	using namespace harz::DynamicAllocatorDetails;

	// Dynamic allocator
	// General allocator for medium/big size allocations
	// NOTE: Returned pointer is not aligned by the allocator itself. (TODO: Alignment of allocation)
	// Allocator type should have static member functions Allocate/Deallocate with arguments as in DYNAMIC_ALLOCATOR_MALLOC

#if DYNAMIC_ALLOCATOR_USE_MALLOC == 1
	template<typename Allocator = DYNAMIC_ALLOCATOR_MALLOC>
#else
	template<typename Allocator>
#endif
	class DynamicAllocator
	{
	public:
		using MemPtr = void*;
		using NodeIDType = uint32;
		using SizeType = uint32;
		using InternalAllocator = Allocator;

		DynamicAllocator(SizeType BaseAllocationSize, uint32 MaxAllocations = MaxAllocationsDefault);
		DynamicAllocator(DynamicAllocator&) = delete;
		DynamicAllocator(DynamicAllocator&&) = delete;

		// Resize Dynamic Allocator
		// Return True if the operation successful
		// False if can't allocate a new chunk of memory 
		// OR (no space/not enough space) to deallocate(if initial size was bigger than input to function)
		bool Resize(SizeType SizeToChange);

		// Allocate block of memory from FreeList free space
		void* Allocate(SizeType size);

		// Deallocate block of memory from FreeList free space
		bool Free(void* address);

		inline SizeType GetTotalSize() const { return TotalSize; };
		inline SizeType GetFreeSpaceSize() const { return FreeSpaceSize; };
		inline SizeType GetOccupiedSpace() const { DYNAMIC_ALLOCATOR_ASSERT(FreeSpaceSize <= TotalSize); return TotalSize - FreeSpaceSize; };
#if DYNAMIC_ALLOCATOR_STATS == 1
		std::string GetAllocatorStats() const;
#endif
		// Clear all allocated memory
		void Clear();

	private:
		using uint8 = unsigned char;

		MemoryHeaderBlockNode GetNodeMetadata(MemPtr nodeMemory);
		SizeType GetFreeNodeIndex();
		SizeType GetNodeSize(MemPtr nodeMemory);

		inline bool CheckAndSetFreeIdsUse()
		{
			if (NodesFreeIdsBin.size() > FreeIdsUseThreshold)
			{
				UseFreeBinNodesID = 1;
				return true;
			}
			return false;
		};

		SizeType TotalSize = 0;
		SizeType FreeSpaceSize = 0;

		NodeIDType HeadNodeIndex = InvalidNodeID;
		NodeIDType LastNodeIndex = InvalidNodeID;

		uint8 UseFreeBinNodesID : 1;

		std::vector<MemoryHeaderBlockNode> Nodes{};
		std::vector<NodeIDType> NodesFreeIdsBin{};
	};

	template<typename Allocator>
	DynamicAllocator<Allocator>::DynamicAllocator(SizeType BaseAllocationSize, uint32 MaxAllocations)
	{
		Nodes.reserve(MaxAllocations);
		NodesFreeIdsBin.reserve(MaxAllocations);

		Resize(BaseAllocationSize);
	};

	template<typename Allocator>
	bool DynamicAllocator<Allocator>::Resize(SizeType SizeToChange)
	{
		bool result = true;

		// if the new size is smaller than the node for the allocated memory block
		if (SizeToChange <= MinAllocSizeRequirement)
		{
			DYNAMIC_ALLOCATOR_REPORT("Dynamic Allocator resize with small amount of memory %llu Bytes.");
		};

		if (Nodes.empty() && TotalSize == 0)
		{
			// Head node must be invalid
			DYNAMIC_ALLOCATOR_ASSERT(HeadNodeIndex == InvalidNodeID);

			void* allocatedMemoryBlockForResize = InternalAllocator::Allocate(SizeToChange);
			DYNAMIC_ALLOCATOR_ASSERT(allocatedMemoryBlockForResize && "Failed to allocated memory for Dynamic Allocator resize");

			TotalSize = SizeToChange;
			FreeSpaceSize = SizeToChange;
			HeadNodeIndex = 0;
			LastNodeIndex = 0;
			MemoryHeaderBlockNode NewReservedNode{};
			NewReservedNode.IsNextNodeAdjacent = 0;
			NewReservedNode.IsPrimaryAllocated = 1;
			NewReservedNode.IsBlockFree = 1;
			NewReservedNode.NodeMemory = allocatedMemoryBlockForResize;
			NewReservedNode.Size = SizeToChange;
			NewReservedNode.NextNodeIndex = InvalidNodeID;
			Nodes.push_back(std::move(NewReservedNode));

		}
		else
		{
			DYNAMIC_ALLOCATOR_ASSERT(SizeToChange != 0);

			//If the Size is less than the current size then this call SHOULD decrease the size of the allocator
			//Try to fully deallocate memory from Allocator preallocated space
			if (SizeToChange < TotalSize && FreeSpaceSize >= SizeToChange)
			{
				NodeIDType previousNodeID = InvalidNodeID;
				for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
				{
					auto& FreedNode = Nodes.at(nodeIndex);
					if (FreedNode.IsPrimaryAllocated == 1 &&
						FreedNode.IsBlockFree == 1 &&
						FreedNode.IsNextNodeAdjacent == 0)
					{
						InternalAllocator::Deallocate(FreedNode.NodeMemory);
						NodesFreeIdsBin.push_back(nodeIndex);
						FreeSpaceSize -= FreedNode.Size;
						TotalSize -= FreedNode.Size;

						if (nodeIndex == HeadNodeIndex)
						{
							HeadNodeIndex = FreedNode.NextNodeIndex;
						}

						if (previousNodeID != InvalidNodeID)
						{
							if (nodeIndex == LastNodeIndex)
							{
								LastNodeIndex = previousNodeID;
							}
							else
							{
								Nodes.at(previousNodeID).NextNodeIndex = FreedNode.NextNodeIndex;
							}
						};

						// Invalidate node
						FreedNode = MemoryHeaderBlockNode{};

						if (FreeSpaceSize <= SizeToChange || TotalSize <= SizeToChange)
						{
							break;
						}
					}
					previousNodeID = nodeIndex;
				};

				CheckAndSetFreeIdsUse();

				if (TotalSize >= SizeToChange || FreeSpaceSize >= SizeToChange)
				{
					return false;
				}
			}
			// If the size is more than the current size of allocated memory, allocate a new block and add it to the total space
			else
			{
				uint32 SizeToAllocate = SizeToChange - TotalSize;
				void* allocatedMemoryBlockForResize = InternalAllocator::Allocate(SizeToAllocate);

				DYNAMIC_ALLOCATOR_ASSERT(allocatedMemoryBlockForResize && "Failed to allocated memory for Dynamic Allocator resize");

				MemoryHeaderBlockNode NewReservedNode{};
				NewReservedNode.IsNextNodeAdjacent = 0;
				NewReservedNode.IsPrimaryAllocated = 1;
				NewReservedNode.IsBlockFree = 1;
				NewReservedNode.NodeMemory = allocatedMemoryBlockForResize;
				NewReservedNode.Size = SizeToAllocate;
				NewReservedNode.NextNodeIndex = InvalidNodeID;
				Nodes.push_back(std::move(NewReservedNode));

				FreeSpaceSize += SizeToAllocate;
				TotalSize = SizeToChange;

				// Update data about the last node
				if (LastNodeIndex == InvalidNodeID)
				{
					DYNAMIC_ALLOCATOR_ASSERT("LastNodeIndex should be valid at this point (bug?)");
					LastNodeIndex = Nodes.size() - 1;
				}
				else
				{
					Nodes.at(LastNodeIndex).NextNodeIndex = Nodes.size() - 1;
					LastNodeIndex = Nodes.size() - 1;
				};
			};
		}
		return result;
	}

	template<typename Allocator>
	void* DynamicAllocator<Allocator>::Allocate(SizeType size)
	{
		void* resultPointer = nullptr;
		if (size <= MinAllocSizeRequirement)
			DYNAMIC_ALLOCATOR_REPORT("Allocation of small amount of memory from Dynamic Allocator, consider using another allocator.");

		if (size > FreeSpaceSize)
			Resize(TotalSize + size);

		if (Nodes.size() > 0)
		{
			// Loop through nodes by using indexes for the array
			NodeIDType BestNodeIDForAllocation = InvalidNodeID;
			for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
			{
				MemoryHeaderBlockNode& NodeCandidateHeader = Nodes.at(nodeIndex);
				// Check if Node is suitable for allocation
				if (NodeCandidateHeader.Size >= size && NodeCandidateHeader.IsBlockFree == 1)
				{
					// 1 time stop...
					if (BestNodeIDForAllocation == InvalidNodeID)
						BestNodeIDForAllocation = nodeIndex;

					// If a candidate is better than current BestNode, make the candidate The Best
					if (BestNodeIDForAllocation != InvalidNodeID
						&& Nodes.at(BestNodeIDForAllocation).Size > NodeCandidateHeader.Size)
					{
						BestNodeIDForAllocation = nodeIndex;
					};
				};
			}

			if (BestNodeIDForAllocation == InvalidNodeID)
				// Do a New Allocation for a block of memory
			{
				DYNAMIC_ALLOCATOR_REPORT("No more space in Dynamic Allocator for allocation(Out of space/Fragmentation of memory blocks) | Dynamic Allocator must do resizing");
				Resize(TotalSize + size);
				BestNodeIDForAllocation = LastNodeIndex;
			}
			// IF we found the best-fitted node or make resizing before this ^^^
			if (BestNodeIDForAllocation != InvalidNodeID)
			{
				MemoryHeaderBlockNode& BestNode = Nodes.at(BestNodeIDForAllocation);
				// Check if we can make a new memory node block from left memory in this memory node block
				if (BestNode.Size > size && BestNode.Size - size >= MinAllocSizeRequirement)
				{
					// Create a new node from remained memory
					MemoryHeaderBlockNode NewNodeFromRemaindedMemoryInBestNode{};
					NewNodeFromRemaindedMemoryInBestNode.IsNextNodeAdjacent = BestNode.IsNextNodeAdjacent;
					NewNodeFromRemaindedMemoryInBestNode.NextNodeIndex = BestNode.NextNodeIndex;
					NewNodeFromRemaindedMemoryInBestNode.IsBlockFree = 1;
					NewNodeFromRemaindedMemoryInBestNode.NodeMemory = (void*)((uint8*)BestNode.NodeMemory + size);
					NewNodeFromRemaindedMemoryInBestNode.Size = BestNode.Size - size;
					NewNodeFromRemaindedMemoryInBestNode.IsPrimaryAllocated = 0;

					NodeIDType NewNodeID = InvalidNodeID;

					if (UseFreeBinNodesID == 0)
					{
						Nodes.push_back(std::move(NewNodeFromRemaindedMemoryInBestNode));
						NewNodeID = Nodes.size() - 1;
					}
					else
					{
						DYNAMIC_ALLOCATOR_ASSERT(NodesFreeIdsBin.size() > 0);
						NodeIDType FreeIndex = NodesFreeIdsBin.at(NodesFreeIdsBin.size() - 1);
						std::swap(Nodes.at(FreeIndex), NewNodeFromRemaindedMemoryInBestNode);
						NodesFreeIdsBin.pop_back();
						NewNodeID = FreeIndex;
						// If no more free indexes, uncheck this flag
						if (NodesFreeIdsBin.size() == 0)
							UseFreeBinNodesID = 0;
					}

					DYNAMIC_ALLOCATOR_ASSERT(NewNodeID != InvalidNodeID);

					//If it's created at the end of the "list", update the last node index
					if (LastNodeIndex == BestNodeIDForAllocation || LastNodeIndex == InvalidNodeID)
						LastNodeIndex = NewNodeID;

					// Edit the best node while taking in mind of loosed memory block at the end
					BestNode.IsNextNodeAdjacent = 1;
					BestNode.IsBlockFree = 0;
					BestNode.Size = size;
					BestNode.NextNodeIndex = NewNodeID;
				}
				else
				{
					BestNode.IsBlockFree = 0;
				}
				resultPointer = BestNode.NodeMemory;
				FreeSpaceSize -= size;
			}
		}

		return resultPointer;
	};


	template<typename Allocator>
	bool DynamicAllocator<Allocator>::Free(void* address)
	{
		NodeIDType previousNodeIndex = InvalidNodeID;
		for (NodeIDType currentNodeIndex = HeadNodeIndex; currentNodeIndex != InvalidNodeID; currentNodeIndex = Nodes.at(currentNodeIndex).NextNodeIndex)
		{
			if (Nodes.at(currentNodeIndex).NodeMemory == address)
			{
				MemoryHeaderBlockNode& DealocatedNode = Nodes.at(currentNodeIndex);
				DealocatedNode.IsBlockFree = 1;
				FreeSpaceSize += DealocatedNode.Size;

				// Check if the next to the freed node is free and adjacent(next in memory),
				if (DealocatedNode.NextNodeIndex != InvalidNodeID &&
					DealocatedNode.IsNextNodeAdjacent == 1 &&
					Nodes.at(DealocatedNode.NextNodeIndex).IsBlockFree == 1)
				{
					// If it is, than add it's size(update other stuff) and make this(next) node as "empty"

					// Update info about this node, add size of next node, set next node index...
					uint32 NextBlockIndex = DealocatedNode.NextNodeIndex;
					MemoryHeaderBlockNode& NextToDealocatedBlock = Nodes.at(NextBlockIndex);
					DealocatedNode.IsNextNodeAdjacent = NextToDealocatedBlock.IsNextNodeAdjacent;
					DealocatedNode.Size += NextToDealocatedBlock.Size;
					DealocatedNode.NextNodeIndex = NextToDealocatedBlock.NextNodeIndex;

					// Make this adjacent node invalid
					NextToDealocatedBlock = MemoryHeaderBlockNode{};

					// If the next block is last, make this the last block
					if (LastNodeIndex == NextBlockIndex)
						LastNodeIndex = currentNodeIndex;

					// Push this adjacent node index into the free node's index bin
					NodesFreeIdsBin.push_back(NextBlockIndex);
				};

				// Check if the previous node is adjacent to this and is empty
				if (previousNodeIndex != InvalidNodeID &&
					Nodes.at(previousNodeIndex).IsNextNodeAdjacent == 1 &&
					Nodes.at(previousNodeIndex).IsBlockFree == 1)
				{
					// If it is, then add to the previous node size of the current node, update other information
					// and make the current node empty
					MemoryHeaderBlockNode& PreviousNodeBlock = Nodes.at(previousNodeIndex);
					PreviousNodeBlock.Size += DealocatedNode.Size;
					PreviousNodeBlock.IsNextNodeAdjacent = DealocatedNode.IsNextNodeAdjacent;
					PreviousNodeBlock.NextNodeIndex = DealocatedNode.NextNodeIndex;

					// If the current node is Last, make the previous node as last
					if (LastNodeIndex == currentNodeIndex)
					{
						LastNodeIndex = previousNodeIndex;
					}

					// Make this deallocated node invalid
					DealocatedNode = MemoryHeaderBlockNode{};

					// Push this deallocated node index into the free node's indexes bin
					NodesFreeIdsBin.push_back(currentNodeIndex);
				}

				// Check, if we have enough free indexes in the free bin to use,(and if yes, then) set dynamic allocator to use them
				CheckAndSetFreeIdsUse();

				// MAYBE should delete node, if it's primarily allocated and is free and 
				// No more chunks of memory from this node memory are in use

				return true;
			}
			previousNodeIndex = currentNodeIndex;
		};
		return false;
	}

#if DYNAMIC_ALLOCATOR_STATS == 1
	template<typename Allocator>
	std::string DynamicAllocator<Allocator>::GetAllocatorStats() const
	{
		std::stringstream result{};
		result << "\n Dynamic Allocator stats: _----------_\n DynamicAllocator address: ";
		result << this;
		result << "\n --------\n Nodes: ";
		for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
		{
			auto& NodeRef = Nodes.at(nodeIndex);
			result << " ID[" << nodeIndex << "] size[" << NodeRef.Size << ']' << std::boolalpha << " isFree[" << (bool)NodeRef.IsBlockFree << ']';
			result << " isPrimarlyAllocated[" << (bool)NodeRef.IsPrimaryAllocated << ']' << " NextNodeID[" << NodeRef.NextNodeIndex << ']';
			result << " isNextNodeAdjacent[" << (bool)NodeRef.IsNextNodeAdjacent << ']' << " NodeAddress[" << NodeRef.NodeMemory << ']';
		}
		result << "\n ------- \n";
		if (NodesFreeIdsBin.size() > 0)
		{
			result << '\n' << " FREE IDS: |";
			for (uint32 index = 0; index < NodesFreeIdsBin.size(); index++)
			{
				result << NodesFreeIdsBin[index] << '|';
			}
		}
		else
		{
			result << "\n NO FREE IDS IN THIS ALLOCATOR";
		};
		result << "\n End of stats : .........-__________-.........\n";
		return result.str();
	}
#endif

	template<typename Allocator>
	void DynamicAllocator<Allocator>::Clear()
	{
		for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
		{
			auto& Node = Nodes.at(nodeIndex);
			if (Node.IsPrimaryAllocated == 1)
			{
				InternalAllocator::Deallocate(Node.NodeMemory);
			}
		};

		Nodes.clear();
		NodesFreeIdsBin.clear();
		HeadNodeIndex = InvalidNodeID;
		LastNodeIndex = InvalidNodeID;
		FreeSpaceSize = 0;
		TotalSize = 0;
		UseFreeBinNodesID = 0;
	};

	template<typename Allocator>
	MemoryHeaderBlockNode DynamicAllocator<Allocator>::GetNodeMetadata(MemPtr nodeMemory)
	{
		for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
		{
			if (Nodes.at(nodeIndex).NodeMemory == nodeMemory)
				return Nodes.at(nodeIndex);
		};
		return {};
	}

	template<typename Allocator>
	uint32 DynamicAllocator<Allocator>::GetFreeNodeIndex()
	{
		for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
		{
			if (Nodes.at(nodeIndex).IsBlockFree == 1)
				return nodeIndex;
		};
		return 0;
	}

	template<typename Allocator>
	uint32 DynamicAllocator<Allocator>::GetNodeSize(MemPtr nodeMemory)
	{
		for (NodeIDType nodeIndex = HeadNodeIndex; nodeIndex != InvalidNodeID; nodeIndex = Nodes.at(nodeIndex).NextNodeIndex)
		{
			if (Nodes.at(nodeIndex).NodeMemory == nodeMemory)
			{
				return Nodes.at(nodeIndex).Size;
			}
		};
		return 0;
	}
};

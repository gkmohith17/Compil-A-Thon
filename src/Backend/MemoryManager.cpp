#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace pim {

// Memory allocation strategies
enum class AllocationStrategy {
  LINEAR,      // Simple linear allocation
  INTERLEAVED, // Interleaved allocation for parallel access
  OPTIMIZED    // Pattern-based optimized allocation
};

class PIMMemoryManager {
public:
  PIMMemoryManager(uint32_t numBanks, uint32_t bankSize);
  ~PIMMemoryManager();
  
  // Allocate memory for a module
  void allocateModule(llvm::Module &M);
  
  // Allocate memory for global variables
  void allocateGlobals(llvm::Module &M);
  
  // Allocate memory for function locals
  void allocateFunction(llvm::Function &F);
  
  // Set memory allocation strategy
  void setAllocationStrategy(AllocationStrategy strategy);
  
  // Generate memory layout information
  void generateMemoryLayout(llvm::raw_ostream &OS);
  
  // Clear all allocations
  void reset();

private:
  struct MemoryRegion {
    uint32_t bank;      // Memory bank
    uint32_t offset;    // Offset within bank
    uint32_t size;      // Size in bytes
    bool isAligned;     // Whether memory is aligned
    
    MemoryRegion(uint32_t b, uint32_t o, uint32_t s, bool a = false)
      : bank(b), offset(o), size(s), isAligned(a) {}
  };
  
  // Memory configuration
  uint32_t numBanks;
  uint32_t bankSize;
  AllocationStrategy strategy;
  
  // Track allocations and bank usage
  llvm::DenseMap<const llvm::Value*, MemoryRegion> allocations;
  std::vector<uint32_t> bankUsage;
  
  // Helper methods
  uint32_t allocateInBank(uint32_t bank, uint32_t size, uint32_t alignment = 1);
  uint32_t findBestBank(uint32_t size, uint32_t alignment = 1);
  void interleaveAllocation(const llvm::Value *V, uint32_t size, uint32_t alignment = 1);
  void linearAllocation(const llvm::Value *V, uint32_t size, uint32_t alignment = 1);
  void optimizedAllocation(const llvm::Value *V, uint32_t size, uint32_t alignment = 1);
};

PIMMemoryManager::PIMMemoryManager(uint32_t numBanks, uint32_t bankSize)
  : numBanks(numBanks), bankSize(bankSize), strategy(AllocationStrategy::LINEAR) {
  
  // Initialize bank usage tracking
  bankUsage.resize(numBanks, 0);
}

PIMMemoryManager::~PIMMemoryManager() {
  // Cleanup as needed
}

void PIMMemoryManager::allocateModule(llvm::Module &M) {
  // Allocate all globals and functions in the module
  allocateGlobals(M);
  
  for (auto &F : M) {
    if (!F.isDeclaration()) {
      allocateFunction(F);
    }
  }
}

void PIMMemoryManager::allocateGlobals(llvm::Module &M) {
  const llvm::DataLayout &DL = M.getDataLayout();
  
  for (auto &GV : M.globals()) {
    if (GV.hasInitializer()) {
      llvm::Type *Ty = GV.getValueType();
      uint32_t size = DL.getTypeAllocSize(Ty);
      uint32_t alignment = GV.getAlignment() ? GV.getAlignment() : DL.getABITypeAlignment(Ty);
      
      // Allocate based on current strategy
      switch (strategy) {
        case AllocationStrategy::INTERLEAVED:
          interleaveAllocation(&GV, size, alignment);
          break;
        case AllocationStrategy::OPTIMIZED:
          optimizedAllocation(&GV, size, alignment);
          break;
        case AllocationStrategy::LINEAR:
        default:
          linearAllocation(&GV, size, alignment);
          break;
      }
    }
  }
}

void PIMMemoryManager::allocateFunction(llvm::Function &F) {
  const llvm::DataLayout &DL = F.getParent()->getDataLayout();
  
  // Handle stack allocations (alloca instructions)
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(&I)) {
        llvm::Type *Ty = AI->getAllocatedType();
        uint32_t size = DL.getTypeAllocSize(Ty);
        uint32_t alignment = AI->getAlignment();
        
        if (alignment == 0) {
          alignment = DL.getABITypeAlignment(Ty);
        }
        
        // Allocate based on current strategy
        switch (strategy) {
          case AllocationStrategy::INTERLEAVED:
            interleaveAllocation(AI, size, alignment);
            break;
          case AllocationStrategy::OPTIMIZED:
            optimizedAllocation(AI, size, alignment);
            break;
          case AllocationStrategy::LINEAR:
          default:
            linearAllocation(AI, size, alignment);
            break;
        }
      }
    }
  }
}

void PIMMemoryManager::setAllocationStrategy(AllocationStrategy newStrategy) {
  strategy = newStrategy;
}

void PIMMemoryManager::generateMemoryLayout(llvm::raw_ostream &OS) {
  OS << "PIM Memory Layout:\n";
  OS << "  Number of Banks: " << numBanks << "\n";
  OS << "  Bank Size: " << bankSize << " bytes\n\n";
  
  // Report bank usage
  OS << "Bank Usage:\n";
  for (uint32_t i = 0; i < numBanks; ++i) {
    OS << "  Bank " << i << ": " << bankUsage[i] << "/" << bankSize << " bytes ("
       << (bankUsage[i] * 100.0 / bankSize) << "%)\n";
  }
  
  // Report allocations
  OS << "\nMemory Allocations:\n";
  for (auto &Alloc : allocations) {
    OS << "  Value: ";
    if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(Alloc.first)) {
      OS << GV->getName();
    } else {
      Alloc.first->print(OS);
    }
    
    const MemoryRegion &MR = Alloc.second;
    OS << "\n    Bank: " << MR.bank
       << ", Offset: 0x" << llvm::format_hex(MR.offset, 0)
       << ", Size: " << MR.size << " bytes"
       << (MR.isAligned ? " (aligned)" : "") << "\n";
  }
}

void PIMMemoryManager::reset() {
  // Clear all allocations
  allocations.clear();
  
  // Reset bank usage
  std::fill(bankUsage.begin(), bankUsage.end(), 0);
}

uint32_t PIMMemoryManager::allocateInBank(uint32_t bank, uint32_t size, uint32_t alignment) {
  // Calculate aligned offset
  uint32_t offset = bankUsage[bank];
  uint32_t alignedOffset = (offset + alignment - 1) & ~(alignment - 1);
  
  // Update bank usage
  bankUsage[bank] = alignedOffset + size;
  
  return alignedOffset;
}

uint32_t PIMMemoryManager::findBestBank(uint32_t size, uint32_t alignment) {
  // Find bank with most free space
  uint32_t bestBank = 0;
  uint32_t maxFree = 0;
  
  for (uint32_t i = 0; i < numBanks; ++i) {
    uint32_t currentOffset = bankUsage[i];
    uint32_t alignedOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
    uint32_t paddingNeeded = alignedOffset - currentOffset;
    
    if (alignedOffset + size <= bankSize) {
      uint32_t freeSpace = bankSize - bankUsage[i];
      
      if (freeSpace > maxFree) {
        maxFree = freeSpace;
        bestBank = i;
      }
    }
  }
  
  // If no bank has enough space, use round-robin
  if (maxFree == 0) {
    bestBank = 0;
    uint32_t minUsage = bankUsage[0];
    
    for (uint32_t i = 1; i < numBanks; ++i) {
      if (bankUsage[i] < minUsage) {
        minUsage = bankUsage[i];
        bestBank = i;
      }
    }
  }
  
  return bestBank;
}

void PIMMemoryManager::linearAllocation(const llvm::Value *V, uint32_t size, uint32_t alignment) {
  // Simple linear allocation strategy
  uint32_t bank = findBestBank(size, alignment);
  uint32_t offset = allocateInBank(bank, size, alignment);
  
  // Record allocation
  allocations[V] = MemoryRegion(bank, offset, size, alignment > 1);
}

void PIMMemoryManager::interleaveAllocation(const llvm::Value *V, uint32_t size, uint32_t alignment) {
  // For arrays, interleave across banks for parallel access
  bool isArray = false;
  
  if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(V)) {
    isArray = GV->getValueType()->isArrayTy();
  } else if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(V)) {
    isArray = AI->getAllocatedType()->isArrayTy();
  }
  
  if (isArray && size > 64) {
    // Interleave large arrays
    // This is a simple placeholder - real implementation would be more complex
    uint32_t bank = 0;
    uint32_t offset = allocateInBank(bank, size, alignment);
    
    // Record allocation
    allocations[V] = MemoryRegion(bank, offset, size, alignment > 1);
  } else {
    // Use linear allocation for non-arrays
    linearAllocation(V, size, alignment);
  }
}

void PIMMemoryManager::optimizedAllocation(const llvm::Value *V, uint32_t size, uint32_t alignment) {
  // Advanced allocation based on access patterns
  // This is a placeholder for a more sophisticated algorithm
  
  // For now, just use linear allocation
  linearAllocation(V, size, alignment);
}

} // namespace pim
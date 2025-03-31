//===-- MemoryMappingTest.cpp - Test for PIM Memory Mapping --===//
//
// This file contains test cases for memory mapping on PIM architecture.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include <memory>
#include <vector>
#include <map>
#include <string>

#include "PIMCompiler.h"
#include "TargetMachine.h"
#include "InstructionSet.h"

using namespace llvm;

// Simulated PIM memory bank layout for testing
struct PIMMemoryBankInfo {
  unsigned ID;
  std::string Name;
  unsigned Size;         // Size in bytes
  unsigned AccessTime;   // Access time in cycles
  bool Volatile;         // Volatile memory
};

// Mock PIM device with memory banks
class MockPIMDevice {
public:
  MockPIMDevice() {
    // Initialize with default memory bank configuration
    MemoryBanks = {
      {0, "SRAM_Fast", 64 * 1024, 1, true},        // 64KB fast SRAM
      {1, "SRAM_Dense", 256 * 1024, 2, true},      // 256KB dense SRAM
      {2, "DRAM_Local", 4 * 1024 * 1024, 4, true}, // 4MB local DRAM
      {3, "NVM", 16 * 1024 * 1024, 10, false}      // 16MB non-volatile memory
    };
    
    // Initialize memory bank allocation tracking
    for (const auto &Bank : MemoryBanks) {
      BankAllocations[Bank.ID] = 0;
    }
  }
  
  // Simulated memory allocation in PIM device
  bool allocateMemory(unsigned BankID, unsigned Size, std::string &ErrorMsg) {
    for (const auto &Bank : MemoryBanks) {
      if (Bank.ID == BankID) {
        if (BankAllocations[BankID] + Size <= Bank.Size) {
          BankAllocations[BankID] += Size;
          return true;
        } else {
          ErrorMsg = "Insufficient memory in bank " + Bank.Name;
          return false;
        }
      }
    }
    ErrorMsg = "Invalid bank ID";
    return false;
  }
  
  // Get memory bank info
  PIMMemoryBankInfo getBankInfo(unsigned BankID) const {
    for (const auto &Bank : MemoryBanks) {
      if (Bank.ID == BankID) {
        return Bank;
      }
    }
    // Return invalid bank if not found
    return {UINT_MAX, "Invalid", 0, 0, false};
  }
  
  // Reset all allocations
  void resetAllocations() {
    for (auto &Alloc : BankAllocations) {
      Alloc.second = 0;
    }
  }
  
  // Get current allocation for a bank
  unsigned getCurrentAllocation(unsigned BankID) const {
    auto It = BankAllocations.find(BankID);
    if (It != BankAllocations.end()) {
      return It->second;
    }
    return 0;
  }
  
  // Get total memory capacity
  unsigned getTotalMemory() const {
    unsigned Total = 0;
    for (const auto &Bank : MemoryBanks) {
      Total += Bank.Size;
    }
    return Total;
  }
  
  // Get memory banks
  const std::vector<PIMMemoryBankInfo>& getMemoryBanks() const {
    return MemoryBanks;
  }
  
private:
  std::vector<PIMMemoryBankInfo> MemoryBanks;
  std::map<unsigned, unsigned> BankAllocations;
};

// Test fixture for PIM memory mapping
class MemoryMappingTest : public ::testing::Test {
protected:
  LLVMContext Context;
  std::unique_ptr<Module> TestModule;
  IRBuilder<> Builder;
  MockPIMDevice PIMDevice;
  
  void SetUp() override {
    // Create a new module for each test
    TestModule = std::make_unique<Module>("MemoryMappingTest", Context);
    Builder = IRBuilder<>(Context);
  }
  
  void TearDown() override {
    // Clean up
    TestModule.reset();
  }
  
  // Helper function to create an array allocation function
  Function* createArrayAllocationFunction(unsigned ElementCount, unsigned ElementSize) {
    // Define function type: void* allocateArray(int count, int element_size)
    Type *Int32Ty = Type::getInt32Ty(Context);
    Type *VoidPtrTy = Type::getInt8PtrTy(Context);
    
    std::vector<Type*> ParamTypes = {Int32Ty, Int32Ty};
    FunctionType *FuncType = FunctionType::get(VoidPtrTy, ParamTypes, false);
    
    // Create function
    Function *AllocFunc = Function::Create(
        FuncType, Function::ExternalLinkage, "allocate_array", TestModule.get());
    
    // Name function arguments
    auto ArgIter = AllocFunc->arg_begin();
    Value *Count = ArgIter++;
    Count->setName("count");
    Value *ElemSize = ArgIter++;
    ElemSize->setName("element_size");
    
    // Create basic block
    BasicBlock *EntryBB = BasicBlock::Create(Context, "entry", AllocFunc);
    Builder.SetInsertPoint(EntryBB);
    
    // Calculate total size
    Value *TotalSize = Builder.CreateMul(Count, ElemSize, "total_size");
    
    // Call malloc
    FunctionType *MallocType = FunctionType::get(VoidPtrTy, {Int32Ty}, false);
    Value *MallocFunc = TestModule->getOrInsertFunction("malloc", MallocType).getCallee();
    Value *Allocation = Builder.CreateCall(MallocType, MallocFunc, {TotalSize}, "allocation");
    
    // Add metadata for PIM memory allocation
    // This is what our memory optimizer would use to determine bank assignment
    LLVMContext &Ctx = AllocFunc->getContext();
    
    // Create metadata for array properties
    Value *ElementCountConst = ConstantInt::get(Int32Ty, ElementCount);
    Value *ElementSizeConst = ConstantInt::get(Int32Ty, ElementSize);
    Metadata *CountMD = ConstantAsMetadata::get(ElementCountConst);
    Metadata *SizeMD = ConstantAsMetadata::get(ElementSizeConst);
    
    // Create memory requirements metadata
    MDNode *ArrayPropsMD = MDNode::get(Ctx, {CountMD, SizeMD});
    AllocFunc->setMetadata("pim.array_props", ArrayPropsMD);
    
    // Return the allocation
    Builder.CreateRet(Allocation);
    
    // Verify the function
    bool HasError = verifyFunction(*AllocFunc, &errs());
    if (HasError) {
      errs() << "Error in array allocation function\n";
      AllocFunc->eraseFromParent();
      return nullptr;
    }
    
    return AllocFunc;
  }
  
  // Helper function to create a memory bank assignment test function
  Function* createMemoryBankAssignmentFunction() {
    // Define function type: int assignMemoryBank(void* ptr, int size, int access_pattern)
    Type *Int32Ty = Type::getInt32Ty(Context);
    Type *VoidPtrTy = Type::getInt8PtrTy(Context);
    
    std::vector<Type*> ParamTypes = {VoidPtrTy, Int32Ty, Int32Ty};
    FunctionType *FuncType = FunctionType::get(Int32Ty, ParamTypes, false);
    
    // Create function
    Function *AssignFunc = Function::Create(
        FuncType, Function::ExternalLinkage, "assign_memory_bank", TestModule.get());
    
    // Name function arguments
    auto ArgIter = AssignFunc->arg_begin();
    Value *Ptr = ArgIter++;
    Ptr->setName("ptr");
    Value *Size = ArgIter++;
    Size->setName("size");
    Value *AccessPattern = ArgIter++;
    AccessPattern->setName("access_pattern");
    
    // Create basic block
    BasicBlock *EntryBB = BasicBlock::Create(Context, "entry", AssignFunc);
    Builder.SetInsertPoint(EntryBB);
    
    // Create switch statement for different access patterns
    Value *DefaultBankID = ConstantInt::get(Int32Ty, 2);  // Default to DRAM
    
    // Create a switch for different access patterns
    SwitchInst *PatternSwitch = Builder.CreateSwitch(AccessPattern, EntryBB, 3);
    
    // Random access pattern - use fast SRAM for small allocations
    BasicBlock *RandomAccessBB = BasicBlock::Create(Context, "random_access", AssignFunc);
    Builder.SetInsertPoint(RandomAccessBB);
    Value *SmallSize = ConstantInt::get(Int32Ty, 4096);  // 4KB threshold
    Value *IsSmall = Builder.CreateICmpSLT(Size, SmallSize, "is_small");
    Value *RandomBankID = Builder.CreateSelect(IsSmall, 
                                             ConstantInt::get(Int32Ty, 0),  // SRAM_Fast
                                             ConstantInt::get(Int32Ty, 1),  // SRAM_Dense
                                             "random_bank_id");
    Builder.CreateBr(EntryBB);
    
    // Sequential access pattern - use dense SRAM or DRAM
    BasicBlock *SequentialAccessBB = BasicBlock::Create(Context, "sequential_access", AssignFunc);
    Builder.SetInsertPoint(SequentialAccessBB);
    Value *MediumSize = ConstantInt::get(Int32Ty, 65536);  // 64KB threshold
    Value *IsMedium = Builder.CreateICmpSLT(Size, MediumSize, "is_medium");
    Value *SequentialBankID = Builder.CreateSelect(IsMedium,
                                                 ConstantInt::get(Int32Ty, 1),  // SRAM_Dense
                                                 ConstantInt::get(Int32Ty, 2),  // DRAM_Local
                                                 "sequential_bank_id");
    Builder.CreateBr(EntryBB);
    
    // Persistent storage pattern - use non-volatile memory
    BasicBlock *PersistentAccessBB = BasicBlock::Create(Context, "persistent_access", AssignFunc);
    Builder.SetInsertPoint(PersistentAccessBB);
    Value *PersistentBankID = ConstantInt::get(Int32Ty, 3);  // NVM
    Builder.CreateBr(EntryBB);
    
    // Add switch cases
    PatternSwitch->addCase(ConstantInt::get(Int32Ty, 0), RandomAccessBB);      // Random access
    PatternSwitch->addCase(ConstantInt::get(Int32Ty, 1), SequentialAccessBB);  // Sequential access
    PatternSwitch->addCase(ConstantInt::get(Int32Ty, 2), PersistentAccessBB);  // Persistent storage
    
    // Set the default case to return default bank ID
    Builder.SetInsertPoint(EntryBB);
    PHINode *BankIDPhi = Builder.CreatePHI(Int32Ty, 4, "bank_id");
    BankIDPhi->addIncoming(DefaultBankID, PatternSwitch->getParent());
    BankIDPhi->addIncoming(RandomBankID, RandomAccessBB);
    BankIDPhi->addIncoming(SequentialBankID, SequentialAccessBB);
    BankIDPhi->addIncoming(PersistentBankID, PersistentAccessBB);
    
    // Add metadata for PIM memory bank assignment
    LLVMContext &Ctx = AssignFunc->getContext();
    MDNode *PIMBankAssignMD = MDNode::get(Ctx, {});
    AssignFunc->setMetadata("pim.bank_assignment", PIMBankAssignMD);
    
    // Return the bank ID
    Builder.CreateRet(BankIDPhi);
    
    // Verify the function
    bool HasError = verifyFunction(*AssignFunc, &errs());
    if (HasError) {
      errs() << "Error in memory bank assignment function\n";
      AssignFunc->eraseFromParent();
      return nullptr;
    }
    
    return AssignFunc;
  }
  
  // Helper function to create a function with different memory access patterns
  Function* createMemoryAccessPatternsFunction() {
    // Define function type: void accessPatterns(float* random, float* sequential, float* reduction, int size)
    Type *FloatPtrTy = Type::getFloatPtrTy(Context);
    Type *Int32Ty = Type::getInt32Ty(Context);
    
    std::vector<Type*> ParamTypes = {FloatPtrTy, FloatPtrTy, FloatPtrTy, Int32Ty};
    FunctionType *FuncType = FunctionType::get(
        Type::getVoidTy(Context), ParamTypes, false);
    
    // Create function
    Function *AccessFunc = Function::Create(
        FuncType, Function::ExternalLinkage, "access_patterns", TestModule.get());
    
    // Name function arguments
    auto ArgIter = AccessFunc->arg_begin();
    Value *RandomArray = ArgIter++;
    RandomArray->setName("random");
    Value *SequentialArray = ArgIter++;
    SequentialArray->setName("sequential");
    Value *ReductionArray = ArgIter++;
    ReductionArray->setName("reduction");
    Value *Size = ArgIter++;
    Size->setName("size");
    
    // Create basic block
    BasicBlock *EntryBB = BasicBlock::Create(Context, "entry", AccessFunc);
    Builder.SetInsertPoint(EntryBB);
    
    // Allocate loop counters
    AllocaInst *IVar = Builder.CreateAlloca(Int32Ty, nullptr, "i");
    
    // Initialize loop counter
    Value *Zero = ConstantInt::get(Int32Ty, 0);
    Value *One = ConstantInt::get(Int32Ty, 1);
    Builder.CreateStore(Zero, IVar);
    
    // Create loop blocks
    BasicBlock *LoopHeader = BasicBlock::Create(Context, "loop.header", AccessFunc);
    BasicBlock *LoopBody = BasicBlock::Create(Context, "loop.body", AccessFunc);
    BasicBlock *LoopLatch = BasicBlock::Create(Context, "loop.latch", AccessFunc);
    BasicBlock *LoopExit = BasicBlock::Create(Context, "loop.exit", AccessFunc);
    
    // Jump to loop header
    Builder.CreateBr(LoopHeader);
    
    // Loop header
    Builder.SetInsertPoint(LoopHeader);
    Value *I = Builder.CreateLoad(Int32Ty, IVar, "i.val");
    Value *ExitCond = Builder.CreateICmpSLT(I, Size, "exit.cond");
    Builder.CreateCondBr(ExitCond, LoopBody, LoopExit);
    
    // Loop body
    Builder.SetInsertPoint(LoopBody);
    
    // Random access pattern - use a hash function to access elements
    Value *Hash = Builder.CreateMul(I, ConstantInt::get(Int32Ty, 17), "hash");
    Value *HashMod = Builder.CreateURem(Hash, Size, "hash.mod");
    Value *RandomPtr = Builder.CreateGEP(Type::getFloatTy(Context), RandomArray, HashMod, "random.ptr");
    Value *RandomVal = Builder.CreateLoad(Type::getFloatTy(Context), RandomPtr, "random.val");
    Value *RandomVal2 = Builder.CreateFAdd(RandomVal, ConstantFP::get(Type::getFloatTy(Context), 1.0), "random.val2");
    Builder.CreateStore(RandomVal2, RandomPtr);
    
    // Set metadata for random access pattern
    LoadInst *RandomLoad = dyn_cast<LoadInst>(RandomVal);
    if (RandomLoad) {
      MDNode *RandomAccessMD = MDNode::get(Context, {});
      RandomLoad->setMetadata("pim.random_access", RandomAccessMD);
    }
    
    // Sequential access pattern - access elements in order
    Value *SequentialPtr = Builder.CreateGEP(Type::getFloatTy(Context), SequentialArray, I, "seq.ptr");
    Value *SequentialVal = Builder.CreateLoad(Type::getFloatTy(Context), SequentialPtr, "seq.val");
    Value *SequentialVal2 = Builder.CreateFAdd(SequentialVal, ConstantFP::get(Type::getFloatTy(Context), 2.0), "seq.val2");
    Builder.CreateStore(SequentialVal2, SequentialPtr);
    
    // Set metadata for sequential access pattern
    LoadInst *SeqLoad = dyn_cast<LoadInst>(SequentialVal);
    if (SeqLoad) {
      MDNode *SequentialAccessMD = MDNode::get(Context, {});
      SeqLoad->setMetadata("pim.sequential_access", SequentialAccessMD);
    }
    
    // Reduction pattern - accumulate into a single element
    Value *ReductionPtr = Builder.CreateGEP(Type::getFloatTy(Context), ReductionArray, Zero, "red.ptr");
    Value *ReductionVal = Builder.CreateLoad(Type::getFloatTy(Context), ReductionPtr, "red.val");
    Value *ElementPtr = Builder.CreateGEP(Type::getFloatTy(Context), SequentialArray, I, "elem.ptr");
    Value *ElementVal = Builder.CreateLoad(Type::getFloatTy(Context), ElementPtr, "elem.val");
    Value *ReductionSum = Builder.CreateFAdd(ReductionVal, ElementVal, "red.sum");
    Builder.CreateStore(ReductionSum, ReductionPtr);
    
    // Set metadata for reduction pattern
    StoreInst *ReductionStore = dyn_cast<StoreInst>(Builder.GetInsertBlock()->getTerminator()->getPrevNode());
    if (ReductionStore) {
      MDNode *ReductionMD = MDNode::get(Context, {});
      ReductionStore->setMetadata("pim.reduction", ReductionMD);
    }
    
    // Loop increment
    Builder.CreateBr(LoopLatch);
    Builder.SetInsertPoint(LoopLatch);
    Value *NextI = Builder.CreateAdd(I, One, "i.next");
    Builder.CreateStore(NextI, IVar);
    Builder.CreateBr(LoopHeader);
    
    // Loop exit
    Builder.SetInsertPoint(LoopExit);
    Builder.CreateRetVoid();
    
    // Verify the function
    bool HasError = verifyFunction(*AccessFunc, &errs());
    if (HasError) {
      errs() << "Error in memory access patterns function\n";
      AccessFunc->eraseFromParent();
      return nullptr;
    }
    
    return AccessFunc;
  }
};

// Test memory bank assignment
TEST_F(MemoryMappingTest, MemoryBankAssignment) {
  // Create memory bank assignment function
  Function *AssignFunc = createMemoryBankAssignmentFunction();
  ASSERT_NE(AssignFunc, nullptr);
  
  // Verify metadata is correctly attached
  MDNode *BankAssignMD = AssignFunc->getMetadata("pim.bank_assignment");
  EXPECT_NE(BankAssignMD, nullptr);
  
  // Basic block counts
  EXPECT_EQ(AssignFunc->size(), 5U);
  
  // Test memory allocation based on access pattern
  const auto &MemoryBanks = PIMDevice.getMemoryBanks();
  ASSERT_GE(MemoryBanks.size(), 4U);
  
  // Random access pattern with small allocation should use fast SRAM
  std::string ErrorMsg;
  bool Allocated = PIMDevice.allocateMemory(0, 2048, ErrorMsg);
  EXPECT_TRUE(Allocated) << ErrorMsg;
  
  // Sequential access pattern with medium allocation should use dense SRAM
  Allocated = PIMDevice.allocateMemory(1, 32768, ErrorMsg);
  EXPECT_TRUE(Allocated) << ErrorMsg;
  
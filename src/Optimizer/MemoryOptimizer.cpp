//===-- MemoryOptimizer.cpp - Memory Optimization for PIM Architecture --===//
//
// This file implements memory-specific optimizations for Processor-in-Memory
// architectures, focusing on data layout and access patterns.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"

#include "PIMCompiler.h"
#include "TargetMachine.h"
#include "InstructionSet.h"

using namespace llvm;

namespace {

// Memory bank structure for PIM architecture
struct PIMMemoryBank {
  unsigned ID;
  unsigned Size;
  unsigned AccessLatency;
};

class MemoryOptimizer : public ModulePass {
public:
  static char ID;
  MemoryOptimizer() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.setPreservesAll();  // We only add metadata, so preserve all analyses
  }

  bool runOnModule(Module &M) override {
    bool Modified = false;
    
    // Log the module being processed
    errs() << "PIM Memory Optimizer running on module: " << M.getName() << "\n";
    
    // Configure PIM memory banks based on target architecture
    configurePIMMemoryBanks();
    
    // Optimize global variables layout for PIM architecture
    Modified |= optimizeGlobalLayout(M);
    
    // Optimize memory access patterns in functions
    for (Function &F : M) {
      if (!F.isDeclaration()) {
        Modified |= optimizeFunctionMemoryAccess(F);
      }
    }
    
    return Modified;
  }

private:
  std::vector<PIMMemoryBank> MemoryBanks;
  
  void configurePIMMemoryBanks() {
    // Configure memory banks according to PIM architecture
    // For example, a typical PIM architecture might have:
    // - Multiple small, fast SRAM banks within compute units
    // - Larger, slightly slower shared memory banks
    
    // Bank 0: Fast SRAM for compute-intensive data
    MemoryBanks.push_back({0, 64 * 1024, 1});  // 64KB with 1-cycle latency
    
    // Bank 1: Medium SRAM for frequently accessed data
    MemoryBanks.push_back({1, 256 * 1024, 2});  // 256KB with 2-cycle latency
    
    // Bank 2: Large shared memory for bulk data
    MemoryBanks.push_back({2, 4 * 1024 * 1024, 4});  // 4MB with 4-cycle latency
    
    errs() << "  Configured " << MemoryBanks.size() << " PIM memory banks\n";
  }
  
  bool optimizeGlobalLayout(Module &M) {
    bool Modified = false;
    
    errs() << "  Optimizing global variable layout for PIM architecture\n";
    
    // Analyze global variables to determine optimal memory bank assignment
    for (GlobalVariable &GV : M.globals()) {
      if (GV.hasInitializer()) {
        // Determine the size of the global variable
        Type *GVType = GV.getValueType();
        const DataLayout &DL = M.getDataLayout();
        unsigned Size = DL.getTypeAllocSize(GVType);
        
        // Assign to appropriate memory bank based on size and usage pattern
        unsigned BankID = assignToBestMemoryBank(Size, &GV);
        
        // Set metadata to indicate memory bank assignment
        LLVMContext &Ctx = M.getContext();
        Metadata *BankIDMD = ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt32Ty(Ctx), BankID));
        GV.setMetadata("pim.memory_bank", 
                       MDNode::get(Ctx, {BankIDMD}));
        
        errs() << "    Assigned global variable '" << GV.getName() 
               << "' to memory bank " << BankID << "\n";
        Modified = true;
      }
    }
    
    return Modified;
  }
  
  bool optimizeFunctionMemoryAccess(Function &F) {
    bool Modified = false;
    
    errs() << "  Optimizing memory access in function: " << F.getName() << "\n";
    
    // Get analysis results we need
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();
    
    // Identify arrays accessed in loops for data layout optimization
    for (Loop *L : LI) {
      Modified |= optimizeLoopDataLayout(L, SE, F);
    }
    
    // Look for memory access patterns that can benefit from PIM operations
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *LI = dyn_cast<LoadInst>(&I)) {
          Modified |= optimizeMemoryLoad(LI);
        } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
          Modified |= optimizeMemoryStore(SI);
        }
      }
    }
    
    // Identify stack allocations that should be placed in specific memory banks
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *AI = dyn_cast<AllocaInst>(&I)) {
          Modified |= optimizeStackAllocation(AI);
        }
      }
    }
    
    return Modified;
  }
  
  bool optimizeLoopDataLayout(Loop *L, ScalarEvolution &SE, Function &F) {
    bool Modified = false;
    
    // Check memory access patterns within the loop
    SmallVector<BasicBlock *, 8> Blocks(L->getBlocks().begin(), L->getBlocks().end());
    
    for (BasicBlock *BB : Blocks) {
      for (Instruction &I : *BB) {
        // Look for array accesses in the loop
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&I)) {
          // Try to analyze the access pattern using ScalarEvolution
          if (const SCEVAddRecExpr *AR = 
                dyn_cast_or_null<SCEVAddRecExpr>(SE.getSCEV(GEP))) {
            
            // Determine if this is a strided access pattern
            if (AR->isAffine()) {
              const SCEV *Stride = AR->getStepRecurrence(SE);
              
              // If we can determine it's a constant stride
              if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Stride)) {
                int StrideValue = SC->getAPInt().getSExtValue();
                
                // Set metadata about the memory access pattern
                LLVMContext &Ctx = F.getContext();
                Metadata *StrideValueMD = ConstantAsMetadata::get(
                    ConstantInt::get(Type::getInt32Ty(Ctx), StrideValue));
                GEP->setMetadata("pim.stride_pattern", 
                               MDNode::get(Ctx, {StrideValueMD}));
                
                errs() << "    Identified stride pattern " << StrideValue 
                       << " in loop\n";
                Modified = true;
              }
            }
          }
        }
      }
    }
    
    // Process nested loops
    for (Loop *SubL : L->getSubLoops()) {
      Modified |= optimizeLoopDataLayout(SubL, SE, F);
    }
    
    return Modified;
  }
  
  bool optimizeMemoryLoad(LoadInst *LI) {
    // Check if this load can benefit from PIM operations
    Type *LoadType = LI->getType();
    
    // Vector loads are good candidates for PIM operations
    if (LoadType->isVectorTy()) {
      LI->setMetadata("pim.vector_load", 
                     MDNode::get(LI->getContext(), {}));
      return true;
    }
    
    // Check for loads within loops that access memory with patterns
    // beneficial for PIM operations (e.g., strided access)
    if (LI->getMetadata("pim.stride_pattern")) {
      LI->setMetadata("pim.optimize", 
                     MDNode::get(LI->getContext(), {}));
      return true;
    }
    
    return false;
  }
  
  bool optimizeMemoryStore(StoreInst *SI) {
    // Similar to optimizeMemoryLoad, but for stores
    Value *StoredValue = SI->getValueOperand();
    
    if (StoredValue->getType()->isVectorTy()) {
      SI->setMetadata("pim.vector_store", 
                     MDNode::get(SI->getContext(), {}));
      return true;
    }
    
    // Check for reduction patterns (e.g., accumulation)
    if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(StoredValue)) {
      if (BinOp->getOpcode() == Instruction::Add || 
          BinOp->getOpcode() == Instruction::FAdd) {
        // Check if one operand is loaded from the same address
        if (LoadInst *LI = dyn_cast<LoadInst>(BinOp->getOperand(0))) {
          if (LI->getPointerOperand() == SI->getPointerOperand()) {
            SI->setMetadata("pim.reduction", 
                           MDNode::get(SI->getContext(), {}));
            return true;
          }
        } else if (LoadInst *LI = dyn_cast<LoadInst>(BinOp->getOperand(1))) {
          if (LI->getPointerOperand() == SI->getPointerOperand()) {
            SI->setMetadata("pim.reduction", 
                           MDNode::get(SI->getContext(), {}));
            return true;
          }
        }
      }
    }
    
    return false;
  }
  
  bool optimizeStackAllocation(AllocaInst *AI) {
    // Determine optimal memory bank for stack allocations
    const DataLayout &DL = AI->getModule()->getDataLayout();
    Type *AllocType = AI->getAllocatedType();
    unsigned Size = DL.getTypeAllocSize(AllocType);
    
    // Assign stack allocation to appropriate memory bank
    unsigned BankID = assignToBestMemoryBank(Size, AI);
    
    // Set metadata to indicate memory bank assignment
    LLVMContext &Ctx = AI->getContext();
    Metadata *BankIDMD = ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(Ctx), BankID));
    AI->setMetadata("pim.memory_bank", 
                   MDNode::get(Ctx, {BankIDMD}));
    
    return true;
  }
  
  unsigned assignToBestMemoryBank(unsigned Size, Value *V) {
    // Simple heuristic to assign memory to appropriate bank
    // In a real implementation, this would be more sophisticated
    // considering access patterns, data reuse, etc.
    
    // Small allocations go to fast SRAM
    if (Size < 1024) {
      return 0;  // Fast SRAM bank
    }
    
    // Medium allocations go to medium SRAM
    if (Size < 32 * 1024) {
      return 1;  // Medium SRAM bank
    }
    
    // Large allocations go to the large shared memory
    return 2;  // Large shared memory bank
  }
};

char MemoryOptimizer::ID = 0;

// Register the pass
static RegisterPass<MemoryOptimizer> X("pim-memory-opt", "PIM Memory Optimizer",
                                       false, false);

// Register memory optimizer pass to be included in the standard optimization pipeline
static RegisterStandardPasses Y(PassManagerBuilder::EP_ModuleOptimizerEarly,
  [](const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
    PM.add(new MemoryOptimizer());
  });

} // end anonymous namespace
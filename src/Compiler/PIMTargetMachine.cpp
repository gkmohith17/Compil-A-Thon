#include "TargetMachine.h"
#include "InstructionSet.h"
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetOptions.h>
#include <iostream>

namespace PIMCompiler {

PIMTargetMachine::PIMTargetMachine(const llvm::Target &T, const llvm::Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const llvm::TargetOptions &Options,
                                   llvm::Optional<llvm::Reloc::Model> RM,
                                   llvm::Optional<llvm::CodeModel::Model> CM,
                                   llvm::CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T, "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128", 
                        TT, CPU, FS, Options, 
                        RM.getValueOr(llvm::Reloc::Static),
                        CM.getValueOr(llvm::CodeModel::Small), 
                        OL, JIT),
      PIMSTI(*this, TT.isArch64Bit() ? PIMISA::PIM64 : PIMISA::PIM32) {
  initAsmInfo();
  std::cout << "Initialized PIM Target Machine\n";
}

bool PIMTargetMachine::addPassesToEmitFile(llvm::legacy::PassManagerBase &PM,
                                          llvm::raw_pwrite_stream &Out,
                                          llvm::raw_pwrite_stream *DwoOut,
                                          llvm::CodeGenFileType FileType,
                                          bool DisableVerify,
                                          llvm::MachineModuleInfoWrapperPass *MMIWP) {
  // Initialize PIM-specific passes
  PM.add(createPIMOptimizationPass());

  // Initialize memory mapping pass
  PM.add(createPIMMemoryMapperPass());

  // Initialize instruction selection and encoding passes
  PM.add(createPIMInstructionEncoderPass(Out));

  return false; // No error
}

void PIMTargetMachine::anchor() {}

extern "C" void LLVMInitializePIMTarget() {
  // Register the target
  RegisterTargetMachine<PIMTargetMachine> X(ThePIMTarget);
  
  // Register ASM parser/printer if needed
  // RegisterAsmInfo<PIMASMInfo> Y(ThePIMTarget);
  
  std::cout << "PIM Target initialized\n";
}

}  // namespace PIMCompiler
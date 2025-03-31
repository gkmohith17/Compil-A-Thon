#ifndef PIM_TARGET_MACHINE_H
#define PIM_TARGET_MACHINE_H

#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"

namespace pim {

class PIMTargetMachine : public llvm::TargetMachine {
public:
  PIMTargetMachine(const llvm::Target &T, 
                   const llvm::Triple &TT,
                   llvm::StringRef CPU, 
                   llvm::StringRef FS,
                   const llvm::TargetOptions &Options,
                   llvm::Optional<llvm::Reloc::Model> RM,
                   llvm::Optional<llvm::CodeModel::Model> CM,
                   llvm::CodeGenOpt::Level OL, 
                   bool JIT);

  // Required target machine interfaces
  llvm::TargetPassConfig *createPassConfig(llvm::PassManagerBase &PM) override;
  
  bool addPassesToEmitFile(llvm::PassManagerBase &PM, 
                           llvm::raw_pwrite_stream &Out,
                           llvm::raw_pwrite_stream *DwoOut,
                           llvm::CodeGenFileType FileType,
                           bool DisableVerify = true) override;

  // PIM specific methods
  void setMemoryConfig(uint32_t banks, uint32_t bankSize);
  void setProcessingConfig(uint32_t threads);

private:
  uint32_t memBanks;
  uint32_t memBankSize;
  uint32_t processingThreads;
};

// Initialize the target registry for PIM targets
void initializePIMTargetInfo();
void initializePIMTarget();
void initializePIMTargetMC();

} // namespace pim

#endif // PIM_TARGET_MACHINE_H
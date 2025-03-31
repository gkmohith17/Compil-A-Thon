#ifndef PIM_COMPILER_H
#define PIM_COMPILER_H

#include <string>
#include <vector>
#include <memory>
#include "llvm/Target/TargetMachine.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"

namespace pim {

enum class OptimizationLevel {
  O0, // No optimization
  O1, // Basic optimizations
  O2, // Moderate optimizations
  O3  // Aggressive optimizations
};

class PIMCompiler {
public:
  PIMCompiler();
  ~PIMCompiler();

  // Compiles a source file to PIM binary
  bool compile(const std::string& sourceFile, 
               const std::string& outputFile,
               OptimizationLevel optLevel = OptimizationLevel::O2);
  
  // Compiles LLVM IR directly
  bool compileIR(llvm::Module& module,
                 const std::string& outputFile,
                 OptimizationLevel optLevel = OptimizationLevel::O2);
  
  // Set PIM-specific target options
  void setMemoryLayout(uint32_t banks, uint32_t bankSize);
  void setThreadCount(uint32_t threads);
  
  // Get error message if compilation fails
  std::string getErrorMessage() const;

private:
  // Internal compilation pipeline
  bool runOptimizationPasses(llvm::Module& module, OptimizationLevel optLevel);
  bool generateTargetCode(llvm::Module& module, const std::string& outputFile);
  
  // Error handling
  std::string errorMessage;
  
  // LLVM context
  std::unique_ptr<llvm::LLVMContext> context;
};

} // namespace pim

#endif // PIM_COMPILER_H
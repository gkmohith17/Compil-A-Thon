#include "InstructionSet.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <cstdint>

namespace pim {

class PIMInstructionEncoder {
public:
  PIMInstructionEncoder(const PIMInstrInfo &instrInfo);
  
  // Encode a single instruction
  uint32_t encodeInstruction(const llvm::MCInst &inst);
  
  // Decode an encoded instruction
  llvm::MCInst decodeInstruction(uint32_t encodedInst);
  
  // Encode a sequence of instructions
  std::vector<uint32_t> encodeInstructions(const std::vector<llvm::MCInst> &insts);
  
  // Generate binary representation of encoded instructions
  void writeBinaryFile(const std::vector<uint32_t> &encodedInsts, 
                       llvm::raw_ostream &OS);
  
  // Dump instruction encoding for debugging
  void dumpEncoding(uint32_t encodedInst, llvm::raw_ostream &OS);

private:
  const PIMInstrInfo &instrInfo;
};

PIMInstructionEncoder::PIMInstructionEncoder(const PIMInstrInfo &info)
  : instrInfo(info) {
}

uint32_t PIMInstructionEncoder::encodeInstruction(const llvm::MCInst &inst) {
  return instrInfo.encodeInstruction(inst);
}

llvm::MCInst PIMInstructionEncoder::decodeInstruction(uint32_t encodedInst) {
  llvm::MCInst inst;
  instrInfo.decodeInstruction(encodedInst, inst);
  return inst;
}

std::vector<uint32_t> PIMInstructionEncoder::encodeInstructions(
    const std::vector<llvm::MCInst> &insts) {
  std::vector<uint32_t> result;
  result.reserve(insts.size());
  
  for (const auto &inst : insts) {
    result.push_back(encodeInstruction(inst));
  }
  
  return result;
}

void PIMInstructionEncoder::writeBinaryFile(
    const std::vector<uint32_t> &encodedInsts, llvm::raw_ostream &OS) {
  // Write header
  constexpr uint32_t MAGIC = 0x504D4950; // "PIMP" in ASCII
  constexpr uint32_t VERSION = 0x00000001;
  
  OS.write(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC));
  OS.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));
  
  // Write instruction count
  uint32_t count = encodedInsts.size();
  OS.write(reinterpret_cast<const char*>(&count), sizeof(count));
  
  // Write encoded instructions
  for (const auto &inst : encodedInsts) {
    OS.write(reinterpret_cast<const char*>(&inst), sizeof(inst));
  }
  
  // Write footer
  OS.write(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC));
}

void PIMInstructionEncoder::dumpEncoding(uint32_t encodedInst, llvm::raw_ostream &OS) {
  const PIMInstructionFormat &format = instrInfo.getInstructionFormat();
  
  // Extract opcode
  unsigned opcode = (encodedInst >> (32 - format.opcodeBits)) & 
                    ((1 << format.opcodeBits) - 1);
  
  OS << "Instruction: 0x" << llvm::format_hex(encodedInst, 8) << "\n";
  OS << "  Opcode: " << opcode << " (" << PIMInstrInfo::getOpcodeName(opcode) << ")\n";
  
  // Determine number and type of operands based on opcode
  llvm::MCInst decodedInst = decodeInstruction(encodedInst);
  
  OS << "  Operands:\n";
  for (unsigned i = 0; i < decodedInst.getNumOperands(); ++i) {
    const llvm::MCOperand &Op = decodedInst.getOperand(i);
    OS << "    " << i << ": ";
    
    if (Op.isReg()) {
      OS << "Register " << Op.getReg() << "\n";
    } else if (Op.isImm()) {
      OS << "Immediate " << Op.getImm() << " (0x" 
         << llvm::format_hex(static_cast<uint32_t>(Op.getImm()), 0) << ")\n";
    } else {
      OS << "Unknown operand type\n";
    }
  }
}

} // namespace pim
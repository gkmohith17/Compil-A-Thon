#include "InstructionSet.h"
#include <llvm/ADT/STLExtras.h>
#include <llvm/CodeGen/MachineInstrBuilder.h>
#include <llvm/MC/MCInstrDesc.h>
#include <iostream>

namespace PIMCompiler {

PIMInstrInfo::PIMInstrInfo(PIMSubtarget &STI)
    : PIMGenInstrInfo(PIMISA::LOAD, PIMISA::STORE),
      Subtarget(STI) {
  std::cout << "PIM Instruction Info initialized\n";
}

void PIMInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              const DebugLoc &DL, MCRegister DestReg,
                              MCRegister SrcReg, bool KillSrc) const {
  // Implementation of physical register copying for PIM architecture
  BuildMI(MBB, MI, DL, get(PIMISA::COPY), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
  
  std::cout << "Physical register copy: " << DestReg << " <- " << SrcReg << "\n";
}

void PIMInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator MI,
                                      Register SrcReg, bool isKill, int FrameIdx,
                                      const TargetRegisterClass *RC,
                                      const TargetRegisterInfo *TRI) const {
  // Implementation for storing register to stack
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  BuildMI(MBB, MI, DL, get(PIMISA::STORE))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx)
      .addImm(0);
  
  std::cout << "Store register to stack: " << SrcReg << " -> [" << FrameIdx << "]\n";
}

void PIMInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register DestReg, int FrameIdx,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  // Implementation for loading register from stack
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  BuildMI(MBB, MI, DL, get(PIMISA::LOAD), DestReg)
      .addFrameIndex(FrameIdx)
      .addImm(0);
  
  std::cout << "Load register from stack: " << DestReg << " <- [" << FrameIdx << "]\n";
}

uint64_t PIMInstrInfo::encodeInstruction(const MachineInstr &MI) const {
  // Basic instruction encoding for PIM architecture
  uint64_t encoding = 0;
  
  // Extract opcode
  uint8_t opcode = MI.getOpcode() & 0xFF;
  encoding |= (static_cast<uint64_t>(opcode) << 56);
  
  // Encode operands - this is a simplified version
  for (unsigned i = 0; i < MI.getNumOperands() && i < 3; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg()) {
      uint16_t reg = MO.getReg() & 0xFFFF;
      encoding |= (static_cast<uint64_t>(reg) << (40 - i * 16));
    } else if (MO.isImm()) {
      uint16_t imm = MO.getImm() & 0xFFFF;
      encoding |= (static_cast<uint64_t>(imm) << (40 - i * 16));
    }
  }
  
  std::cout << "Encoded instruction: 0x" << std::hex << encoding << std::dec << "\n";
  return encoding;
}

bool PIMInstrInfo::isMemoryAccess(const MachineInstr &MI) const {
  // Check if instruction accesses memory
  switch (MI.getOpcode()) {
    case PIMISA::LOAD:
    case PIMISA::STORE:
    case PIMISA::MATRIX_LOAD:
    case PIMISA::MATRIX_STORE:
      return true;
    default:
      return false;
  }
}

bool PIMInstrInfo::isMatrixOperation(const MachineInstr &MI) const {
  // Check if instruction performs matrix operation
  switch (MI.getOpcode()) {
    case PIMISA::MATRIX_MUL:
    case PIMISA::MATRIX_ADD:
    case PIMISA::MATRIX_SUB:
    case PIMISA::MATRIX_TRANSPOSE:
      return true;
    default:
      return false;
  }
}

llvm::MachineFunctionPass *createPIMOptimizationPass() {
  return new PIMOptimizationPass();
}

}  // namespace PIMCompiler
// include/PIMInstructionSet.hpp
#pragma once
#include <cstdint>
#include <vector>
#include <memory>

namespace PIM {
    // Custom Instruction Encoding
    struct Instruction {
        enum class OpCode : uint8_t {
            LOAD = 0x01,
            STORE = 0x02,
            COMPUTE = 0x03,
            MATRIX_MUL = 0x04,
            SYNC = 0x05
        };

        OpCode opcode;
        uint64_t sourceAddress;
        uint64_t destinationAddress;
        uint32_t size;
        std::vector<uint64_t> operands;

        // Instruction Encoding Method
        std::vector<uint8_t> encode() const {
            std::vector<uint8_t> encodedInstruction;
            
            // Encode Opcode
            encodedInstruction.push_back(static_cast<uint8_t>(opcode));
            
            // Encode Addresses
            for (auto addr : {sourceAddress, destinationAddress}) {
                for (size_t i = 0; i < sizeof(uint64_t); ++i) {
                    encodedInstruction.push_back((addr >> (i * 8)) & 0xFF);
                }
            }

            // Encode Size
            for (size_t i = 0; i < sizeof(uint32_t); ++i) {
                encodedInstruction.push_back((size >> (i * 8)) & 0xFF);
            }

            // Encode Operands
            for (auto operand : operands) {
                for (size_t i = 0; i < sizeof(uint64_t); ++i) {
                    encodedInstruction.push_back((operand >> (i * 8)) & 0xFF);
                }
            }

            return encodedInstruction;
        }
    };

    // Instruction Stream Generator
    class InstructionGenerator {
    public:
        static std::vector<Instruction> generateMatrixMultiplicationInstructions(
            const Matrix<int>& A, 
            const Matrix<int>& B
        ) {
            std::vector<Instruction> instructions;

            // Load Matrix A
            instructions.push_back({
                Instruction::OpCode::LOAD,
                reinterpret_cast<uint64_t>(A.data()),
                0,
                static_cast<uint32_t>(A.getRows() * A.getCols() * sizeof(int)),
                {}
            });

            // Load Matrix B
            instructions.push_back({
                Instruction::OpCode::LOAD,
                reinterpret_cast<uint64_t>(B.data()),
                0,
                static_cast<uint32_t>(B.getRows() * B.getCols() * sizeof(int)),
                {}
            });

            // Perform Matrix Multiplication
            instructions.push_back({
                Instruction::OpCode::MATRIX_MUL,
                reinterpret_cast<uint64_t>(A.data()),
                reinterpret_cast<uint64_t>(B.data()),
                static_cast<uint32_t>(A.getRows() * B.getCols() * sizeof(int)),
                {
                    A.getRows(),
                    A.getCols(),
                    B.getCols()
                }
            });

            // Synchronization
            instructions.push_back({
                Instruction::OpCode::SYNC,
                0,
                0,
                0,
                {}
            });

            return instructions;
        }
    };
}
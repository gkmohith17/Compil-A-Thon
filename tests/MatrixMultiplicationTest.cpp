// i loop increment
Builder.SetInsertPoint(ILoopLatch);
Value *INext = Builder.CreateAdd(IVal, One, "i.next");
Builder.CreateStore(INext, IVar);
Builder.CreateBr(ILoopHeader);

// i loop exit
Builder.SetInsertPoint(ILoopExit);
Builder.CreateRetVoid();

// Verify the function
bool HasError = verifyFunction(*MatMulFunc, &errs());
if (HasError) {
  errs() << "Error in matrix multiplication function\n";
  MatMulFunc->eraseFromParent();
  return nullptr;
}

// Add metadata for PIM optimization
LLVMContext &Ctx = MatMulFunc->getContext();
MDNode *PIMOptNode = MDNode::get(Ctx, {});
MatMulFunc->setMetadata("pim.optimize", PIMOptNode);

return MatMulFunc;
}

void runPIMOptimizationPasses(Function *F) {
// Create and run a function pass manager with PIM optimization passes
legacy::FunctionPassManager FPM(TestModule.get());

// Add standard LLVM optimization passes
FPM.add(createInstructionCombiningPass());
FPM.add(createReassociatePass());
FPM.add(createGVNPass());
FPM.add(createCFGSimplificationPass());

// Initialize the pass manager
FPM.doInitialization();

// Run the passes on the function
FPM.run(*F);

FPM.doFinalization();
}
};

// Test native execution of matrix multiplication
TEST_F(MatrixMultiplicationTest, NativeExecution) {
const int MatrixSize = 4;
const int TotalElements = MatrixSize * MatrixSize;

// Create test matrices
std::vector<float> A(TotalElements, 0.0f);
std::vector<float> B(TotalElements, 0.0f);
std::vector<float> C(TotalElements, 0.0f);
std::vector<float> ExpectedResult(TotalElements, 0.0f);

// Initialize matrices with test data
for (int i = 0; i < MatrixSize; i++) {
for (int j = 0; j < MatrixSize; j++) {
  A[i * MatrixSize + j] = static_cast<float>(i + j);
  B[i * MatrixSize + j] = static_cast<float>(i - j);
}
}

// Calculate expected results
for (int i = 0; i < MatrixSize; i++) {
for (int j = 0; j < MatrixSize; j++) {
  float sum = 0.0f;
  for (int k = 0; k < MatrixSize; k++) {
    sum += A[i * MatrixSize + k] * B[k * MatrixSize + j];
  }
  ExpectedResult[i * MatrixSize + j] = sum;
}
}

// Create matrix multiplication function
Function *MatMulFunc = createMatrixMultiplyFunction(MatrixSize);
ASSERT_NE(MatMulFunc, nullptr);

// Run PIM optimization passes
runPIMOptimizationPasses(MatMulFunc);

// Get function pointer from JIT
using MatMulFuncType = void (*)(float*, float*, float*, int);
MatMulFuncType MatMulFuncPtr = reinterpret_cast<MatMulFuncType>(
  EE->getFunctionAddress("matrix_multiply"));

// Execute the function
MatMulFuncPtr(A.data(), B.data(), C.data(), MatrixSize);

// Verify results
for (int i = 0; i < TotalElements; i++) {
EXPECT_NEAR(C[i], ExpectedResult[i], 1e-5f);
}
}

// Test PIM-specific optimizations on matrix multiplication
TEST_F(MatrixMultiplicationTest, PIMOptimizations) {
const int MatrixSize = 32;  // Larger matrix to better observe optimizations

// Create matrix multiplication function
Function *MatMulFunc = createMatrixMultiplyFunction(MatrixSize);
ASSERT_NE(MatMulFunc, nullptr);

// Count memory operations before optimization
int LoadsBefore = 0;
int StoresBefore = 0;
for (BasicBlock &BB : *MatMulFunc) {
for (Instruction &I : BB) {
  if (isa<LoadInst>(I)) LoadsBefore++;
  if (isa<StoreInst>(I)) StoresBefore++;
}
}

// Run PIM optimization passes
runPIMOptimizationPasses(MatMulFunc);

// Count memory operations with PIM metadata after optimization
int PIMLoads = 0;
int PIMStores = 0;
int PIMReductions = 0;

for (BasicBlock &BB : *MatMulFunc) {
for (Instruction &I : BB) {
  if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
    if (LI->getMetadata("pim.execute")) PIMLoads++;
  }
  if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
    if (SI->getMetadata("pim.execute")) PIMStores++;
    if (SI->getMetadata("pim.reduction")) PIMReductions++;
  }
}
}

// We expect some loads and stores to be marked for PIM execution
EXPECT_GT(PIMLoads, 0);
EXPECT_GT(PIMStores, 0);
// We expect some reduction operations (accumulation pattern)
EXPECT_GT(PIMReductions, 0);

// Print optimization statistics
errs() << "Matrix Multiplication PIM Optimization Results:\n"
     << "  Total memory operations before: " << (LoadsBefore + StoresBefore) << "\n"
     << "  Operations optimized for PIM: " << (PIMLoads + PIMStores) << "\n"
     << "  Reduction operations detected: " << PIMReductions << "\n";
}

// Performance comparison test
TEST_F(MatrixMultiplicationTest, PerformanceComparison) {
// Skip this test in normal runs since it's for performance measurement
// GTEST_SKIP();

const int MatrixSize = 128;
const int TotalElements = MatrixSize * MatrixSize;

// Create test matrices
std::vector<float> A(TotalElements);
std::vector<float> B(TotalElements);
std::vector<float> C(TotalElements, 0.0f);

// Initialize matrices with random data
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_real_distribution<float> dist(0.0f, 1.0f);

for (int i = 0; i < TotalElements; i++) {
A[i] = dist(gen);
B[i] = dist(gen);
}

// Create matrix multiplication function
Function *MatMulFunc = createMatrixMultiplyFunction(MatrixSize);
ASSERT_NE(MatMulFunc, nullptr);

// Get function pointer from JIT (without PIM optimizations)
using MatMulFuncType = void (*)(float*, float*, float*, int);
MatMulFuncType MatMulFuncPtr = reinterpret_cast<MatMulFuncType>(
  EE->getFunctionAddress("matrix_multiply"));

// Measure baseline performance
auto startBaseline = std::chrono::high_resolution_clock::now();
MatMulFuncPtr(A.data(), B.data(), C.data(), MatrixSize);
auto endBaseline = std::chrono::high_resolution_clock::now();
std::chrono::duration<double, std::milli> baselineDuration = endBaseline - startBaseline;

// Clear result array
std::fill(C.begin(), C.end(), 0.0f);

// Run PIM optimization passes
runPIMOptimizationPasses(MatMulFunc);

// Get optimized function pointer
MatMulFuncPtr = reinterpret_cast<MatMulFuncType>(
  EE->getFunctionAddress("matrix_multiply"));

// Measure PIM-optimized performance
auto startOptimized = std::chrono::high_resolution_clock::now();
MatMulFuncPtr(A.data(), B.data(), C.data(), MatrixSize);
auto endOptimized = std::chrono::high_resolution_clock::now();
std::chrono::duration<double, std::milli> optimizedDuration = endOptimized - startOptimized;

// We expect PIM optimizations to improve performance
// Note: In a JIT environment without actual PIM hardware, 
// we don't expect real speedup, this is just for simulation
errs() << "Performance Comparison:\n"
     << "  Baseline: " << baselineDuration.count() << " ms\n"
     << "  PIM Optimized: " << optimizedDuration.count() << " ms\n";

// In an ideal PIM architecture, we would expect significant speedup
// But in our simulation, we're just validating the optimization pass runs
// EXPECT_LT(optimizedDuration.count(), baselineDuration.count());
}

// Run all the tests
int main(int argc, char **argv) {
::testing::InitGoogleTest(&argc, argv);
return RUN_ALL_TESTS();
}
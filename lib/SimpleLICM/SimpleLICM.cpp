#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Dominators.h"



using namespace llvm;

namespace {

/// Simple LICM:
/// - Hoists only register-to-register, side-effect-free instructions
/// - NO PHIs, NO memory ops, NO calls
/// - Does NOT use isLoopInvariant()
struct SimpleLICMPass : public PassInfoMixin<SimpleLICMPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &LI = FAM.getResult<LoopAnalysis>(F);

    bool Changed = false;

    SmallVector<Loop *, 8> TopLoops(LI.begin(), LI.end());
    for (Loop *L : TopLoops) {
      Changed |= processLoop(*L);
      for (Loop *Sub : L->getSubLoops())
        Changed |= processLoop(*Sub);
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  static bool isSafeRegToReg(Instruction *I) {
    if (isa<PHINode>(I) || I->isTerminator())
      return false;
    if (I->mayReadOrWriteMemory())
      return false;
    return isa<BinaryOperator>(I) || isa<CastInst>(I) ||
           isa<CmpInst>(I) || isa<GetElementPtrInst>(I) ||
           isa<SelectInst>(I);
  }

  static bool definedOutsideLoop(Value *V, Loop *L) {
    if (isa<Constant>(V)) return true;
    if (auto *I = dyn_cast<Instruction>(V))
      return !L->contains(I);
    return true;
  }

  static bool operandsInvariant(Instruction *I,
                                Loop *L,
                                const SmallPtrSetImpl<Instruction *> &Invariant) {
    if (!isSafeRegToReg(I)) return false;
    for (Value *Op : I->operands()) {
      if (auto *OpI = dyn_cast<Instruction>(Op)) {
        if (L->contains(OpI) && !Invariant.count(OpI))
          return false;
      }
    }
    return true;
  }

  bool processLoop(Loop &L) {
    BasicBlock *Preheader = L.getLoopPreheader();
    if (!Preheader) return false;

    SmallVector<Instruction *, 32> Work;
    SmallPtrSet<Instruction *, 32> Invariant;

    // Seed with instructions whose operands are all outside the loop.
    for (BasicBlock *BB : L.blocks()) {
      for (Instruction &I : *BB) {
        if (!isSafeRegToReg(&I)) continue;
        bool AllOutside = true;
        for (Value *Op : I.operands()) {
          if (!definedOutsideLoop(Op, &L)) { AllOutside = false; break; }
        }
        if (AllOutside)
          Work.push_back(&I);
      }
    }

    // Fixed-point grow of invariant set.
    while (!Work.empty()) {
      Instruction *I = Work.pop_back_val();
      if (Invariant.count(I)) continue;
      if (!operandsInvariant(I, &L, Invariant)) continue;
      Invariant.insert(I);

      // New invariant may unlock others.
      for (BasicBlock *BB : L.blocks()) {
        for (Instruction &J : *BB) {
          if (&J == I || Invariant.count(&J)) continue;
          if (operandsInvariant(&J, &L, Invariant))
            Work.push_back(&J);
        }
      }
    }

	bool Changed = false	;
	// Hoist in a stable order.
	for (BasicBlock *BB : L.blocks()) {
  	   for (Instruction &I : make_early_inc_range(*BB)) {
             if (!Invariant.count(&I)) continue;
             if (I.getParent() == Preheader) continue;

    	     // Reinsert before the preheader terminator (avoid deprecated moveBefore)
             Instruction *T = Preheader->getTerminator();
             I.removeFromParent();
             I.insertBefore(T);

             errs() << "Hoisting: " << I << "\n";
             Changed = true;
       }
 }
		
    return Changed;
  }
};

} // end anonymous namespace


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SimpleLICM", "0.1",
          [](PassBuilder &PB) {
            // Register analyses our pass (indirectly) needs.
            PB.registerAnalysisRegistrationCallback(
              [](FunctionAnalysisManager &FAM) {
                FAM.registerPass([] { return DominatorTreeAnalysis(); }); // <-- needed by LoopAnalysis
                FAM.registerPass([] { return LoopAnalysis(); });
              });

            // Hook for -passes=simple-licm
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "simple-licm") {
                    FPM.addPass(SimpleLICMPass());
                    return true;
                  }
                  return false;
                });
          }};
}


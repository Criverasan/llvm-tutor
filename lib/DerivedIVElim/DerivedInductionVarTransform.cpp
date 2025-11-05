#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

using namespace llvm;

namespace {

int beans = 0;
struct DerivedIVElimPass : public PassInfoMixin<DerivedIVElimPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

    SmallVector<Loop *, 8> Inner;
    for (Loop *L : LI)
      collectInnerLoops(L, Inner);

    bool Changed = false;
    for (Loop *L : Inner)
      Changed |= processInnerLoop(*L, SE);

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  static void collectInnerLoops(Loop *L, SmallVectorImpl<Loop *> &Out) {
    if (L->getSubLoops().empty()) {
      Out.push_back(L);
      return;
    }
    for (Loop *SL : L->getSubLoops())
      collectInnerLoops(SL, Out);
  }

  static PHINode *findCanonicalIntegerIV(Loop &L, ScalarEvolution &SE) {
    BasicBlock *Header = L.getHeader();
    if (!Header) return nullptr;

    for (PHINode &PN : Header->phis()) {
      if (!PN.getType()->isIntegerTy()) continue;
      const SCEV *S = SE.getSCEV(&PN);
      if (auto *AR = dyn_cast<SCEVAddRecExpr>(S)) {
        if (AR->getLoop() != &L) continue;
        if (isa<SCEVConstant>(AR->getStepRecurrence(SE)))
          return &PN;
      }
    }
    return nullptr;
  }

  static bool isAffineInSingleAddRec(const SCEV *S, const SCEVAddRecExpr *&OutAR) {
    if (auto *AR = dyn_cast<SCEVAddRecExpr>(S)) {
      OutAR = AR;
      return true;
    }
    if (auto *Sum = dyn_cast<SCEVAddExpr>(S)) {
      const SCEVAddRecExpr *Found = nullptr;
      for (const SCEV *Op : Sum->operands()) {
        const SCEVAddRecExpr *Tmp = nullptr;
        if (isAffineInSingleAddRec(Op, Tmp)) {
          if (Found && Tmp != Found) return false;
          Found = Tmp;
        }
      }
      if (Found) { OutAR = Found; return true; }
      return false;
    }
    if (auto *Mul = dyn_cast<SCEVMulExpr>(S)) {
      const SCEVAddRecExpr *Found = nullptr;
      for (const SCEV *Op : Mul->operands()) {
        if (isa<SCEVConstant>(Op)) continue;
        const SCEVAddRecExpr *Tmp = nullptr;
        if (!isAffineInSingleAddRec(Op, Tmp)) return false;
        if (Found && Tmp != Found) return false;
        Found = Tmp;
      }
      if (Found) { OutAR = Found; return true; }
      return false;
    }
    return false;
  }

  bool processInnerLoop(Loop &L, ScalarEvolution &SE) {
    PHINode *IV = findCanonicalIntegerIV(L, SE);
    if (!IV) return false;

    const SCEV *IVS = SE.getSCEV(IV);
    auto *IVAR = dyn_cast<SCEVAddRecExpr>(IVS);
    if (!IVAR) return false;

    SmallVector<Instruction *, 32> ToRewrite;

    for (BasicBlock *BB : L.blocks()) {
      for (Instruction &I : *BB) {
        if (&I == IV) continue;
        if (!I.getType()->isIntegerTy()) continue;

        const SCEV *S = SE.getSCEV(&I);
        const SCEVAddRecExpr *Aff = nullptr;
        if (!isAffineInSingleAddRec(S, Aff)) continue;
        if (Aff->getLoop() != &L) continue;

        ToRewrite.push_back(&I);
      }
    }

    if (ToRewrite.empty()) return false;

    bool Changed = false;
    const DataLayout &DL = L.getHeader()->getModule()->getDataLayout();
    SCEVExpander Exp(SE, DL, "ivelim");

    auto It = L.getHeader()->getFirstInsertionPt();
    if (It == L.getHeader()->end()) return false;
    Instruction *InsertPt = &*It;

    for (Instruction *I : ToRewrite) {
      const SCEV *S = SE.getSCEV(I);
      Value *NewV = Exp.expandCodeFor(S, I->getType(), InsertPt);
      if (NewV && NewV != I) {
        I->replaceAllUsesWith(NewV);
        if (I->use_empty()) {
          I->eraseFromParent();
        }
        Changed = true;
      }
    }
    return Changed;
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DerivedIVElim", "0.1",
          [](PassBuilder &PB) {
            // Register all analyses this pass (and SCEV) depends on.
            PB.registerAnalysisRegistrationCallback(
              [](FunctionAnalysisManager &FAM) {
                FAM.registerPass([] { return DominatorTreeAnalysis(); });
                FAM.registerPass([] { return LoopAnalysis(); });
                FAM.registerPass([] { return TargetLibraryAnalysis(); });
                FAM.registerPass([] { return ScalarEvolutionAnalysis(); });
              });

            // Allow: -passes=derived-iv-elim
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "derived-iv-elim") {
                    FPM.addPass(DerivedIVElimPass());
                    return true;
                  }
                  return false;
                });
          }};
}


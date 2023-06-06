#ifndef LLVM_ANALYSIS_OWNERSHIP_BASED_ALIAS_ANALYSIS_H
#define LLVM_ANALYSIS_OWNERSHIP_BASED_ALIAS_ANALYSIS_H

#include <llvm/IR/PassManager.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Analysis/AliasAnalysis.h>

#include <unordered_set>

namespace llvm {
    class OwnershipAACache {
    public:
        auto clear() -> void;
        auto harvest(IntrinsicInst const *II) -> void;

        auto owner(Value const *Ptr) const -> LoadInst const *;
        auto ownerAddressAnnotated(Value const *Ptr) const -> IntrinsicInst const *;
        auto ownerAddress(Value const *Ptr) const -> Value const *;

    private:
        auto harvestDependents(Value const *V, LoadInst const *owner) -> void;

        std::unordered_set<IntrinsicInst const*> ownerAddresses_;
        std::unordered_map<LoadInst const *, IntrinsicInst const *> owningPtrToAddressMap_;
        std::unordered_map<Value const *, LoadInst const *> dependentValuesReverseMap_;
    };

    class OwnershipAAResult : public AAResultBase {
    public:
        OwnershipAAResult(Function &F);

        auto alias(
                MemoryLocation const &LocA,
                MemoryLocation const &LocB,
                AAQueryInfo &AAQI,
                Instruction const *CtxI
            ) -> AliasResult;

        auto invalidate(
                Function &,
                PreservedAnalyses const &,
                FunctionAnalysisManager::Invalidator &Inv
            ) -> bool;

    private:
        auto scanFunction(Function &F) -> void;

        OwnershipAACache unique_;
        OwnershipAACache shared_;
    };

    class OwnershipAA : public AnalysisInfoMixin<OwnershipAA> {
    public:    
        using Result = OwnershipAAResult;

        auto run(Function &F, FunctionAnalysisManager &FAM) -> Result;

        static auto isRequired() { return true; }

        static AnalysisKey Key;
    };

    class OwnershipAAWrapperPass : public FunctionPass {
    public:
        static char ID;

        OwnershipAAWrapperPass();

        auto &getResult() { return *result_; }
        auto &getResult() const { return *result_; }

        bool runOnFunction(Function &F) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;

    private:
        std::unique_ptr<OwnershipAAResult> result_;
    };    

    FunctionPass *createOwnershipAAWrapperPass();
    OwnershipAAResult createLegacyPMOwnershipAAResult(Pass &P, Function &F);

    FunctionPass *createOwnershipAAWrapperPass();
}

#endif

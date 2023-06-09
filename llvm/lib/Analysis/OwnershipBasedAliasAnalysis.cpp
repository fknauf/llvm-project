#include "llvm/Analysis/OwnershipBasedAliasAnalysis.h"

#include "llvm/InitializePasses.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Pass.h"

#include <cassert>

using namespace llvm;

static cl::opt<bool> EnableOwnershipAA("enable-ownership-aa",
                                        cl::init(true), cl::Hidden);

auto OwnershipAACache::clear() -> void {
    ownerAddresses_.clear();
    owningPtrToAddressMap_.clear();
    dependentValuesReverseMap_.clear();
}

auto OwnershipAACache::harvest(IntrinsicInst const *II) -> void {
    errs() << "---\nowning address: ";
    II->print(errs());
    errs() << "\n";
    
    ownerAddresses_.insert(II);

    for(auto *U : II->users()) {
        auto LI = dyn_cast<LoadInst>(U);

        if(LI != nullptr && LI->getOperand(0) == II) {
            errs() << "owning pointer: ";
            LI->print(errs());
            errs() << "\n";

            assert(owningPtrToAddressMap_.count(LI) == 0);
            owningPtrToAddressMap_[LI] = II;
            harvestDependents(LI, LI);
        }
    }    
}

auto OwnershipAACache::harvestDependents(
        Value const *V,
        LoadInst const *owner
    ) -> void
{
    if(dependentValuesReverseMap_.count(V) > 0) {
        errs() << "DUPLICATE REVMAP: ";
    } else {
        errs() << "dependent ptr:  ";
    }

    V->print(errs());
    errs() << "\n";

    dependentValuesReverseMap_[V] = owner;

    for(auto *U : V->users()) {
        auto UserGEP = dyn_cast<GetElementPtrInst>(U);

        if(UserGEP == nullptr || UserGEP->getOperand(0) != V) {
            continue;
        }

        harvestDependents(U, owner);
    }
}

auto OwnershipAACache::owner(Value const *Ptr) const -> LoadInst const * {
    auto it = dependentValuesReverseMap_.find(Ptr);

    if(it != dependentValuesReverseMap_.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

auto OwnershipAACache::ownerAddressAnnotated(Value const *Ptr) const
    -> IntrinsicInst const *
{
    auto O = owner(Ptr);
    auto it = owningPtrToAddressMap_.find(O);

    if(it != owningPtrToAddressMap_.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

auto OwnershipAACache::ownerAddress(Value const *Ptr) const -> Value const * {
    auto OA = ownerAddressAnnotated(Ptr);

    if(OA != nullptr) {
        return OA->getOperand(0);
    } else {
        return nullptr;
    }
}

OwnershipAAResult::OwnershipAAResult(Function &F) {
    scanFunction(F);
}

auto OwnershipAAResult::scanFunction(Function &F) -> void {
    unique_.clear();
    shared_.clear();

    for(auto &&BB : F) {
        for(auto &&I: BB) {
            auto II = dyn_cast<IntrinsicInst>(&I);

            if(II == nullptr || 
                II->getIntrinsicID() != Intrinsic::ptr_annotation)
            {
                continue;
            }

            if(auto annotation = dyn_cast<GlobalVariable>(II->getOperand(1))) {
                if(auto annotationStr = dyn_cast<ConstantDataSequential>(annotation->getInitializer())) {
                    if(!annotationStr->isString()) {
                        continue;
                    } else if(annotationStr->getAsCString() == "fknauf.owner.unique") {
                        unique_.harvest(II);
                    } else if(annotationStr->getAsCString() == "fknauf.owner.shared") {
                        shared_.harvest(II);
                    }
                }
            }
        }
    }
}

auto OwnershipAAResult::invalidate(
        Function &F,
        PreservedAnalyses const &,
        FunctionAnalysisManager::Invalidator &
    ) -> bool
{
    scanFunction(F);
    return true;
}

auto OwnershipAAResult::alias(
        MemoryLocation const &LocA,
        MemoryLocation const &LocB,
        AAQueryInfo &AAQI,
        Instruction const *CtxI
    ) -> AliasResult
{
    auto upstream = AAResultBase::alias(LocA, LocB, AAQI, CtxI);

    if(!EnableOwnershipAA) {
        llvm::errs() << "ownership aa disabled.\n";
        return upstream;
    }

    if(upstream != AliasResult::MayAlias) {
        return upstream;
    }

    auto ownerA = unique_.ownerAddress(LocA.Ptr);
    auto ownerB = unique_.ownerAddress(LocB.Ptr);

    if(ownerA == nullptr || ownerB == nullptr) {
        return upstream;
    }

    auto ownerLocA = MemoryLocation { ownerA, 1 };
    auto ownerLocB = MemoryLocation { ownerB, 1 };

    auto ownerResult = AAQI.AAR.alias(ownerLocA, ownerLocB);

    llvm::errs() << ownerResult << " for ";
    ownerA->printAsOperand(llvm::errs());
    llvm::errs() << ", ";
    ownerB->printAsOperand(llvm::errs());
    llvm::errs() << "\n";

    if(ownerResult == AliasResult::MustAlias || ownerResult == AliasResult::NoAlias) {
        return ownerResult;
    }

    return AAResultBase::alias(LocA, LocB, AAQI, CtxI);
}

AnalysisKey OwnershipAA::Key;

auto OwnershipAA::run(llvm::Function &F, llvm::FunctionAnalysisManager &) -> Result  {
    return { F };
}

char OwnershipAAWrapperPass::ID = 0;

bool OwnershipAAWrapperPass::runOnFunction(llvm::Function &F) {
    result_.reset(new OwnershipAAResult(F));
    return true;
}

void OwnershipAAWrapperPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

INITIALIZE_PASS(OwnershipAAWrapperPass, "ownership-aa", "Ownership-Based Alias Analysis", false, true)

OwnershipAAWrapperPass::OwnershipAAWrapperPass() : llvm::FunctionPass(ID) {
    llvm::initializeOwnershipAAWrapperPassPass(*llvm::PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createOwnershipAAWrapperPass() {
    return new OwnershipAAWrapperPass();
}

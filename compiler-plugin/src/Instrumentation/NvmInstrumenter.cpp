/*
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version. This program is
 * distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU Lesser
 * General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
 

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"

#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"

using namespace llvm;

#define DEBUG_TYPE "nvm_instr"

STATISTIC(NumNvmAcquire, "Number of acquires instrumented");
STATISTIC(NumNvmRelease, "Number of releases instrumented");
STATISTIC(NumNvmStore, "Number of stores instrumented");
STATISTIC(NumNvmMemCpy, "Number of memcopies instrumented");
STATISTIC(NumNvmMemMove, "Number of memmoves instrumented");
STATISTIC(NumNvmMemSet, "Number of memsets instrumented");
STATISTIC(NumNvmStrCpy, "Number of strcpys instrumented");
STATISTIC(NumNvmStrCat, "Number of strcats instrumented");

namespace {
    class NvmInstrumenter : public FunctionPass
    {
    public:
        enum CallOpType { None, Acquire, Release, MemCpy, MemMove, MemSet, StrCpy, StrNCpy, StrCat, StrNCat };
        static char ID;
        NvmInstrumenter()
            : FunctionPass(ID), AcquireFuncEntry(0), ReleaseFuncEntry(0), 
              StoreFuncEntry(0), PsyncAcqFuncEntry(0),
              MemCpyFuncEntry(0), MemMoveFuncEntry(0), MemSetFuncEntry(0),
              StrCpyFuncEntry(0), StrCatFuncEntry(0),
              StrLenFuncEntry(0),
              BarrierFuncEntry(0),
            AsyncDataFlushEntry(0), AsyncMemOpDataFlushEntry(0)
            {
//                initializeNvmInstrumenterPass(
//                    *PassRegistry::getPassRegistry());
            }
        bool runOnFunction(Function &F);
        const char *getPassName() const { return "nvm_instr"; }
    private:
        void initializeAcquire(Module &M);
        void initializeRelease(Module &M);
        void initializeStore(Module &M);
        void initializePsyncAcqFuncEntry(Module &M);
        void initializeMemCpyFuncEntry(Module &M);
        void initializeMemMoveFuncEntry(Module &M);
        void initializeMemSetFuncEntry(Module &M);
        void initializeStrCpyFuncEntry(Module &M);
        void initializeStrCatFuncEntry(Module &M);
        void initializeStrLenFuncEntry(Module &M);
        void initializeBarrierFuncEntry(Module &M);
        void initializeAsyncDataFlushEntry(Module &M);
        void initializeAsyncMemOpDataFlushEntry(Module &M);
        
        bool shouldInstrumentStore(StoreInst *SI);
        CallOpType getCallOperationType(Instruction *I);

        bool performNvmInstrumentation(
            Function &F, 
            const SmallVectorImpl<Instruction*> &Stores,
            const SmallVectorImpl<Instruction*> &Acquires,
            const SmallVectorImpl<Instruction*> &Releases,
            const SmallVectorImpl<Instruction*> &MemCpys,
            const SmallVectorImpl<Instruction*> &MemMoves,
            const SmallVectorImpl<Instruction*> &MemSets,
            const SmallVectorImpl<Instruction*> &StrCpys,
            const SmallVectorImpl<Instruction*> &StrCats);
        void addMemInstrumentation(Instruction *I, Function *FuncEntry);

        Function *AcquireFuncEntry;
        Function *StoreFuncEntry;
        Function *ReleaseFuncEntry;
        Function *PsyncAcqFuncEntry;
        Function *MemCpyFuncEntry;
        Function *MemMoveFuncEntry;
        Function *MemSetFuncEntry;
        Function *StrCpyFuncEntry;
        Function *StrCatFuncEntry;
        Function *StrLenFuncEntry;
        Function *BarrierFuncEntry;
        Function *AsyncDataFlushEntry;
        Function *AsyncMemOpDataFlushEntry;
    };
}

static StringRef LockAcquireName("pthread_mutex_lock");
static StringRef LockReleaseName("pthread_mutex_unlock");
static StringRef MemCpy32Name("llvm.memcpy.p0i8.p0i8.i32");
static StringRef MemCpy64Name("llvm.memcpy.p0i8.p0i8.i64");
static StringRef MemMove32Name("llvm.memmove.p0i8.p0i8.i32");
static StringRef MemMove64Name("llvm.memmove.p0i8.p0i8.i64");
static StringRef MemSet32Name("llvm.memset.p0i8.i32");
static StringRef MemSet64Name("llvm.memset.p0i8.i64");
static StringRef StrCpyName("strcpy");
static StringRef StrNCpyName("strncpy");
static StringRef StrCatName("strcat");
static StringRef StrNCatName("strncat");

bool NvmInstrumenter::runOnFunction(Function &F)
{

    SmallVector<Instruction*, 8> Stores;
    SmallVector<Instruction*, 8> Acquires;
    SmallVector<Instruction*, 8> Releases;
    SmallVector<Instruction*, 8> MemCpys;
    SmallVector<Instruction*, 8> MemMoves;
    SmallVector<Instruction*, 8> MemSets;
    SmallVector<Instruction*, 8> StrCpys;
    SmallVector<Instruction*, 8> StrCats;

    Instruction *I;
    bool Res = false;
    // Traverse all instructions
    // Look for stores, lock acquires and releases
    for (Function::iterator BI = F.begin(), BE = F.end(); BI != BE; ++BI) {
        BasicBlock &BB = *BI;
        for (BasicBlock::iterator II = BB.begin(), IE = BB.end();
             II != IE; ++ II) {
            I = dyn_cast<Instruction>(II);
            if (isa<StoreInst>(I) &&
                shouldInstrumentStore(dyn_cast<StoreInst>(I))) {
                ++NumNvmStore;
                Stores.push_back(I);
            }
            else if (isa<CallInst>(I)) {
                CallOpType CT = getCallOperationType(I);
                if (CT == Acquire) {
                    ++NumNvmAcquire;
                    Acquires.push_back(I);
                }
                else if (CT == Release) {
                    ++NumNvmRelease;
                    Releases.push_back(I);
                }
                else if (CT == MemCpy) {
                    ++NumNvmMemCpy;
                    MemCpys.push_back(I);
                }
                else if (CT == MemMove) {
                    ++NumNvmMemMove;
                    MemMoves.push_back(I);
                }
                else if (CT == MemSet) {
                    ++NumNvmMemSet;
                    MemSets.push_back(I);
                }
                else if (CT == StrCpy || CT == StrNCpy) {
                    ++NumNvmStrCpy;
                    StrCpys.push_back(I);
                }
                else if (CT == StrCat || CT == StrNCat) {
                    ++NumNvmStrCat;
                    StrCats.push_back(I);
                }
            }
        }
    }
    Res |= performNvmInstrumentation(
        F, Stores, Acquires, Releases, MemCpys, MemMoves, MemSets, StrCpys, StrCats);
    if ( Stores.size() || Acquires.size() || Releases.size() || MemCpys.size() || MemMoves.size() || MemSets.size() || StrCpys.size() || StrCats.size())
        errs() << "Atlas instrumentation done on " << F.getName() << "\n";
    return Res;
}

bool NvmInstrumenter::shouldInstrumentStore(StoreInst *SI)
{
    Value *Addr = cast<StoreInst>(SI)->getPointerOperand();
    if (isa<AllocaInst>(Addr)) return false; // local variable
    return true;
}

NvmInstrumenter::CallOpType
NvmInstrumenter::getCallOperationType(Instruction *I)
{
    CallInst *CallInstruction = cast<CallInst>(I);
    Function *CalledFunction = CallInstruction->getCalledFunction();
    if (!CalledFunction) return None;
    
    if (!CalledFunction->isDeclaration()) return None;

    // TODO attribute check to make sure it is not overridden
    
    if (CalledFunction->getName().equals(LockAcquireName))
        return Acquire;
    else if (CalledFunction->getName().equals(LockReleaseName))
        return Release;
    else if (CalledFunction->getName().equals(MemCpy32Name) ||
             CalledFunction->getName().equals(MemCpy64Name))
        return MemCpy;
    else if (CalledFunction->getName().equals(MemMove32Name) ||
             CalledFunction->getName().equals(MemMove64Name))
        return MemMove;
    else if (CalledFunction->getName().equals(StrCpyName))
        return StrCpy;
    else if (CalledFunction->getName().equals(StrNCpyName))
        return StrNCpy;
    else if (CalledFunction->getName().equals(StrCatName))
        return StrCat;
    else if (CalledFunction->getName().equals(StrNCatName))
        return StrNCat;
    return None;
}
void NvmInstrumenter::initializeAcquire(Module &M)
{
    if (AcquireFuncEntry) return;
    
    IRBuilder<> IRB(M.getContext());
    AcquireFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_acquire", IRB.getVoidTy(),
                              Type::getInt8PtrTy(M.getContext()), NULL));
    assert(AcquireFuncEntry);
}

void NvmInstrumenter::initializeRelease(Module &M)
{
    if (ReleaseFuncEntry) return;

    IRBuilder<> IRB(M.getContext());
    ReleaseFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_release", IRB.getVoidTy(),
                              Type::getInt8PtrTy(M.getContext()), NULL));
    assert(ReleaseFuncEntry);
}

void NvmInstrumenter::initializeStore(Module &M)
{
    if (StoreFuncEntry) return;

    StoreFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_store",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(StoreFuncEntry);
}
        
void NvmInstrumenter::initializeMemCpyFuncEntry(Module &M)
{
    if (MemCpyFuncEntry) return;

    MemCpyFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_memcpy",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(MemCpyFuncEntry);
}

void NvmInstrumenter::initializeMemMoveFuncEntry(Module &M)
{
    if (MemMoveFuncEntry) return;

    MemMoveFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_memmove",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(MemMoveFuncEntry);
}

void NvmInstrumenter::initializeMemSetFuncEntry(Module &M)
{
    if (MemSetFuncEntry) return;

    MemSetFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_memset",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(MemSetFuncEntry);
}

void NvmInstrumenter::initializeStrCpyFuncEntry(Module &M)
{
    if (StrCpyFuncEntry) return;

    StrCpyFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_strcpy",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(StrCpyFuncEntry);
}

void NvmInstrumenter::initializeStrCatFuncEntry(Module &M)
{
    if (StrCatFuncEntry) return;

    StrCatFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_strcat",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(StrCatFuncEntry);
}

void NvmInstrumenter::initializeStrLenFuncEntry(Module &M)
{
    if (StrLenFuncEntry) return;

    StrLenFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_strlen",
                              Type::getInt64Ty(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()), NULL));
    assert(StrLenFuncEntry);
}

void NvmInstrumenter::initializeBarrierFuncEntry(Module &M)
{
    if (BarrierFuncEntry) return;

    BarrierFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_barrier",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()), NULL));
    assert(BarrierFuncEntry);
}

void NvmInstrumenter::initializePsyncAcqFuncEntry(Module &M)
{
    if (PsyncAcqFuncEntry) return;

    PsyncAcqFuncEntry = dyn_cast<Function>(
        M.getOrInsertFunction("nvm_psync_acq",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(PsyncAcqFuncEntry);
}
    
void NvmInstrumenter::initializeAsyncDataFlushEntry(Module &M)
{
    if (AsyncDataFlushEntry) return;

    AsyncDataFlushEntry = dyn_cast<Function>(
        M.getOrInsertFunction("AsyncDataFlush",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()), NULL));
    assert(AsyncDataFlushEntry);
}

void NvmInstrumenter::initializeAsyncMemOpDataFlushEntry(Module &M)
{
    if (AsyncMemOpDataFlushEntry) return;

    AsyncMemOpDataFlushEntry = dyn_cast<Function>(
        M.getOrInsertFunction("AsyncMemOpDataFlush",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext()),
                              Type::getInt64Ty(M.getContext()), NULL));
    assert(AsyncMemOpDataFlushEntry);
}
bool NvmInstrumenter::performNvmInstrumentation(
    Function &F, 
    const SmallVectorImpl<Instruction*> &Stores,
    const SmallVectorImpl<Instruction*> &Acquires,
    const SmallVectorImpl<Instruction*> &Releases,
    const SmallVectorImpl<Instruction*> &MemCpys,
    const SmallVectorImpl<Instruction*> &MemMoves,
    const SmallVectorImpl<Instruction*> &MemSets,
    const SmallVectorImpl<Instruction*> &StrCpys,
    const SmallVectorImpl<Instruction*> &StrCats)
{
    if (Acquires.size()) initializeAcquire(*F.getParent());
    if (Releases.size()) initializeRelease(*F.getParent());
    if (MemCpys.size()) initializeMemCpyFuncEntry(*F.getParent());
    if (MemMoves.size()) initializeMemMoveFuncEntry(*F.getParent());
    if (MemSets.size()) initializeMemSetFuncEntry(*F.getParent());
    if (StrCpys.size() || StrCats.size()) initializeStrLenFuncEntry(*F.getParent());
    if (StrCpys.size()) initializeStrCpyFuncEntry(*F.getParent());
    if (StrCats.size()) initializeStrCatFuncEntry(*F.getParent());
    if (getenv("USE_TABLE_FLUSH")) {
        if (Stores.size()) initializeAsyncDataFlushEntry(*F.getParent());
        if (MemCpys.size() || MemMoves.size() || MemSets.size() || StrCpys.size() || StrCats.size())
            initializeAsyncMemOpDataFlushEntry(*F.getParent());
    }
    else {
        if (Stores.size()) initializeBarrierFuncEntry(*F.getParent());
        if (MemCpys.size() || MemMoves.size() || MemSets.size() || StrCpys.size() || StrCats.size())
            initializePsyncAcqFuncEntry(*F.getParent());
    }
    
    IRBuilder<> IRB(F.getParent()->getContext());
    
    for (SmallVectorImpl<Instruction*>::const_iterator AB = Acquires.begin(),
         AE = Acquires.end(); AB != AE; ++AB) {
        Instruction *I = *AB;
        assert(isa<CallInst>(I) && "Found a non-call instruction");

        CallInst *CallInstruction = cast<CallInst>(I);
        assert(CallInstruction->getNumArgOperands() == 1 &&
               "Expected 1 argument to pthread_mutex_lock");

        PointerType *ArgType =
            Type::getInt8PtrTy(F.getParent()->getContext());
        Value *OP = CallInstruction->getArgOperand(0);
        Value *Arg1 = OP->getType() == ArgType ? NULL :
            IRB.CreatePointerCast(OP, ArgType);
        Value *Args[] = {Arg1 ? Arg1 : OP};
        CallInst *NI = CallInst::Create(
            AcquireFuncEntry, ArrayRef<Value*>(Args));
        NI->insertAfter(CallInstruction);
        if (Arg1 && isa<Instruction>(Arg1))
            dyn_cast<Instruction>(Arg1)->insertBefore(NI);
    }
    for (SmallVectorImpl<Instruction*>::const_iterator RB = Releases.begin(),
             RE = Releases.end(); RB != RE; ++RB) {
        Instruction *I = *RB;
        assert(isa<CallInst>(I) && "Found a non-call instruction");

        CallInst *CallInstruction = cast<CallInst>(I);
        assert(CallInstruction->getNumArgOperands() == 1 &&
               "Expected 1 argument to pthread_mutex_unlock");

        PointerType *ArgType =
            Type::getInt8PtrTy(F.getParent()->getContext());
        Value *OP = CallInstruction->getArgOperand(0);
        Value *Arg1 = OP->getType() == ArgType ? NULL :
            IRB.CreatePointerCast(OP, ArgType);
        Value *Args[] = {Arg1 ? Arg1 : OP};
        CallInst *NI = CallInst::Create(
            ReleaseFuncEntry, ArrayRef<Value*>(Args), "", CallInstruction);
        if (Arg1 && isa<Instruction>(Arg1))
            dyn_cast<Instruction>(Arg1)->insertBefore(NI);
    }
    for (SmallVectorImpl<Instruction*>::const_iterator SB = Stores.begin(),
         SE = Stores.end(); SB != SE; ++SB) {
        Instruction *I = *SB;
        assert(isa<StoreInst>(I) && "Found a non-store instruction");

        StoreInst *StoreInstruction = cast<StoreInst>(I);
        Value *Addr = StoreInstruction->getPointerOperand();
        Value *Val = StoreInstruction->getValueOperand();
        
        unsigned sz;
        if (Val->getType()->isIntegerTy() ||
            Val->getType()->isFloatTy() ||
            Val->getType()->isDoubleTy() ||
            Val->getType()->isX86_FP80Ty() ||
            Val->getType()->isFP128Ty())
            sz = Val->getType()->getPrimitiveSizeInBits();
        else if (Val->getType()->isPointerTy()) sz = 64;
        else {
            Val->dump();
            Val->getType()->dump();
            assert(0);
        }

        unsigned extra_sz = 0;
        if (sz > 64) {
            assert(sz <= 128 && "This type is not supported");
            extra_sz = sz - 64;
            assert(!(sz % 8));
            sz = 64;
        }
        
        PointerType *ArgType =
            Type::getInt8PtrTy(F.getParent()->getContext());
        Value *Arg1 = Addr->getType() == ArgType ? NULL :
            IRB.CreatePointerCast(Addr, ArgType);
        Value *ConstantSize = ConstantInt::get(
            Type::getInt64Ty(F.getParent()->getContext()), sz);
        
        Value *Args[] = {Arg1 ? Arg1 : Addr, ConstantSize};
        
        CallInst *NI = NULL;
        initializeStore(*F.getParent());
        NI = CallInst::Create(StoreFuncEntry, ArrayRef<Value*>(Args),
                              "", StoreInstruction);
        if (Arg1 && isa<Instruction>(Arg1))
            dyn_cast<Instruction>(Arg1)->insertBefore(NI);
        
        if (isa<Instruction>(ConstantSize))
            dyn_cast<Instruction>(ConstantSize)->insertBefore(NI);

        if (extra_sz) {
            Value *Word = ConstantInt::get(
                Type::getInt64Ty(F.getParent()->getContext()), 8);
            Value *IntReprOfAddr =
                IRB.CreatePtrToInt(
                    Addr, Type::getInt64Ty(F.getParent()->getContext()));
            if (isa<Instruction>(IntReprOfAddr))
                dyn_cast<Instruction>(IntReprOfAddr)->insertBefore(
                    StoreInstruction);
            Instruction *add_word = BinaryOperator::Create(
                Instruction::Add,
                IntReprOfAddr,
                Word, "add_word", StoreInstruction);

            Value *PtrReprOfIncrement =
                IRB.CreateIntToPtr(add_word, ArgType);
            if (isa<Instruction>(PtrReprOfIncrement))
                dyn_cast<Instruction>(PtrReprOfIncrement)->insertBefore(
                    StoreInstruction);
            Value *ExtraConstantSize = ConstantInt::get(
                Type::getInt64Ty(F.getParent()->getContext()), extra_sz);
            Value *ExtraArgs[] = {PtrReprOfIncrement, ExtraConstantSize};
            CallInst::Create(StoreFuncEntry, ArrayRef<Value*>(ExtraArgs),
                             "", StoreInstruction);
        }
            
        Value *BarrierArgs[] = {Arg1 ? Arg1 : Addr};
        if (getenv("USE_TABLE_FLUSH")) {
            CallInst *TFI = CallInst::Create(AsyncDataFlushEntry,
                                             ArrayRef<Value*>(BarrierArgs));
            dyn_cast<Instruction>(TFI)->insertAfter(StoreInstruction);
        }
        else {
            CallInst *BI = CallInst::Create(BarrierFuncEntry,
                                            ArrayRef<Value*>(BarrierArgs));
            dyn_cast<Instruction>(BI)->insertAfter(StoreInstruction);
        }
    }
    for (SmallVectorImpl<Instruction*>::const_iterator MCB = MemCpys.begin(),
             MCE = MemCpys.end(); MCB != MCE; ++MCB)
        addMemInstrumentation(*MCB, MemCpyFuncEntry);
    for (SmallVectorImpl<Instruction*>::const_iterator MMB = MemMoves.begin(),
             MME = MemMoves.end(); MMB != MME; ++MMB)
        addMemInstrumentation(*MMB, MemMoveFuncEntry);
    for (SmallVectorImpl<Instruction*>::const_iterator MSB = MemSets.begin(),
             MSE = MemSets.end(); MSB != MSE; ++MSB)
        addMemInstrumentation(*MSB, MemSetFuncEntry);
    for (SmallVectorImpl<Instruction*>::const_iterator SCB = StrCpys.begin(),
             SCE = StrCpys.end(); SCB != SCE; ++SCB) {
        Instruction *I = *SCB;
        CallOpType CT = getCallOperationType(I);
        if (CallInst *CallInstruction = dyn_cast<CallInst>(I)) {
            Value *size;
            if (CT == StrCpy) {
                CallInst* CI = CallInst::Create(
                    StrLenFuncEntry,
                    ArrayRef<Value*>(CallInstruction->getArgOperand(0)), "",
                    CallInstruction);
                size = CI;
            } else {
                size = CallInstruction->getArgOperand(2);
            }
            Value *Args[] = {CallInstruction->getArgOperand(0), size};
            CallInst::Create(StrCpyFuncEntry, ArrayRef<Value*>(Args), "",
                             CallInstruction);
            if (getenv("USE_TABLE_FLUSH")) {
                CallInst *TFI = CallInst::Create(AsyncMemOpDataFlushEntry,
                                                 ArrayRef<Value*>(Args));
                dyn_cast<Instruction>(TFI)->insertAfter(CallInstruction);
            }
            else {
                CallInst *FI = CallInst::Create(PsyncAcqFuncEntry,
                                                ArrayRef<Value*>(Args));
                dyn_cast<Instruction>(FI)->insertAfter(CallInstruction);
            }
        }
    }
    for (SmallVectorImpl<Instruction*>::const_iterator SCAB = StrCats.begin(),
             SCAE = StrCats.end(); SCAB != SCAE; ++SCAB) {
        Instruction *I = *SCAB;
        if (CallInst *CallInstruction = dyn_cast<CallInst>(I)) {
            CallInst* DSLI = CallInst::Create(
                StrLenFuncEntry,
                ArrayRef<Value*>(CallInstruction->getArgOperand(0)), "",
                CallInstruction);
            Value *Args[] = {CallInstruction->getArgOperand(0), DSLI};
            CallInst::Create(StrCatFuncEntry,ArrayRef<Value*>(Args), "",
                             CallInstruction);
            if (getenv("USE_TABLE_FLUSH")) {
                CallInst *TFI = CallInst::Create(AsyncMemOpDataFlushEntry,
                                                 ArrayRef<Value*>(Args));
                dyn_cast<Instruction>(TFI)->insertAfter(CallInstruction);
            }
            else {
                CallInst *FI = CallInst::Create(PsyncAcqFuncEntry,
                                                ArrayRef<Value*>(Args));
                dyn_cast<Instruction>(FI)->insertAfter(CallInstruction);
            }
        }
        else errs() << "Found a non-call instruction in strcpys...";
    }
    
    return true;
}

void NvmInstrumenter::addMemInstrumentation(
    Instruction *I, Function *FuncEntry)
{
    assert(isa<CallInst>(I) && "Found a non-call instruction");

    CallInst *CallInstruction = cast<CallInst>(I);
    assert(CallInstruction->getNumArgOperands() == 5 &&
           "Expected 5 arguments to memset");
    Value *Args[] = {CallInstruction->getArgOperand(0),
                     CallInstruction->getArgOperand(2)};
    CallInst::Create(FuncEntry, ArrayRef<Value*>(Args), "", CallInstruction);
    if (getenv("USE_TABLE_FLUSH")) {
        CallInst *TFI = CallInst::Create(AsyncMemOpDataFlushEntry,
                                         ArrayRef<Value*>(Args));
        dyn_cast<Instruction>(TFI)->insertAfter(CallInstruction);
    }
    else {
        CallInst *FI = CallInst::Create(PsyncAcqFuncEntry,
                                        ArrayRef<Value*>(Args));
        dyn_cast<Instruction>(FI)->insertAfter(CallInstruction);
    }
}

char NvmInstrumenter::ID = 0;
static RegisterPass<NvmInstrumenter>
X("NvmInstrumenter",
  "Instruments persistent stores and synchronization operations",
  false, false);

static void registerNvmInstrumenter(const PassManagerBuilder &,
                                          legacy::PassManagerBase &PM) {
    PM.add(new NvmInstrumenter());
}

static RegisterStandardPasses RegisterNvmInstrumenter(PassManagerBuilder::EP_EarlyAsPossible, registerNvmInstrumenter);

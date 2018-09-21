

#ifndef PROFILING_INSTRUMENTATION_PASS_H
#define PROFILING_INSTRUMENTATION_PASS_H


#include "llvm/IR/Module.h"
#include "llvm/Pass.h"


namespace cgprofiler {


struct ProfilingInstrumentationPass : public llvm::ModulePass {

  static char ID;

  llvm::DenseMap<llvm::StringRef, int64_t> fn_names;
  ProfilingInstrumentationPass()
    : llvm::ModulePass(ID)
      { }

  bool runOnModule(llvm::Module& m) override;
};


}


#endif

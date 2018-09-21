

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

#include "ProfilingInstrumentationPass.h"


using namespace cgprofiler;
using namespace llvm;


namespace cgprofiler {

char ProfilingInstrumentationPass::ID = 0;
static RegisterPass<ProfilingInstrumentationPass> X(
	"ProfilingInstrumentationPass",
	"Callgraph Profiler",
	false /* Only looks at CFG */,
	false /* Analysis Pass */);
} // namespace cgprofiler


static llvm::Constant*
createConstantString(llvm::Module& m, llvm::StringRef str) {
	auto& context = m.getContext();

	auto* name    = llvm::ConstantDataArray::getString(context, str, true);
	auto* int8Ty  = llvm::Type::getInt8Ty(context);
	auto* arrayTy = llvm::ArrayType::get(int8Ty, str.size() + 1);
	auto* asStr   = new llvm::GlobalVariable(
			m, arrayTy, true, llvm::GlobalValue::PrivateLinkage, name);

	auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
	llvm::Value* indices[] = {zero, zero};
	return llvm::ConstantExpr::getInBoundsGetElementPtr(arrayTy, asStr, indices);
}

static void
createFunctionTable(Module& m, uint64_t numFunctions, std::vector<Function*> functions) {
	auto& context = m.getContext();
	auto *int64Ty = Type::getInt64Ty(context);
	auto *stringTy = Type::getInt8PtrTy(context);
	Type *fieldTys[] = {stringTy, int64Ty};
	auto* structTy   = StructType::get(context, fieldTys, false);
	auto* tableTy    = ArrayType::get(structTy, numFunctions);

	std::vector<Constant*> values;
	std::transform(
		functions.begin(),
		functions.end(),
		std::back_inserter(values),
		[&m, structTy, int64Ty](Function* f) {
			Constant* structFields[] = {
				createConstantString(m, f->getName()),
				ConstantInt::get(int64Ty, 0, false)
			};
			return ConstantStruct::get(structTy, structFields);
		}
	);
	auto *funTable = ConstantArray::get(tableTy, values);

	new GlobalVariable(m, tableTy, false, GlobalValue::ExternalLinkage,
		funTable, "CaLlPrOfIlEr_fnmap");

}

static void
createCallTable(Module& m, uint64_t numCalls, std::vector<CallSite> calls) {
	auto& context = m.getContext();

	auto *int64Ty = Type::getInt64Ty(context);
	auto *stringTy = Type::getInt8PtrTy(context);
	Type *fieldTys[] = {stringTy, int64Ty, stringTy};
	auto* structTy   = StructType::get(context, fieldTys, false);
	auto* tableTy    = ArrayType::get(structTy, numCalls);

	std::vector<Constant*> values;
	std::transform(
		calls.begin(),
		calls.end(),
		std::back_inserter(values),
		[&m, structTy, int64Ty](CallSite cs) {
			DILocation *Loc = cs.getInstruction()->getDebugLoc();

			Constant* structFields[] = {
				createConstantString(m, Loc->getFilename()), // srcfile
				ConstantInt::get(int64Ty, Loc->getLine(), false), // line_num
				createConstantString(m, cs.getParent()->getParent()->getName()), // caller
			};
			return ConstantStruct::get(structTy, structFields);
		}
	);
	auto* callTable = ConstantArray::get(tableTy, values);
	new GlobalVariable(m, tableTy, false, GlobalValue::ExternalLinkage,
		callTable, "CaLlPrOfIlEr_csmap");

}


bool
ProfilingInstrumentationPass::runOnModule(llvm::Module& m) {
	// This is the entry point of your instrumentation pass.

	auto& context = m.getContext();
	/* Initial pass */
	/* For each function in module */
	uint64_t fn_id = 0; /* function ids */
	uint64_t cs_id = 0; /* callsite ids */

	/* common types */
	auto *int64Ty = Type::getInt64Ty(context);
	auto *stringTy = Type::getInt8PtrTy(context);
	auto* voidTy  = Type::getVoidTy(context);

	/* type for the `record` runtime helper */
	auto* recordTy = FunctionType::get(voidTy, {stringTy, int64Ty, stringTy, int64Ty}, false);
	auto* recordfn  = m.getOrInsertFunction("CaLlPrOfIlEr_record", recordTy);

	/* type for the `map` runtime helper */
	auto* mapTy = FunctionType::get(voidTy, {int64Ty, int64Ty}, false);
	auto* mapfn = m.getOrInsertFunction("CaLlPrOfIlEr_map", mapTy);

	/* create a global constructor for running the map runtime helper */
	auto* ctorfn = llvm::cast<Function>(m.getOrInsertFunction("CaLlPrOfIlEr_Ctor", voidTy));
	ctorfn->setCallingConv(CallingConv::C);
	BasicBlock* ctorblock = BasicBlock::Create(context, "entry", ctorfn);

	/* lists of things to keep track of */
	DenseMap<int64_t, StringRef> fn_names;
	std::vector<Function*> fn_list;
	std::vector<CallSite> cs_list;

	for (auto& f : m) {
		/* record the name of the function */
		auto name = f.getName();
		/* skip debug */
		if (name.startswith("llvm.dbg") || name.startswith("CaLlPrOfIlEr_")) {
			continue;
		}
		fn_names[fn_id] = name;

		/* save a reference to the function */
		fn_list.push_back(&f);

		fn_id++;

		/* add instrumentation to callsites */
		for (auto& bb : f) {
			for (auto& i : bb) {
				CallSite cs = CallSite(&i);
				if (!cs.getInstruction()) {
					continue;
				}

				/* save a reference to the callsite */
				cs_list.push_back(cs);

				cs_id++;

				/* get debug info */
				DILocation *Loc = cs.getInstruction()->getDebugLoc();
				unsigned line = Loc->getLine();
				StringRef filename = Loc->getFilename();

				IRBuilder<> builder(cs.getInstruction());
				if (auto* directcall = cs.getCalledFunction()) {
					auto calledname = directcall->getName();
					if (calledname.startswith("llvm.dbg") ||
						calledname.startswith("CaLlPrOfIlEr_")) {
						continue;
					}
				}
				/* cast the function to an int */
				auto called = cs.getCalledValue()->stripPointerCasts();
				auto* v = builder.CreateBitOrPointerCast(called, int64Ty);

				/* assemble the arguments */
				std::vector<Value*> args;
				/* filename, line#, caller name, fn_ptr */
				args.push_back(createConstantString(m, filename));
				args.push_back(ConstantInt::get(int64Ty, line, false));
				args.push_back(createConstantString(m, name));
				args.push_back(v);

				/* insert a call to the recording function */
				builder.CreateCall(recordfn, ArrayRef<Value*>(args));
			}
		}
	}

	/* add calls to the `map` helper function to the constructor function */
	IRBuilder<> mapbuilder(ctorblock);
	uint64_t i = 0;
	for (auto &f : fn_list) {
		/* hackcast */
		auto* v = mapbuilder.CreateBitCast(f, int64Ty);
		std::vector<Value*> args;
		/* fn_ptr, fn_id */
		args.push_back(v);
		args.push_back(ConstantInt::get(int64Ty, i++, false));

		mapbuilder.CreateCall(mapfn, ArrayRef<Value*>(args));
	}
	mapbuilder.CreateRetVoid();
	appendToGlobalCtors(m, llvm::cast<Function>(ctorfn), 0);

	/* build the runtime `fnmap` table. */
	auto* global_fnmap_size = ConstantInt::get(int64Ty, fn_id, false);
	new GlobalVariable(m, int64Ty, true, GlobalValue::ExternalLinkage,
		global_fnmap_size, "CaLlPrOfIlEr_fnmap_size");

	createFunctionTable(m, fn_id, fn_list);


	/* build the runtime `csmap` table */
	auto* global_csmap_size = ConstantInt::get(int64Ty, fn_id, false);
	new GlobalVariable(m, int64Ty, true, GlobalValue::ExternalLinkage,
		global_csmap_size, "CaLlPrOfIlEr_csmap_size");

	createCallTable(m, cs_id, cs_list);

	/* build the array for holding the counts */
	auto *countTy = ArrayType::get(int64Ty, cs_id * fn_id);
	auto *global_counts = ConstantArray::getNullValue(countTy);
	new GlobalVariable(m, countTy, false, GlobalValue::ExternalLinkage,
		global_counts, "CaLlPrOfIlEr_calls");

	/* add a call to the printer function to the destructor list */
	auto* printer = m.getOrInsertFunction("CaLlPrOfIlEr_print", voidTy);
	appendToGlobalDtors(m, llvm::cast<Function>(printer), 0);

	return true;
}

#include "Reachability.h"

#include "AE/Svfexe/AbstractInterpretation.h"
#include "SVF-LLVM/PAGBuilder.h"
#include "Util/Options.h"
#include "Util/cJSON.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#if defined(__linux__)
#include <sys/resource.h>
#endif

using namespace SVF;

namespace
{
llvm::StringRef kind(llvm::StringRef name)
{
    if (name == "__verifier_assert") return "assert";
    if (name == "__verifier_eq") return "eq";
    if (name == "__verifier_unreachable") return "unreachable";
    if (name == "reach_error") return "reach_error";
    return {};
}

bool assume(llvm::StringRef name)
{
    return name == "__verifier_assume" || name == "__VERIFIER_assume";
}

bool terminates(llvm::StringRef name)
{
    return name == "abort" || name == "exit" || name == "_Exit";
}

bool nondet(llvm::StringRef name)
{
    return name.starts_with("__VERIFIER_nondet_") ||
           name == "verifier_nondet_int" || name == "verifier_nondet_uint" ||
           name == "verifier_nondet_llong" || name == "verifier_nondet_char" ||
           name == "verifier_nondet_bool";
}

llvm::Value* condition(llvm::CallBase& call)
{
    if (call.arg_empty() || !call.getArgOperand(0)->getType()->isIntegerTy())
        return nullptr;
    return call.getArgOperand(0);
}

llvm::Value* booleanCondition(llvm::Value* value)
{
    if (auto* extend = llvm::dyn_cast<llvm::ZExtInst>(value);
        extend && extend->getSrcTy()->isIntegerTy(1))
        value = extend->getOperand(0);
    while (auto* outer = llvm::dyn_cast<llvm::BinaryOperator>(value))
    {
        if (outer->getOpcode() != llvm::Instruction::Xor || !outer->getType()->isIntegerTy(1) ||
            !llvm::PatternMatch::match(outer->getOperand(1), llvm::PatternMatch::m_One()))
            break;
        auto* inner = llvm::dyn_cast<llvm::BinaryOperator>(outer->getOperand(0));
        if (!inner || inner->getOpcode() != llvm::Instruction::Xor ||
            !llvm::PatternMatch::match(inner->getOperand(1), llvm::PatternMatch::m_One()))
            break;
        value = inner->getOperand(0);
    }
    return value;
}

const llvm::ConstantInt* integerConstant(const llvm::Value* value,
                                        const llvm::DataLayout& layout, unsigned depth = 0)
{
    if (const auto* constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(value))
        return constant;
    const auto* instruction = llvm::dyn_cast_or_null<llvm::Instruction>(value);
    if (!instruction || depth == 8 ||
        (!llvm::isa<llvm::BinaryOperator>(instruction) && !llvm::isa<llvm::ICmpInst>(instruction) &&
         !llvm::isa<llvm::CastInst>(instruction)))
        return nullptr;
    llvm::SmallVector<llvm::Constant*, 2> operands;
    for (const auto& operand : instruction->operands())
    {
        const auto* constant = integerConstant(operand.get(), layout, depth + 1);
        if (!constant)
            return nullptr;
        operands.push_back(const_cast<llvm::ConstantInt*>(constant));
    }
    return llvm::dyn_cast_or_null<llvm::ConstantInt>(
               llvm::ConstantFoldInstOperands(const_cast<llvm::Instruction*>(instruction), operands, layout));
}

const llvm::Function* callee(const llvm::CallBase& call)
{
    return llvm::dyn_cast<llvm::Function>(call.getCalledOperand()->stripPointerCasts());
}

struct Site
{
    llvm::CallBase* call;
    std::string kind;
    std::string file;
    std::string function;
    unsigned line;
    unsigned column;
};

struct Coverage
{
    std::set<const llvm::Instruction*> visited;
    std::set<std::string> gaps;
};

using BranchFacts = std::map<const llvm::BranchInst*, bool>;

Coverage cover(const llvm::Module& module, bool stopChecks, const BranchFacts& facts = {})
{
    Coverage result;
    const auto* main = module.getFunction("main");
    if (!main || main->empty())
    {
        result.gaps.insert("missing main definition");
        return result;
    }
    std::vector<const llvm::Instruction*> pending{&main->getEntryBlock().front()};
    while (!pending.empty())
    {
        const auto* inst = pending.back();
        pending.pop_back();
        if (!result.visited.insert(inst).second)
            continue;
        if (const auto* call = llvm::dyn_cast<llvm::CallBase>(inst))
        {
            const auto* target = callee(*call);
            if (!target)
                result.gaps.insert("indirect, inline assembly, or unresolved call");
            else
            {
                const auto name = target->getName();
                const auto site = kind(name);
                const auto* value = call->arg_empty() ? nullptr :
                                    integerConstant(call->getArgOperand(0), module.getDataLayout());
                if (terminates(name) || (assume(name) && value && value->isZero()))
                    continue;
                if (stopChecks && (site == "unreachable" || site == "reach_error" ||
                                   (!site.empty() && value && value->isZero())))
                {
                    continue;
                }
                if (!site.empty() || assume(name) || nondet(name) ||
                    name.starts_with("llvm.dbg.") || name.starts_with("llvm.lifetime."))
                {
                    // These operations cannot invoke user callbacks in the property model.
                }
                else if (target->isDeclaration())
                    result.gaps.insert("unmodelled external call " + name.str());
                else
                    pending.push_back(&target->getEntryBlock().front());
            }
        }
        if (auto* next = inst->getNextNode())
            pending.push_back(next);
        else if (const auto* branch = llvm::dyn_cast<llvm::BranchInst>(inst);
                 branch && branch->isConditional())
        {
            const auto* constant = integerConstant(branch->getCondition(), module.getDataLayout());
            const auto fact = facts.find(branch);
            if (constant || fact != facts.end())
            {
                const bool taken = constant ? !constant->isZero() : fact->second;
                pending.push_back(&branch->getSuccessor(taken ? 0 : 1)->front());
            }
            else
                for (const auto* successor : llvm::successors(inst->getParent()))
                    pending.push_back(&successor->front());
        }
        else
            for (const auto* successor : llvm::successors(inst->getParent()))
                pending.push_back(&successor->front());
    }
    return result;
}

std::optional<bool> predicateAt(llvm::Value* value, llvm::Instruction* at,
                                llvm::LazyValueInfo& ranges)
{
    if (const auto* constant = integerConstant(value, at->getDataLayout()))
        return !constant->isZero();
    value = booleanCondition(value);
    if (const auto* constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(ranges.getConstant(value, at)))
        return !constant->isZero();
    auto* compare = llvm::dyn_cast<llvm::ICmpInst>(value);
    if (!compare || !compare->getOperand(0)->getType()->isIntegerTy())
        return std::nullopt;
    const auto pred = compare->getPredicate();
    auto* lhs = compare->getOperand(0);
    auto* rhs = compare->getOperand(1);
    if (const auto* constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(
            ranges.getPredicateAt(pred, lhs, rhs, at, true)))
        return !constant->isZero();
    if (lhs == rhs && llvm::isGuaranteedNotToBeUndefOrPoison(lhs))
        return llvm::ICmpInst::isTrueWhenEqual(pred);
    return llvm::isImpliedByDomCondition(pred, lhs, rhs, at, at->getDataLayout());
}

void refine(llvm::CallInst& call, llvm::Value* cond)
{
    auto* block = call.getParent();
    auto* continuation = block->splitBasicBlock(call.getNextNode(), "reach.pass");
    block->getTerminator()->eraseFromParent();
    llvm::IRBuilder<> builder(block);
    if (cond)
    {
        cond = booleanCondition(cond);
        auto* pass = cond->getType()->isIntegerTy(1) ? cond : builder.CreateIsNotNull(cond);
        auto* stopped = llvm::BasicBlock::Create(call.getContext(), "reach.stop", call.getFunction());
        builder.CreateCondBr(pass, continuation, stopped);
        builder.SetInsertPoint(stopped);
    }
    builder.CreateUnreachable();
}

const char* revision(const char* name)
{
    if (const auto* value = std::getenv(name))
        return value;
    return "unknown";
}
}

struct Reachability::Run
{
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    std::string path;
    std::vector<std::string> command;
    std::vector<Site> sites;
    Coverage coverage;
    Coverage withoutStops;
    std::set<const llvm::CallBase*> proven;
    std::set<const llvm::CallBase*> literal;
    std::map<const llvm::CallBase*, std::vector<const Site*>> dependencies;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

Reachability::Reachability(int& argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const llvm::StringRef argument(argv[i]);
        if (!argument.starts_with("-ae-reach="))
            continue;
        run = std::make_unique<Run>();
        run->path = argument.drop_front(sizeof("-ae-reach=") - 1).str();
        run->command.assign(argv, argv + argc);
        for (int j = i; j < argc; ++j)
            argv[j] = argv[j + 1];
        --argc;
        break;
    }
}

Reachability::~Reachability() = default;

bool Reachability::buildModule(const std::vector<std::string>& inputs)
{
    if (!run)
        return false;
    if (inputs.size() != 1 || run->path.empty())
    {
        llvm::errs() << "-ae-reach=<report.json> requires one input module\n";
        std::exit(2);
    }
    llvm::SMDiagnostic diagnostic;
    run->module = llvm::parseIRFile(inputs.front(), diagnostic, run->context);
    if (!run->module)
    {
        diagnostic.print("ae", llvm::errs());
        std::exit(2);
    }
    run->withoutStops = cover(*run->module, false);
    std::vector<std::pair<llvm::CallInst*, llvm::Value*>> refinements;
    for (auto& function : *run->module)
        for (auto& block : function)
            for (auto& instruction : block)
            {
                auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                const auto* target = call ? callee(*call) : nullptr;
                if (!target)
                    continue;
                const auto name = target->getName();
                const auto site = kind(name);
                if (!site.empty())
                {
                    const auto& loc = call->getDebugLoc();
                    unsigned line = loc ? loc.getLine() : 0;
                    if (site != "reach_error" && !call->arg_empty())
                        if (const auto* literal =
                                llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(call->arg_size() - 1)))
                            line = literal->getLimitedValue();
                    run->sites.push_back({call, site.str(),
                                          loc ? loc->getFilename().str() : run->module->getSourceFileName(),
                                          function.getName().str(), line, loc ? loc.getCol() : 0});
                }
                if (auto* plain = llvm::dyn_cast<llvm::CallInst>(call); plain && !plain->isMustTailCall())
                {
                    if (site == "unreachable" || site == "reach_error" || terminates(name))
                        refinements.emplace_back(plain, nullptr);
                    else if (site == "eq" || site == "assert" || assume(name))
                        if (auto* cond = condition(*plain))
                            refinements.emplace_back(plain, cond);
                }
            }
    for (const auto& site : run->sites)
        for (const auto& earlier : run->sites)
        {
            if (earlier.call == site.call || !run->withoutStops.visited.count(earlier.call))
                continue;
            if (earlier.call->getFunction() == site.call->getFunction() &&
                llvm::isPotentiallyReachable(earlier.call, site.call))
                run->dependencies[site.call].push_back(&earlier);
        }
    for (const auto& entry : refinements)
        refine(*entry.first, entry.second);
    BranchFacts facts;
    for (auto& function : *run->module)
    {
        if (function.empty())
            continue;
        llvm::AssumptionCache assumptions(function);
        llvm::LazyValueInfo ranges(&assumptions, &run->module->getDataLayout());
        llvm::DominatorTree dom(function);
        for (auto& block : function)
            if (auto* branch = llvm::dyn_cast<llvm::BranchInst>(block.getTerminator());
                dom.isReachableFromEntry(&block) && branch && branch->isConditional())
                if (auto taken = predicateAt(branch->getCondition(), branch, ranges))
                    facts.emplace(branch, *taken);
        for (const auto& site : run->sites)
        {
            if (site.call->getFunction() != &function || (site.kind != "assert" && site.kind != "eq"))
                continue;
            if (auto* value = condition(*site.call))
            {
                const auto* literal = integerConstant(value, run->module->getDataLayout());
                if (literal && !literal->isZero())
                    run->literal.insert(site.call);
                else if (dom.isReachableFromEntry(site.call->getParent()) &&
                         predicateAt(value, site.call, ranges).value_or(false))
                    run->proven.insert(site.call);
            }
        }
    }
    run->coverage = cover(*run->module, true, facts);
    LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(*run->module);
    return true;
}

bool Reachability::writeReport(AbstractInterpretation& ae) const
{
    if (!run)
        return true;
    auto* modules = LLVMModuleSet::getLLVMModuleSet();
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), cJSON_Delete);
    auto* report = root.get();
    cJSON_AddNumberToObject(report, "schema_version", 1);
    cJSON_AddStringToObject(report, "tool", "ae-reach");
    cJSON_AddStringToObject(report, "tool_commit", revision("AE_REACH_TOOL_COMMIT"));
    cJSON_AddStringToObject(report, "svf_commit", revision("AE_REACH_SVF_COMMIT"));
    const auto mode = Options::AESparsity();
    cJSON_AddStringToObject(report, "mode", mode == AbstractInterpretation::Dense ? "dense" :
                           mode == AbstractInterpretation::SemiSparse ? "semi-sparse" : "sparse");
    auto* command = cJSON_AddArrayToObject(report, "command");
    for (const auto& argument : run->command)
        cJSON_AddItemToArray(command, cJSON_CreateString(argument.c_str()));
    cJSON_AddStringToObject(report, "status", "ok");
    cJSON_AddNumberToObject(report, "seconds",
                          std::chrono::duration<double>(std::chrono::steady_clock::now() - run->start).count());
#if defined(__linux__)
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        cJSON_AddNumberToObject(report, "peak_kib", usage.ru_maxrss);
#endif

    bool allSafe = !run->sites.empty();
    auto* sites = cJSON_AddArrayToObject(report, "sites");
    for (const auto& site : run->sites)
    {
        auto* row = cJSON_CreateObject();
        cJSON_AddItemToArray(sites, row);
        auto* loc = cJSON_AddObjectToObject(row, "location");
        cJSON_AddStringToObject(loc, "file", site.file.c_str());
        cJSON_AddNumberToObject(loc, "line", site.line);
        cJSON_AddNumberToObject(loc, "column", site.column);
        cJSON_AddStringToObject(loc, "function", site.function.c_str());
        cJSON_AddStringToObject(row, "kind", site.kind.c_str());
        const bool closed = run->coverage.gaps.empty();
        const bool dead = !run->coverage.visited.count(site.call);
        const bool literal = run->literal.count(site.call);
        const bool predicate = run->proven.count(site.call);
        const bool safe = closed && (dead || literal || predicate);
        allSafe &= safe;
        cJSON_AddStringToObject(row, "verdict", safe ? "safe" : "unknown");
        auto* facts = cJSON_AddArrayToObject(row, "coverage");
        if (closed)
            cJSON_AddItemToArray(facts, cJSON_CreateString("closed direct-call CFG traversal from main"));
        for (const auto& gap : run->coverage.gaps)
            cJSON_AddItemToArray(facts, cJSON_CreateString(gap.c_str()));
        auto* dependencies = cJSON_AddArrayToObject(row, "depends_on");
        if (!literal && (!dead || run->withoutStops.visited.count(site.call)))
            if (const auto it = run->dependencies.find(site.call); it != run->dependencies.end())
            {
                std::set<unsigned> lines;
                for (const auto* earlier : it->second)
                    if (!closed || (run->coverage.visited.count(earlier->call) &&
                                    !run->literal.count(earlier->call) && !run->proven.count(earlier->call)))
                        lines.insert(earlier->line);
                for (const auto line : lines)
                    cJSON_AddItemToArray(dependencies, cJSON_CreateNumber(line));
            }
        auto* values = cJSON_AddObjectToObject(row, "abstract_values");
        const auto* node = modules->getICFGNode(site.call);
        const bool hasState = ae.hasAbsState(node);
        cJSON_AddBoolToObject(row, "ae_state_present", hasState);
        bool observedZero = false;
        const char* keys[] = {"condition", "left", "right"};
        const unsigned count = site.kind == "eq" ? 3 : site.kind == "assert" ? 1 : 0;
        for (unsigned i = 0; i < count; ++i)
        {
            const auto* operand = i < site.call->arg_size() ? site.call->getArgOperand(i) : nullptr;
            if (operand && hasState && modules->hasValueNode(operand))
            {
                const auto id = modules->getValueNode(operand);
                const auto& value = ae.getAbsValue(PAG::getPAG()->getSVFVar(id), node);
                if (value.isInterval())
                {
                    observedZero |= i == 0 && value.getInterval().equals(IntervalValue(0));
                    cJSON_AddStringToObject(values, keys[i], value.getInterval().toString().c_str());
                    continue;
                }
            }
            cJSON_AddNullToObject(values, keys[i]);
        }
        cJSON_AddStringToObject(row, "reason", !closed ? "open-coverage" :
                               dead ? "closed-cfg-unreachable" :
                               literal ? "literal-nonzero-condition" :
                               predicate ? "dominating-predicate" :
                               observedZero ? "ae-observed-zero" : "violation-not-excluded");
    }
    cJSON_AddStringToObject(report, "verdict", allSafe ? "safe" : "unknown");
    std::unique_ptr<char, decltype(&cJSON_free)> encoded(cJSON_Print(report), cJSON_free);
    std::ofstream output(run->path);
    if (encoded)
        output << encoded.get() << '\n';
    if (!encoded || !output)
    {
        llvm::errs() << "Could not write reachability report " << run->path << '\n';
        return false;
    }
    return true;
}

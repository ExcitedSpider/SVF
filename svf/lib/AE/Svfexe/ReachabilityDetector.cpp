#include "AE/Svfexe/ReachabilityDetector.h"
#include "AE/Svfexe/AbstractInterpretation.h"
#include "Util/Options.h"
#include "Util/cJSON.h"

#include <fstream>
#include <optional>

using namespace SVF;

namespace
{
enum class CheckKind { None, Assert, Eq, Unreachable, ReachError };

CheckKind checkKind(const CallICFGNode* call)
{
    const auto* callee = call->getCalledFunction();
    if (!callee)
        return CheckKind::None;
    const auto& name = callee->getName();
    if (name == "__verifier_assert")
        return CheckKind::Assert;
    if (name == "__verifier_eq")
        return CheckKind::Eq;
    if (name == "__verifier_unreachable")
        return CheckKind::Unreachable;
    if (name == "reach_error")
        return CheckKind::ReachError;
    return CheckKind::None;
}

const char* modeName()
{
    switch (Options::AESparsity())
    {
    case AbstractInterpretation::SemiSparse:
        return "semi-sparse";
    case AbstractInterpretation::Sparse:
        return "sparse";
    default:
        return "dense";
    }
}

std::optional<IntervalValue> argumentInterval(AbstractInterpretation& ae,
        const CallICFGNode* call, u32_t index)
{
    if (index >= call->getNumArgOperands())
        return std::nullopt;
    const ValVar* var = call->getArgument(index);
    if (!ae.hasAbsValue(var, call))
        return std::nullopt;
    return ae.getAbsValue(var, call).getInterval();
}

std::string endpoint(const BoundedInt& value)
{
    if (value.is_plus_infinity())
        return "+inf";
    if (value.is_minus_infinity())
        return "-inf";
    return std::to_string(value.getNumeral());
}

cJSON* intervalJSON(const std::optional<IntervalValue>& interval)
{
    if (!interval)
        return cJSON_CreateNull();
    auto* bounds = cJSON_CreateArray();
    if (!interval->isBottom())
    {
        cJSON_AddItemToArray(bounds, cJSON_CreateString(endpoint(interval->lb()).c_str()));
        cJSON_AddItemToArray(bounds, cJSON_CreateString(endpoint(interval->ub()).c_str()));
    }
    return bounds;
}

void addLocation(cJSON* site, const CallICFGNode* call, CheckKind kind)
{
    const std::string source = call->ICFGNode::getSourceLoc();
    const auto start = source.find('{');
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> location(
        start == std::string::npos ? nullptr : cJSON_Parse(source.c_str() + start),
        cJSON_Delete);
    const auto* file = cJSON_GetObjectItemCaseSensitive(location.get(), "fl");
    const auto* line = cJSON_GetObjectItemCaseSensitive(location.get(), "ln");
    const auto* column = cJSON_GetObjectItemCaseSensitive(location.get(), "cl");
    const auto* caller = call->getCaller();
    cJSON_AddStringToObject(site, "file", cJSON_IsString(file) ? file->valuestring : "");
    cJSON_AddStringToObject(site, "function", caller ? caller->getName().c_str() : "");
    cJSON_AddNumberToObject(site, "column", cJSON_IsNumber(column) ? column->valueint : 0);

    // Kit intrinsics carry the macro invocation's line in their last argument.
    int sourceLine = cJSON_IsNumber(line) ? line->valueint : 0;
    if (kind != CheckKind::ReachError)
    {
        const u32_t index = kind == CheckKind::Eq ? 3 : kind == CheckKind::Assert ? 1 : 0;
        if (index < call->getNumArgOperands())
            if (const auto* constant =
                    SVFUtil::dyn_cast<ConstIntValVar>(call->getArgument(index)))
                sourceLine = static_cast<int>(constant->getSExtValue());
    }
    cJSON_AddNumberToObject(site, "line", sourceLine);
}

bool addVerdict(cJSON* site, AbstractInterpretation& ae,
                const CallICFGNode* call, CheckKind kind)
{
    bool safe = false;
    const char* reason = "ae-no-state";
    const bool hasState = ae.hasAbsState(call);
    auto* intervals = cJSON_AddObjectToObject(site, "intervals");
    if (kind == CheckKind::Assert || kind == CheckKind::Eq)
    {
        const auto condition = hasState ? argumentInterval(ae, call, 0) : std::nullopt;
        cJSON_AddItemToObject(intervals, "condition", intervalJSON(condition));
        if (kind == CheckKind::Eq)
        {
            const auto lhs = hasState ? argumentInterval(ae, call, 1) : std::nullopt;
            const auto rhs = hasState ? argumentInterval(ae, call, 2) : std::nullopt;
            cJSON_AddItemToObject(intervals, "lhs", intervalJSON(lhs));
            cJSON_AddItemToObject(intervals, "rhs", intervalJSON(rhs));
            safe = lhs && rhs && lhs->is_numeral() && *lhs == *rhs;
        }
        else
            safe = true;
        if (hasState)
        {
            const bool mustFail = condition && *condition == IntervalValue(0);
            safe = safe && condition && *condition == IntervalValue(1);
            reason = safe ? "ae-condition-true" : mustFail ?
                     "ae-must-fail-if-reached" : "ae-condition-unknown";
        }
        else
            safe = false;
    }
    else if (hasState)
        reason = "ae-reachable";
    cJSON_AddStringToObject(site, "verdict", safe ? "safe" : "unknown");
    cJSON_AddStringToObject(site, "reason", reason);
    return safe;
}
}

ReachabilityDetector::ReachabilityDetector(AbstractInterpretation& ae,
        std::string path, int argc, char* const argv[])
    : ae(ae), path(std::move(path)), commandLine(argv, argv + argc)
{
    // Inventory call sites even if AE never visits them. No edges are read.
    for (const auto& entry : *PAG::getPAG()->getICFG())
        detect(entry.second);
}

void ReachabilityDetector::detect(const ICFGNode* node)
{
    if (const auto* call = SVFUtil::dyn_cast<CallICFGNode>(node))
        handleStubFunctions(call);
}

void ReachabilityDetector::handleStubFunctions(const CallICFGNode* call)
{
    if (checkKind(call) != CheckKind::None)
        checks.emplace(call->getId(), call);
}

void ReachabilityDetector::reportBug()
{
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> report(
        cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(report.get(), "schema_version", 1);
    cJSON_AddStringToObject(report.get(), "svf_commit", SVF_REACH_BUILD_COMMIT);
    cJSON_AddStringToObject(report.get(), "mode", modeName());
    cJSON_AddStringToObject(report.get(), "status", "ok");
    auto* command = cJSON_AddArrayToObject(report.get(), "command_line");
    for (const auto& arg : commandLine)
        cJSON_AddItemToArray(command, cJSON_CreateString(arg.c_str()));

    auto* sites = cJSON_AddArrayToObject(report.get(), "sites");
    bool safe = true;
    for (const auto& [id, call] : checks)
    {
        const auto kind = checkKind(call);
        auto* site = cJSON_CreateObject();
        cJSON_AddItemToArray(sites, site);
        addLocation(site, call, kind);
        const char* kinds[] = {"", "assert", "eq", "unreachable", "reach_error"};
        cJSON_AddStringToObject(site, "kind", kinds[static_cast<unsigned>(kind)]);
        safe = addVerdict(site, ae, call, kind) && safe;
    }
    cJSON_AddStringToObject(report.get(), "verdict", safe ? "safe" : "unknown");

    const std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_Print(report.get()), cJSON_free);
    std::ofstream output(path);
    if (json)
        output << json.get() << '\n';
    output.close();
    if (!json || !output)
        std::cerr << "Could not write AE reachability report to " << path << '\n';
}

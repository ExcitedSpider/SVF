#pragma once

#include "AE/Svfexe/AEDetector.h"

#include <map>

namespace SVF
{

class ReachabilityDetector final : public AEDetector
{
public:
    ReachabilityDetector(AbstractInterpretation& ae, std::string path,
                         int argc, char* const argv[]);

    void detect(const ICFGNode* node) override;
    void handleStubFunctions(const CallICFGNode* call) override;
    void reportBug() override;

private:
    AbstractInterpretation& ae;
    std::string path;
    std::vector<std::string> commandLine;
    std::map<NodeID, const CallICFGNode*> checks;
};

}

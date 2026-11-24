#pragma once

#include <memory>
#include <string>
#include <vector>

namespace SVF
{
class AbstractInterpretation;

class Reachability
{
public:
    Reachability(int& argc, char** argv);
    ~Reachability();
    bool buildModule(const std::vector<std::string>& inputs);
    bool writeReport(AbstractInterpretation& ae) const;

private:
    struct Run;
    std::unique_ptr<Run> run;
};
}

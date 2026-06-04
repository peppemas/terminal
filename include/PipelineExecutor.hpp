#pragma once

#ifndef TERMINAL_PIPELINEEXECUTOR_HPP
#define TERMINAL_PIPELINEEXECUTOR_HPP

#include "Pipeline.hpp"
#include "CommandParser.hpp"
#include <windows.h>

class PipelineExecutor {
public:
    explicit PipelineExecutor(const CommandParser& parser);
    bool execute(const Pipeline& pipeline) const;

private:
    const CommandParser& m_parser;

    bool runInternalStage(const Stage& stage,
                          std::istream& input,
                          std::ostream& output) const;

    bool runExternalStage(const Stage& stage,
                          HANDLE hInputRead,
                          HANDLE hOutputWrite) const;

    bool executeStage(const Stage& stage,
                      std::istream& input,
                      std::ostream& output,
                      HANDLE hInputRead,
                      HANDLE hOutputWrite,
                      bool useRealHandles) const;
};

#endif // TERMINAL_PIPELINEEXECUTOR_HPP

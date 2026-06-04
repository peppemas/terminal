#pragma once

#ifndef TERMINAL_PIPELINE_HPP
#define TERMINAL_PIPELINE_HPP

#include <string>
#include <vector>

struct Stage {
    std::string command;
    std::vector<std::string> args;
};

struct Pipeline {
    std::vector<Stage> stages;
};

Pipeline parsePipeline(const std::string& line);

#endif // TERMINAL_PIPELINE_HPP

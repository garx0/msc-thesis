#pragma once
#ifndef DELAYTOOL_CONFIGIO_H
#define DELAYTOOL_CONFIGIO_H

#include "tinyxml2/tinyxml2.h"
#include "algo.h"

constexpr double jitStartDefault = 500; // us
constexpr int sminDefault = 64; // bytes
constexpr uint64_t bpMaxIterDefault = 100000;
constexpr uint64_t cyclicMaxIterDefault = 100;
constexpr int nFabricsDefault = 8;

std::vector<int> TokenizeCsv(const std::string& str);

VlinkConfigOwn fromXml(tinyxml2::XMLDocument& doc, const std::string& scheme,
        double jitDefaultValue = jitStartDefault, int forceLinkRate = 0,
        double loadFactor = 1., uint64_t bpMaxIter = bpMaxIterDefault,
        uint64_t cyclicMaxIter = cyclicMaxIterDefault, int nFabrics = nFabricsDefault);

// doc must already contain the resources and VL configuration
// (e.g. doc used for building config)
bool toXml(VlinkConfig* config, tinyxml2::XMLDocument& doc);

struct stats_t {double min, max, mean, var;};

stats_t getStats(std::map<int, double> data);

// check if bandwidth usage of any link is <= 100%
bool bwCorrect(std::map<int, double> bwUsage);

void DebugInfo(const VlinkConfig* config);

#endif //DELAYTOOL_CONFIGIO_H

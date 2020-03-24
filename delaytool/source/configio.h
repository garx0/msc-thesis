#pragma once
#ifndef DELAYTOOL_CONFIGIO_H
#define DELAYTOOL_CONFIGIO_H

#include "tinyxml2/tinyxml2.h"
#include "algo.h"

VlinkConfigOwn fromXml(tinyxml2::XMLDocument& doc, const std::string& scheme);

// doc must already contain the resources and VL configuration
// (e.g. doc used for building config)
bool toXml(VlinkConfig* config, tinyxml2::XMLDocument& doc);

void DebugInfo(const VlinkConfig* config);

#endif //DELAYTOOL_CONFIGIO_H

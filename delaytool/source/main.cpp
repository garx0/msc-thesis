#include <iostream>
#include <cstdio>
#include <string>
#include "tinyxml2/tinyxml2.h"
#include "argparse/argparse.hpp"
#include "configio.h"
#include "algo.h"

std::string strToLower(const std::string& str) {
    std::string str2 = str;
    std::for_each(str2.begin(), str2.end(), [](char& c){
        c = static_cast<char>(std::tolower(c));
    });
    return str2;
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("delaytool");

    program.add_argument("input")
            .help("input xml file with network resources and virtual links");

    program.add_argument("output")
            .help("output xml file with delays of all VLs to all their destinations");

    program.add_argument("-s", "--scheme")
            .help("scheme name: oq|cioq (default: cioq)")
            .default_value(std::string("CIOQ"))
            .action([](const std::string& value) {
                static const std::map<std::string, std::string> mapping = {
                        {"oq", "OQ"},
                        {"cioq", "CIOQ"},
                };
                auto found = mapping.find(strToLower(value));
                if(found != mapping.end()) {
                    return found->second;
                } else {
                    throw std::runtime_error("invalid value of -s");
                }
            });

    program.add_argument("--nfabrics")
            .help("clock period for VOQ scheme")
            .action([](const std::string& value) { return std::stoi(value); })
            .default_value(nFabricsDefault);

    program.add_argument("--printconfig")
            .implicit_value(true)
            .default_value(false)
            .help("print verbose info about resources and VL configuration");

    program.add_argument("--printdelays")
            .implicit_value(true)
            .default_value(false)
            .help("print calculated E2E delays");

    program.add_argument("--printcioq")
            .implicit_value(true)
            .default_value(false)
            .help("print verbose info about CIOQ mapping of input queues and fabrics");

    program.add_argument("-j", "--jitdef")
            .action([](const std::string& value) { return std::stof(value); })
            .default_value(static_cast<float>(jitStartDefault))
            .help("default start jitter in microseconds if not specified in input data (default: 500)");

    program.add_argument("-r", "--rate")
            .action([](const std::string& value) { return std::stoi(value); })
            .default_value(0)
            .help("change (force) link rate to specified value, in byte/ms");

    program.add_argument("-f", "--factor")
            .action([](const std::string& value) { return std::stof(value); })
            .default_value(1.f)
            .help("multiply max packet size for all VLs by factor (and cast to integer)");

    program.add_argument("--bpmaxit")
            .action([](const std::string& value) { return static_cast<uint64_t>(std::stof(value)); })
            .default_value(static_cast<uint64_t>(bpMaxIterDefault))
            .help(std::string("max number of iterations of calculating busy period.\n")
                  + "its calculation won't be endless because its parameters are checked for a sign of this earlier,"
                  + "but it may take too long. set 0 for no restrictions.");

    program.add_argument("--cycmaxit")
            .action([](const std::string& value) { return static_cast<uint64_t>(std::stof(value)); })
            .default_value(static_cast<uint64_t>(cyclicMaxIterDefault))
            .help(std::string("max number of iterations for calculating delays if the data dependencies are cyclic.\n")
                  + "set 0 for no restrictions.");

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cout << program;
        return 0;
    }

    std::string fileIn = program.get<std::string>("input");
    std::string fileOut = program.get<std::string>("output");
    std::string scheme = program.get<std::string>("--scheme");
    float startJitDefault = program.get<float>("--jitdef");
    int forceLinkRate = program.get<int>("--rate");
    int nFabrics = program.get<int>("--nfabrics");
    float sizeFactor = program.get<float>("--factor");
    bool printConfig = program.get<bool>("--printconfig");
    bool printDelays = program.get<bool>("--printdelays");
    bool printCioq = program.get<bool>("--printcioq");
    uint64_t bpMaxIter = program.get<uint64_t>("--bpmaxit");
    uint64_t cyclicMaxIter = program.get<uint64_t>("--cycmaxit");

    tinyxml2::XMLDocument doc;
    auto err = doc.LoadFile(fileIn.c_str());
    if(err) {
        fprintf(stderr, "error: can't load input file: %s\n", tinyxml2::XMLDocument::ErrorIDToName(err));
        return 0;
    }
    FILE *fpOut = fopen(fileOut.c_str(), "w");
    if(fpOut == nullptr) {
        fprintf(stderr, "error: can't open output file: %s\n", fileOut.c_str());
        return 0;
    }
    VlinkConfigOwn config = fromXml(doc, scheme,
            startJitDefault, forceLinkRate, sizeFactor, bpMaxIter, cyclicMaxIter, nFabrics);
    if(config == nullptr) {
        fprintf(stderr, "error reading from xml\n");
        fclose(fpOut);
        return 0;
    }
    if(printConfig) {
        DebugInfo(config.get());
    }
    auto bwUsage = config->bwUsage();
    if(!bwCorrect(bwUsage)) {
        fprintf(stderr, "error: bandwidth usage is more than 100%%\n");
        fclose(fpOut);
        return 0;
    }
    auto bwStats = getStats(bwUsage);
    printf("bwUsage: min=%f, max=%f, mean=%f, var=%f\n",
           bwStats.min, bwStats.max, bwStats.mean, bwStats.var);

    try {
        Error cioqErr = config->buildTables(printCioq);
        if(cioqErr) {
            fprintf(stderr, "error building VIQ/fabrics mapping: %s, %s\n",
                    cioqErr.TypeString().c_str(), cioqErr.Verbose().c_str());
            fclose(fpOut);
            return 0;
        }
    } catch(std::exception& e) {
        fprintf(stderr, "error calculating delay because of exception: %s\n", e.what());
    }

    try {
        Error calcErr = config->calcDelays(printDelays);
        if(calcErr) {
            fprintf(stderr, "error calculating delay, can't calculate delays on this network configuration: %s, %s\n",
                    calcErr.TypeString().c_str(), calcErr.Verbose().c_str());
            fclose(fpOut);
            return 0;
        }
    } catch(std::exception& e) {
        fprintf(stderr, "error calculating delay because of exception: %s\n", e.what());
    }
    bool ok = toXml(config.get(), doc);
    if(!ok) {
        fprintf(stderr, "error converting to xml\n");
        fclose(fpOut);
        return 0;
    }
    err = doc.SaveFile(fpOut, false);
    if(err) {
        fprintf(stderr, "error writing to output file: %s\n", tinyxml2::XMLDocument::ErrorIDToName(err));
    }
    fclose(fpOut);
    return 0;
}

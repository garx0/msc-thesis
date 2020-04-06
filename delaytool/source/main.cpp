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
            .help("scheme type: voqa|voqb|oqp|oqa|oqb (default: oqp)")
            .default_value(std::string("OqPacket"))
            .action([](const std::string& value) {
                static const std::map<std::string, std::string> mapping = {
                        {"voqa", "VoqA"},
                        {"voqb", "VoqB"},
                        {"oqp", "OqPacket"},
                        {"oqa", "OqA"},
                        {"oqb", "OqB"},
                        {"oqc", "OqC"}, // DEBUG
                        {"mock", "Mock"}, // DEBUG
                };
                auto found = mapping.find(strToLower(value));
                if(found != mapping.end()) {
                    return found->second;
                } else {
                    throw std::runtime_error("bad value of -s");
                }
            });

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cout << program;
        return 0;
    }

    std::string fileIn = program.get<std::string>("input");
    std::string fileOut = program.get<std::string>("output");
    std::string scheme = program.get<std::string>("--scheme");
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
    VlinkConfigOwn config = fromXml(doc, scheme);
    if(config == nullptr) {
        fprintf(stderr, "error reading from xml\n");
        fclose(fpOut);
        return 0;
    }
    DebugInfo(config.get()); // DEBUG
    Error calcErr = config->calcE2e();
    if(calcErr) {
        fprintf(stderr, "error calculating delay because of bad VL configuration: %s, %s\n",
                calcErr.TypeString().c_str(), calcErr.Verbose().c_str());
        fclose(fpOut);
        return 0;
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

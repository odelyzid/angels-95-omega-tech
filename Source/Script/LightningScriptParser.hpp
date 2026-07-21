#pragma once
#include "LightningEntityDef.hpp"
#include <string>
#include <vector>

// LightningScriptParser — parses .ozls entity definition files into EntityDef
class LightningScriptParser {
public:
    // Parse a single .ozls file content into an EntityDef
    static EntityDef Parse(const std::string& content, const std::string& sourcePath = "");

    // Parse multiple .ozls files, returns all definitions found
    static std::vector<EntityDef> ParseAll(const std::vector<std::string>& fileContents,
                                            const std::vector<std::string>& sourcePaths = {});

private:
    struct ParseState {
        const std::string* content;
        size_t pos = 0;
        int line = 1;
        std::string sourcePath;
    };

    static void SkipWhitespace(ParseState& s);
    static std::string ReadToken(ParseState& s);
    static std::string ReadString(ParseState& s);
    static float ReadNumber(ParseState& s);
    static void Expect(ParseState& s, const std::string& expected);
    static void SkipBlock(ParseState& s);

    static EntityStatBlock ParseStatBlock(ParseState& s);
    static EntityAction ParseAction(ParseState& s);
    static EntityAction ParseActionWithName(const std::string& name, ParseState& s);
};

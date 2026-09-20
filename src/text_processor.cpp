#include "aiws/text_processor.hpp"

#include <algorithm>
#include <cctype>

namespace aiws {

std::vector<TokenInfo> TextProcessor::tokenize(const std::string& text) {
    std::vector<TokenInfo> tokens;
    const std::size_t n = text.size();

    std::size_t paragraph = 0;
    bool have_newline = false;
    bool only_ws_since_newline = true;

    bool in_token = false;
    std::size_t token_start = 0;
    std::string current;

    for (std::size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        bool is_letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        bool is_digit = (c >= '0' && c <= '9');

        if (is_letter || is_digit) {
            if (!in_token) {
                in_token = true;
                token_start = i;
                current.clear();
            }
            current.push_back(is_letter ? static_cast<char>(std::tolower(c)) : static_cast<char>(c));
            have_newline = false;
            only_ws_since_newline = true;
            continue;
        }

        if (in_token) {
            tokens.push_back(TokenInfo{current, token_start, i, paragraph});
            in_token = false;
        }

        if (c == '\n') {
            if (have_newline && only_ws_since_newline) {
                ++paragraph;
            }
            have_newline = true;
            only_ws_since_newline = true;
        } else if (c == ' ' || c == '\t' || c == '\r') {
        } else {
            have_newline = false;
            only_ws_since_newline = true;
        }
    }

    if (in_token) {
        tokens.push_back(TokenInfo{current, token_start, n, paragraph});
    }
    return tokens;
}

std::vector<std::string> TextProcessor::terms(const std::string& text) {
    auto tokens = tokenize(text);
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (auto& t : tokens) {
        result.push_back(std::move(t.token));
    }
    return result;
}

std::string TextProcessor::normalize(const std::string& text) {
    auto t = terms(text);
    return join(t, 0, t.size());
}

std::string TextProcessor::join(const std::vector<TokenInfo>& tokens, std::size_t begin, std::size_t end) {
    if (begin >= tokens.size() || begin >= end) return {};
    end = std::min(end, tokens.size());

    std::string out;
    for (std::size_t i = begin; i < end; ++i) {
        if (i > begin) out += ' ';
        out += tokens[i].token;
    }
    return out;
}

std::string TextProcessor::join(const std::vector<std::string>& tokens, std::size_t begin, std::size_t end) {
    if (begin >= tokens.size() || begin >= end) return {};
    end = std::min(end, tokens.size());

    std::string out;
    for (std::size_t i = begin; i < end; ++i) {
        if (i > begin) out += ' ';
        out += tokens[i];
    }
    return out;
}

}  // namespace aiws


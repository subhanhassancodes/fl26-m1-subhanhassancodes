#include "aiws/chunker.hpp"

#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <utility>

namespace aiws {

Chunker::Chunker(ChunkingPolicy policy) : policy_(policy) {
    if (policy_.max_tokens == 0 || policy_.overlap >= policy_.max_tokens ||
        policy_.paragraph_window > policy_.max_tokens) {
        throw std::invalid_argument("invalid chunking policy");
    }
}

std::vector<Chunk> Chunker::chunk(const Document& document, std::size_t document_order) const {
    std::vector<Chunk> result;

    const auto tokens = TextProcessor::tokenize(document.text());
    if (tokens.empty()) {
        return result;
    }

    const std::size_t total = tokens.size();
    const std::size_t max_tokens = policy_.max_tokens;
    const std::size_t overlap = policy_.overlap;
    const std::size_t window = policy_.paragraph_window;
    const std::size_t lower_bound = (window <= max_tokens) ? (max_tokens - window) : 0;

    std::size_t start = 0;
    std::size_t sequence = 0;

    while (start < total) {
        const std::size_t remaining = total - start;
        std::size_t end;

        if (remaining <= max_tokens) {
            end = total;
        } else {
            end = start + max_tokens;

            for (std::size_t pos = max_tokens;; --pos) {
                const std::size_t last_idx = start + pos - 1;
                const std::size_t next_idx = last_idx + 1;
                if (next_idx < total && tokens[last_idx].paragraph != tokens[next_idx].paragraph) {
                    end = start + pos;
                    break;
                }
                if (pos == lower_bound) break;
            }
        }

        Chunk c;
        c.document_id = document.id();
        c.document_order = document_order;
        c.sequence = sequence;
        c.id = document.id() + "#" + std::to_string(sequence);
        c.text = TextProcessor::join(tokens, start, end);
        c.token_count = end - start;
        c.source_begin = tokens[start].begin;
        c.source_end = tokens[end - 1].end;
        result.push_back(std::move(c));

        if (end >= total) break;

        std::size_t next_start = (end > overlap) ? (end - overlap) : 0;
        if (next_start <= start) {
            next_start = start + 1;
        }
        start = next_start;
        ++sequence;
    }

    return result;
}

}  // namespace aiws


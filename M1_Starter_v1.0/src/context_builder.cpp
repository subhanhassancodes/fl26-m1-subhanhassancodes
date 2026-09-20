#include "aiws/context_builder.hpp"

#include "aiws/text_processor.hpp"

namespace aiws {

std::vector<ContextItem> ContextBuilder::build(const std::vector<SearchResult>& ranked,
                                                std::size_t token_budget) const {
    std::vector<ContextItem> items;
    std::size_t used = 0;

    for (const auto& result : ranked) {
        if (used >= token_budget) break;

        const std::size_t remaining = token_budget - used;
        const auto tokens = TextProcessor::terms(result.text);
        const std::size_t item_tokens = tokens.size();

        ContextItem item;
        item.chunk_id = result.chunk_id;
        item.document_id = result.document_id;
        item.chunk_sequence = result.chunk_sequence;
        item.score = result.score;

        if (item_tokens <= remaining) {
            item.text = result.text;
            item.token_count = item_tokens;
            item.truncated = false;
            items.push_back(std::move(item));
            used += item_tokens;
        } else {
            item.text = TextProcessor::join(tokens, 0, remaining);
            item.token_count = remaining;
            item.truncated = true;
            items.push_back(std::move(item));
            break;
        }
    }

    return items;
}

}  // namespace aiws


#include "aiws/retrieval_engine.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace aiws {

double RetrievalEngine::canonical_score(double value) {
    constexpr double kScale = 1e12;
    return std::round(value * kScale) / kScale;
}

std::vector<SearchResult> RetrievalEngine::search(const std::string& query,
                                                   int k,
                                                   const std::vector<Chunk>& chunks,
                                                   const CorpusIndex& index) const {
    if (k < 0) {
        throw std::invalid_argument("k must not be negative");
    }

    std::vector<SearchResult> results;
    if (k == 0) {
        return results;
    }

    std::vector<std::string> unique_terms;
    {
        std::unordered_set<std::string> seen;
        for (auto& term : TextProcessor::terms(query)) {
            if (seen.insert(term).second) {
                unique_terms.push_back(term);
            }
        }
    }
    if (unique_terms.empty()) {
        return results;
    }

    const double N = static_cast<double>(chunks.size());
    const double Q = static_cast<double>(unique_terms.size());

    std::unordered_map<std::size_t, double> base_scores;
    std::unordered_map<std::size_t, std::size_t> matched_counts;

    for (const auto& term : unique_terms) {
        const auto* postings = index.postings(term);
        if (!postings || postings->empty()) continue;

        const double df = static_cast<double>(postings->size());
        const double idf = std::log((N + 1.0) / (df + 1.0)) + 1.0;

        for (const auto& posting : *postings) {
            const double tf = 1.0 + std::log(static_cast<double>(posting.frequency));
            base_scores[posting.chunk_index] += tf * idf;
            matched_counts[posting.chunk_index] += 1;
        }
    }

    std::vector<std::size_t> candidates;
    candidates.reserve(base_scores.size());
    std::unordered_map<std::size_t, double> final_scores;
    final_scores.reserve(base_scores.size());

    for (const auto& [chunk_idx, base] : base_scores) {
        if (chunk_idx >= chunks.size()) continue;
        const std::size_t matched = matched_counts[chunk_idx];
        const double coverage = 1.0 + 0.10 * static_cast<double>(matched) / Q;
        final_scores[chunk_idx] = canonical_score(base * coverage);
        candidates.push_back(chunk_idx);
    }

    std::sort(candidates.begin(), candidates.end(), [&](std::size_t a, std::size_t b) {
        const double sa = final_scores[a];
        const double sb = final_scores[b];
        if (sa != sb) return sa > sb;
        if (chunks[a].document_order != chunks[b].document_order) {
            return chunks[a].document_order < chunks[b].document_order;
        }
        return chunks[a].sequence < chunks[b].sequence;
    });

    const std::size_t limit = std::min(candidates.size(), static_cast<std::size_t>(k));
    results.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        const std::size_t idx = candidates[i];
        const Chunk& c = chunks[idx];

        SearchResult r;
        r.chunk_id = c.id;
        r.document_id = c.document_id;
        r.chunk_sequence = c.sequence;
        r.text = c.text;
        r.score = final_scores[idx];
        r.matched_terms = matched_counts[idx];
        results.push_back(std::move(r));
    }

    return results;
}

}  // namespace aiws
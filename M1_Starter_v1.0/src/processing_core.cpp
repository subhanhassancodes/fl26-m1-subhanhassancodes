#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace aiws {

struct ProcessingCore::Impl {
    Chunker chunker{ChunkingPolicy{ProcessingCore::kMaxChunkTokens,
                                    ProcessingCore::kChunkOverlap,
                                    ProcessingCore::kParagraphPreferenceWindow}};
    std::vector<Chunk> chunks;
    CorpusIndex index;
    RetrievalEngine engine;
    ContextBuilder context_builder;
};

namespace {
std::string normalize_single_term(const std::string& raw, bool& is_empty) {
    auto tokens = TextProcessor::terms(raw);
    if (tokens.empty()) {
        is_empty = true;
        return {};
    }
    if (tokens.size() > 1) {
        throw std::invalid_argument("term must normalize to a single token");
    }
    is_empty = false;
    return tokens.front();
}
}  // namespace

ProcessingCore::ProcessingCore() : impl_(std::make_unique<Impl>()) {}

ProcessingCore::~ProcessingCore() = default;

ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;

ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    const auto& docs = workspace.documents();

    std::unordered_set<std::string> seen_ids;
    seen_ids.reserve(docs.size());
    for (const auto& doc : docs) {
        if (!seen_ids.insert(doc.id()).second) {
            throw std::invalid_argument("duplicate document id in workspace");
        }
    }

    std::vector<Chunk> new_chunks;
    for (std::size_t order = 0; order < docs.size(); ++order) {
        auto doc_chunks = impl_->chunker.chunk(docs[order], order);
        for (auto& c : doc_chunks) {
            new_chunks.push_back(std::move(c));
        }
    }

    CorpusIndex new_index;
    new_index.build(new_chunks);

    impl_->chunks = std::move(new_chunks);
    impl_->index = std::move(new_index);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept {
    return impl_->chunks;
}

std::size_t ProcessingCore::chunk_count() const noexcept {
    return impl_->chunks.size();
}

std::size_t ProcessingCore::document_frequency(const std::string& term) const {
    bool empty = false;
    auto normalized = normalize_single_term(term, empty);
    if (empty) return 0;
    return impl_->index.document_frequency(normalized);
}

std::size_t ProcessingCore::term_frequency(const std::string& term,
                                           const std::string& chunk_id) const {
    bool empty = false;
    auto normalized = normalize_single_term(term, empty);
    if (empty) return 0;
    return impl_->index.term_frequency(normalized, chunk_id);
}

std::vector<SearchResult> ProcessingCore::search(const std::string& query, int k) const {
    return impl_->engine.search(query, k, impl_->chunks, impl_->index);
}

std::vector<ContextItem> ProcessingCore::build_context(const std::string& query,
                                                        int k,
                                                        std::size_t token_budget) const {
    auto ranked = search(query, k);
    return impl_->context_builder.build(ranked, token_budget);
}

}  // namespace aiws


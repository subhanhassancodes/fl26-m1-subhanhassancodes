#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/document.hpp"
#include "aiws/processing_core.hpp"
#include "aiws/processing_types.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"
#include "aiws/workspace.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::string numbered_words(int n, int start = 0) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (!s.empty()) s += ' ';
        s += "w" + std::to_string(start + i);
    }
    return s;
}

}  // namespace

void test_text_processing() {
    using namespace aiws;

    check(TextProcessor::normalize("R2-D2") == "r2 d2", "hyphen splits alnum runs");
    check(TextProcessor::normalize("Hello,  WORLD! 2026") == "hello world 2026",
          "case-fold + separator collapse + digit retention");
    check(TextProcessor::normalize("...---***").empty(), "punctuation-only input normalizes to empty");
    check(TextProcessor::normalize("").empty(), "empty input normalizes to empty");
    check(TextProcessor::normalize("   \t  ").empty(), "whitespace-only input normalizes to empty");

    auto single_nl = TextProcessor::tokenize("alpha\nbeta");
    check(single_nl.size() == 2 && single_nl[0].paragraph == 0 && single_nl[1].paragraph == 0,
          "a single newline does not create a paragraph boundary");

    auto blank_line = TextProcessor::tokenize("alpha\n\nbeta");
    check(blank_line.size() == 2 && blank_line[0].paragraph == 0 && blank_line[1].paragraph == 1,
          "a blank line (LF) creates a paragraph boundary");

    auto blank_line_crlf = TextProcessor::tokenize("alpha\r\n\r\nbeta");
    check(blank_line_crlf.size() == 2 && blank_line_crlf[0].paragraph == 0 &&
              blank_line_crlf[1].paragraph == 1,
          "CRLF blank lines are treated the same as LF blank lines");

    auto blank_line_spaces = TextProcessor::tokenize("alpha\n   \nbeta");
    check(blank_line_spaces.size() == 2 && blank_line_spaces[0].paragraph == 0 &&
              blank_line_spaces[1].paragraph == 1,
          "spaces/tabs between the two newlines still count as a blank line");

    auto spans = TextProcessor::tokenize("Hi, World");
    check(spans.size() == 2 && spans[0].begin == 0 && spans[0].end == 2 && spans[0].token == "hi",
          "token span covers original (pre-normalization) characters");
    check(spans[1].begin == 4 && spans[1].end == 9 && spans[1].token == "world",
          "second token span is correctly offset past the separator");

    check(TextProcessor::join(TextProcessor::terms("a b c"), 1, 3) == "b c",
          "join(vector<string>) reproduces a token subrange with single spaces");
    check(TextProcessor::join(TextProcessor::terms("a b c"), 5, 6).empty(),
          "join with an out-of-range subrange returns empty rather than throwing/crashing");
}

void test_chunker_boundaries() {
    using namespace aiws;
    Chunker chunker;

    {
        std::string text = numbered_words(105) + "\n\n" + numbered_words(30, 105);
        Document doc{"pw", "Preferred window", text};
        auto chunks = chunker.chunk(doc, 0);
        check(chunks.size() == 2, "paragraph-preferred split still produces the expected chunk count");
        check(!chunks.empty() && chunks[0].token_count == 105,
              "chunk ends at the paragraph boundary (position 105) inside the window, not at 120");
        check(chunks.size() > 1 && chunks[1].token_count == 50,
              "second chunk covers the 20-token overlap plus the remaining tokens");
    }

    {
        std::string text = numbered_words(50) + "\n\n" + numbered_words(80, 50);
        Document doc{"ow", "Outside window", text};
        auto chunks = chunker.chunk(doc, 0);
        check(!chunks.empty() && chunks[0].token_count == 120,
              "an out-of-window paragraph boundary is ignored; chunk uses the 120-token hard limit");
    }

    {
        std::string text = numbered_words(121);
        Document doc{"seqdoc", "Sequencing", text};
        auto chunks = chunker.chunk(doc, 3);
        check(chunks.size() == 2, "121 tokens split into exactly two chunks");
        check(chunks[0].id == "seqdoc#0" && chunks[1].id == "seqdoc#1",
              "chunk ids follow <document-id>#<sequence>, sequence starting at 0");
        check(chunks[0].document_order == 3 && chunks[1].document_order == 3,
              "document_order is stamped on every chunk from that document");
        check(chunks[1].token_count == 21, "20-token overlap: second chunk holds 120-100=20 overlap + 1 new");
    }

    {
        Document empty_doc{"e1", "Empty", "   ...--- \t\n\n  "};
        check(chunker.chunk(empty_doc, 0).empty(), "punctuation/whitespace-only document produces no chunks");
        Document truly_empty{"e2", "Empty2", ""};
        check(chunker.chunk(truly_empty, 0).empty(), "empty-string document produces no chunks");
    }

    {
        Document doc{"d", "D", numbered_words(200)};
        auto a = chunker.chunk(doc, 0);
        auto b = chunker.chunk(doc, 0);
        check(a.size() == b.size(), "reprocessing produces the same chunk count");
        bool identical = true;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i].id != b[i].id || a[i].text != b[i].text || a[i].token_count != b[i].token_count) {
                identical = false;
            }
        }
        check(identical, "reprocessing an unchanged document yields identical chunks and ids");
    }

    {
        Document doc{"span", "Span", "hello world"};
        auto chunks = chunker.chunk(doc, 0);
        check(chunks.size() == 1 && chunks[0].source_begin == 0 && chunks[0].source_end == 11,
              "single-chunk source span covers the full original document text");
    }
}

void test_corpus_index_and_rebuild() {
    using namespace aiws;

    {
        Chunk c0;
        c0.id = "docA#0";
        c0.document_id = "docA";
        c0.document_order = 0;
        c0.sequence = 0;
        c0.text = "cat dog cat";
        c0.token_count = 3;

        Chunk c1;
        c1.id = "docB#0";
        c1.document_id = "docB";
        c1.document_order = 1;
        c1.sequence = 0;
        c1.text = "dog bird";
        c1.token_count = 2;

        std::vector<Chunk> chunks{c0, c1};
        CorpusIndex index(chunks);

        check(index.document_frequency("dog") == 2, "dog appears in both indexed chunks");
        check(index.document_frequency("cat") == 1, "cat appears in exactly one chunk");
        check(index.document_frequency("nonexistent") == 0, "unseen term has document frequency 0");
        check(index.term_frequency("cat", "docA#0") == 2, "cat occurs twice within docA#0");
        check(index.term_frequency("dog", "docB#0") == 1, "dog occurs once within docB#0");
        check(index.term_frequency("cat", "missing#0") == 0, "unknown chunk id yields term frequency 0");
        check(index.chunk_index("docB#0") == 1, "chunk_index resolves a known chunk id to its position");

        const auto* postings = index.postings("dog");
        check(postings != nullptr && postings->size() == 2, "postings() exposes the raw posting list for a term");

        const Chunk* found = index.find_chunk(chunks, "docA#0");
        check(found != nullptr && found->text == "cat dog cat", "find_chunk resolves id to the matching Chunk");
        check(index.find_chunk(chunks, "nope#0") == nullptr, "find_chunk returns nullptr for an unknown id");

        Chunk c2;
        c2.id = "docC#0";
        c2.document_id = "docC";
        c2.document_order = 0;
        c2.sequence = 0;
        c2.text = "only unique here";
        std::vector<Chunk> smaller{c2};
        index.build(smaller);
        check(index.document_frequency("dog") == 0, "rebuild drops postings for terms no longer indexed");
        check(index.document_frequency("unique") == 1, "rebuild reflects the newly built corpus");
        check(index.find_chunk(smaller, "docA#0") == nullptr, "stale chunk ids are gone after rebuild");
    }

    {
        ProcessingCore core;
        Workspace ws;
        ws.add_document(Document{"a", "A", "keepme unique term"});
        core.rebuild(ws);
        check(core.document_frequency("keepme") == 1, "initial rebuild indexes the first workspace");

        Workspace bad_ws;
        bad_ws.add_document(Document{"dup", "One", "x"});
        bad_ws.add_document(Document{"dup", "Two", "y"});
        bool threw = false;
        try {
            core.rebuild(bad_ws);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "duplicate document ids in a rebuild throw std::invalid_argument");
        check(core.document_frequency("keepme") == 1,
              "a failed rebuild leaves the previously valid corpus completely unchanged");
        check(core.chunk_count() == 1, "chunk_count also reflects the untouched prior corpus after a failed rebuild");

        Workspace ws2;
        ws2.add_document(Document{"b", "B", "different words entirely"});
        core.rebuild(ws2);
        check(core.document_frequency("keepme") == 0, "successful rebuild removes terms from documents no longer present");
        check(core.document_frequency("different") == 1, "successful rebuild indexes the new workspace's terms");
    }

    {
        ProcessingCore core;
        Workspace ws;
        ws.add_document(Document{"d", "D", "alpha beta"});
        core.rebuild(ws);

        check(core.document_frequency("???") == 0, "a term that normalizes to no tokens returns 0, not an error");
        bool threw = false;
        try {
            (void)core.document_frequency("two words");
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "a term that normalizes to more than one token throws std::invalid_argument");
    }
}

void test_ranking_and_determinism() {
    using namespace aiws;

    Chunk c_first;
    c_first.id = "first#0";
    c_first.document_id = "first";
    c_first.document_order = 0;
    c_first.sequence = 0;
    c_first.text = "zzzsame";

    Chunk c_second;
    c_second.id = "second#0";
    c_second.document_id = "second";
    c_second.document_order = 1;
    c_second.sequence = 0;
    c_second.text = "zzzsame";

    Chunk c_irrelevant;
    c_irrelevant.id = "third#0";
    c_irrelevant.document_id = "third";
    c_irrelevant.document_order = 2;
    c_irrelevant.sequence = 0;
    c_irrelevant.text = "unrelated content only";

    std::vector<Chunk> chunks{c_first, c_second, c_irrelevant};
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    auto results = engine.search("zzzsame", 10, chunks, index);
    check(results.size() == 2, "only chunks containing a query term are returned as candidates");
    check(results[0].score == results[1].score, "identical chunks produce identical (byte-equal) scores");
    check(results[0].document_id == "first" && results[1].document_id == "second",
          "equal scores are broken by ascending source-document insertion order");

    Chunk d0;
    d0.id = "doc#0";
    d0.document_id = "doc";
    d0.document_order = 0;
    d0.sequence = 1;
    d0.text = "same";

    Chunk d1;
    d1.id = "doc#1";
    d1.document_id = "doc";
    d1.document_order = 0;
    d1.sequence = 0;
    d1.text = "same";

    std::vector<Chunk> seq_chunks{d0, d1};
    CorpusIndex seq_index(seq_chunks);
    auto seq_results = engine.search("same", 10, seq_chunks, seq_index);
    check(seq_results.size() == 2 && seq_results[0].chunk_sequence == 0 && seq_results[1].chunk_sequence == 1,
          "within one document, equal scores are broken by ascending chunk sequence");

    check(engine.search("zzzsame", 1, chunks, index).size() == 1, "search truncates to at most k results");
    check(engine.search("zzzsame", 0, chunks, index).empty(), "k == 0 returns no results");
    check(engine.search("???", 10, chunks, index).empty(), "an effectively empty query returns no results");
    check(engine.search("totallyabsentterm", 10, chunks, index).empty(),
          "a query term absent from the corpus contributes nothing and yields no candidates");

    bool threw = false;
    try {
        (void)engine.search("zzzsame", -1, chunks, index);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a negative k throws std::invalid_argument");

    Chunk both;
    both.id = "both#0";
    both.document_id = "both";
    both.document_order = 0;
    both.sequence = 0;
    both.text = "north south";

    Chunk one_only;
    one_only.id = "one#0";
    one_only.document_id = "one";
    one_only.document_order = 1;
    one_only.sequence = 0;
    one_only.text = "north north";

    std::vector<Chunk> cov_chunks{both, one_only};
    CorpusIndex cov_index(cov_chunks);
    auto cov_results = engine.search("north south", 10, cov_chunks, cov_index);
    check(cov_results.size() == 2 && cov_results[0].document_id == "both" && cov_results[0].matched_terms == 2,
          "matching both query terms outranks matching only one, via the coverage bonus");
}

void test_context_budgets() {
    using namespace aiws;
    ContextBuilder builder;

    SearchResult r1;
    r1.chunk_id = "a#0";
    r1.document_id = "a";
    r1.chunk_sequence = 0;
    r1.text = "one two three";
    r1.score = 5.0;

    SearchResult r2;
    r2.chunk_id = "b#0";
    r2.document_id = "b";
    r2.chunk_sequence = 0;
    r2.text = "four five";
    r2.score = 4.0;

    std::vector<SearchResult> ranked{r1, r2};

    check(builder.build(ranked, 0).empty(), "a zero token budget returns no context items");

    auto exact = builder.build(ranked, 5);
    check(exact.size() == 2 && !exact[0].truncated && !exact[1].truncated,
          "a budget that exactly fits every ranked chunk includes all of them untruncated");
    check(exact[0].token_count == 3 && exact[1].token_count == 2, "token counts match each chunk's normalized length");

    auto generous = builder.build(ranked, 100);
    check(generous.size() == 2, "a budget larger than the available content still includes everything, no more");

    auto mid = builder.build(ranked, 4);
    check(mid.size() == 2, "budget that ends inside the second chunk still yields an item for it, truncated");
    check(!mid[0].truncated && mid[0].token_count == 3, "the first chunk fully fits before the budget runs out");
    check(mid[1].truncated && mid[1].token_count == 1 && mid[1].text == "four",
          "the second chunk is cut to its largest fitting prefix and marked truncated");

    auto tiny = builder.build(ranked, 2);
    check(tiny.size() == 1 && tiny[0].truncated && tiny[0].token_count == 2 && tiny[0].text == "one two",
          "when even the first chunk doesn't fit, its largest prefix is taken and building stops");

    check(!mid.empty() && mid[0].document_id == "a", "context items preserve the caller-supplied ranked order");
}

void test_end_to_end_multi_document() {
    using namespace aiws;

    ProcessingCore core;
    Workspace ws;
    ws.add_document(Document{
        "notes", "Notes",
        "Retrieval systems normalize text before indexing.\n\n"
        "A good pipeline keeps document order and paragraph structure so chunk "
        "boundaries stay meaningful across rebuilds."});
    ws.add_document(Document{
        "guide", "Guide",
        "Ranking combines term frequency and inverse document frequency to score "
        "candidate chunks for a query."});
    ws.add_document(Document{"empty_doc", "Empty", "   \n\n  "});

    core.rebuild(ws);

    check(core.chunk_count() == 2, "the effectively-empty third document contributes no chunks");

    auto results = core.search("document chunk", 10);
    check(!results.empty(), "a query matching terms present in the corpus returns candidates");

    auto top_ids = std::vector<std::string>{};
    for (auto& r : results) top_ids.push_back(r.chunk_id);
    bool has_notes = false;
    for (auto& id : top_ids) {
        if (id.rfind("notes#", 0) == 0) has_notes = true;
    }
    check(has_notes, "the notes document, which mentions both query terms, appears among the results");

    auto ctx = core.build_context("document chunk", 10, 6);
    std::size_t total_tokens = 0;
    for (auto& item : ctx) total_tokens += item.token_count;
    check(total_tokens <= 6, "build_context never exceeds the requested token budget");
    check(!ctx.empty(), "a reasonable budget still yields at least one context item");

    core.rebuild(ws);
    check(core.chunk_count() == 2, "rebuilding an unchanged workspace does not change the chunk count");
}

int main() {
    test_text_processing();
    test_chunker_boundaries();
    test_corpus_index_and_rebuild();
    test_ranking_and_determinism();
    test_context_budgets();
    test_end_to_end_multi_document();

    if (g_failures == 0) {
        std::cout << "All student tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " student test(s) failed.\n";
    return 1;
}
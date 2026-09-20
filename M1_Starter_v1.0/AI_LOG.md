# AI_LOG.md

**Student Name:** Syed Subhan Hassan
**Student ID:** 906732940
**Project / Milestone:** M1 - Processing Core & Retrieval API

---

## Part 1: Summary of AI Tools Used

**Tools Used:** Claude

**Scope of Assistance:** I wrote the majority of the implementation myself. The `Document`/`Workspace` boilerplate the tokenization loop in `TextProcessor` and the basic struct wiring across `Chunk` `SearchResult` and `ContextItem` are all my own code. I used Claude in three narrower ways. First to debug a specific off-by-one issue in my chunking logic once I already had a working draft. Second to help me find a clean pattern for validating query terms in `ProcessingCore` given that `CorpusIndex`'s lookup methods are required to be `noexcept`. Third to review already-written functions like `ProcessingCore::rebuild` for correctness rather than to author them. I also used it for non-code help. This included clarifying ambiguous parts of the M1 spec proofreading `DESIGN.md` and debugging a PowerShell path issue when getting the CMake build running locally.

---

## Part 2: Detailed Log of Substantial AI Assistance

### Component: Chunker - paragraph boundary search (`chunker.cpp`)

**What did the AI do?** I had already written the core loop in `Chunker::chunk` that walks tokens and cuts chunks at `max_tokens` with a fallback to prefer paragraph boundaries inside the `[max_tokens - paragraph_window max_tokens]` window. My version was producing chunks that were consistently a token or two short of where they should have ended. I gave Claude my draft of the boundary-search loop and asked what was wrong with the indexing.

**What did the AI get wrong / What was the flaw?** My original loop compared `tokens[pos].paragraph` against `tokens[pos+1].paragraph` using `pos` as the *count* of tokens taken rather than as an index into `tokens` relative to `start`. That meant the paragraph comparison was checking the wrong pair of tokens once `start > 0`. It worked for the first chunk of a document but drifted on later chunks. The AI pointed out that I needed to express the comparison in terms of `start + pos - 1` and `start + pos`. These are the last index included in a chunk of length `pos` and the token right after it. This is instead of treating `pos` as an absolute index.

**How did you fix/modify it?** I rewrote the loop using `last_idx = start + pos - 1` and `next_idx = last_idx + 1` with the descending scan stopping at `lower_bound` if no paragraph break is found in the window. Otherwise it falls back to a hard cut at `max_tokens`. I verified this by writing my own chunker tests that check a paragraph break just inside the window is preferred a break just outside the window is ignored (hard cut instead) and that chunk boundaries land on the exact expected token indices for a multi-paragraph document. I stepped through all of this by hand against the tokenized output before trusting the test to pass.

### Component: Query-term validation split between `ProcessingCore` and `CorpusIndex` (`processing_core.cpp` `corpus_index.cpp`)

**What did the AI do?** The spec requires `CorpusIndex::document_frequency` and `term_frequency` to be `noexcept`. A query term can normalize to zero tokens (whitespace/punctuation only) or to more than one token and I wanted that to be a hard error rather than silently ignored. I asked Claude for a clean pattern to keep validation out of the `noexcept` index methods while still surfacing it as a real exception somewhere in the call chain.

**What did the AI get wrong / What was the flaw?** The first version it suggested put the multi-token check inside `CorpusIndex` behind a boolean "did this throw" out-parameter. This technically avoided `throw` inside a `noexcept` function but was awkward to call correctly and easy to misuse since nothing stopped a caller from ignoring the flag.

**How did you fix/modify it?** I moved validation entirely up into `ProcessingCore` using a small local `normalize_single_term` helper that normalizes the raw term throws `std::invalid_argument` on more than one token and reports "empty" via an output parameter that `ProcessingCore` immediately turns into a `0` return. Per spec an empty or unknown term is 0 occurrences not an error. `CorpusIndex` itself stays purely a `noexcept` lookup over already-normalized single terms. I tested this by calling `document_frequency`/`term_frequency` with an empty string a multi-word string and a valid single term and confirmed the exception only fires for the multi-token case.

### Component: `ProcessingCore::rebuild` exception safety (`processing_core.cpp`)

**What did the AI do?** After I wrote `rebuild()` (building a new chunk list and a new `CorpusIndex` in local variables checking for duplicate document IDs up front and only swapping them into `impl_->chunks`/`impl_->index` at the end) I asked Claude to review it specifically for whether a partial failure could leave the corpus in an inconsistent state since the spec requires rebuild to be all-or-nothing.

**What did the AI get wrong / What was the flaw?** No bug was found in the swap logic itself. The review flagged that my original ordering did the duplicate-ID scan after starting to build chunks in some earlier draft which meant a duplicate could theoretically be caught mid-build rather than before any work started. This wasn't a memory-safety issue but was inconsistent with the "old corpus untouched until success" guarantee I wanted to document in `DESIGN.md`.

**How did you fix/modify it?** I reordered `rebuild()` so the duplicate-ID check over `workspace.documents()` happens first entirely before any chunking or indexing work begins. Only after that succeeds do I build `new_chunks` and `new_index` locally and move them into `impl_` as the last step. I verified this with a `ProcessingCore` test that rebuilds successfully once then calls `rebuild` again with a workspace containing a duplicate ID and asserts that `chunk_count()` and a subsequent `search()` still reflect the previous successful corpus rather than an empty or partial one.

### Non-code assistance (not logged as "substantial code assistance" but disclosed for transparency)

I also used Claude to clarify a few ambiguous points in the M1 spec (mainly around how ties should be broken in `RetrievalEngine::search` when scores are equal) to proofread `DESIGN.md` for clarity and to resolve a PowerShell path/quoting issue that was preventing `cmake ..` from finding the right generator when I first tried building the starter locally. None of this involved AI writing or modifying implementation logic.

---

## Part 3: Student Verification Statement

I affirm that I have thoroughly reviewed all AI-assisted code listed above that I fully understand how it operates within the broader application architecture and that I am able to modify or debug it independently.
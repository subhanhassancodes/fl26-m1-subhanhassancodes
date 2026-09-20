# M1 DESIGN.md

## 1. System structure

TextProcessor handles normalization and tokenization, including paragraph tracking. Chunker uses that to split a Document into Chunks. CorpusIndex stores the term to chunk postings for search. RetrievalEngine scores and ranks chunks for a query using the index. ContextBuilder fits ranked results into a token budget. ProcessingCore ties all of it together and owns the live state.

Flow: Workspace -> TextProcessor -> Chunker -> CorpusIndex, and Query -> TextProcessor -> RetrievalEngine -> ContextBuilder.

## 2. Design decisions

Each Chunk stores document_order along with sequence so ties in ranking can be broken by document insertion order even after chunks from different docs get mixed together. CorpusIndex only stores postings and an id to index map, not the actual chunk data, so there's only one copy of chunk data and it lives in ProcessingCore.

Term validation (empty vs multi token) happens in ProcessingCore instead of CorpusIndex since the index lookups are noexcept and can't throw. Rebuild builds the new chunk list and index in local variables first and only swaps them in if nothing fails, so a bad rebuild can't leave things half updated. Scores are rounded to 12 decimals right when they're computed so sorting and comparisons always use the same rounded value.

## 3. Correctness and consistency

Normalization always goes through TextProcessor so documents and queries stay consistent. Rebuild is all or nothing, it either fully replaces the corpus or throws and leaves the old one untouched. Ranking ties are broken with document_order then sequence pulled straight from the Chunk struct since map ordering isn't guaranteed. Chunking always advances by at least one token so it can't loop forever, and the paragraph boundary search only looks in the 100-120 window like the spec says.

## 4. Testing strategy

Tests cover the areas from the spec and several test components directly instead of only going through ProcessingCore. Text processing tests cover normalization and paragraph detection for LF/CRLF. Chunker tests check the paragraph boundary preference inside vs outside the window, chunk ids and sequencing, empty docs, and that reprocessing is deterministic. CorpusIndex tests hit the index directly for frequency lookups and rebuild behavior, plus a ProcessingCore test for the duplicate id failure case. RetrievalEngine tests cover tie breaking, k edge cases, empty/unknown query terms, and the coverage bonus. ContextBuilder tests cover budget edge cases like zero budget and truncating mid chunk. Last one is a full multi doc end to end test through ProcessingCore.

## 5. Alternatives considered

Considered having CorpusIndex store full Chunk copies instead of just indexing an external vector, but that duplicates data and risks it getting out of sync, so keeping chunks owned in one place by ProcessingCore was better.

Also considered updating the index incrementally on rebuild instead of rebuilding from scratch, which would be faster but easier to mess up by leaving stale postings behind. Since the spec cares more about rebuild correctness than performance here, a full rebuild each time was the safer call.
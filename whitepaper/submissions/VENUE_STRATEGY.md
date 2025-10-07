# ZeroIPC Publication Strategy

## Primary Publication Venues

### Tier 1: Top Systems Conferences (Main Target)

#### OSDI 2025 (Operating Systems Design and Implementation)
- **Deadline**: April 30, 2025 (Abstract), May 7, 2025 (Full)
- **Page Limit**: 12 pages (+ references)
- **Focus**: Novel OS/systems abstractions, implementation excellence
- **Why Strong Fit**: Codata in shared memory is a fundamental systems contribution
- **Submission Type**: Full paper

#### SOSP 2025 (Symposium on Operating Systems Principles)
- **Deadline**: April 15, 2025 (expected)
- **Page Limit**: 14 pages (+ references)
- **Focus**: Fundamental OS principles, theoretical + practical
- **Why Strong Fit**: Bridge between theory (codata) and systems
- **Submission Type**: Full paper

#### EuroSys 2025
- **Deadline**: October 15, 2024 (Fall cycle) / May 2025 (Spring)
- **Page Limit**: 14 pages (including references)
- **Focus**: European systems conference, practical systems
- **Why Strong Fit**: Strong implementation with real-world impact
- **Submission Type**: Full paper

#### ASPLOS 2025 (Architectural Support for Programming Languages and OS)
- **Deadline**: August 2, 2024 (Summer) / March 28, 2025 (Spring)
- **Page Limit**: 11 pages (+ references)
- **Focus**: Cross-layer innovations (architecture/OS/languages)
- **Why Strong Fit**: Cross-language support, lock-free architecture
- **Submission Type**: Full paper

### Tier 2: Parallel/Concurrent Systems

#### PPoPP 2025 (Principles and Practice of Parallel Programming)
- **Deadline**: August 9, 2024 / February 2025
- **Page Limit**: 10 pages (+ references)
- **Focus**: Parallel programming abstractions
- **Why Strong Fit**: Lock-free codata, parallel functional patterns
- **Submission Type**: Full paper

#### ICPP 2025 (International Conference on Parallel Processing)
- **Deadline**: April 2025
- **Page Limit**: 10 pages
- **Focus**: Parallel and distributed computing
- **Why Strong Fit**: IPC for parallel applications
- **Submission Type**: Full paper

### Tier 3: Software Engineering

#### ICSE 2025 (International Conference on Software Engineering)
- **Deadline**: March/August cycles
- **Page Limit**: 10 pages (+ 2 references)
- **Focus**: Software engineering innovations
- **Why Strong Fit**: Cross-language interoperability, API design
- **Submission Type**: Technical paper

#### FSE 2025 (Foundations of Software Engineering)
- **Deadline**: March/September cycles
- **Page Limit**: 10 pages (+ references)
- **Focus**: Software development tools and techniques
- **Why Strong Fit**: Developer tools, testing infrastructure
- **Submission Type**: Research paper

### Tier 4: Middleware/Distributed Systems

#### Middleware 2025
- **Deadline**: May 2025
- **Page Limit**: 12 pages
- **Focus**: Middleware for distributed systems
- **Why Strong Fit**: IPC middleware layer
- **Submission Type**: Full paper

#### ICDCS 2025 (International Conference on Distributed Computing Systems)
- **Deadline**: January 2025
- **Page Limit**: 10 pages
- **Focus**: Distributed systems and applications
- **Why Strong Fit**: Distributed codata abstractions
- **Submission Type**: Full paper

## Short Paper/Workshop Venues

#### HotOS 2025 (Workshop on Hot Topics in Operating Systems)
- **Deadline**: March 2025
- **Page Limit**: 5 pages
- **Focus**: Provocative OS ideas
- **Submission Type**: Position paper on codata for systems

#### PLOS 2024 (Workshop on Programming Languages and Operating Systems)
- **Deadline**: July 2024
- **Page Limit**: 6 pages
- **Focus**: Language/OS interaction
- **Submission Type**: Workshop paper

## Journal Publications

#### ACM TOCS (Transactions on Computer Systems)
- **Page Limit**: No strict limit (typically 25-35 pages)
- **Focus**: Comprehensive systems contributions
- **Timeline**: 6-12 month review cycle

#### IEEE TPDS (Transactions on Parallel and Distributed Systems)
- **Page Limit**: 14 pages (can extend)
- **Focus**: Parallel and distributed computing
- **Timeline**: 4-8 month review cycle

## Software/Artifact Venues

#### JOSS (Journal of Open Source Software)
- **Page Limit**: 1-2 page summary + repository
- **Focus**: Quality open source software
- **Timeline**: 2-4 weeks

#### SoftwareX
- **Page Limit**: 4-6 pages
- **Focus**: Software with research impact
- **Timeline**: 6-8 weeks

## Preprint and Repository Strategy

#### arXiv
- **Category**: cs.OS (Operating Systems) or cs.DC (Distributed Computing)
- **When**: Immediately after first conference submission

#### GitHub Release
- **Version**: v1.0.0
- **Assets**: Source tarball, documentation, benchmarks
- **Citation**: CITATION.cff file

#### PyPI (Python Package Index)
- **Package**: zeroipc
- **Version**: 1.0.0

## Submission Order Strategy

1. **Immediate** (Within 1 week):
   - arXiv preprint
   - JOSS submission
   - GitHub release v1.0.0

2. **Short-term** (1-2 months):
   - OSDI 2025 (main target)
   - PPoPP 2025 (parallel focus)

3. **Medium-term** (3-4 months):
   - SOSP 2025 (if OSDI rejected)
   - EuroSys 2025 (European visibility)

4. **Long-term** (6+ months):
   - Journal version (TOCS/TPDS)
   - Workshop papers for specific aspects

## Key Differentiators by Venue

- **Systems (OSDI/SOSP)**: Emphasize lock-free implementation, performance
- **Parallel (PPoPP/ICPP)**: Focus on parallel patterns, scalability
- **Software (ICSE/FSE)**: Highlight cross-language design, testing
- **Theory**: Stress formal codata semantics, correctness proofs
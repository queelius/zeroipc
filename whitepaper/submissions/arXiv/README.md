# arXiv Submission Instructions

## Paper Information

- **Title**: ZeroIPC: Bringing Codata to Shared Memory - A Novel Approach to Lock-Free Inter-Process Communication
- **Categories**: cs.OS (Operating Systems) as primary, cs.DC (Distributed, Parallel, and Cluster Computing) as secondary
- **MSC Classification**: 68M14 (Distributed systems)

## Submission Process

1. **Create arXiv Account**
   - Go to https://arxiv.org/submit
   - Register if you don't have an account
   - Request endorsement for cs.OS if needed

2. **Prepare Files**
   ```bash
   # Create submission tarball
   tar czf zeroipc_arxiv.tar.gz \
     zeroipc_arxiv.tex \
     references.bib \
     figures/
   ```

3. **Upload Process**
   - Start new submission
   - Upload source files (tar.gz)
   - Select categories: cs.OS (primary), cs.DC (secondary)
   - Add metadata:
     - Title (as above)
     - Authors (to be added after double-blind review)
     - Abstract (from paper)
     - Comments: "47 pages, 8 figures, 6 tables"

4. **Abstract for arXiv**
   Use the abstract from the paper - it's already formatted correctly.

5. **License Selection**
   Recommended: CC BY 4.0 (Creative Commons Attribution)

## Files in This Directory

- `zeroipc_arxiv.tex` - Main paper formatted for arXiv
- `references.bib` - Bibliography
- `arxiv.sty` - arXiv style file (download from arxiv.org if needed)
- `figures/` - Directory for figures (create PDF versions)

## Timing Strategy

1. **Initial Submission**: After first conference submission (OSDI/SOSP)
2. **Updates**: After incorporating reviewer feedback
3. **Final Version**: After conference acceptance

## Required Figures

Create these figures and save as PDF in `figures/`:

1. `architecture.pdf` - System architecture diagram
2. `scaling.pdf` - Performance scaling graph
3. `comparison.pdf` - Comparison with other systems

## Compilation

```bash
pdflatex zeroipc_arxiv.tex
bibtex zeroipc_arxiv
pdflatex zeroipc_arxiv.tex
pdflatex zeroipc_arxiv.tex
```

## Checklist

- [ ] Account created and endorsed
- [ ] All coauthors agree to submission
- [ ] Figures prepared as PDF
- [ ] Bibliography complete
- [ ] Compiles without errors
- [ ] Under 50 pages total
- [ ] License selected
- [ ] Categories chosen correctly

## Notes

- arXiv is not anonymous - add real author information
- Can update paper after initial submission
- Consider submitting after conference reviews for improvements
- Link to GitHub repo in the paper for code availability
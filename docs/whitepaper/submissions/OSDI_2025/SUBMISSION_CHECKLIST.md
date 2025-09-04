# OSDI 2025 Submission Checklist

## Important Dates
- **Abstract Registration**: April 30, 2025, 3:00 pm PDT
- **Full Paper Submission**: May 7, 2025, 3:00 pm PDT
- **Author Response Period**: July 21-23, 2025
- **Notification**: August 13, 2025
- **Camera-Ready**: October 1, 2025

## Submission URL
https://osdi25.usenix.org/submitting

## Pre-Submission Requirements

### Account Setup
- [ ] Create HotCRP account at submission site
- [ ] Add all co-authors with correct email addresses
- [ ] Verify all authors can access the submission

### Anonymization (Double-Blind Review)
- [ ] Remove all author names and affiliations from paper
- [ ] Replace self-citations with "Anonymous" or third-person
- [ ] Remove acknowledgments section
- [ ] Check PDF metadata is anonymized
- [ ] Remove institutional logos/watermarks
- [ ] Anonymize GitHub URLs (use anonymous.4open.science)

## Paper Formatting

### Length Requirements
- [ ] Main paper: Maximum 12 pages (excluding references)
- [ ] References: No page limit (typically 1-2 pages)
- [ ] Total should not exceed 14 pages

### Style Compliance
- [ ] Using usenix2019_v3.sty template
- [ ] Letter paper size (8.5" x 11")
- [ ] Two-column format
- [ ] 10pt font size
- [ ] 1-inch margins on all sides
- [ ] Sections numbered correctly
- [ ] Figures and tables properly captioned

### Content Requirements
- [ ] Title clearly describes the contribution
- [ ] Abstract (150-200 words) summarizes key contributions
- [ ] Introduction motivates the problem
- [ ] Related work positions contribution
- [ ] Design section explains architecture
- [ ] Implementation details provided
- [ ] Evaluation is comprehensive
- [ ] Limitations discussed honestly

## Technical Validation

### Compilation
- [ ] LaTeX compiles without errors: `pdflatex main.tex`
- [ ] Bibliography compiles: `bibtex main`
- [ ] All references cited in text
- [ ] All citations have bibliography entries
- [ ] No undefined references or labels

### Figures and Tables
- [ ] All figures are vector graphics (PDF/EPS) where possible
- [ ] Raster images are high resolution (300+ DPI)
- [ ] Figures are colorblind-friendly
- [ ] Tables use booktabs package
- [ ] All figures/tables referenced in text

## Supplementary Material

### Artifact Package
- [ ] Source code tarball prepared
- [ ] Build instructions included
- [ ] README with reproduction steps
- [ ] Sample inputs/outputs provided
- [ ] Docker container (optional)

### Submission Items
- [ ] Appendices (if needed) in separate PDF
- [ ] Artifact evaluation package
- [ ] Video demo (optional, max 3 minutes)

## Final Checks

### PDF Validation
- [ ] PDF opens correctly in Adobe Reader
- [ ] File size < 20MB
- [ ] All fonts embedded
- [ ] No broken hyperlinks
- [ ] Page numbers present

### Content Review
- [ ] Spell check completed
- [ ] Grammar check performed
- [ ] Terminology consistent throughout
- [ ] Acronyms defined on first use
- [ ] Claims supported by evidence

### Conflict of Interest
- [ ] List all PC members with conflicts
- [ ] Include advisors, recent collaborators
- [ ] Same institution = conflict
- [ ] Family members = conflict

## Submission Process

1. **Before April 30, 2025, 3:00 pm PDT**:
   - [ ] Register paper with title and abstract
   - [ ] Add all authors
   - [ ] Select topics/keywords

2. **Before May 7, 2025, 3:00 pm PDT**:
   - [ ] Upload final PDF
   - [ ] Upload supplementary materials
   - [ ] Enter conflicts of interest
   - [ ] Submit paper

3. **After Submission**:
   - [ ] Verify submission receipt email
   - [ ] Check PDF uploaded correctly
   - [ ] Save submission number

## Response Phase (July 21-23)

### Preparation
- [ ] Read all reviews carefully
- [ ] Identify key concerns to address
- [ ] Prepare point-by-point response
- [ ] Stay within word limit (typically 500 words)
- [ ] Be respectful and constructive

## If Accepted

### Camera-Ready Preparation
- [ ] De-anonymize paper
- [ ] Add acknowledgments
- [ ] Address reviewer feedback
- [ ] Shepherd approval (if assigned)
- [ ] Copyright form submission
- [ ] Author registration for conference

## Notes

- OSDI values: **strong systems contributions**, **solid implementation**, **comprehensive evaluation**
- Emphasize: **performance at scale**, **novel abstractions**, **practical impact**
- Common rejection reasons: incremental contribution, weak evaluation, unclear writing
- Tips: Start submission early, test on multiple PDF viewers, have colleagues review

## Contact

Technical issues: osdi25chairs@usenix.org

## Checklist Complete

- [ ] All items above checked
- [ ] Paper ready for submission
- [ ] Backup submission plan prepared
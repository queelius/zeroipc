# SOSP 2025 Submission Checklist

## Important Dates
- **Paper Registration**: April 15, 2025, 11:59 PM EDT
- **Full Paper Submission**: April 22, 2025, 11:59 PM EDT  
- **Author Response Period**: July 7-9, 2025
- **Notification**: August 5, 2025
- **Camera-Ready**: September 15, 2025
- **Conference**: November 3-5, 2025 (Austin, TX)

## Submission URL
https://sosp25.usenix.org/cfp

## Requirements

### Paper Format
- [ ] ACM format using `acmart` class with `sigconf` option
- [ ] Anonymous submission (`anonymous` option in documentclass)
- [ ] Maximum 14 pages including everything except references
- [ ] References have no page limit
- [ ] Double-column format
- [ ] 10pt font minimum

### Anonymization 
- [ ] Remove all author names and affiliations
- [ ] Use "Anonymous Submission" as author
- [ ] Replace self-citations with anonymous references
- [ ] Remove acknowledgments
- [ ] Check PDF metadata is clean
- [ ] Anonymize artifact URLs (use anonymous.4open.science)

### Content Requirements
- [ ] Clear statement of contributions in intro
- [ ] Thorough related work section
- [ ] Complete system design description
- [ ] Implementation details
- [ ] Comprehensive evaluation
- [ ] Discussion of limitations
- [ ] Artifact availability statement

## Submission Process

### Before April 15, 2025
- [ ] Create HotCRP account
- [ ] Register paper with title and abstract
- [ ] Add topics/keywords
- [ ] Specify conflicts of interest

### Before April 22, 2025
- [ ] Upload final PDF
- [ ] Upload supplementary material (optional)
- [ ] Confirm all metadata correct
- [ ] Submit paper

### Conflicts of Interest
- [ ] List all PC members from same institution
- [ ] Include PhD/postdoc advisors
- [ ] Recent collaborators (last 2 years)
- [ ] Family members on PC

## Technical Checklist

### LaTeX Compilation
- [ ] `pdflatex main.tex` runs without errors
- [ ] `bibtex main` processes references
- [ ] All citations resolved
- [ ] No overfull hboxes
- [ ] Figures in PDF/EPS format

### Artifact Package (Optional but Recommended)
- [ ] Source code repository
- [ ] README with build instructions
- [ ] Scripts to reproduce key results
- [ ] Docker container (if complex dependencies)
- [ ] Data sets (or instructions to obtain)

## SOSP Specific Focus

SOSP values:
- **Fundamental OS principles and theory**
- **Novel abstractions and interfaces**
- **Strong implementation and evaluation**
- **Broad impact on systems community**

Key review criteria:
- Technical depth and correctness
- Novelty and significance
- Clarity of presentation
- Thorough evaluation
- Honest discussion of limitations

## Response Phase Preparation

During author response (July 7-9):
- [ ] Address major reviewer concerns
- [ ] Clarify misunderstandings
- [ ] Provide additional data if requested
- [ ] Stay within word limit (typically 750 words)
- [ ] Be respectful and factual

## Final Checks

- [ ] Paper tells a clear story
- [ ] Abstract summarizes contributions
- [ ] Introduction motivates the work
- [ ] Evaluation supports claims
- [ ] Writing is clear and concise
- [ ] All co-authors have reviewed
- [ ] Backup submission prepared

## Notes

- SOSP is highly competitive (~15-20% acceptance rate)
- Focus on fundamental contributions to OS/systems
- Strong preference for complete systems with evaluation
- Artifact evaluation increasingly important
- Consider HotOS for preliminary ideas
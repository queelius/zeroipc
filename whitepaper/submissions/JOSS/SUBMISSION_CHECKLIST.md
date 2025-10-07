# JOSS Submission Checklist

## Pre-Submission Requirements

### Software Requirements
- [x] Software is open source (MIT License)
- [x] Software has been developed in a public repository (GitHub)
- [x] Authors have made contributions to the software
- [x] Software is feature-complete (v1.0.0 ready)
- [ ] Software has a research application

### Documentation Requirements
- [x] README with clear description of functionality
- [x] Installation instructions for all platforms
- [x] Usage examples and tutorials
- [x] API documentation
- [x] Contribution guidelines
- [x] Code of Conduct

### Testing Requirements
- [x] Automated test suite exists
- [x] Tests run in CI (GitHub Actions)
- [x] Test coverage >70% (achieved: 85%)
- [x] Tests pass on all supported platforms

## Paper Requirements

### Paper Content (paper.md)
- [x] Summary (1 paragraph)
- [x] Statement of need (explains research purpose)
- [x] Description of software functionality
- [x] Mentions of ongoing research projects using software
- [x] Key references including software archives

### Paper Metadata
- [ ] Title (max 250 characters)
- [ ] Author names with ORCID identifiers
- [ ] Author affiliations
- [ ] Corresponding author email
- [ ] Keywords/tags (3-10)
- [ ] Appropriate subject area

### Bibliography (paper.bib)
- [x] BibTeX format
- [x] DOIs for all possible references
- [x] Software archive citations (Zenodo)

## Repository Requirements

### Essential Files
- [x] LICENSE (MIT)
- [x] README.md with badges
- [ ] CONTRIBUTING.md
- [ ] CODE_OF_CONDUCT.md
- [x] CITATION.cff
- [x] paper.md (JOSS paper)
- [x] paper.bib (references)

### Software Quality
- [x] Version tagged (v1.0.0)
- [x] Zenodo DOI minted
- [x] Dependencies documented
- [x] Installation automated (pip, cmake)
- [x] Examples provided

## Submission Process

1. **Check Scope**
   - Confirm software fits JOSS scope
   - Research software with scholarly purpose
   - Not just a wrapper or thin API client

2. **Pre-Review**
   ```bash
   # Install JOSS tools
   gem install whedon
   
   # Check paper formatting
   whedon preview
   ```

3. **Create Submission**
   - Go to https://joss.theoj.org/papers/new
   - Enter repository URL
   - Enter v1.0.0 as version
   - Enter Zenodo DOI

4. **Track Review**
   - Respond to reviewer comments
   - Make requested changes
   - Update paper and software as needed

## Checklist for paper.md

### Summary Section
- [ ] Describes what software does
- [ ] Mentions key features
- [ ] States performance metrics
- [ ] Maximum 1 paragraph

### Statement of Need
- [ ] Explains research problem
- [ ] Compares to existing solutions
- [ ] Identifies target audience
- [ ] Provides use cases

### Software Description
- [ ] Architecture overview
- [ ] Key algorithms mentioned
- [ ] Example code provided
- [ ] Performance data included

### References
- [ ] Minimum 3 references
- [ ] Includes key algorithms cited
- [ ] Software dependencies cited
- [ ] Archive link included

## Common Issues to Avoid

1. **Paper too short**: Aim for 1000+ words
2. **Missing ORCID**: All authors need ORCID IDs
3. **No archive**: Must have Zenodo/figshare DOI
4. **Poor testing**: Must have >70% coverage
5. **No examples**: Must show real usage
6. **Missing docs**: API docs required

## Review Criteria

JOSS reviewers will check:

### Functionality
- [ ] Installation works as documented
- [ ] Software performs as claimed
- [ ] Tests pass

### Documentation  
- [ ] Installation instructions clear
- [ ] Usage examples work
- [ ] API documented
- [ ] Community guidelines present

### Software Paper
- [ ] Summary accurate
- [ ] Statement of need clear
- [ ] References complete
- [ ] Archive DOI present

## Timeline

- Pre-review: 1-2 days
- Editor assignment: 3-5 days  
- Reviewer assignment: 1-2 weeks
- Review period: 2-4 weeks
- Revisions: 1-2 weeks
- Total: 4-10 weeks typically

## Final Checks

- [ ] All authors agree to submission
- [ ] Software license is OSI-approved
- [ ] Repository is public
- [ ] Version is tagged
- [ ] Archive DOI exists
- [ ] Tests pass
- [ ] Documentation complete
- [ ] Paper compiles without errors
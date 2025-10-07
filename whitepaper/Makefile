# Makefile for building ZeroIPC whitepaper

LATEX = pdflatex
BIBTEX = bibtex
MAIN = zeroipc_whitepaper

.PHONY: all clean view

all: $(MAIN).pdf

$(MAIN).pdf: $(MAIN).tex
	$(LATEX) $(MAIN)
	$(LATEX) $(MAIN)
	$(LATEX) $(MAIN)

clean:
	rm -f $(MAIN).aux $(MAIN).log $(MAIN).out $(MAIN).pdf $(MAIN).bbl $(MAIN).blg

view: $(MAIN).pdf
	xdg-open $(MAIN).pdf 2>/dev/null || open $(MAIN).pdf 2>/dev/null || echo "Please open $(MAIN).pdf manually"

# For quick compilation without bibliography processing
quick: $(MAIN).tex
	$(LATEX) $(MAIN)

help:
	@echo "Available targets:"
	@echo "  all   - Build the complete PDF (default)"
	@echo "  clean - Remove all generated files"
	@echo "  view  - Build and open the PDF"
	@echo "  quick - Quick compilation (single pass)"
	@echo "  help  - Show this help message"
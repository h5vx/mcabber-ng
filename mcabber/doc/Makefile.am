man_MANS = mcabber-ng.1

EXTRA_DIST = $(man_MANS) mcabber-ng.1.txt mcabber-ng.1.html

SUBDIRS = help

# The manual page and its HTML version are generated from the AsciiDoc
# source and kept in the repository: "make doc" regenerates them (needs
# asciidoc, xsltproc and docbook-xsl).
doc: $(srcdir)/mcabber-ng.1.txt
	a2x -f manpage -D $(srcdir) $(srcdir)/mcabber-ng.1.txt
	asciidoc -b html5 -o $(srcdir)/mcabber-ng.1.html $(srcdir)/mcabber-ng.1.txt

.PHONY: doc

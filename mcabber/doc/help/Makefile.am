helpdir = $(datadir)/mcabber-ng/help

install-data-local:
	$(INSTALL) -d $(DESTDIR)$(helpdir)
	for lang in en cs de fr it nl pl ru uk; do \
		$(INSTALL) -d $(DESTDIR)$(helpdir)/$$lang; \
		$(INSTALL) -m 0644 $(srcdir)/$$lang/* \
		           $(DESTDIR)$(helpdir)/$$lang; \
	done

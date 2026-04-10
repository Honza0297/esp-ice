VERSION := 1.3.2
URL := https://zlib.net/zlib-$(VERSION).tar.gz
TARBALL := zlib-$(VERSION).tar.gz
SRC := zlib-$(VERSION)

all: $(PREFIX)/lib/libz.a

$(TARBALL):
	curl -fsSL -o $@ $(URL)

$(PREFIX)/lib/libz.a: $(TARBALL)
	@echo "Building zlib $(VERSION)..."
	@tar xf $(TARBALL)
	@cd $(SRC) && \
		CC="$(CC)" ./configure --prefix=$(PREFIX) --static && \
		$(MAKE) -j$(NPROC) && \
		$(MAKE) install
	@echo "zlib done."

clean:
	rm -rf $(SRC)

.PHONY: all clean

VERSION := 5.4.7
URL := https://github.com/tukaani-project/xz/releases/download/v$(VERSION)/xz-$(VERSION).tar.gz
SHA256 := 8db6664c48ca07908b92baedcfe7f3ba23f49ef2476864518ab5db6723836e71
TARBALL := xz-$(VERSION).tar.gz
SRC := xz-$(VERSION)

all: $(PREFIX)/lib/liblzma.a

$(TARBALL):
	curl -fsSL -o $@ $(URL)
	echo "$(SHA256)  $@" | sha256sum -c -

# Decoder-only static liblzma: skip the CLI tools, encoders, and
# everything we don't need.  Pinned to the 5.4.x branch (the last
# pre-5.6 line, untouched by CVE-2024-3094).
$(PREFIX)/lib/liblzma.a: $(TARBALL)
	@echo "Building xz-utils $(VERSION) (liblzma decoder)..."
	@rm -rf $(SRC)
	@tar xf $(TARBALL)
	@cd $(SRC) && \
		CC="$(CC)" ./configure --prefix=$(PREFIX) $(CONFIGURE_HOST) \
			--disable-shared --enable-static \
			--disable-xz --disable-xzdec --disable-lzmadec \
			--disable-lzmainfo --disable-scripts --disable-doc \
			--disable-nls --disable-encoders \
			--enable-decoders=lzma1,lzma2 \
			--enable-checks=crc32,crc64 && \
		$(MAKE) -j$(NPROC) && \
		$(MAKE) install
	@echo "xz-utils done."

clean:
	rm -rf $(SRC)

.PHONY: all clean

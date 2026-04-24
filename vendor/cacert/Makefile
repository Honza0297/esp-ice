# vendor/cacert is a *data* import: cacert.pem holds Mozilla NSS root
# CAs (extracted by curl.se), and ca_bundle.c is the same payload as a
# C byte array compiled into libice.a.  Both files are committed; this
# Makefile only owns the re-sync recipe and is not invoked from the
# parent build.

URL := https://curl.se/ca/cacert.pem

all:
	@echo "vendor/cacert: nothing to build (ca_bundle.c is committed);"
	@echo "                run 'make update' to refresh the bundle."

update: cacert.pem ca_bundle.c

cacert.pem:
	curl -fsSL -o $@ $(URL)

ca_bundle.c: cacert.pem
	@{ \
		printf '/* Auto-generated from cacert.pem; do not edit by hand. */\n' ; \
		printf '/* Run `make -C vendor/cacert update` to refresh. */\n\n' ; \
		printf '#include <stddef.h>\n\n' ; \
		printf 'const unsigned char ca_bundle_pem[] = {\n' ; \
		od -An -w16 -vtx1 cacert.pem | sed -e 's/^ //; s/ /, 0x/g; s/^/\t0x/; s/$$/,/' ; \
		printf '};\n' ; \
		printf 'const size_t ca_bundle_pem_len = sizeof(ca_bundle_pem);\n' ; \
	} > $@

# Force-refresh: re-fetch the PEM and regenerate the byte array.  Use
# this from the project root via `make update-ca`.
refresh:
	rm -f cacert.pem ca_bundle.c
	$(MAKE) update

.PHONY: all update refresh

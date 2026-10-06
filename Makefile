# Runs inside the build image (docker/Dockerfile); see DEVELOPMENT.md.
NATIVE_TOOL := .deps/native-app/build/host/ps5-native-tool
SDK := .deps/native-app/.deps/native/ps5-payload-sdk

.PHONY: package release ps5 deps runtime host launcher test

package: ps5 launcher
	bash tools/package.sh

# dist/PPSA99096.zip, the title folder at its top, and its .sha256, for a GitHub release.
release: package
	rm -f dist/PPSA99096.zip dist/PPSA99096.zip.sha256
	cd dist && python3 -m zipfile -c PPSA99096.zip PPSA99096 && sha256sum PPSA99096.zip > PPSA99096.zip.sha256

ps5: deps vanilla-conquer/CMakeLists.txt
	bash tools/build-ps5.sh

deps: runtime
	bash tools/build-deps.sh

runtime: $(NATIVE_TOOL)
	bash tools/build-runtime.sh

host: vanilla-conquer/CMakeLists.txt
	bash tools/build-host.sh

# The importer's tests, and the launcher, on this PC (needs the sources from deps).
test: deps
	bash tools/test-import.sh

launcher: build/launcher/ralaunch.elf

build/launcher/ralaunch.elf: tools/launcher/launch.c $(NATIVE_TOOL)
	@mkdir -p $(dir $@)
	$(SDK)/bin/prospero-clang -O2 -Wall -o $@ $< -lSceSystemService -lSceUserService

$(NATIVE_TOOL):
	bash tools/fetch-native-tools.sh

vanilla-conquer/CMakeLists.txt:
	bash tools/fetch-vanilla-conquer.sh

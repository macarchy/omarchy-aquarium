PREFIX ?= $(HOME)/.local
BIN    ?= $(PREFIX)/bin

PKGS   := wayland-client wayland-egl egl glesv2
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CFLAGS += $(shell pkg-config --cflags $(PKGS)) -Ibuild
LDLIBS := $(shell pkg-config --libs $(PKGS)) -lm

WL_PROTO_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_SHELL_XML := $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml

GEN := build/wlr-layer-shell-unstable-v1-client-protocol.h \
       build/wlr-layer-shell-unstable-v1-protocol.c \
       build/xdg-shell-protocol.c \
       build/shader_frag.h

OBJ := build/main.o build/palette.o build/hypr.o build/seeds.o build/anim.o build/react.o \
       build/wlr-layer-shell-unstable-v1-protocol.o \
       build/xdg-shell-protocol.o

all: build/omarchy-aquarium

preview: build/aquarium-preview

build:
	@mkdir -p build

build/%-client-protocol.h: protocol/%.xml | build
	wayland-scanner client-header $< $@

build/%-protocol.c: protocol/%.xml | build
	wayland-scanner private-code $< $@

# Specialise the shader (unroll entity loops, inline seed literals), then
# embed it as a C string so the binary is self-contained.
build/aquarium.gen.frag: src/aquarium.frag tools/gen_shader.py | build
	python3 tools/gen_shader.py $< $@

build/shader_frag.h: build/aquarium.gen.frag | build
	@printf 'static const char *AQUARIUM_FRAG =\n' > $@
	@sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' $< >> $@
	@printf ';\n' >> $@

build/main.o: src/main.c $(GEN) src/palette.h src/hypr.h src/seeds.h src/anim.h src/react.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/palette.o: src/palette.c src/palette.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# No FP contraction: these values must round exactly like the shader
# compiler's own constant folder, one operation at a time.
build/seeds.o: src/seeds.c src/seeds.h | build
	$(CC) $(CFLAGS) -ffp-contract=off -c $< -o $@

build/anim.o: src/anim.c src/anim.h src/seeds.h | build
	$(CC) $(CFLAGS) -ffp-contract=off -c $< -o $@

build/react.o: src/react.c src/react.h src/anim.h src/seeds.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/hypr.o: src/hypr.c src/hypr.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/preview.o: src/preview.c $(GEN) src/palette.h src/seeds.h src/anim.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/aquarium-preview: build/preview.o build/palette.o build/seeds.o build/anim.o
	$(CC) $^ -o $@ $(shell pkg-config --libs egl glesv2) -lm

# wlr-layer-shell references xdg_popup, so its interface table must be linked in.
build/xdg-shell-protocol.c: $(XDG_SHELL_XML) | build
	wayland-scanner private-code $< $@

build/%-protocol.o: build/%-protocol.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/omarchy-aquarium: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

# Regenerate the demo clip in docs/media straight from the offscreen renderer,
# so what the README shows can never drift from the shader it claims to show.
# 200 frames at t = 40 .. 48 s, in the default palette: what a stranger with
# no theme file installed will actually see.
# Needs ffmpeg.
DEMO_FRAMES := build/demo-frames

demo: preview
	@command -v ffmpeg >/dev/null || { echo "make demo: ffmpeg is required" >&2; exit 1; }
	@mkdir -p docs/media
	@rm -rf $(DEMO_FRAMES) && mkdir -p $(DEMO_FRAMES)
	@echo "rendering 200 frames into $(DEMO_FRAMES)"
	@for i in $$(seq 0 199); do \
		t=$$(awk "BEGIN{printf \"%.4f\", 40 + $$i/25.0}"); \
		./build/aquarium-preview --width 1280 --height 800 --time $$t \
			--out $(DEMO_FRAMES)/f$$(printf %04d $$i).ppm || exit 1; \
	done
	ffmpeg -y -framerate 25 -i $(DEMO_FRAMES)/f%04d.ppm -c:v libx264 -pix_fmt yuv420p \
		-crf 20 -movflags +faststart docs/media/aquarium.mp4
	ffmpeg -y -framerate 25 -i $(DEMO_FRAMES)/f%04d.ppm -t 6 \
		-vf "fps=16,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" \
		-loop 0 docs/media/aquarium.gif
	@rm -rf $(DEMO_FRAMES)

install: all
	install -Dm755 build/omarchy-aquarium $(BIN)/omarchy-aquarium
	install -Dm755 bin/omarchy-aquarium-toggle $(BIN)/omarchy-aquarium-toggle
	install -Dm755 bin/omarchy-aquarium-notify $(BIN)/omarchy-aquarium-notify

uninstall:
	rm -f $(BIN)/omarchy-aquarium $(BIN)/omarchy-aquarium-toggle \
	      $(BIN)/omarchy-aquarium-notify

clean:
	rm -rf build

.PHONY: all preview demo install uninstall clean

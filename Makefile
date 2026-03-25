# ─────────────────────────────────────────────
#  Maze — Emscripten Build
#  Requires: emcc (Emscripten SDK)
#  Install:  brew install emscripten
#            or https://emscripten.org/docs/getting_started/downloads.html
# ─────────────────────────────────────────────

CXX      = emcc
SRC      = maze.cpp
OUT_JS   = maze.js
OUT_WASM = maze.wasm

EXPORTED_FUNCTIONS = \
  _generate,\
  _solve_bfs,\
  _get_steps_ptr,\
  _get_steps_size,\
  _get_cells_ptr,\
  _get_width,\
  _get_height

CFLAGS = \
  -O2 \
  -std=c++17 \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="MazeModule" \
  -s EXPORTED_FUNCTIONS='[$(EXPORTED_FUNCTIONS)]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP32"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT='web'

all: $(OUT_JS)

$(OUT_JS): $(SRC)
	$(CXX) $(SRC) -o $(OUT_JS) $(CFLAGS)
	@echo "✅  Built $(OUT_JS) + $(OUT_WASM)"

clean:
	rm -f $(OUT_JS) $(OUT_WASM)

# Quick sanity check — compiles a hello-world to confirm emcc works
check:
	echo '#include <stdio.h>\nint main(){printf("emcc ok\\n");}' | $(CXX) -x c - -o /tmp/emcc_test.js
	@echo "✅  emcc is working"
# Sprite .cpp → scaled PNG (pixel-art, each pixel = SCALE×SCALE)
# Requires: tools/cpp_to_png.py (and Python + PIL)

PY     ?= python3
SCRIPT := tools/cpp_to_png.py
SCALE  ?= 8
SPRITES_DIR := src/sprites

# Single file: make sprite-png FILE=src/sprites/grot_0.cpp
# Or: make sprite-png FILE=src/sprites/egg_shake_1.cpp SCALE=16
sprite-png:
	@test -n "$(FILE)" || (echo "Usage: make sprite-png FILE=src/sprites/<name>.cpp [SCALE=8]"; exit 1)
	@test -f "$(SCRIPT)" || (echo "Missing $(SCRIPT)"; exit 1)
	$(PY) $(SCRIPT) "$(FILE)" $(SCALE)

# All .cpp sprite files in src/sprites (exclude grot_frames.cpp)
SPRITE_CPPS := $(filter-out %/grot_frames.cpp, $(wildcard $(SPRITES_DIR)/*.cpp))
# Batch: make sprites-png [SCALE=8]
sprites-png:
	@test -f "$(SCRIPT)" || (echo "Missing $(SCRIPT)"; exit 1)
	@for f in $(SPRITE_CPPS); do $(PY) $(SCRIPT) "$$f" $(SCALE); done
	@echo "Done. Outputs in $(SPRITES_DIR)/*_$(SCALE)x.png"

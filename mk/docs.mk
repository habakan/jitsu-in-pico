# The concept diagram lives in the submodule because it describes the modules.
everywhere:
	$(MAKE) -C components/parts everywhere
	open components/parts/docs/everywhere.svg
.PHONY: everywhere

# Generate diagrams from the wiring data. wiring shows signal connections (WireViz and Graphviz); breadboard shows hole positions.
wiring: docs/wiring.yml
	uv run -q --with wireviz wireviz $< -o build/wiring
	cp build/wiring/wiring.svg docs/wiring.svg
	open build/wiring/wiring.html

breadboard: docs/breadboard.yml tools/generate/draw_breadboard.py
	uv run -q tools/generate/draw_breadboard.py $< docs/breadboard.svg
	open docs/breadboard.svg
.PHONY: wiring breadboard

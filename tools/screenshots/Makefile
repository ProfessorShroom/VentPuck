
LVGL_DIR ?= lvgl
ROOT := ../..
BUILD := build

CFLAGS := -O2 -w -DLV_CONF_INCLUDE_SIMPLE -Istubs -I$(ROOT)/include -I$(ROOT)/src -I$(LVGL_DIR)
CXXFLAGS := $(CFLAGS) -std=gnu++17

LVGL_SRC = $(shell find $(LVGL_DIR)/src -name '*.c')
APP_SRC := $(ROOT)/src/icons.c $(ROOT)/src/icon_beta.c $(ROOT)/src/font_digits.c
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(LVGL_SRC) $(APP_SRC))) $(BUILD)/sim.o

vpath %.c $(sort $(dir $(LVGL_SRC) $(APP_SRC)))

ventpuck-sim: $(OBJS)
	$(CXX) -o $@ $^ -lm

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sim.o: sim.cpp $(ROOT)/src/ui.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) ventpuck-sim

.PHONY: clean

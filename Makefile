RISCV_PREFIX  ?= riscv64-unknown-elf-
RISCV_CXX     := $(RISCV_PREFIX)g++
HOST_CXX      := g++
QEMU          := /home/youssef_abdelaty/qemu/build/qemu-riscv64
QEMU_SYSROOT  := 
QEMU_BASE     := $(QEMU)
BUILD_DIR     := build
SCRIPTS_DIR   := scripts
ARCH_FLAGS    := -march=rv64gcv -mabi=lp64d
OPT           ?= -O2
RISCV_FLAGS   := $(ARCH_FLAGS) $(OPT) -std=c++17 -Wall
HOST_FLAGS    := $(OPT) -std=c++17 -Wall
GTEST_INC     := -I/usr/local/include
GTEST_LIBS    := -L/usr/local/lib -lgtest -lgtest_main -lpthread

# --- Phase 4 Optimization Flags ---
RV_FLAGS_O0       := $(ARCH_FLAGS) -O0 -std=c++17 -Wall
RV_FLAGS_O2       := $(ARCH_FLAGS) -O2 -std=c++17 -Wall
RV_FLAGS_O3       := $(ARCH_FLAGS) -O3 -std=c++17 -Wall
RV_FLAGS_AUTO_VEC := $(ARCH_FLAGS) -O3 -ftree-vectorize -fopt-info-vec-all -std=c++17 -Wall

.PHONY: all clean verify verify128 verify256 verify512 verify-all test help canny_rv run sweep_O0 sweep_O2 sweep_O3 sweep_autovec

all: verify-all

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/rvv_verify: $(SCRIPTS_DIR)/rvv_verify.cpp | $(BUILD_DIR)
	$(RISCV_CXX) $(RISCV_FLAGS) $< -o $@

verify128: $(BUILD_DIR)/rvv_verify
	$(QEMU_BASE) -cpu rv64,v=true,vlen=128,elen=64 ./$(BUILD_DIR)/rvv_verify

verify256: $(BUILD_DIR)/rvv_verify
	$(QEMU_BASE) -cpu rv64,v=true,vlen=256,elen=64 ./$(BUILD_DIR)/rvv_verify

verify512: $(BUILD_DIR)/rvv_verify
	$(QEMU_BASE) -cpu rv64,v=true,vlen=512,elen=64 ./$(BUILD_DIR)/rvv_verify

verify-all: verify128 verify256 verify512

verify: verify256

test: | $(BUILD_DIR)
	$(HOST_CXX) $(HOST_FLAGS) $(GTEST_INC) tests/*.cpp $(GTEST_LIBS) -o $(BUILD_DIR)/host_test
	./$(BUILD_DIR)/host_test

# --- Standard Build/Run (Default to O3 for Phase 4) ---
canny_rv: | $(BUILD_DIR)
	$(RISCV_CXX) $(RV_FLAGS_O3) src/Phase_4.cpp -o $(BUILD_DIR)/canny_rv

run: canny_rv
	$(QEMU_BASE) -cpu rv64,v=true,vlen=128,elen=64 ./$(BUILD_DIR)/canny_rv

# --- Phase 4 Optimization Sweep Targets ---
sweep_O0: | $(BUILD_DIR)
	$(RISCV_CXX) $(RV_FLAGS_O0) src/Phase_4.cpp -o $(BUILD_DIR)/canny_O0
	@echo "--- O0 Binary Size ---"
	@ls -lh $(BUILD_DIR)/canny_O0
	@echo "--- Running O0 Benchmark ---"
	$(QEMU_BASE) -cpu rv64,v=true,vlen=128,elen=64 ./$(BUILD_DIR)/canny_O0

sweep_O2: | $(BUILD_DIR)
	$(RISCV_CXX) $(RV_FLAGS_O2) src/Phase_4.cpp -o $(BUILD_DIR)/canny_O2
	@echo "--- O2 Binary Size ---"
	@ls -lh $(BUILD_DIR)/canny_O2
	@echo "--- Running O2 Benchmark ---"
	$(QEMU_BASE) -cpu rv64,v=true,vlen=128,elen=64 ./$(BUILD_DIR)/canny_O2

sweep_O3: | $(BUILD_DIR)
	$(RISCV_CXX) $(RV_FLAGS_O3) src/Phase_4.cpp -o $(BUILD_DIR)/canny_O3
	@echo "--- O3 Binary Size ---"
	@ls -lh $(BUILD_DIR)/canny_O3
	@echo "--- Running O3 Benchmark ---"
	$(QEMU_BASE) -cpu rv64,v=true,vlen=128,elen=64 ./$(BUILD_DIR)/canny_O3

sweep_autovec: | $(BUILD_DIR)
	$(RISCV_CXX) $(RV_FLAGS_AUTO_VEC) src/Phase_4.cpp -o $(BUILD_DIR)/canny_autovec
	@echo "--- Auto-Vectorized Binary Size ---"
	@ls -lh $(BUILD_DIR)/canny_autovec
	@echo "--- Auto-Vectorization Instruction Count ---"
	@riscv64-unknown-elf-objdump -d $(BUILD_DIR)/canny_autovec | grep -c vset || echo "0 (No vector instructions generated)"
	@echo "--- Running Auto-Vec Benchmark ---"
	$(QEMU_BASE) -cpu rv64,v=true,vlen=128,elen=64 ./$(BUILD_DIR)/canny_autovec

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "make verify-all - Run RVV test at VLEN 128, 256, 512"
	@echo "make test       - Run GoogleTest host native"
	@echo "make canny_rv   - Compile the Canny pipeline for RISC-V"
	@echo "make run        - Execute the RISC-V Canny pipeline on QEMU"
	@echo "make clean      - Remove build files"
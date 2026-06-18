RISCV_PREFIX  ?= riscv64-unknown-elf-
RISCV_CXX     := $(RISCV_PREFIX)g++
HOST_CXX      := g++
QEMU          := /home/basel/rvv/qemu/build/qemu-riscv64
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

.PHONY: all clean verify verify128 verify256 verify512 verify-all test help

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

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "make verify-all - Run RVV test at VLEN 128, 256, 512"
	@echo "make test       - Run GoogleTest host native"
	@echo "make clean      - Remove build files"

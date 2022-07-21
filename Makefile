GITHUB_DEPS += simplerobot/build-scripts
GITHUB_DEPS += simplerobot/logger
GITHUB_DEPS += simplerobot/test
GITHUB_DEPS += simplerobot/test-stm32
GITHUB_DEPS += simplerobot/rlm3-hardware
GITHUB_DEPS += simplerobot/rlm3-base
GITHUB_DEPS += simplerobot/rlm3-sim-base
GITHUB_DEPS += simplerobot/rlm3-driver-base
GITHUB_DEPS += simplerobot/hw-test-agent
include ../build-scripts/build/release/include.make

CPU_CC = g++
CPU_CFLAGS = -Wall -Werror -pthread -DTEST -fsanitize=address -static-libasan -g -Og

MCU_TOOLCHAIN_PATH = /opt/gcc-arm-none-eabi-7-2018-q2-update/bin/arm-none-eabi-
MCU_CC = $(MCU_TOOLCHAIN_PATH)gcc
MCU_AS = $(MCU_TOOLCHAIN_PATH)gcc -x assembler-with-cpp
MCU_SZ = $(MCU_TOOLCHAIN_PATH)size
MCU_HX = $(MCU_TOOLCHAIN_PATH)objcopy -O ihex
MCU_BN = $(MCU_TOOLCHAIN_PATH)objcopy -O binary -S
MCU_CFLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fdata-sections -ffunction-sections -Wall -Werror -DUSE_HAL_DRIVER -DSTM32F427xx -DTEST -DUSE_FULL_ASSERT=1 -fexceptions -g -Og -gdwarf-2
MCU_CLIBS = -lc -lm -lstdc++

SOURCE_DIR = source
MAIN_SOURCE_DIR = $(SOURCE_DIR)/main
CPU_TEST_SOURCE_DIR = $(SOURCE_DIR)/test-cpu
MCU_TEST_SOURCE_DIR = $(SOURCE_DIR)/test-mcu

BUILD_DIR = build
LIBRARY_BUILD_DIR = $(BUILD_DIR)/library
CPU_TEST_BUILD_DIR = $(BUILD_DIR)/test-cpu
MCU_TEST_BUILD_DIR = $(BUILD_DIR)/test-mcu
RELEASE_DIR = $(BUILD_DIR)/release

LIBRARY_FILES = $(notdir $(wildcard $(MAIN_SOURCE_DIR)/*))

CPU_TEST_SOURCE_DIRS = $(MAIN_SOURCE_DIR) $(CPU_TEST_SOURCE_DIR) $(PKG_RLM3_BASE_DIR) $(PKG_LOGGER_DIR) $(PKG_TEST_DIR) $(PKG_RLM3_SIM_BASE_DIR) 
CPU_TEST_SOURCE_FILES = $(notdir $(wildcard $(CPU_TEST_SOURCE_DIRS:%=%/*.c) $(CPU_TEST_SOURCE_DIRS:%=%/*.cpp)))
CPU_TEST_O_FILES = $(addsuffix .o,$(basename $(CPU_TEST_SOURCE_FILES)))
CPU_INCLUDES = $(CPU_TEST_SOURCE_DIRS:%=-I%)

MCU_TEST_SOURCE_DIRS = $(MAIN_SOURCE_DIR) $(MCU_TEST_SOURCE_DIR) $(PKG_RLM3_HARDWARE_DIR) $(PKG_RLM3_BASE_DIR) $(PKG_LOGGER_DIR) $(PKG_TEST_STM32_DIR) $(PKG_RLM3_DRIVER_BASE_DIR)
MCU_TEST_SOURCE_FILES = $(notdir $(wildcard $(MCU_TEST_SOURCE_DIRS:%=%/*.c) $(MCU_TEST_SOURCE_DIRS:%=%/*.cpp) $(MCU_TEST_SOURCE_DIRS:%=%/*.s)))
MCU_TEST_O_FILES = $(addsuffix .o,$(basename $(MCU_TEST_SOURCE_FILES)))
MCU_TEST_LD_FILE = $(wildcard $(PKG_RLM3_HARDWARE_DIR)/*.ld)
MCU_INCLUDES = $(MCU_TEST_SOURCE_DIRS:%=-I%)

VPATH = $(MCU_TEST_SOURCE_DIRS) $(CPU_TEST_SOURCE_DIRS)

.PHONY: default all library test-cpu test-mcu release clean

default : all

all : release

library : $(LIBRARY_FILES:%=$(LIBRARY_BUILD_DIR)/%)

$(LIBRARY_BUILD_DIR)/% : $(MAIN_SOURCE_DIR)/% | $(LIBRARY_BUILD_DIR)
	cp $< $@

$(LIBRARY_BUILD_DIR) :
	mkdir -p $@

test-cpu : library $(CPU_TEST_BUILD_DIR)/a.out
	$(CPU_TEST_BUILD_DIR)/a.out

$(CPU_TEST_BUILD_DIR)/a.out : $(CPU_TEST_O_FILES:%=$(CPU_TEST_BUILD_DIR)/%)
	$(CPU_CC) $(CPU_CFLAGS) $^ -o $@

$(CPU_TEST_BUILD_DIR)/%.o : %.cpp Makefile | $(CPU_TEST_BUILD_DIR)
	$(CPU_CC) -c $(CPU_CFLAGS) $(CPU_INCLUDES) -MMD $< -o $@
	
$(CPU_TEST_BUILD_DIR)/%.o : %.c Makefile | $(CPU_TEST_BUILD_DIR)
	$(CPU_CC) -c $(CPU_CFLAGS) $(CPU_INCLUDES) -MMD $< -o $@

$(CPU_TEST_BUILD_DIR) :
	mkdir -p $@

test-mcu : library test-cpu $(MCU_TEST_BUILD_DIR)/test.bin $(MCU_TEST_BUILD_DIR)/test.hex
	$(PKG_HW_TEST_AGENT_DIR)/sr-hw-test-agent --run --test-timeout=60 --system-frequency=180m --trace-frequency=2m --board RLM36 --file $(MCU_TEST_BUILD_DIR)/test.bin

$(MCU_TEST_BUILD_DIR)/test.bin : $(MCU_TEST_BUILD_DIR)/test.elf
	$(MCU_BN) $< $@

$(MCU_TEST_BUILD_DIR)/test.hex : $(MCU_TEST_BUILD_DIR)/test.elf
	$(MCU_HX) $< $@

$(MCU_TEST_BUILD_DIR)/test.elf : $(MCU_TEST_O_FILES:%=$(MCU_TEST_BUILD_DIR)/%)
	$(MCU_CC) $(MCU_CFLAGS) $(MCU_TEST_LD_FILE:%=-T%) -Wl,--gc-sections $^ $(MCU_CLIBS) -s -o $@ -Wl,-Map=$@.map,--cref
	$(MCU_SZ) $@

$(MCU_TEST_BUILD_DIR)/%.o : %.c Makefile | $(MCU_TEST_BUILD_DIR)
	$(MCU_CC) -c $(MCU_CFLAGS) $(MCU_INCLUDES) -MMD $< -o $@

$(MCU_TEST_BUILD_DIR)/%.o : %.cpp Makefile | $(MCU_TEST_BUILD_DIR)
	$(MCU_CC) -c $(MCU_CFLAGS) $(MCU_INCLUDES) -MMD -std=c++11 $< -o $@

$(MCU_TEST_BUILD_DIR)/%.o : %.s Makefile | $(MCU_TEST_BUILD_DIR)
	$(MCU_AS) -c $(MCU_CFLAGS) -MMD $< -o $@

$(MCU_TEST_BUILD_DIR) :
	mkdir -p $@

release : test-cpu test-mcu $(LIBRARY_FILES:%=$(RELEASE_DIR)/%)

$(RELEASE_DIR)/% : $(LIBRARY_BUILD_DIR)/% | $(RELEASE_DIR)
	cp $< $@
	
$(RELEASE_DIR) :
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(wildcard $(CPU_TEST_BUILD_DIR)/*.d $(MCU_TEST_BUILD_DIR)/*.d)




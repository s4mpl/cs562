CXX=clang++
CXXFLAGS=-Wall -Wextra -pedantic -O0 -g
OUT=elfread


all: $(OUT)

$(OUT): main.cpp elf.h Makefile
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: clean

clean:
	rm -f $(OUT)

PLATFORM := Linux
BITS := 64
CC := g++
AR := ar
PREFIX := /usr/local
SO_EXT := .so

ifeq ($(OS), Windows_NT)
PLATFORM := mingw
endif

ifeq ($(BITS),32)
BIT_FLAG := -m32
endif

ifeq ($(PLATFORM),Linux)
SO_PRE := lib
else ifeq ($(PLATFORM),mingw)

ifeq ($(BITS),32)
CC := i686-w64-mingw32-g++
AR := i686-w64-mingw32-ar
PREFIX := /usr/i686-w64-mingw32
else
CC := x86_64-w64-mingw32-g++
AR := x86_64-w64-mingw32-ar
PREFIX := /usr/x86_64-w64-mingw32
endif

SO_EXT := .dll
endif

INC_FLAG = -Iinclude

NAME = synthutil
SRCS = $(wildcard src/*.cpp)
OBJS = $(patsubst src/%.cpp,obj/%.o,$(SRCS))
SHARED_LIB = build/$(SO_PRE)$(NAME)$(SO_EXT)
STATIC_LIB = build/lib$(NAME).a
HEADERS = $(wildcard include/*.hpp)
FLAGS = -laviutil -lflacutil -ljpegutil -lbitutil

.PHONY: shared
shared: $(SHARED_LIB)

$(SHARED_LIB): $(OBJS)
	$(CC) -shared -fPIC $(BIT_FLAG) -o $@ $^ $(FLAGS)

.PHONY: static
static: $(STATIC_LIB)

$(STATIC_LIB): $(OBJS)
	$(AR) -crs $@ $^

obj/%.o: src/%.cpp
	$(CC) -fPIC $(BIT_FLAG) $(INC_FLAG) -o $@ -c $^ $(FLAGS)

.PHONY: clean
clean:
	rm -f obj/*

.PHONY: delete
delete: clean
	rm -f build/*

.PHONY: install_headers
install_headers: $(HEADERS)
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 -t $(DESTDIR)$(PREFIX)/include $^

.PHONY: install_shared
install_shared: $(SHARED_LIB) install_headers
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 -t $(DESTDIR)$(PREFIX)/lib $<

.PHONY: install
install: install_shared

.PHONY: install_static
install_static: $(STATIC_LIB) install_headers
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 -t $(DESTDIR)$(PREFIX)/lib $<
CC := gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -municode
LDFLAGS := -s -mwindows -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -luuid -lshlwapi
TARGET := build/nova-desktop.exe
SOURCE := src/main.c src/core/workspace.c src/core/launch_queue.c src/platform/shell_icons.c src/ui/hold_drag.c src/ui/file_manager.c src/platform/storage.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCE) src/nova.rc src/nova.manifest src/assets/nova.ico third_party/sqlite/sqlite3.c third_party/sqlite/sqlite3.h build-sqlite.bat
	@if not exist build mkdir build
	@call build-sqlite.bat
	windres src/nova.rc -O coff -o build/nova-resource.o
	$(CC) $(CFLAGS) $(SOURCE) build/sqlite3.o build/nova-resource.o -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	start "" $(TARGET)

clean:
	@if exist build\nova-desktop.exe del /q build\nova-desktop.exe

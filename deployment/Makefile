# Run from the repository root: mingw32-make -f deployment/Makefile
SHELL := cmd.exe
.SHELLFLAGS := /c

.PHONY: all run clean

all:
	@call deployment\build.bat

run: all
	@start "" build\nova-desktop.exe

clean:
	@if exist build\nova-desktop.exe del /q build\nova-desktop.exe

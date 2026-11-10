CXX ?= g++
CXXFLAGS ?= -O2 -Wall
PREFIX ?= /boot/system/non-packaged

all: mac2vendor mac2vendor.convert

mac2vendor: mac2vendor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

mac2vendor.convert: mac2vendor.convert.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Build the database from the IEEE CSV files placed next to the Makefile.
oui.m2v: mac2vendor.convert oui.csv mam.csv oui36.csv
	./mac2vendor.convert $@ oui.csv mam.csv oui36.csv

db: oui.m2v

fetch:
	./getoui

install: mac2vendor
	mkdir -p $(PREFIX)/bin
	cp mac2vendor $(PREFIX)/bin/

install-db: oui.m2v
	mkdir -p $(PREFIX)/data/mac2vendor
	cp oui.m2v $(PREFIX)/data/mac2vendor/

clean:
	rm -f mac2vendor mac2vendor.convert

.PHONY: all db fetch install install-db clean

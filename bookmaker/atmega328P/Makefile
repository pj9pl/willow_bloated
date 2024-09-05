#!/usr/bin/make -f 
# Makefile for the ATmega328P datasheet
#   
# usage: make [book | clean]
#
doc1=ATmega48A-PA-88A-PA-168A-PA-328-P-DS-DS40002061A.pdf
prefix=atmega328P
sig=$(prefix).sig.ps
first=$(prefix).1st.ps
second=$(prefix).2nd.ps
target=book

book: mergedout

doc1.ps: $(doc1)
	pdftops $(doc1) doc1.ps

mergedout: doc1.ps
	cp doc1.ps mergedout.ps
	# add four blank pages at beginning.
	psselect -p_,_,_,_,- mergedout.ps >mergedout.tmp
	mv mergedout.tmp mergedout.ps
	# create 16-page (4-sheet) sections.
	psbook -s 16 mergedout.ps >$(sig)
	# split into first and second printer passes.
	pstops -pa4 '4:0L@.7(20.5cm,-0.2cm)+1L@.7(20.5cm,14.8cm)' $(sig) >$(first)
	pstops -pa4 '4:2L@.7(20.5cm,-0.2cm)+3L@.7(20.5cm,14.8cm)' $(sig) >$(second)

clean:
	rm -f *.ps *.pdf *.tmp


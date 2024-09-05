#!/usr/bin/make -f 
# Makefile for the ad7124-8.pdf datasheet
#   
# usage: make [book | clean]
#
doc1=ad7124-8.pdf
prefix=ad7124-8
sig=$(prefix).sig.ps
first=$(prefix).1st.ps
second=$(prefix).2nd.ps
target=book

book: mergedout

doc1.ps: $(doc1)
	pdftops $(doc1) doc1.ps

mergedout: doc1.ps
	cp doc1.ps mergedout.ps
	# add two blank pages at beginning.
	psselect -p_,_,- mergedout.ps >mergedout.tmp
	mv mergedout.tmp mergedout.ps
	# 16-page (4-sheet) sections results in 16 blank pages at the end.
	psbook -s 16 mergedout.ps >$(sig)
	# split into first and second printer passes.
	pstops -pa4 '4:0L@.7(20.5cm,-0.2cm)+1L@.7(20.5cm,14.8cm)' $(sig) >$(first)
	pstops -pa4 '4:2L@.7(20.5cm,-0.2cm)+3L@.7(20.5cm,14.8cm)' $(sig) >$(second)

clean:
	rm -f *.ps *.pdf *.tmp


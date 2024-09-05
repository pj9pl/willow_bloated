#!/usr/bin/make -f 
# Makefile for RV-3028-C7_App-Manual.pdf
#   
# usage: make [book | clean]
#
docpath=/home/pjw/doc/g4-pdfs/datasheets/rtc
doc1=$(docpath)/RV-3028-C7_App-Manual.pdf
prefix=RV_3028_C7_App_Manual
sig=$(prefix).sig.ps
first=$(prefix).1st.ps
second=$(prefix).2nd.ps
target=book

book: mergedout

doc1.ps: $(doc1)
	pdftops $(doc1) doc1.ps

mergedout: doc1.ps
	# add four blank pages at beginning
	psselect -p_,_,_,_,- doc1.ps >mergedout.ps
	# 16-page (4-sheet) sections results in 16 blank pages at the end.
	# 20-page (5-sheet) sections has only 4 blank pages at the end.
	psbook -s 16 mergedout.ps >$(sig)
	# split into first and second printer passes.
	pstops -pa4 '4:0L@.7(21.0cm,0.1cm)+1L@.7(21.0cm,14.9cm)' $(sig) >$(first)
	pstops -pa4 '4:2L@.7(21.0cm,0.1cm)+3L@.7(21.0cm,14.9cm)' $(sig) >$(second)

clean:
	rm -f *.ps *.pdf *.tmp

send: mergedout
	rsync -av $(first) $(second) sahara:print/rtc

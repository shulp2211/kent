kentSrc = ../..
include ../../inc/common.mk

L += -lm ${MYSQLLIBS} 
MYLIBDIR = ../../lib/${MACHTYPE}
MYLIBS =  ${MYLIBDIR}/jkhgapcgi.a ${MYLIBDIR}/jkhgap.a ${MYLIBDIR}/jkweb.a

A = hgc

O = $A.o bamClick.o barChartClick.o bigBedClick.o bigDbSnpClick.o \
	ccdsClick.o cgapSageClick.o clinvarSubLolly.o dbRIP.o \
	encodeClick.o expClick.o facetedBar.o \
	gencodeClick.o gtexEqtlClusterClick.o gtexClick.o gvfClick.o hgdpClick.o \
	interactClick.o lowelab.o lrgClick.o \
	mafClick.o makeItemsClick.o mgcClick.o \
	parClick.o peakClusters.o regMotif.o retroClick.o rmskJoinedClick.o \
	bigRmskClick.o rnaFoldClick.o togaClick.o \
	transMapClick.o txCdsInfo.o pubs.o vcfClick.o wiggleClick.o \
	wikiTrack.o variomeClick.o numtsClick.o geneReviewsClick.o

include ../../inc/cgi_build_rules.mk

mgsc: compile
	mv $A ${CGI_BIN}-mgsc/$A

compile: $O 
	${CC} ${CFLAGS} $O ${F64} ${MYLIBS} ${L}
	mv ${AOUT} $A${EXE}

my::
	rm -rf ${CGI_BIN}-${USER}/hgcData ${CGI_BIN}-${USER}/lsSnpPdbChimera.py
	rsync -aO --exclude=CVS/ --exclude=.svn/ --delete hgcData ../lib/lsSnpPdbChimera.py ${CGI_BIN}-${USER}/
	chmod -R a+rX ${CGI_BIN}-${USER}/hgcData ${CGI_BIN}-${USER}/lsSnpPdbChimera.py

alpha::
	rm -fr ${CGI_BIN}/hgcData ${CGI_BIN}/lsSnpPdbChimera.py
	rsync -aO --exclude=CVS/ --exclude=.svn/ --delete hgcData ../lib/lsSnpPdbChimera.py ${CGI_BIN}/
	chmod -R a+rX,g+w ${CGI_BIN}/hgcData ${CGI_BIN}/lsSnpPdbChimera.py

beta::
	rm -fr ${CGI_BIN}-beta/hgcData ${CGI_BIN}-beta/lsSnpPdbChimera.py
	rsync -aO --exclude=CVS/ --exclude=.svn/ --delete hgcData ../lib/lsSnpPdbChimera.py ${CGI_BIN}-beta/
	chmod -R a+rX,g+w ${CGI_BIN}-beta/hgcData ${CGI_BIN}-beta/lsSnpPdbChimera.py

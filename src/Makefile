indexhtml.h: index.html
	xxd -i index.html >indexhtml.h
	sed -i .old '1 s/.*/const unsigned char index_html[] PROGMEM = {/'  indexhtml.h
	rm indexhtml.h.old



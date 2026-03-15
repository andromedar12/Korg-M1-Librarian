wm1_32.exe: wm1.obj wm1edit.obj wm1.res
	lcclnk -s wm1.obj wm1edit.obj wm1.res winmm.lib -subsystem windows -o wm1_32.exe

wm1.obj: wm1.c wm1.h wm1edit.h
	lcc -c wm1.c

wm1edit.obj: wm1edit.c wm1edit.h wm1.h
	lcc -c wm1edit.c

wm1.res: wm1.rc
	lrc wm1.rc

clean:
	del wm1.obj wm1edit.obj wm1.res wm1_32.exe

zip:
	zip -9 -j wm1_10.zip wm1_32.exe wm1_10.txt

dist:
	zip -9 wm1_src.zip COPYING README wm1_10.txt makefile *.c *.h *.ico *.rc


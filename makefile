all: cal-new admin client

cal-new: cal-new.c
	gcc -o cal-new.exe cal-new.c -pthread -lm

admin: admin.c
	gcc admin.c -o admin.exe

client: client.c
	gcc client.c -o client.exe

clean:
	rm -f *.o
	rm -f *.exe

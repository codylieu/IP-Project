#include <stdio.h>

int readFile(int argc, char *argv[]) {
int i;
FILE *fp;
int c;

for (i = 1; i < argc; i++) {
    fp = fopen(argv[i], "r");

    if (fp == NULL) {
        fprint(stderr, "cat: can't open %s\n", argv[i]);
        continue;
    }

    while ((c = getc(fp)) != EOF) {
        putchar(c);
    }

    fclose(fp);
}

return 0;
}
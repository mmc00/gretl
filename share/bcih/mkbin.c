#include <stdio.h>
#include <stdlib.h>

int main (int argc, char *argv[])
{
    const char *binfile = "bcih.bin";
    const char *datfile = "bcih.dat";
    char datpath[128];
    FILE *fdat, *fbin;
    double xx;
    float x;

    if (argc > 1) {
	sprintf(datpath, "%s/%s", argv[1], datfile);
    } else {
	strcpy(datpath, datfile);
    }

    fdat = fopen(datpath, "r");
    if (fdat == NULL) {
	fprintf(stderr, "Couldn't open %s\n", datfile);
	exit(EXIT_FAILURE);
    }

    fbin = fopen(binfile, "wb");
    if (fbin == NULL) {
	fprintf(stderr, "Couldn't open %s\n", binfile);
	exit(EXIT_FAILURE);
    }

    while (fscanf(fdat, "%lf", &xx) == 1) {
	x = xx;
	fwrite(&x, sizeof x, 1, fbin);
    }

    fclose(fbin);
    fclose(fdat);

    return 0;
}

#include <icslab2_net.h>

int main(int argc, char *argv[]) {
    // MB
    size_t file_size = atoi(argv[1]);
    size_t file_size_bytes = file_size * 1000 * 1000;

    FILE *file = fopen("test.bin", "wb");

    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', BUFFER_SIZE);

    size_t written = 0;

    while (written < file_size_bytes) {
        size_t remaining = file_size_bytes - written;
        size_t chunk_size = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
        size_t n = fwrite(buffer, 1, chunk_size, file);
        written += n;
    }

    fclose(file);
    printf("Created %zu bytes\n", written);

    return 0;
}
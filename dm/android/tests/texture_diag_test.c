#include "texture_diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL at line %d\\n", __LINE__); return 1; } } while (0)

int main(void) {
    CHECK(texture_diag_classify(1, 0, 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_UNBOUND);
    CHECK(texture_diag_classify(0, 7, 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_MISSING);
    CHECK(texture_diag_classify(1, 7, 0x9a, 0, 0, 0, 0x9a, 0) == TEXTURE_DIAG_UNSUPPORTED_FORMAT);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 0, 0, 0x25, 0) == TEXTURE_DIAG_UNSAMPLED_FORMAT);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 1, 1, 0x38, 0) == TEXTURE_DIAG_VIEW_FORMAT);
    CHECK(texture_diag_classify(1, 7, 0x32, 1, 1, 0, 0x32, 0) == TEXTURE_DIAG_DEPTH);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 1, 0, 0x25, 0x10) == TEXTURE_DIAG_LINEAR);
    CHECK(texture_diag_classify(1, 7, 0x25, 1, 1, 0, 0x25, 0) == TEXTURE_DIAG_OK);
    CHECK(!strcmp(texture_diag_name(TEXTURE_DIAG_MISSING), "missing"));
    puts("PASS: texture diagnostic classification");
    return 0;
}

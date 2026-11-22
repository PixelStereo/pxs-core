/* The shared library (libpxs_c), loaded the way dart:ffi loads it: a plain C program, only the C header. */
#include <pxs/c/pxs.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    char *key = pxs_websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    int ok = key && strcmp(key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0 && pxs_api_version() == PXS_C_API_VERSION;
    pxs_free(key);
    if (!ok) return 1;
    printf("c_api_shared: ok (pxs-core %s)\n", pxs_version());
    return 0;
}

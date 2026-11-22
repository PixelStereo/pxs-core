/* The C interface's header is C: this is compiled as C99 with the warnings on, and run. */
#include <pxs/c/pxs.h>

#include <stdio.h>
#include <string.h>

static int get(void *user, pxs_value *out)
{
    double *level = (double *)user;
    memset(out, 0, sizeof *out);
    out->kind = PXS_DOUBLE;
    out->number = *level;
    return 0;
}

static int set(void *user, const pxs_value *in)
{
    double *level = (double *)user;
    *level = in->number;
    return 0;
}

int main(void)
{
    double level = 0.25;
    uint8_t *bytes = NULL;
    size_t size = 0;
    pxs_value arg;
    pxs_tree *tree = pxs_tree_new();
    int status = 0;
    char *body;
    int accepted;

    if (pxs_api_version() != PXS_C_API_VERSION || !tree) return 1;
    if (pxs_tree_add_parameter(tree, "/level", "Level", PXS_PARAM_FLOAT, 0, 1, NULL, 0, get, set, &level) != PXS_OK) return 2;

    memset(&arg, 0, sizeof arg);
    arg.kind = PXS_DOUBLE;
    arg.number = 0.5;
    if (pxs_osc_encode("/level", "f", &arg, 1, &bytes, &size) != PXS_OK) return 3;
    accepted = pxs_tree_apply_packet(tree, bytes, size);
    pxs_free(bytes);
    if (accepted != 1 || level != 0.5) return 4;

    body = pxs_oscquery_answer(tree, "C", 1, "GET", "/level?VALUE", &status);
    if (!body || status != 200 || strcmp(body, "{\"VALUE\":[0.5]}") != 0) return 5;
    pxs_free(body);
    pxs_tree_free(tree);
    printf("c_api_header: ok\n");
    return 0;
}

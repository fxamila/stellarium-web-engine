/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#include "swe.h"

typedef struct star_name star_name_t;
struct star_name
{
    UT_hash_handle  hh;
    int             hd;
    char            *name;
};

struct skyculture
{
    star_name_t     *star_names;
    int             nb_constellations;
    constellation_infos_t *constellations;
};

static void parse_names(skyculture_t *cult, const char *names)
{
    char *data, *line, *tmp = NULL;
    star_name_t *star_name;

    data = strdup(names);
    for (line = strtok_r(data, "\n", &tmp); line;
          line = strtok_r(NULL, "\n", &tmp)) {
        if (str_startswith(line, "//")) continue;
        star_name = calloc(1, sizeof(*star_name));
        if (sscanf(strtok(line, " "), "%d", &star_name->hd) != 1) {
            LOG_W("Cannot parse star name: '%s'", line);
        }
        star_name->name = strdup(strtok(NULL, "\n"));
        HASH_ADD_INT(cult->star_names, hd, star_name);
    }
    free(data);
}

static void trim_right_spaces(char *s)
{
    int l = strlen(s);
    while (s[l - 1] == ' ') {
        s[l - 1] = '\0';
        l--;
    }
}

static int parse_star(const char *cst, const char *tok)
{
    int hd;
    char bayer_id[32];
    if (tok[0] >= '0' && tok[1] <= '9') { // A HD number
        sscanf(tok, "%d", &hd);
        return hd;
    }
    // Otherwise a bayer id.
    sprintf(bayer_id, "%s %s", tok, cst);
    tok = (char*)identifiers_search(bayer_id);
    assert(tok && str_startswith(tok, "HD "));
    tok += 3;
    sscanf(tok, "%d", &hd);
    return hd;
}

static void parse_constellations(skyculture_t *cult, const char *consts)
{
    char *data, *line, *tmp = NULL, *tok;
    bool linked;
    int nb = 0, i, star, last_star = 0;
    constellation_infos_t *cons;

    data = strdup(consts);

    // Count the number of lines in the file.
    for (line = data; *line; line = strchr(line, '\n') + 1) nb++;
    assert(nb);

    cult->nb_constellations = nb;
    cult->constellations = calloc(nb + 1, sizeof(constellation_infos_t));

    for (i = 0, line = strtok_r(data, "\n", &tmp); line;
          line = strtok_r(NULL, "\n", &tmp), i++) {
        cons = &cult->constellations[i];
        strcpy(cons->id, strtok(line, "|"));
        strcpy(cons->name, strtok(NULL, "|"));
        trim_right_spaces(cons->name);
        nb = 0;
        while ((tok = strtok(NULL, " -"))) {
            // Check if the last separator was a '-'.
            linked = consts[tok - 1 - data] == '-';
            if (linked) assert(last_star);
            star = parse_star(cons->id, tok);
            if (linked) {
                cons->lines[nb][0] = last_star;
                cons->lines[nb][1] = star;
                nb++;
            }
            last_star = star;
        }
        cons->nb_lines = nb;
    }
    free(data);
}

static constellation_infos_t *get_constellation(
        skyculture_t *cult, const char *id)
{
    int i;
    for (i = 0; i < cult->nb_constellations; i++) {
        if (strcasecmp(cult->constellations[i].id, id) == 0)
            return &cult->constellations[i];
    }
    return NULL;
}

static void parse_edges(skyculture_t *cult, const char *edges)
{
    constellation_infos_t *info = NULL;
    const char *line;
    char cst[2][8];
    int i, ra1_h, ra1_m, ra1_s, ra2_h, ra2_m, ra2_s;
    char dec1_sign, dec2_sign;
    int dec1_d, dec1_m, dec1_s, dec2_d, dec2_m, dec2_s;
    double ra1, dec1, ra2, dec2;
    const int MAX_EDGES = ARRAY_SIZE(info->edges);

    for (line = edges; *line; line = strchr(line, '\n') + 1) {
        sscanf(line, "%*s %*s"
                     "%d:%d:%d %c%d:%d:%d "
                     "%d:%d:%d %c%d:%d:%d "
                     "%s %s",
                     &ra1_h, &ra1_m, &ra1_s,
                     &dec1_sign, &dec1_d, &dec1_m, &dec1_s,
                     &ra2_h, &ra2_m, &ra2_s,
                     &dec2_sign, &dec2_d, &dec2_m, &dec2_s,
                     cst[0], cst[1]);
        eraTf2a('+', ra1_h, ra1_m, ra1_s, &ra1);
        eraTf2a('+', ra2_h, ra2_m, ra2_s, &ra2);
        eraAf2a(dec1_sign, dec1_d, dec1_m, dec1_s, &dec1);
        eraAf2a(dec2_sign, dec2_d, dec2_m, dec2_s, &dec2);
        for (i = 0; i < 2; i++) {
            info = get_constellation(cult, cst[i]);
            if (!info) continue;
            if (info->nb_edges >= MAX_EDGES) {
                LOG_E("Too many bounds in constellation %s", cst);
                continue;
            }
            info->edges[info->nb_edges][0][0] = ra1;
            info->edges[info->nb_edges][0][1] = dec1;
            info->edges[info->nb_edges][1][0] = ra2;
            info->edges[info->nb_edges][1][1] = dec2;
            info->nb_edges++;
        }
    }
}

skyculture_t *skyculture_create(void)
{
    char id[64];
    const char *names, *constellations, *edges;
    star_name_t *star_name, *tmp;
    skyculture_t *cult = calloc(1, sizeof(*cult));

    names = asset_get_data(
            "asset://skycultures/western/names.txt", NULL, NULL);
    constellations = asset_get_data(
            "asset://skycultures/western/constellations.txt", NULL, NULL);
    edges = asset_get_data(
            "asset://skycultures/western/edges.txt", NULL, NULL);

    assert(names);
    assert(constellations);
    assert(edges);

    parse_names(cult, names);
    parse_constellations(cult, constellations);
    parse_edges(cult, edges);

    // Register all the names.
    HASH_ITER(hh, cult->star_names, star_name, tmp) {
        sprintf(id, "HD %d", star_name->hd);
        identifiers_add(id, "NAME", star_name->name, NULL, NULL);
    }

    return cult;
}

const char *skyculture_get_star_name(const skyculture_t *cult, int hd)
{
    star_name_t *star_name;
    HASH_FIND_INT(cult->star_names, &hd, star_name);
    return star_name ? star_name->name : NULL;
}

int skyculture_search_star_name(const skyculture_t *cult, const char *name)
{
    star_name_t *star_name, *tmp;
    HASH_ITER(hh, cult->star_names, star_name, tmp) {
        if (strcasecmp(star_name->name, name) == 0) {
            return star_name->hd;
        }
    }
    return 0;
}

const constellation_infos_t *skyculture_get_constellations(
        const skyculture_t *cult,
        int *nb)
{
    if (nb) *nb = cult->nb_constellations;
    return cult->constellations;
}

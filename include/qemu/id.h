#pragma once

typedef enum IdSubSystems {
    ID_QDEV,
    ID_BLOCK,
    ID_CHR,
    ID_NET,
    ID_MAX      /* last element, used as array size */
} IdSubSystems;

char *id_generate(IdSubSystems id);
bool id_wellformed(const char *id);

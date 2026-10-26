/* 8.3 names. */
#include "ext2/internal.h"

#include <ctype.h>

/* Turns "notes.txt" into the 8.3 form "NOTES   TXT". */
int format_name(const char *name, char formatted[MAX_ENTRY_NAME_LENGTH]) {
    size_t i, baseLength = 0, extLength = 0;
    int inExtension = 0;

    memset(formatted, SPACE_CHAR, MAX_ENTRY_NAME_LENGTH);

    /* . and .. keep their dots and, like every 8.3 name, are padded with spaces rather than terminated. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        formatted[0] = '.';
        if (name[1] == '.')
            formatted[1] = '.';
        return EXT2_SUCCESS;
    }

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c == '.') {
            if (inExtension)
                return EXT2_ERROR;
            inExtension = 1;
        } else if (isalnum(c)) {
            if (inExtension) {
                if (extLength == 3)
                    return EXT2_ERROR;
                formatted[8 + extLength++] = (char)toupper(c);
            } else {
                if (baseLength == 8)
                    return EXT2_ERROR;
                formatted[baseLength++] = (char)toupper(c);
            }
        } else {
            return EXT2_ERROR;
        }
    }
    return baseLength == 0 ? EXT2_ERROR : EXT2_SUCCESS;
}

UINT32 name_length(const char name[MAX_ENTRY_NAME_LENGTH]) {
    UINT32 length = 0;

    while (length < MAX_ENTRY_NAME_LENGTH && name[length] != SPACE_CHAR)
        length++;
    return length;
}

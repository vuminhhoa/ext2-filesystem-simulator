#ifndef EXT2SIM_EXT2_SHELL_H
#define EXT2SIM_EXT2_SHELL_H

#include "ext2/ext2.h"
#include "shell/shell.h"

/* Connects the generic shell to the ext2 implementation. */
void shell_register_filesystem(SHELL_FILESYSTEM *fs);

#endif

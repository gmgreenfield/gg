#ifndef GG_FILE_IO_H
#define GG_FILE_IO_H

#include "editor.h"

int load_file(editor_state *s);
int save_file(const editor_state *s);

#endif

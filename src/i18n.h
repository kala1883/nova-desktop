#ifndef NOVA_I18N_H
#define NOVA_I18N_H
#include <windows.h>

/* 0 = Simplified Chinese, 1 = English. The preference is stored in nova.sqlite. */
extern BOOL nova_english;

static inline const wchar_t *nova_text(const wchar_t *zh_cn,const wchar_t *en){
    return nova_english?en:zh_cn;
}

#endif

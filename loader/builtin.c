/*
 * Built-in modules
 *
 * Copyright 1996 Alexandre Julliard
 */

#ifndef WINELIB

#include <ctype.h>
#include <string.h>
#include "windows.h"
#include "gdi.h"
#include "global.h"
#include "module.h"
#include "neexe.h"
#include "user.h"
#include "stddebug.h"
#include "debug.h"


/* Built-in modules descriptors */
/* Don't change these structures! (see tools/build.c) */

typedef struct
{
    const BYTE *code_start;        /* 32-bit address of DLL code */
    const BYTE *data_start;        /* 32-bit address of DLL data */
} WIN16_DESCRIPTOR;

typedef struct
{
    int                 base;      /* Ordinal base */
    int                 size;      /* Number of functions */
    const void        **functions; /* Pointer to functions table */
    const char * const *names;     /* Pointer to names table */
} WIN32_DESCRIPTOR;

typedef struct
{
    const char *name;              /* DLL name */
    void       *module_start;      /* 32-bit address of the module data */
    int         module_size;       /* Size of the module data */
    union
    {
        WIN16_DESCRIPTOR win16;    /* Descriptor for Win16 DLL */
        WIN32_DESCRIPTOR win32;    /* Descriptor for Win32 DLL */
    } u;
} DLL_DESCRIPTOR;

typedef struct
{
    const DLL_DESCRIPTOR *descr;   /* DLL descriptor */
    int                   flags;   /* flags (see below) */
} BUILTIN_DLL;


/* DLL flags */
#define DLL_FLAG_NOT_USED    0x01  /* Use original Windows DLL if possible */
#define DLL_FLAG_ALWAYS_USED 0x02  /* Always use built-in DLL */
#define DLL_FLAG_WIN32       0x04  /* DLL is a Win32 DLL */


/* 16-bit DLLs */

extern const DLL_DESCRIPTOR KERNEL_Descriptor;
extern const DLL_DESCRIPTOR USER_Descriptor;
extern const DLL_DESCRIPTOR GDI_Descriptor;
extern const DLL_DESCRIPTOR WIN87EM_Descriptor;
extern const DLL_DESCRIPTOR MMSYSTEM_Descriptor;
extern const DLL_DESCRIPTOR SHELL_Descriptor;
extern const DLL_DESCRIPTOR SOUND_Descriptor;
extern const DLL_DESCRIPTOR KEYBOARD_Descriptor;
extern const DLL_DESCRIPTOR WINSOCK_Descriptor;
extern const DLL_DESCRIPTOR STRESS_Descriptor;
extern const DLL_DESCRIPTOR SYSTEM_Descriptor;
extern const DLL_DESCRIPTOR TOOLHELP_Descriptor;
extern const DLL_DESCRIPTOR MOUSE_Descriptor;
extern const DLL_DESCRIPTOR COMMDLG_Descriptor;
extern const DLL_DESCRIPTOR OLE2_Descriptor;
extern const DLL_DESCRIPTOR OLE2CONV_Descriptor;
extern const DLL_DESCRIPTOR OLE2DISP_Descriptor;
extern const DLL_DESCRIPTOR OLE2NLS_Descriptor;
extern const DLL_DESCRIPTOR OLE2PROX_Descriptor;
extern const DLL_DESCRIPTOR OLECLI_Descriptor;
extern const DLL_DESCRIPTOR OLESVR_Descriptor;
extern const DLL_DESCRIPTOR COMPOBJ_Descriptor;
extern const DLL_DESCRIPTOR STORAGE_Descriptor;
extern const DLL_DESCRIPTOR WPROCS_Descriptor;
extern const DLL_DESCRIPTOR DDEML_Descriptor;
extern const DLL_DESCRIPTOR LZEXPAND_Descriptor;
extern const DLL_DESCRIPTOR VER_Descriptor;
extern const DLL_DESCRIPTOR W32SYS_Descriptor;

/* 32-bit DLLs */

extern const DLL_DESCRIPTOR ADVAPI32_Descriptor;
extern const DLL_DESCRIPTOR COMCTL32_Descriptor;
extern const DLL_DESCRIPTOR COMDLG32_Descriptor;
extern const DLL_DESCRIPTOR CRTDLL_Descriptor;
extern const DLL_DESCRIPTOR OLE32_Descriptor;
extern const DLL_DESCRIPTOR GDI32_Descriptor;
extern const DLL_DESCRIPTOR KERNEL32_Descriptor;
extern const DLL_DESCRIPTOR NTDLL_Descriptor;
extern const DLL_DESCRIPTOR SHELL32_Descriptor;
extern const DLL_DESCRIPTOR USER32_Descriptor;
extern const DLL_DESCRIPTOR WINSPOOL_Descriptor;
extern const DLL_DESCRIPTOR WSOCK32_Descriptor;

/* Table of all built-in DLLs */

static BUILTIN_DLL BuiltinDLLs[] =
{
    /* Win16 DLLs */
    { &KERNEL_Descriptor,   DLL_FLAG_ALWAYS_USED },
    { &USER_Descriptor,     DLL_FLAG_ALWAYS_USED },
    { &GDI_Descriptor,      DLL_FLAG_ALWAYS_USED },
    { &WIN87EM_Descriptor,  DLL_FLAG_NOT_USED },
    { &SHELL_Descriptor,    0 },
    { &SOUND_Descriptor,    0 },
    { &KEYBOARD_Descriptor, 0 },
    { &WINSOCK_Descriptor,  0 },
    { &STRESS_Descriptor,   0 },
    { &MMSYSTEM_Descriptor, 0 },
    { &SYSTEM_Descriptor,   0 },
    { &TOOLHELP_Descriptor, 0 },
    { &MOUSE_Descriptor,    0 },
    { &COMMDLG_Descriptor,  DLL_FLAG_NOT_USED },
    { &OLE2_Descriptor,     DLL_FLAG_NOT_USED },
    { &OLE2CONV_Descriptor, DLL_FLAG_NOT_USED },
    { &OLE2DISP_Descriptor, DLL_FLAG_NOT_USED },
    { &OLE2NLS_Descriptor,  DLL_FLAG_NOT_USED },
    { &OLE2PROX_Descriptor, DLL_FLAG_NOT_USED },
    { &OLECLI_Descriptor,   DLL_FLAG_NOT_USED },
    { &OLESVR_Descriptor,   DLL_FLAG_NOT_USED },
    { &COMPOBJ_Descriptor,  DLL_FLAG_NOT_USED },
    { &STORAGE_Descriptor,  DLL_FLAG_NOT_USED },
    { &WPROCS_Descriptor,   DLL_FLAG_ALWAYS_USED },
    { &DDEML_Descriptor,    DLL_FLAG_NOT_USED },
    { &LZEXPAND_Descriptor, 0 },
    { &VER_Descriptor,      0 },
    { &W32SYS_Descriptor,   0 },
    /* Win32 DLLs */
    { &ADVAPI32_Descriptor, 0 },
    { &COMCTL32_Descriptor, 0 },
    { &COMDLG32_Descriptor, 0 },
    { &CRTDLL_Descriptor, 0 },
    { &OLE32_Descriptor,    0 },
    { &GDI32_Descriptor,    0 },
    { &KERNEL32_Descriptor, DLL_FLAG_ALWAYS_USED },
    { &NTDLL_Descriptor,    0 },
    { &SHELL32_Descriptor,  0 },
    { &USER32_Descriptor,   0 },
    { &WINSPOOL_Descriptor, 0 },
    { &WSOCK32_Descriptor,  0 },
    /* Last entry */
    { NULL, 0 }
};


/***********************************************************************
 *           BUILTIN_Init
 *
 * Load all built-in modules marked as 'always used'.
 */
BOOL BUILTIN_Init(void)
{
    BUILTIN_DLL *dll;
    NE_MODULE *pModule;

    for (dll = BuiltinDLLs; dll->descr; dll++)
        if (dll->flags & DLL_FLAG_ALWAYS_USED)
            if (!BUILTIN_LoadModule(dll->descr->name, TRUE)) return FALSE;

    /* Initialize KERNEL.178 (__WINFLAGS) with the correct flags value */

    MODULE_SetEntryPoint( GetModuleHandle( "KERNEL" ), 178, GetWinFlags() );

    /* Set the USER and GDI heap selectors */

    pModule      = MODULE_GetPtr( GetModuleHandle( "USER" ));
    USER_HeapSel = (NE_SEG_TABLE( pModule ) + pModule->dgroup - 1)->selector;
    pModule      = MODULE_GetPtr( GetModuleHandle( "GDI" ));
    GDI_HeapSel  = (NE_SEG_TABLE( pModule ) + pModule->dgroup - 1)->selector;

    return TRUE;
}


/***********************************************************************
 *           BUILTIN_LoadModule
 *
 * Load a built-in module. If the 'force' parameter is FALSE, we only
 * load the module if it has not been disabled via the -dll option.
 */
HMODULE BUILTIN_LoadModule( LPCSTR name, BOOL force )
{
    HMODULE hModule;
    NE_MODULE *pModule;
    BUILTIN_DLL *table;
    char dllname[16], *p;

    /* Fix the name in case we have a full path and extension */

    if ((p = strrchr( name, '\\' ))) name = p + 1;
    lstrcpyn( dllname, name, sizeof(dllname) );
    if ((p = strrchr( dllname, '.' ))) *p = '\0';

    for (table = BuiltinDLLs; table->descr; table++)
        if (!lstrcmpi( table->descr->name, dllname )) break;
    if (!table->descr) return 0;
    if ((table->flags & DLL_FLAG_NOT_USED) && !force) return 0;

    hModule = GLOBAL_CreateBlock( GMEM_MOVEABLE, table->descr->module_start,
                                  table->descr->module_size, 0,
                                  FALSE, FALSE, FALSE, NULL );
    if (!hModule) return 0;
    FarSetOwner( hModule, hModule );

    dprintf_module( stddeb, "Built-in %s: hmodule=%04x\n",
                    table->descr->name, hModule );
    pModule = (NE_MODULE *)GlobalLock16( hModule );
    pModule->self = hModule;

    if (pModule->flags & NE_FFLAGS_WIN32)
    {
        pModule->pe_module = (PE_MODULE *)table;
    }
    else  /* Win16 module */
    {
        const WIN16_DESCRIPTOR *descr = &table->descr->u.win16;
        int minsize;

        /* Allocate the code segment */

        SEGTABLEENTRY *pSegTable = NE_SEG_TABLE( pModule );
        pSegTable->selector = GLOBAL_CreateBlock(GMEM_FIXED, descr->code_start,
                                                 pSegTable->minsize, hModule,
                                                 TRUE, TRUE, FALSE, NULL );
        if (!pSegTable->selector) return 0;
        pSegTable++;

        /* Allocate the data segment */

        minsize = pSegTable->minsize ? pSegTable->minsize : 0x10000;
        minsize += pModule->heap_size;
        if (minsize > 0x10000) minsize = 0x10000;
        pSegTable->selector = GLOBAL_Alloc( GMEM_FIXED, minsize,
                                            hModule, FALSE, FALSE, FALSE );
        if (!pSegTable->selector) return 0;
        if (pSegTable->minsize) memcpy( GlobalLock16( pSegTable->selector ),
                                        descr->data_start, pSegTable->minsize);
        if (pModule->heap_size)
            LocalInit( pSegTable->selector, pSegTable->minsize, minsize );
    }

    MODULE_RegisterModule( pModule );
    return hModule;
}


/***********************************************************************
 *           BUILTIN_GetEntryPoint
 *
 * Return the built-in module, ordinal and name corresponding
 * to a CS:IP address. This is used only by relay debugging.
 */
NE_MODULE *BUILTIN_GetEntryPoint( WORD cs, WORD ip, WORD *pOrd, char **ppName )
{
    WORD ordinal, i, max_offset;
    register BYTE *p;
    NE_MODULE *pModule;

    if (!(pModule = MODULE_GetPtr( FarGetOwner( GlobalHandle16(cs) ))))
        return NULL;

    /* Search for the ordinal */

    p = (BYTE *)pModule + pModule->entry_table;
    max_offset = 0;
    ordinal = 1;
    *pOrd = 0;
    while (*p)
    {
        switch(p[1])
        {
        case 0:    /* unused */
            ordinal += *p;
            p += 2;
            break;
        case 1:    /* code segment */
            i = *p;
            p += 2;
            while (i-- > 0)
            {
                p++;
                if ((*(WORD *)p <= ip) && (*(WORD *)p >= max_offset))
                {
                    max_offset = *(WORD *)p;
                    *pOrd = ordinal;
                }
                p += 2;
                ordinal++;
            }
            break;
        case 0xff: /* moveable (should not happen in built-in modules) */
            fprintf( stderr, "Built-in module has moveable entry\n" );
            ordinal += *p;
            p += 2 + *p * 6;
            break;
        default:   /* other segment */
            ordinal += *p;
            p += 2 + *p * 3;
            break;
        }
    }

    /* Search for the name in the resident names table */
    /* (built-in modules have no non-resident table)   */
    
    p = (BYTE *)pModule + pModule->name_table;
    *ppName = "???";
    while (*p)
    {
        p += *p + 1 + sizeof(WORD);
        if (*(WORD *)(p + *p + 1) == *pOrd)
        {
            *ppName = (char *)p;
            break;
        }
    }

    return pModule;
}


/***********************************************************************
 *           BUILTIN_GetProcAddress32
 *
 * Implementation of GetProcAddress() for built-in Win32 modules.
 * FIXME: this should be unified with the real GetProcAddress32().
 */
DWORD BUILTIN_GetProcAddress32( NE_MODULE *pModule, char *function )
{
    BUILTIN_DLL *dll = (BUILTIN_DLL *)pModule->pe_module;
    const WIN32_DESCRIPTOR *info = &dll->descr->u.win32;

    if (!dll) return 0;

    if (HIWORD(function))  /* Find function by name */
    {
        int i;

        dprintf_module( stddeb, "Looking for function %s in %s\n",
                        function, dll->descr->name );
        for (i = 0; i < info->size; i++)
            if (info->names[i] && !strcmp( function, info->names[i] ))
                return (DWORD)info->functions[i];
    }
    else  /* Find function by ordinal */
    {
        WORD ordinal = LOWORD(function);
        dprintf_module( stddeb, "Looking for ordinal %d in %s\n",
                        ordinal, dll->descr->name );
        if (ordinal && ordinal < info->size)
            return (DWORD)info->functions[ordinal - info->base];
    }
    return 0;
}


/***********************************************************************
 *           BUILTIN_ParseDLLOptions
 *
 * Set runtime DLL usage flags
 */
BOOL BUILTIN_ParseDLLOptions( const char *str )
{
    BUILTIN_DLL *dll;
    const char *p;

    while (*str)
    {
        while (*str && isspace(*str)) str++;
        if (!*str) return TRUE;
        if ((*str != '+') && (*str != '-')) return FALSE;
        str++;
        if (!(p = strchr( str, ',' ))) p = str + strlen(str);
        while ((p > str) && isspace(p[-1])) p--;
        if (p == str) return FALSE;
        for (dll = BuiltinDLLs; dll->descr; dll++)
        {
            if (!lstrncmpi( str, dll->descr->name, (int)(p - str) ))
            {
                if (str[-1] == '-')
                {
                    if (dll->flags & DLL_FLAG_ALWAYS_USED) return FALSE;
                    dll->flags |= DLL_FLAG_NOT_USED;
                }
                else dll->flags &= ~DLL_FLAG_NOT_USED;
                break;
            }
        }
        if (!dll->descr) return FALSE;
        str = p;
        while (*str && (isspace(*str) || (*str == ','))) str++;
    }
    return TRUE;
}


/***********************************************************************
 *           BUILTIN_PrintDLLs
 *
 * Print the list of built-in DLLs that can be disabled.
 */
void BUILTIN_PrintDLLs(void)
{
    int i;
    BUILTIN_DLL *dll;

    for (i = 0, dll = BuiltinDLLs; dll->descr; dll++)
    {
        if (!(dll->flags & DLL_FLAG_ALWAYS_USED))
            fprintf( stderr, "%-9s%c", dll->descr->name,
                     ((++i) % 8) ? ' ' : '\n' );
    }
    fprintf(stderr,"\n");
    exit(1);
}

#endif  /* WINELIB */

/*
 * Module definitions
 *
 * Copyright 1995 Alexandre Julliard
 */

#ifndef _WINE_MODULE_H
#define _WINE_MODULE_H

#include "wintypes.h"
#include "pe_image.h"

#ifndef WINELIB
#pragma pack(1)
#endif

  /* In-memory module structure. See 'Windows Internals' p. 219 */
typedef struct
{
    WORD    magic;            /* 00 'NE' signature */
    WORD    count;            /* 02 Usage count */
    WORD    entry_table;      /* 04 Near ptr to entry table */
    HMODULE next;             /* 06 Selector to next module */
    WORD    dgroup_entry;     /* 08 Near ptr to segment entry for DGROUP */
    WORD    fileinfo;         /* 0a Near ptr to file info (OFSTRUCT) */
    WORD    flags;            /* 0c Module flags */
    WORD    dgroup;           /* 0e Logical segment for DGROUP */
    WORD    heap_size;        /* 10 Initial heap size */
    WORD    stack_size;       /* 12 Initial stack size */
    WORD    ip;               /* 14 Initial ip */
    WORD    cs;               /* 16 Initial cs (logical segment) */
    WORD    sp;               /* 18 Initial stack pointer */
    WORD    ss;               /* 1a Initial ss (logical segment) */
    WORD    seg_count;        /* 1c Number of segments in segment table */
    WORD    modref_count;     /* 1e Number of module references */
    WORD    nrname_size;      /* 20 Size of non-resident names table */
    WORD    seg_table;        /* 22 Near ptr to segment table */
    WORD    res_table;        /* 24 Near ptr to resource table */
    WORD    name_table;       /* 26 Near ptr to resident names table */
    WORD    modref_table;     /* 28 Near ptr to module reference table */
    WORD    import_table;     /* 2a Near ptr to imported names table */
    DWORD   nrname_fpos;      /* 2c File offset of non-resident names table */
    WORD    moveable_entries; /* 30 Number of moveable entries in entry table*/
    WORD    alignment;        /* 32 Alignment shift count */
    WORD    truetype;         /* 34 Set to 2 if TrueType font */
    BYTE    os_flags;         /* 36 Operating system flags */
    BYTE    misc_flags;       /* 37 Misc. flags */
    HANDLE  dlls_to_init;     /* 38 List of DLLs to initialize */
    HANDLE  nrname_handle;    /* 3a Handle to non-resident name table */
    WORD    min_swap_area;    /* 3c Min. swap area size */
    WORD    expected_version; /* 3e Expected Windows version */
    /* From here, these are extra fields not present in normal Windows */
    PE_MODULE *pe_module;     /* 40 PE module handle for Win32 modules */
    HMODULE self;             /* 44 Handle for this module */
    WORD    self_loading_sel; /* 46 Selector used for self-loading apps. */
} NE_MODULE;


  /* In-memory segment table */
typedef struct
{
    WORD    filepos;   /* Position in file, in sectors */
    WORD    size;      /* Segment size on disk */
    WORD    flags;     /* Segment flags */
    WORD    minsize;   /* Min. size of segment in memory */
    HANDLE  selector;  /* Selector of segment in memory */
} SEGTABLEENTRY;


  /* Self-loading modules contain this structure in their first segment */

typedef struct
{
    WORD    version;		/* Must be 0xA0 */
    WORD    reserved;   
    FARPROC BootApp;    	/* startup procedure */
    FARPROC LoadAppSeg; 	/* procedure to load a segment */
    FARPROC reserved2;
    FARPROC MyAlloc;     	/* memory allocation procedure, 
				 * wine must write this field */
    FARPROC EntryAddrProc;
    FARPROC ExitProc;		/* exit procedure */
    WORD    reserved3[4];
    FARPROC SetOwner;           /* Set Owner procedure, exported by wine */
} SELFLOADHEADER;

  /* Parameters for LoadModule() */

typedef struct
{
    HANDLE hEnvironment;  /* Environment segment */
    SEGPTR cmdLine;       /* Command-line */
    SEGPTR showCmd;       /* Code for ShowWindow() */
    SEGPTR reserved;
} LOADPARAMS;

/* Resource types */
typedef struct resource_typeinfo_s NE_TYPEINFO;
typedef struct resource_nameinfo_s NE_NAMEINFO;

#define NE_SEG_TABLE(pModule) \
    ((SEGTABLEENTRY *)((char *)(pModule) + (pModule)->seg_table))

#define NE_MODULE_TABLE(pModule) \
    ((WORD *)((char *)(pModule) + (pModule)->modref_table))

#define NE_MODULE_NAME(pModule) \
    (((OFSTRUCT *)((char*)(pModule) + (pModule)->fileinfo))->szPathName)

#define NE_WIN32_MODULE(pModule) \
    ((struct pe_data *)(((pModule)->flags & NE_FFLAGS_WIN32) ? \
                    ((NE_WIN32_EXTRAINFO *)((pModule) + 1))->pe_module : 0))

#ifndef WINELIB
#pragma pack(4)
#endif

/* module.c */
extern NE_MODULE *MODULE_GetPtr( HMODULE hModule );
extern void MODULE_DumpModule( HMODULE hmodule );
extern void MODULE_WalkModules(void);
extern int MODULE_OpenFile( HMODULE hModule );
extern LPSTR MODULE_GetModuleName( HMODULE hModule );
extern void MODULE_RegisterModule( NE_MODULE *pModule );
extern HINSTANCE MODULE_GetInstance( HMODULE hModule );
extern WORD MODULE_GetOrdinal( HMODULE hModule, const char *name );
extern SEGPTR MODULE_GetEntryPoint( HMODULE hModule, WORD ordinal );
extern BOOL MODULE_SetEntryPoint( HMODULE hModule, WORD ordinal, WORD offset );
extern FARPROC16 MODULE_GetWndProcEntry16( const char *name );

/* builtin.c */
extern BOOL BUILTIN_Init(void);
extern HMODULE BUILTIN_LoadModule( LPCSTR name, BOOL force );
extern NE_MODULE *BUILTIN_GetEntryPoint( WORD cs, WORD ip,
                                         WORD *pOrd, char **ppName );
extern DWORD BUILTIN_GetProcAddress32( NE_MODULE *pModule, char *function );
extern BOOL BUILTIN_ParseDLLOptions( const char *str );
extern void BUILTIN_PrintDLLs(void);

/* ne_image.c */
extern BOOL NE_LoadSegment( HMODULE hModule, WORD segnum );
extern void NE_FixupPrologs( NE_MODULE *pModule );
extern void NE_InitializeDLLs( HMODULE hModule );

#endif  /* _WINE_MODULE_H */

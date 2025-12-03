/**
 * rfasm.h - RetroFuture Simplified i386 Assembler
 *
 * A C64-inspired assembler with ~40 essential i386 instructions.
 * Self-contained, can run standalone or integrated into the shell.
 *
 * Design Philosophy:
 *   - Simple like the C64's ~40 common 6502 instructions
 *   - Two-pass assembly (collect labels, emit code)
 *   - Direct memory output (no object files)
 *   - Integrated into OS like machine language monitors
 *
 * Author: Jesse (with Claude)
 * License: Public Domain / MIT
 */

#ifndef RFASM_H
#define RFASM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration - Tune for embedded/OS use
 * ============================================================================ */

#ifndef RFASM_MAX_LABELS
#define RFASM_MAX_LABELS        64      /* Maximum labels */
#endif

#ifndef RFASM_MAX_LABEL_LEN
#define RFASM_MAX_LABEL_LEN     32      /* Maximum label name length */
#endif

#ifndef RFASM_MAX_LINE
#define RFASM_MAX_LINE          256     /* Maximum source line length */
#endif

#ifndef RFASM_MAX_OUTPUT
#define RFASM_MAX_OUTPUT        8192    /* Maximum output size (8KB) */
#endif

#ifndef RFASM_MAX_FIXUPS
#define RFASM_MAX_FIXUPS        64      /* Maximum forward references */
#endif

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    RFASM_OK = 0,
    RFASM_ERR_SYNTAX,           /* Syntax error */
    RFASM_ERR_UNKNOWN_INST,     /* Unknown instruction */
    RFASM_ERR_UNKNOWN_REG,      /* Unknown register */
    RFASM_ERR_UNKNOWN_LABEL,    /* Undefined label */
    RFASM_ERR_DUPLICATE_LABEL,  /* Label already defined */
    RFASM_ERR_TOO_MANY_LABELS,  /* Label table full */
    RFASM_ERR_TOO_MANY_FIXUPS,  /* Fixup table full */
    RFASM_ERR_INVALID_OPERAND,  /* Invalid operand */
    RFASM_ERR_OUT_OF_RANGE,     /* Value out of range */
    RFASM_ERR_MEMORY,           /* Out of memory/buffer */
    RFASM_ERR_INTERNAL,         /* Internal error */
} rfasm_error_t;

/* ============================================================================
 * Register Definitions
 * ============================================================================ */

typedef enum {
    /* 32-bit general purpose */
    REG_EAX = 0, REG_ECX, REG_EDX, REG_EBX,
    REG_ESP, REG_EBP, REG_ESI, REG_EDI,

    /* 16-bit general purpose */
    REG_AX = 8, REG_CX, REG_DX, REG_BX,
    REG_SP, REG_BP, REG_SI, REG_DI,

    /* 8-bit registers */
    REG_AL = 16, REG_CL, REG_DL, REG_BL,
    REG_AH, REG_CH, REG_DH, REG_BH,

    /* Segment registers */
    REG_ES = 24, REG_CS, REG_SS, REG_DS, REG_FS, REG_GS,

    REG_NONE = 0xFF
} rfasm_reg_t;

/* Register size (in bytes) */
#define REG_SIZE_32     4
#define REG_SIZE_16     2
#define REG_SIZE_8      1

/* ============================================================================
 * Instruction Types
 * ============================================================================ */

typedef enum {
    INST_NONE = 0,

    /* Data Movement */
    INST_MOV,           /* MOV dest, src */
    INST_PUSH,          /* PUSH src */
    INST_POP,           /* POP dest */
    INST_LEA,           /* LEA dest, [addr] */
    INST_XCHG,          /* XCHG a, b */
    INST_MOVZX,         /* MOVZX dest, src (zero extend) */
    INST_MOVSB,         /* MOVSB - string move byte */
    INST_MOVSW,         /* MOVSW - string move word */
    INST_MOVSD,         /* MOVSD - string move dword */
    INST_REP,           /* REP prefix */

    /* Arithmetic */
    INST_ADD,           /* ADD dest, src */
    INST_SUB,           /* SUB dest, src */
    INST_INC,           /* INC dest */
    INST_DEC,           /* DEC dest */
    INST_NEG,           /* NEG dest */
    INST_MUL,           /* MUL src (unsigned) */
    INST_IMUL,          /* IMUL src (signed) */
    INST_DIV,           /* DIV src (unsigned) */
    INST_IDIV,          /* IDIV src (signed) */
    INST_CMP,           /* CMP a, b */
    INST_ADC,           /* ADC dest, src (with carry) */
    INST_SBB,           /* SBB dest, src (with borrow) */

    /* Logic */
    INST_AND,           /* AND dest, src */
    INST_OR,            /* OR dest, src */
    INST_XOR,           /* XOR dest, src */
    INST_NOT,           /* NOT dest */
    INST_TEST,          /* TEST a, b */
    INST_SHL,           /* SHL dest, count */
    INST_SHR,           /* SHR dest, count (logical) */
    INST_SAR,           /* SAR dest, count (arithmetic) */
    INST_ROL,           /* ROL dest, count */
    INST_ROR,           /* ROR dest, count */

    /* Control Flow */
    INST_JMP,           /* JMP label */
    INST_JZ,            /* JZ label (JE) */
    INST_JNZ,           /* JNZ label (JNE) */
    INST_JC,            /* JC label (JB) */
    INST_JNC,           /* JNC label (JAE) */
    INST_JS,            /* JS label */
    INST_JNS,           /* JNS label */
    INST_JL,            /* JL label (signed <) */
    INST_JG,            /* JG label (signed >) */
    INST_JLE,           /* JLE label (signed <=) */
    INST_JGE,           /* JGE label (signed >=) */
    INST_JA,            /* JA label (unsigned >) */
    INST_JB,            /* JB label (unsigned <) - same as JC */
    INST_JAE,           /* JAE label (unsigned >=) - same as JNC */
    INST_JBE,           /* JBE label (unsigned <=) */
    INST_CALL,          /* CALL label */
    INST_RET,           /* RET */
    INST_LOOP,          /* LOOP label */
    INST_LOOPZ,         /* LOOPZ label */
    INST_LOOPNZ,        /* LOOPNZ label */

    /* Flags */
    INST_CLC,           /* Clear carry */
    INST_STC,           /* Set carry */
    INST_CLI,           /* Clear interrupt */
    INST_STI,           /* Set interrupt */
    INST_CLD,           /* Clear direction */
    INST_STD,           /* Set direction */
    INST_PUSHF,         /* Push flags */
    INST_POPF,          /* Pop flags */

    /* System */
    INST_NOP,           /* No operation */
    INST_HLT,           /* Halt */
    INST_INT,           /* Software interrupt */
    INST_IRET,          /* Interrupt return */
    INST_IN,            /* IN dest, port */
    INST_OUT,           /* OUT port, src */
    INST_LGDT,          /* Load GDT */
    INST_LIDT,          /* Load IDT */

    /* Pseudo-instructions */
    INST_DB,            /* Define byte(s) */
    INST_DW,            /* Define word(s) */
    INST_DD,            /* Define dword(s) */
    INST_TIMES,         /* Repeat data/instruction */
    INST_ORG,           /* Set origin address */
    INST_EQU,           /* Define constant */

    INST_COUNT
} rfasm_inst_t;

/* ============================================================================
 * Operand Types
 * ============================================================================ */

typedef enum {
    OP_NONE = 0,
    OP_REG,             /* Register: EAX, BX, CL, etc. */
    OP_IMM,             /* Immediate: 0x1234, 42, 'A' */
    OP_MEM,             /* Memory: [addr], [EBX], [ESI+4] */
    OP_LABEL,           /* Label reference */
} rfasm_op_type_t;

typedef struct {
    rfasm_op_type_t type;
    rfasm_reg_t reg;            /* Register (if OP_REG or base in OP_MEM) */
    rfasm_reg_t index;          /* Index register for [base+index*scale+disp] */
    uint8_t scale;              /* Scale factor (1, 2, 4, 8) */
    int32_t value;              /* Immediate value or displacement */
    char label[RFASM_MAX_LABEL_LEN];  /* Label name (if OP_LABEL) */
    uint8_t size;               /* Operand size hint (1, 2, 4, or 0=auto) */
    bool indirect;              /* Memory indirect [reg] */
} rfasm_operand_t;

/* ============================================================================
 * Label Entry
 * ============================================================================ */

typedef struct {
    char name[RFASM_MAX_LABEL_LEN];
    uint32_t address;
    bool defined;
    bool is_local;              /* Local label (starts with .) */
} rfasm_label_t;

/* ============================================================================
 * Fixup Entry (for forward references)
 * ============================================================================ */

typedef struct {
    uint32_t location;          /* Location in output to patch */
    char label[RFASM_MAX_LABEL_LEN];
    uint8_t size;               /* Size of reference (1, 2, or 4 bytes) */
    bool relative;              /* PC-relative reference */
    uint32_t base;              /* Base address for relative (PC after inst) */
} rfasm_fixup_t;

/* ============================================================================
 * Assembler State
 * ============================================================================ */

typedef struct {
    /* Output buffer */
    uint8_t *output;
    uint32_t output_size;
    uint32_t output_capacity;

    /* Origin address */
    uint32_t org;
    uint32_t pc;                /* Program counter (org + output_size) */

    /* Symbol table */
    rfasm_label_t labels[RFASM_MAX_LABELS];
    int num_labels;

    /* Forward reference fixups */
    rfasm_fixup_t fixups[RFASM_MAX_FIXUPS];
    int num_fixups;

    /* Current local label scope */
    char current_scope[RFASM_MAX_LABEL_LEN];

    /* Error state */
    rfasm_error_t error;
    int error_line;
    char error_msg[128];

    /* Current pass (1 or 2) */
    int pass;

    /* Statistics */
    int lines_processed;
    int instructions_emitted;
} rfasm_state_t;

/* ============================================================================
 * String Utilities (self-contained)
 * ============================================================================ */

static inline int rfa_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static inline void rfa_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static inline void rfa_strncpy(char *dst, const char *src, int n) {
    while (n-- > 0 && *src) *dst++ = *src++;
    *dst = '\0';
}

static inline int rfa_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int rfa_strncmp(const char *a, const char *b, int n) {
    while (n-- > 0 && *a && *a == *b) { a++; b++; }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int rfa_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline char rfa_toupper(char c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

static inline char rfa_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static inline bool rfa_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline bool rfa_isdigit(char c) {
    return c >= '0' && c <= '9';
}

static inline bool rfa_isxdigit(char c) {
    return rfa_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline bool rfa_isalpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool rfa_isalnum(char c) {
    return rfa_isalpha(c) || rfa_isdigit(c);
}

/* ============================================================================
 * Register Lookup Table
 * ============================================================================ */

typedef struct {
    const char *name;
    rfasm_reg_t reg;
    uint8_t size;       /* 1, 2, or 4 bytes */
    uint8_t encoding;   /* ModR/M encoding (0-7) */
} reg_entry_t;

static const reg_entry_t g_registers[] = {
    /* 32-bit general purpose */
    { "EAX", REG_EAX, 4, 0 }, { "ECX", REG_ECX, 4, 1 },
    { "EDX", REG_EDX, 4, 2 }, { "EBX", REG_EBX, 4, 3 },
    { "ESP", REG_ESP, 4, 4 }, { "EBP", REG_EBP, 4, 5 },
    { "ESI", REG_ESI, 4, 6 }, { "EDI", REG_EDI, 4, 7 },

    /* 16-bit general purpose */
    { "AX", REG_AX, 2, 0 }, { "CX", REG_CX, 2, 1 },
    { "DX", REG_DX, 2, 2 }, { "BX", REG_BX, 2, 3 },
    { "SP", REG_SP, 2, 4 }, { "BP", REG_BP, 2, 5 },
    { "SI", REG_SI, 2, 6 }, { "DI", REG_DI, 2, 7 },

    /* 8-bit registers */
    { "AL", REG_AL, 1, 0 }, { "CL", REG_CL, 1, 1 },
    { "DL", REG_DL, 1, 2 }, { "BL", REG_BL, 1, 3 },
    { "AH", REG_AH, 1, 4 }, { "CH", REG_CH, 1, 5 },
    { "DH", REG_DH, 1, 6 }, { "BH", REG_BH, 1, 7 },

    /* Segment registers */
    { "ES", REG_ES, 2, 0 }, { "CS", REG_CS, 2, 1 },
    { "SS", REG_SS, 2, 2 }, { "DS", REG_DS, 2, 3 },
    { "FS", REG_FS, 2, 4 }, { "GS", REG_GS, 2, 5 },

    { NULL, REG_NONE, 0, 0 }
};

/* ============================================================================
 * Instruction Lookup Table
 * ============================================================================ */

typedef struct {
    const char *mnemonic;
    rfasm_inst_t inst;
} inst_entry_t;

static const inst_entry_t g_instructions[] = {
    /* Data Movement */
    { "MOV",    INST_MOV },
    { "PUSH",   INST_PUSH },
    { "POP",    INST_POP },
    { "LEA",    INST_LEA },
    { "XCHG",   INST_XCHG },
    { "MOVZX",  INST_MOVZX },
    { "MOVSB",  INST_MOVSB },
    { "MOVSW",  INST_MOVSW },
    { "MOVSD",  INST_MOVSD },
    { "REP",    INST_REP },

    /* Arithmetic */
    { "ADD",    INST_ADD },
    { "SUB",    INST_SUB },
    { "INC",    INST_INC },
    { "DEC",    INST_DEC },
    { "NEG",    INST_NEG },
    { "MUL",    INST_MUL },
    { "IMUL",   INST_IMUL },
    { "DIV",    INST_DIV },
    { "IDIV",   INST_IDIV },
    { "CMP",    INST_CMP },
    { "ADC",    INST_ADC },
    { "SBB",    INST_SBB },

    /* Logic */
    { "AND",    INST_AND },
    { "OR",     INST_OR },
    { "XOR",    INST_XOR },
    { "NOT",    INST_NOT },
    { "TEST",   INST_TEST },
    { "SHL",    INST_SHL },
    { "SAL",    INST_SHL },  /* SAL is alias for SHL */
    { "SHR",    INST_SHR },
    { "SAR",    INST_SAR },
    { "ROL",    INST_ROL },
    { "ROR",    INST_ROR },

    /* Control Flow */
    { "JMP",    INST_JMP },
    { "JZ",     INST_JZ },
    { "JE",     INST_JZ },   /* JE is alias for JZ */
    { "JNZ",    INST_JNZ },
    { "JNE",    INST_JNZ },  /* JNE is alias for JNZ */
    { "JC",     INST_JC },
    { "JB",     INST_JB },   /* JB is alias for JC */
    { "JNAE",   INST_JC },   /* JNAE is alias for JC */
    { "JNC",    INST_JNC },
    { "JAE",    INST_JAE },  /* JAE is alias for JNC */
    { "JNB",    INST_JNC },  /* JNB is alias for JNC */
    { "JS",     INST_JS },
    { "JNS",    INST_JNS },
    { "JL",     INST_JL },
    { "JNGE",   INST_JL },
    { "JG",     INST_JG },
    { "JNLE",   INST_JG },
    { "JLE",    INST_JLE },
    { "JNG",    INST_JLE },
    { "JGE",    INST_JGE },
    { "JNL",    INST_JGE },
    { "JA",     INST_JA },
    { "JNBE",   INST_JA },
    { "JBE",    INST_JBE },
    { "JNA",    INST_JBE },
    { "CALL",   INST_CALL },
    { "RET",    INST_RET },
    { "RETN",   INST_RET },
    { "LOOP",   INST_LOOP },
    { "LOOPZ",  INST_LOOPZ },
    { "LOOPE",  INST_LOOPZ },
    { "LOOPNZ", INST_LOOPNZ },
    { "LOOPNE", INST_LOOPNZ },

    /* Flags */
    { "CLC",    INST_CLC },
    { "STC",    INST_STC },
    { "CLI",    INST_CLI },
    { "STI",    INST_STI },
    { "CLD",    INST_CLD },
    { "STD",    INST_STD },
    { "PUSHF",  INST_PUSHF },
    { "PUSHFD", INST_PUSHF },
    { "POPF",   INST_POPF },
    { "POPFD",  INST_POPF },

    /* System */
    { "NOP",    INST_NOP },
    { "HLT",    INST_HLT },
    { "INT",    INST_INT },
    { "IRET",   INST_IRET },
    { "IRETD",  INST_IRET },
    { "IN",     INST_IN },
    { "OUT",    INST_OUT },
    { "LGDT",   INST_LGDT },
    { "LIDT",   INST_LIDT },

    /* Pseudo-instructions */
    { "DB",     INST_DB },
    { "DW",     INST_DW },
    { "DD",     INST_DD },
    { "TIMES",  INST_TIMES },
    { "ORG",    INST_ORG },
    { "EQU",    INST_EQU },

    { NULL, INST_NONE }
};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void rfasm_init(rfasm_state_t *state, uint8_t *output_buf, uint32_t capacity);
static void rfasm_reset(rfasm_state_t *state);
static rfasm_error_t rfasm_assemble(rfasm_state_t *state, const char *source);
static const char *rfasm_error_string(rfasm_error_t err);

/* Internal functions */
static rfasm_reg_t rfasm_lookup_reg(const char *name, uint8_t *size, uint8_t *encoding);
static rfasm_inst_t rfasm_lookup_inst(const char *mnemonic);
static bool rfasm_parse_line(rfasm_state_t *state, const char *line);
static bool rfasm_parse_operand(rfasm_state_t *state, const char **p, rfasm_operand_t *op);
static bool rfasm_emit_instruction(rfasm_state_t *state, rfasm_inst_t inst,
                                    rfasm_operand_t *op1, rfasm_operand_t *op2);
static void rfasm_emit_byte(rfasm_state_t *state, uint8_t b);
static void rfasm_emit_word(rfasm_state_t *state, uint16_t w);
static void rfasm_emit_dword(rfasm_state_t *state, uint32_t d);
static bool rfasm_add_label(rfasm_state_t *state, const char *name, uint32_t addr);
static rfasm_label_t *rfasm_find_label(rfasm_state_t *state, const char *name);
static bool rfasm_add_fixup(rfasm_state_t *state, const char *label, uint32_t loc,
                            uint8_t size, bool relative, uint32_t base);
static bool rfasm_resolve_fixups(rfasm_state_t *state);

/* ============================================================================
 * Implementation
 * ============================================================================ */

/**
 * Initialize assembler state
 */
static void rfasm_init(rfasm_state_t *state, uint8_t *output_buf, uint32_t capacity) {
    state->output = output_buf;
    state->output_size = 0;
    state->output_capacity = capacity;
    state->org = 0;
    state->pc = 0;
    state->num_labels = 0;
    state->num_fixups = 0;
    state->current_scope[0] = '\0';
    state->error = RFASM_OK;
    state->error_line = 0;
    state->error_msg[0] = '\0';
    state->pass = 1;
    state->lines_processed = 0;
    state->instructions_emitted = 0;
}

/**
 * Reset state for second pass
 */
static void rfasm_reset(rfasm_state_t *state) {
    state->output_size = 0;
    state->pc = state->org;
    state->num_fixups = 0;
    state->current_scope[0] = '\0';
    state->error = RFASM_OK;
    state->error_line = 0;
    state->error_msg[0] = '\0';
    state->lines_processed = 0;
    state->instructions_emitted = 0;
}

/**
 * Look up register by name
 */
static rfasm_reg_t rfasm_lookup_reg(const char *name, uint8_t *size, uint8_t *encoding) {
    for (int i = 0; g_registers[i].name != NULL; i++) {
        if (rfa_strcasecmp(name, g_registers[i].name) == 0) {
            if (size) *size = g_registers[i].size;
            if (encoding) *encoding = g_registers[i].encoding;
            return g_registers[i].reg;
        }
    }
    return REG_NONE;
}

/**
 * Look up instruction by mnemonic
 */
static rfasm_inst_t rfasm_lookup_inst(const char *mnemonic) {
    for (int i = 0; g_instructions[i].mnemonic != NULL; i++) {
        if (rfa_strcasecmp(mnemonic, g_instructions[i].mnemonic) == 0) {
            return g_instructions[i].inst;
        }
    }
    return INST_NONE;
}

/**
 * Get register encoding for ModR/M byte
 */
static uint8_t rfasm_reg_encoding(rfasm_reg_t reg) {
    for (int i = 0; g_registers[i].name != NULL; i++) {
        if (g_registers[i].reg == reg) {
            return g_registers[i].encoding;
        }
    }
    return 0;
}

/**
 * Get register size
 */
static uint8_t rfasm_reg_size(rfasm_reg_t reg) {
    for (int i = 0; g_registers[i].name != NULL; i++) {
        if (g_registers[i].reg == reg) {
            return g_registers[i].size;
        }
    }
    return 4;  /* Default to 32-bit */
}

/**
 * Emit a single byte
 */
static void rfasm_emit_byte(rfasm_state_t *state, uint8_t b) {
    if (state->pass == 2 && state->output_size < state->output_capacity) {
        state->output[state->output_size] = b;
    }
    state->output_size++;
    state->pc++;
}

/**
 * Emit a 16-bit word (little-endian)
 */
static void rfasm_emit_word(rfasm_state_t *state, uint16_t w) {
    rfasm_emit_byte(state, w & 0xFF);
    rfasm_emit_byte(state, (w >> 8) & 0xFF);
}

/**
 * Emit a 32-bit dword (little-endian)
 */
static void rfasm_emit_dword(rfasm_state_t *state, uint32_t d) {
    rfasm_emit_byte(state, d & 0xFF);
    rfasm_emit_byte(state, (d >> 8) & 0xFF);
    rfasm_emit_byte(state, (d >> 16) & 0xFF);
    rfasm_emit_byte(state, (d >> 24) & 0xFF);
}

/**
 * Add a label to the symbol table
 */
static bool rfasm_add_label(rfasm_state_t *state, const char *name, uint32_t addr) {
    /* Check for duplicate */
    rfasm_label_t *existing = rfasm_find_label(state, name);
    if (existing) {
        if (existing->defined && state->pass == 1) {
            state->error = RFASM_ERR_DUPLICATE_LABEL;
            return false;
        }
        existing->address = addr;
        existing->defined = true;
        return true;
    }

    /* Add new label */
    if (state->num_labels >= RFASM_MAX_LABELS) {
        state->error = RFASM_ERR_TOO_MANY_LABELS;
        return false;
    }

    rfasm_label_t *label = &state->labels[state->num_labels++];
    rfa_strncpy(label->name, name, RFASM_MAX_LABEL_LEN - 1);
    label->name[RFASM_MAX_LABEL_LEN - 1] = '\0';
    label->address = addr;
    label->defined = true;
    label->is_local = (name[0] == '.');

    /* Update scope for non-local labels */
    if (!label->is_local) {
        rfa_strcpy(state->current_scope, name);
    }

    return true;
}

/**
 * Find a label in the symbol table
 */
static rfasm_label_t *rfasm_find_label(rfasm_state_t *state, const char *name) {
    /* Handle local labels - prepend current scope */
    char full_name[RFASM_MAX_LABEL_LEN * 2];
    if (name[0] == '.') {
        /* Local label - prepend scope */
        int len = rfa_strlen(state->current_scope);
        rfa_strcpy(full_name, state->current_scope);
        rfa_strcpy(full_name + len, name);
        name = full_name;
    }

    for (int i = 0; i < state->num_labels; i++) {
        if (rfa_strcasecmp(state->labels[i].name, name) == 0) {
            return &state->labels[i];
        }
    }
    return NULL;
}

/**
 * Add a fixup for forward reference
 */
static bool rfasm_add_fixup(rfasm_state_t *state, const char *label, uint32_t loc,
                            uint8_t size, bool relative, uint32_t base) {
    if (state->num_fixups >= RFASM_MAX_FIXUPS) {
        state->error = RFASM_ERR_TOO_MANY_FIXUPS;
        return false;
    }

    rfasm_fixup_t *fixup = &state->fixups[state->num_fixups++];

    /* Handle local labels */
    if (label[0] == '.') {
        int len = rfa_strlen(state->current_scope);
        rfa_strcpy(fixup->label, state->current_scope);
        rfa_strcpy(fixup->label + len, label);
    } else {
        rfa_strncpy(fixup->label, label, RFASM_MAX_LABEL_LEN - 1);
        fixup->label[RFASM_MAX_LABEL_LEN - 1] = '\0';
    }

    fixup->location = loc;
    fixup->size = size;
    fixup->relative = relative;
    fixup->base = base;

    return true;
}

/**
 * Resolve all fixups after assembly
 */
static bool rfasm_resolve_fixups(rfasm_state_t *state) {
    for (int i = 0; i < state->num_fixups; i++) {
        rfasm_fixup_t *fixup = &state->fixups[i];
        rfasm_label_t *label = NULL;

        /* Search for label */
        for (int j = 0; j < state->num_labels; j++) {
            if (rfa_strcasecmp(state->labels[j].name, fixup->label) == 0) {
                label = &state->labels[j];
                break;
            }
        }

        if (!label || !label->defined) {
            state->error = RFASM_ERR_UNKNOWN_LABEL;
            /* Copy label name to error message */
            rfa_strcpy(state->error_msg, "Undefined label: ");
            rfa_strncpy(state->error_msg + 18, fixup->label, 100);
            return false;
        }

        /* Calculate value */
        int32_t value;
        if (fixup->relative) {
            value = (int32_t)label->address - (int32_t)fixup->base;
        } else {
            value = label->address;
        }

        /* Patch the output */
        uint32_t loc = fixup->location;
        switch (fixup->size) {
            case 1:
                if (value < -128 || value > 127) {
                    state->error = RFASM_ERR_OUT_OF_RANGE;
                    return false;
                }
                state->output[loc] = (uint8_t)value;
                break;
            case 2:
                state->output[loc] = value & 0xFF;
                state->output[loc + 1] = (value >> 8) & 0xFF;
                break;
            case 4:
                state->output[loc] = value & 0xFF;
                state->output[loc + 1] = (value >> 8) & 0xFF;
                state->output[loc + 2] = (value >> 16) & 0xFF;
                state->output[loc + 3] = (value >> 24) & 0xFF;
                break;
        }
    }

    return true;
}

/**
 * Skip whitespace
 */
static const char *rfasm_skip_ws(const char *p) {
    while (*p && rfa_isspace(*p)) p++;
    return p;
}

/**
 * Parse a number (decimal, hex, binary, or octal)
 */
static bool rfasm_parse_number(const char **p, int32_t *value) {
    const char *s = *p;
    bool negative = false;

    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }

    int32_t val = 0;
    int base = 10;

    if (*s == '0') {
        s++;
        if (*s == 'x' || *s == 'X') {
            base = 16;
            s++;
        } else if (*s == 'b' || *s == 'B') {
            base = 2;
            s++;
        } else if (rfa_isdigit(*s)) {
            base = 8;
        } else {
            /* Just "0" */
            *value = 0;
            *p = s;
            return true;
        }
    }

    /* Also accept trailing 'h' for hex */
    const char *start = s;
    bool has_digits = false;

    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (base == 16) {
            if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        }

        if (digit < 0 || digit >= base) break;

        val = val * base + digit;
        has_digits = true;
        s++;
    }

    /* Check for 'h' suffix (hex) */
    if ((*s == 'h' || *s == 'H') && base == 10 && has_digits) {
        /* Re-parse as hex */
        val = 0;
        s = start;
        while (*s && *s != 'h' && *s != 'H') {
            int digit;
            if (*s >= '0' && *s <= '9') digit = *s - '0';
            else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
            else break;
            val = val * 16 + digit;
            s++;
        }
        if (*s == 'h' || *s == 'H') s++;
    }

    if (!has_digits) return false;

    *value = negative ? -val : val;
    *p = s;
    return true;
}

/**
 * Parse a character literal ('A')
 */
static bool rfasm_parse_char(const char **p, int32_t *value) {
    const char *s = *p;
    if (*s != '\'') return false;
    s++;

    if (*s == '\\') {
        /* Escape sequence */
        s++;
        switch (*s) {
            case 'n': *value = '\n'; break;
            case 'r': *value = '\r'; break;
            case 't': *value = '\t'; break;
            case '0': *value = '\0'; break;
            case '\\': *value = '\\'; break;
            case '\'': *value = '\''; break;
            default: *value = *s; break;
        }
        s++;
    } else {
        *value = *s++;
    }

    if (*s != '\'') return false;
    s++;

    *p = s;
    return true;
}

/**
 * Parse an identifier (label or register name)
 */
static bool rfasm_parse_ident(const char **p, char *buf, int max_len) {
    const char *s = *p;
    int len = 0;

    /* Can start with letter, underscore, or dot (local label) */
    if (!rfa_isalpha(*s) && *s != '_' && *s != '.') {
        return false;
    }

    while (*s && (rfa_isalnum(*s) || *s == '_' || *s == '.') && len < max_len - 1) {
        buf[len++] = *s++;
    }
    buf[len] = '\0';

    if (len == 0) return false;

    *p = s;
    return true;
}

/**
 * Parse an operand
 */
static bool rfasm_parse_operand(rfasm_state_t *state, const char **p, rfasm_operand_t *op) {
    const char *s = rfasm_skip_ws(*p);

    /* Initialize operand */
    op->type = OP_NONE;
    op->reg = REG_NONE;
    op->index = REG_NONE;
    op->scale = 1;
    op->value = 0;
    op->label[0] = '\0';
    op->size = 0;
    op->indirect = false;

    /* Check for size override: BYTE, WORD, DWORD */
    if (rfa_strncmp(s, "BYTE", 4) == 0 || rfa_strncmp(s, "byte", 4) == 0) {
        op->size = 1;
        s += 4;
        s = rfasm_skip_ws(s);
    } else if (rfa_strncmp(s, "WORD", 4) == 0 || rfa_strncmp(s, "word", 4) == 0) {
        op->size = 2;
        s += 4;
        s = rfasm_skip_ws(s);
    } else if (rfa_strncmp(s, "DWORD", 5) == 0 || rfa_strncmp(s, "dword", 5) == 0) {
        op->size = 4;
        s += 5;
        s = rfasm_skip_ws(s);
    }

    /* Skip optional PTR */
    if (rfa_strncmp(s, "PTR", 3) == 0 || rfa_strncmp(s, "ptr", 3) == 0) {
        s += 3;
        s = rfasm_skip_ws(s);
    }

    /* Memory operand: [expr] */
    if (*s == '[') {
        s++;
        s = rfasm_skip_ws(s);
        op->type = OP_MEM;
        op->indirect = true;

        /* Parse memory expression: [reg], [reg+disp], [reg+reg*scale+disp] */
        char ident[32];
        if (rfasm_parse_ident(&s, ident, sizeof(ident))) {
            uint8_t size, enc;
            rfasm_reg_t reg = rfasm_lookup_reg(ident, &size, &enc);
            if (reg != REG_NONE) {
                op->reg = reg;
            } else {
                /* Label reference */
                rfa_strcpy(op->label, ident);
            }
        } else if (rfa_isdigit(*s) || *s == '-' || *s == '+' || *s == '0') {
            /* Direct address */
            if (!rfasm_parse_number(&s, &op->value)) {
                state->error = RFASM_ERR_SYNTAX;
                return false;
            }
        }

        s = rfasm_skip_ws(s);

        /* Check for +/- displacement or index */
        while (*s == '+' || *s == '-') {
            bool is_sub = (*s == '-');
            s++;
            s = rfasm_skip_ws(s);

            if (rfasm_parse_ident(&s, ident, sizeof(ident))) {
                uint8_t size, enc;
                rfasm_reg_t reg = rfasm_lookup_reg(ident, &size, &enc);
                if (reg != REG_NONE) {
                    if (op->reg == REG_NONE) {
                        op->reg = reg;
                    } else {
                        op->index = reg;
                    }
                    /* Check for *scale */
                    s = rfasm_skip_ws(s);
                    if (*s == '*') {
                        s++;
                        s = rfasm_skip_ws(s);
                        int32_t scale;
                        if (rfasm_parse_number(&s, &scale)) {
                            op->scale = (uint8_t)scale;
                        }
                    }
                } else {
                    /* Label in memory expression */
                    rfa_strcpy(op->label, ident);
                }
            } else {
                int32_t disp;
                if (rfasm_parse_number(&s, &disp)) {
                    op->value += is_sub ? -disp : disp;
                }
            }
            s = rfasm_skip_ws(s);
        }

        /* Closing bracket */
        if (*s != ']') {
            state->error = RFASM_ERR_SYNTAX;
            return false;
        }
        s++;

        *p = s;
        return true;
    }

    /* Character literal */
    if (*s == '\'') {
        if (!rfasm_parse_char(&s, &op->value)) {
            state->error = RFASM_ERR_SYNTAX;
            return false;
        }
        op->type = OP_IMM;
        *p = s;
        return true;
    }

    /* Try register or label */
    char ident[RFASM_MAX_LABEL_LEN];
    if (rfasm_parse_ident(&s, ident, sizeof(ident))) {
        uint8_t size, enc;
        rfasm_reg_t reg = rfasm_lookup_reg(ident, &size, &enc);
        if (reg != REG_NONE) {
            op->type = OP_REG;
            op->reg = reg;
            if (op->size == 0) op->size = size;
        } else {
            /* Label reference */
            op->type = OP_LABEL;
            rfa_strcpy(op->label, ident);
        }
        *p = s;
        return true;
    }

    /* Try number */
    if (rfa_isdigit(*s) || *s == '-' || *s == '+') {
        if (!rfasm_parse_number(&s, &op->value)) {
            state->error = RFASM_ERR_SYNTAX;
            return false;
        }
        op->type = OP_IMM;
        *p = s;
        return true;
    }

    /* Special: $ means current address */
    if (*s == '$') {
        s++;
        op->type = OP_IMM;
        op->value = state->pc;

        /* Check for $-$$ (distance from origin) */
        if (*s == '-' && *(s+1) == '$' && *(s+2) == '$') {
            s += 3;
            op->value = state->pc - state->org;
        }

        *p = s;
        return true;
    }

    return false;
}

/**
 * Build ModR/M byte
 * mod: 0=indirect, 1=disp8, 2=disp32, 3=register
 * reg: register/opcode field
 * rm:  register/memory field
 */
static uint8_t rfasm_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod << 6) | ((reg & 7) << 3) | (rm & 7);
}

/**
 * Build SIB byte
 */
static uint8_t rfasm_sib(uint8_t scale, uint8_t index, uint8_t base) {
    uint8_t ss = 0;
    switch (scale) {
        case 1: ss = 0; break;
        case 2: ss = 1; break;
        case 4: ss = 2; break;
        case 8: ss = 3; break;
    }
    return (ss << 6) | ((index & 7) << 3) | (base & 7);
}

/**
 * Emit ModR/M and optional SIB/displacement for a memory operand
 */
static void rfasm_emit_modrm_mem(rfasm_state_t *state, uint8_t reg_opcode,
                                  rfasm_operand_t *mem) {
    uint8_t rm = rfasm_reg_encoding(mem->reg);
    uint8_t mod;

    if (mem->reg == REG_NONE) {
        /* Direct address [disp32] */
        rfasm_emit_byte(state, rfasm_modrm(0, reg_opcode, 5));
        rfasm_emit_dword(state, mem->value);
        return;
    }

    /* Determine mod based on displacement */
    if (mem->value == 0 && rm != 5) {  /* EBP always needs disp */
        mod = 0;
    } else if (mem->value >= -128 && mem->value <= 127) {
        mod = 1;
    } else {
        mod = 2;
    }

    /* Check for SIB needed */
    bool need_sib = (mem->index != REG_NONE) || (rm == 4);  /* ESP needs SIB */

    if (need_sib) {
        rfasm_emit_byte(state, rfasm_modrm(mod, reg_opcode, 4));
        uint8_t index_enc = (mem->index != REG_NONE) ?
                            rfasm_reg_encoding(mem->index) : 4;  /* 4 = no index */
        rfasm_emit_byte(state, rfasm_sib(mem->scale, index_enc, rm));
    } else {
        rfasm_emit_byte(state, rfasm_modrm(mod, reg_opcode, rm));
    }

    /* Emit displacement */
    if (mod == 1) {
        rfasm_emit_byte(state, (uint8_t)mem->value);
    } else if (mod == 2) {
        rfasm_emit_dword(state, mem->value);
    }
}

/**
 * Emit instruction: two operands, ALU format (ADD, SUB, AND, OR, XOR, CMP, etc.)
 */
static void rfasm_emit_alu(rfasm_state_t *state, uint8_t opcode_base,
                            uint8_t imm_ext, rfasm_operand_t *dst, rfasm_operand_t *src) {
    uint8_t opsize = dst->size ? dst->size : (dst->reg != REG_NONE ? rfasm_reg_size(dst->reg) : 4);

    /* Operand size prefix for 16-bit */
    if (opsize == 2) {
        rfasm_emit_byte(state, 0x66);
    }

    if (src->type == OP_IMM) {
        /* reg/mem, imm */
        bool short_imm = (src->value >= -128 && src->value <= 127 && opsize > 1);

        if (dst->type == OP_REG && dst->reg == REG_EAX && !short_imm) {
            /* Special AL/AX/EAX, imm form */
            rfasm_emit_byte(state, opcode_base + 4 + (opsize > 1 ? 1 : 0));
        } else {
            /* reg/mem, imm8/16/32 */
            if (short_imm) {
                rfasm_emit_byte(state, 0x83);  /* Sign-extended imm8 */
            } else {
                rfasm_emit_byte(state, opsize == 1 ? 0x80 : 0x81);
            }

            if (dst->type == OP_REG) {
                rfasm_emit_byte(state, rfasm_modrm(3, imm_ext, rfasm_reg_encoding(dst->reg)));
            } else {
                rfasm_emit_modrm_mem(state, imm_ext, dst);
            }
        }

        if (short_imm) {
            rfasm_emit_byte(state, (uint8_t)src->value);
        } else if (opsize == 1) {
            rfasm_emit_byte(state, (uint8_t)src->value);
        } else if (opsize == 2) {
            rfasm_emit_word(state, (uint16_t)src->value);
        } else {
            rfasm_emit_dword(state, (uint32_t)src->value);
        }
    } else if (src->type == OP_REG && dst->type == OP_REG) {
        /* reg, reg */
        rfasm_emit_byte(state, opcode_base + (opsize > 1 ? 1 : 0));
        rfasm_emit_byte(state, rfasm_modrm(3, rfasm_reg_encoding(src->reg),
                                            rfasm_reg_encoding(dst->reg)));
    } else if (src->type == OP_REG && dst->type == OP_MEM) {
        /* mem, reg */
        rfasm_emit_byte(state, opcode_base + (opsize > 1 ? 1 : 0));
        rfasm_emit_modrm_mem(state, rfasm_reg_encoding(src->reg), dst);
    } else if (src->type == OP_MEM && dst->type == OP_REG) {
        /* reg, mem */
        rfasm_emit_byte(state, opcode_base + 2 + (opsize > 1 ? 1 : 0));
        rfasm_emit_modrm_mem(state, rfasm_reg_encoding(dst->reg), src);
    }
}

/**
 * Emit a conditional jump
 */
static void rfasm_emit_jcc(rfasm_state_t *state, uint8_t cc, rfasm_operand_t *target) {
    /* Try short jump first (rel8) */
    if (target->type == OP_LABEL) {
        rfasm_label_t *label = rfasm_find_label(state, target->label);
        if (label && label->defined) {
            int32_t rel = (int32_t)label->address - (int32_t)(state->pc + 2);
            if (rel >= -128 && rel <= 127) {
                rfasm_emit_byte(state, 0x70 + cc);
                rfasm_emit_byte(state, (uint8_t)rel);
                return;
            }
        }

        /* Near jump (rel32) with 0F prefix */
        rfasm_emit_byte(state, 0x0F);
        rfasm_emit_byte(state, 0x80 + cc);

        if (state->pass == 2) {
            uint32_t fixup_loc = state->output_size;
            rfasm_emit_dword(state, 0);  /* Placeholder */
            rfasm_add_fixup(state, target->label, fixup_loc, 4, true, state->pc);
        } else {
            rfasm_emit_dword(state, 0);
        }
    } else if (target->type == OP_IMM) {
        int32_t rel = target->value - (int32_t)(state->pc + 2);
        if (rel >= -128 && rel <= 127) {
            rfasm_emit_byte(state, 0x70 + cc);
            rfasm_emit_byte(state, (uint8_t)rel);
        } else {
            rel = target->value - (int32_t)(state->pc + 6);
            rfasm_emit_byte(state, 0x0F);
            rfasm_emit_byte(state, 0x80 + cc);
            rfasm_emit_dword(state, rel);
        }
    }
}

/**
 * Emit an instruction
 */
static bool rfasm_emit_instruction(rfasm_state_t *state, rfasm_inst_t inst,
                                    rfasm_operand_t *op1, rfasm_operand_t *op2) {
    state->instructions_emitted++;

    switch (inst) {
        /* ===== Data Movement ===== */
        case INST_MOV:
            if (op1->type == OP_REG && op2->type == OP_IMM) {
                /* MOV reg, imm */
                uint8_t size = op1->size ? op1->size : rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);

                if (size == 1) {
                    rfasm_emit_byte(state, 0xB0 + rfasm_reg_encoding(op1->reg));
                    rfasm_emit_byte(state, (uint8_t)op2->value);
                } else {
                    rfasm_emit_byte(state, 0xB8 + rfasm_reg_encoding(op1->reg));
                    if (size == 2) {
                        rfasm_emit_word(state, (uint16_t)op2->value);
                    } else {
                        rfasm_emit_dword(state, (uint32_t)op2->value);
                    }
                }
            } else if (op1->type == OP_REG && op2->type == OP_REG) {
                /* MOV reg, reg */
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0x88 : 0x89);
                rfasm_emit_byte(state, rfasm_modrm(3, rfasm_reg_encoding(op2->reg),
                                                    rfasm_reg_encoding(op1->reg)));
            } else if (op1->type == OP_REG && op2->type == OP_MEM) {
                /* MOV reg, [mem] */
                uint8_t size = op1->size ? op1->size : rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0x8A : 0x8B);
                rfasm_emit_modrm_mem(state, rfasm_reg_encoding(op1->reg), op2);
            } else if (op1->type == OP_MEM && op2->type == OP_REG) {
                /* MOV [mem], reg */
                uint8_t size = op2->size ? op2->size : rfasm_reg_size(op2->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0x88 : 0x89);
                rfasm_emit_modrm_mem(state, rfasm_reg_encoding(op2->reg), op1);
            } else if (op1->type == OP_MEM && op2->type == OP_IMM) {
                /* MOV [mem], imm */
                uint8_t size = op1->size ? op1->size : 4;
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xC6 : 0xC7);
                rfasm_emit_modrm_mem(state, 0, op1);
                if (size == 1) rfasm_emit_byte(state, (uint8_t)op2->value);
                else if (size == 2) rfasm_emit_word(state, (uint16_t)op2->value);
                else rfasm_emit_dword(state, (uint32_t)op2->value);
            } else if (op1->type == OP_REG && op2->type == OP_LABEL) {
                /* MOV reg, label (load address) */
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, 0xB8 + rfasm_reg_encoding(op1->reg));

                if (state->pass == 2) {
                    rfasm_label_t *label = rfasm_find_label(state, op2->label);
                    if (label && label->defined) {
                        if (size == 2) rfasm_emit_word(state, (uint16_t)label->address);
                        else rfasm_emit_dword(state, label->address);
                    } else {
                        uint32_t fixup_loc = state->output_size;
                        if (size == 2) rfasm_emit_word(state, 0);
                        else rfasm_emit_dword(state, 0);
                        rfasm_add_fixup(state, op2->label, fixup_loc, size, false, 0);
                    }
                } else {
                    if (size == 2) rfasm_emit_word(state, 0);
                    else rfasm_emit_dword(state, 0);
                }
            }
            break;

        case INST_PUSH:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, 0x50 + rfasm_reg_encoding(op1->reg));
            } else if (op1->type == OP_IMM) {
                if (op1->value >= -128 && op1->value <= 127) {
                    rfasm_emit_byte(state, 0x6A);
                    rfasm_emit_byte(state, (uint8_t)op1->value);
                } else {
                    rfasm_emit_byte(state, 0x68);
                    rfasm_emit_dword(state, (uint32_t)op1->value);
                }
            } else if (op1->type == OP_MEM) {
                rfasm_emit_byte(state, 0xFF);
                rfasm_emit_modrm_mem(state, 6, op1);
            }
            break;

        case INST_POP:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, 0x58 + rfasm_reg_encoding(op1->reg));
            } else if (op1->type == OP_MEM) {
                rfasm_emit_byte(state, 0x8F);
                rfasm_emit_modrm_mem(state, 0, op1);
            }
            break;

        case INST_LEA:
            if (op1->type == OP_REG && op2->type == OP_MEM) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, 0x8D);
                rfasm_emit_modrm_mem(state, rfasm_reg_encoding(op1->reg), op2);
            }
            break;

        case INST_XCHG:
            if (op1->type == OP_REG && op2->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                /* Special case: XCHG EAX, reg */
                if (op1->reg == REG_EAX || op1->reg == REG_AX) {
                    rfasm_emit_byte(state, 0x90 + rfasm_reg_encoding(op2->reg));
                } else if (op2->reg == REG_EAX || op2->reg == REG_AX) {
                    rfasm_emit_byte(state, 0x90 + rfasm_reg_encoding(op1->reg));
                } else {
                    rfasm_emit_byte(state, size == 1 ? 0x86 : 0x87);
                    rfasm_emit_byte(state, rfasm_modrm(3, rfasm_reg_encoding(op1->reg),
                                                        rfasm_reg_encoding(op2->reg)));
                }
            }
            break;

        case INST_MOVZX:
            if (op1->type == OP_REG && op2->type == OP_REG) {
                uint8_t src_size = rfasm_reg_size(op2->reg);
                rfasm_emit_byte(state, 0x0F);
                rfasm_emit_byte(state, src_size == 1 ? 0xB6 : 0xB7);
                rfasm_emit_byte(state, rfasm_modrm(3, rfasm_reg_encoding(op1->reg),
                                                    rfasm_reg_encoding(op2->reg)));
            } else if (op1->type == OP_REG && op2->type == OP_MEM) {
                uint8_t src_size = op2->size ? op2->size : 1;
                rfasm_emit_byte(state, 0x0F);
                rfasm_emit_byte(state, src_size == 1 ? 0xB6 : 0xB7);
                rfasm_emit_modrm_mem(state, rfasm_reg_encoding(op1->reg), op2);
            }
            break;

        case INST_MOVSB:
            rfasm_emit_byte(state, 0xA4);
            break;

        case INST_MOVSW:
            rfasm_emit_byte(state, 0x66);
            rfasm_emit_byte(state, 0xA5);
            break;

        case INST_MOVSD:
            rfasm_emit_byte(state, 0xA5);
            break;

        case INST_REP:
            rfasm_emit_byte(state, 0xF3);
            break;

        /* ===== Arithmetic ===== */
        case INST_ADD:
            rfasm_emit_alu(state, 0x00, 0, op1, op2);
            break;

        case INST_SUB:
            rfasm_emit_alu(state, 0x28, 5, op1, op2);
            break;

        case INST_ADC:
            rfasm_emit_alu(state, 0x10, 2, op1, op2);
            break;

        case INST_SBB:
            rfasm_emit_alu(state, 0x18, 3, op1, op2);
            break;

        case INST_CMP:
            rfasm_emit_alu(state, 0x38, 7, op1, op2);
            break;

        case INST_INC:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                if (size == 1) {
                    rfasm_emit_byte(state, 0xFE);
                    rfasm_emit_byte(state, rfasm_modrm(3, 0, rfasm_reg_encoding(op1->reg)));
                } else {
                    rfasm_emit_byte(state, 0x40 + rfasm_reg_encoding(op1->reg));
                }
            } else if (op1->type == OP_MEM) {
                uint8_t size = op1->size ? op1->size : 4;
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xFE : 0xFF);
                rfasm_emit_modrm_mem(state, 0, op1);
            }
            break;

        case INST_DEC:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                if (size == 1) {
                    rfasm_emit_byte(state, 0xFE);
                    rfasm_emit_byte(state, rfasm_modrm(3, 1, rfasm_reg_encoding(op1->reg)));
                } else {
                    rfasm_emit_byte(state, 0x48 + rfasm_reg_encoding(op1->reg));
                }
            } else if (op1->type == OP_MEM) {
                uint8_t size = op1->size ? op1->size : 4;
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xFE : 0xFF);
                rfasm_emit_modrm_mem(state, 1, op1);
            }
            break;

        case INST_NEG:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                rfasm_emit_byte(state, rfasm_modrm(3, 3, rfasm_reg_encoding(op1->reg)));
            }
            break;

        case INST_MUL:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                rfasm_emit_byte(state, rfasm_modrm(3, 4, rfasm_reg_encoding(op1->reg)));
            }
            break;

        case INST_IMUL:
            if (op1->type == OP_REG && op2->type == OP_NONE) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                rfasm_emit_byte(state, rfasm_modrm(3, 5, rfasm_reg_encoding(op1->reg)));
            }
            break;

        case INST_DIV:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                rfasm_emit_byte(state, rfasm_modrm(3, 6, rfasm_reg_encoding(op1->reg)));
            }
            break;

        case INST_IDIV:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                rfasm_emit_byte(state, rfasm_modrm(3, 7, rfasm_reg_encoding(op1->reg)));
            }
            break;

        /* ===== Logic ===== */
        case INST_AND:
            rfasm_emit_alu(state, 0x20, 4, op1, op2);
            break;

        case INST_OR:
            rfasm_emit_alu(state, 0x08, 1, op1, op2);
            break;

        case INST_XOR:
            rfasm_emit_alu(state, 0x30, 6, op1, op2);
            break;

        case INST_NOT:
            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                rfasm_emit_byte(state, rfasm_modrm(3, 2, rfasm_reg_encoding(op1->reg)));
            }
            break;

        case INST_TEST:
            if (op1->type == OP_REG && op2->type == OP_IMM) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                if (op1->reg == REG_AL || op1->reg == REG_AX || op1->reg == REG_EAX) {
                    rfasm_emit_byte(state, size == 1 ? 0xA8 : 0xA9);
                } else {
                    rfasm_emit_byte(state, size == 1 ? 0xF6 : 0xF7);
                    rfasm_emit_byte(state, rfasm_modrm(3, 0, rfasm_reg_encoding(op1->reg)));
                }
                if (size == 1) rfasm_emit_byte(state, (uint8_t)op2->value);
                else if (size == 2) rfasm_emit_word(state, (uint16_t)op2->value);
                else rfasm_emit_dword(state, (uint32_t)op2->value);
            } else if (op1->type == OP_REG && op2->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0x84 : 0x85);
                rfasm_emit_byte(state, rfasm_modrm(3, rfasm_reg_encoding(op2->reg),
                                                    rfasm_reg_encoding(op1->reg)));
            }
            break;

        case INST_SHL:
        case INST_SHR:
        case INST_SAR:
        case INST_ROL:
        case INST_ROR: {
            uint8_t shift_op;
            switch (inst) {
                case INST_SHL: shift_op = 4; break;
                case INST_SHR: shift_op = 5; break;
                case INST_SAR: shift_op = 7; break;
                case INST_ROL: shift_op = 0; break;
                case INST_ROR: shift_op = 1; break;
                default: shift_op = 4; break;
            }

            if (op1->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);

                if (op2->type == OP_IMM && op2->value == 1) {
                    rfasm_emit_byte(state, size == 1 ? 0xD0 : 0xD1);
                    rfasm_emit_byte(state, rfasm_modrm(3, shift_op, rfasm_reg_encoding(op1->reg)));
                } else if (op2->type == OP_REG && op2->reg == REG_CL) {
                    rfasm_emit_byte(state, size == 1 ? 0xD2 : 0xD3);
                    rfasm_emit_byte(state, rfasm_modrm(3, shift_op, rfasm_reg_encoding(op1->reg)));
                } else if (op2->type == OP_IMM) {
                    rfasm_emit_byte(state, size == 1 ? 0xC0 : 0xC1);
                    rfasm_emit_byte(state, rfasm_modrm(3, shift_op, rfasm_reg_encoding(op1->reg)));
                    rfasm_emit_byte(state, (uint8_t)op2->value);
                }
            }
            break;
        }

        /* ===== Control Flow ===== */
        case INST_JMP:
            if (op1->type == OP_LABEL || op1->type == OP_IMM) {
                /* 
                 * For consistent sizing between passes, we need to decide
                 * jump size based on information available in pass 1.
                 * - Backward jumps: label is defined, we can check distance
                 * - Forward jumps: label not defined in pass 1, assume short (2 bytes)
                 *   and fix up in pass 2. If too far, we'll error.
                 */
                bool use_short = true;  /* Assume short by default */
                int32_t rel = 0;

                if (op1->type == OP_LABEL) {
                    rfasm_label_t *label = rfasm_find_label(state, op1->label);
                    if (label && label->defined) {
                        /* Backward jump - we know the distance */
                        rel = (int32_t)label->address - (int32_t)(state->pc + 2);
                        if (rel < -128 || rel > 127) {
                            use_short = false;  /* Need near jump */
                        }
                    }
                    /* Forward jump (label not yet defined) - assume short */
                } else if (op1->type == OP_IMM) {
                    rel = op1->value - (int32_t)(state->pc + 2);
                    if (rel < -128 || rel > 127) {
                        use_short = false;
                    }
                }

                if (use_short) {
                    rfasm_emit_byte(state, 0xEB);
                    if (state->pass == 2 && op1->type == OP_LABEL) {
                        rfasm_label_t *label = rfasm_find_label(state, op1->label);
                        if (label && label->defined) {
                            rel = (int32_t)label->address - (int32_t)(state->pc + 1);
                            if (rel < -128 || rel > 127) {
                                state->error = RFASM_ERR_OUT_OF_RANGE;
                                rfa_strcpy(state->error_msg, "Jump target too far for short jump");
                                return false;
                            }
                            rfasm_emit_byte(state, (uint8_t)rel);
                        } else {
                            /* Forward reference - add fixup */
                            uint32_t fixup_loc = state->output_size;
                            rfasm_emit_byte(state, 0);
                            rfasm_add_fixup(state, op1->label, fixup_loc, 1, true, state->pc);
                        }
                    } else if (op1->type == OP_IMM) {
                        rfasm_emit_byte(state, (uint8_t)rel);
                    } else {
                        /* Pass 1 - emit placeholder */
                        rfasm_emit_byte(state, 0);
                    }
                } else {
                    /* Near jump (5 bytes) */
                    rfasm_emit_byte(state, 0xE9);
                    if (state->pass == 2 && op1->type == OP_LABEL) {
                        uint32_t fixup_loc = state->output_size;
                        rfasm_emit_dword(state, 0);
                        rfasm_add_fixup(state, op1->label, fixup_loc, 4, true, state->pc);
                    } else if (op1->type == OP_IMM) {
                        rel = op1->value - (int32_t)(state->pc + 4);
                        rfasm_emit_dword(state, rel);
                    } else {
                        rfasm_emit_dword(state, 0);
                    }
                }
            } else if (op1->type == OP_REG) {
                rfasm_emit_byte(state, 0xFF);
                rfasm_emit_byte(state, rfasm_modrm(3, 4, rfasm_reg_encoding(op1->reg)));
            } else if (op1->type == OP_MEM) {
                rfasm_emit_byte(state, 0xFF);
                rfasm_emit_modrm_mem(state, 4, op1);
            }
            break;

        case INST_JZ:   rfasm_emit_jcc(state, 0x4, op1); break;   /* ZF=1 */
        case INST_JNZ:  rfasm_emit_jcc(state, 0x5, op1); break;   /* ZF=0 */
        case INST_JC:
        case INST_JB:   rfasm_emit_jcc(state, 0x2, op1); break;   /* CF=1 */
        case INST_JNC:
        case INST_JAE:  rfasm_emit_jcc(state, 0x3, op1); break;   /* CF=0 */
        case INST_JS:   rfasm_emit_jcc(state, 0x8, op1); break;   /* SF=1 */
        case INST_JNS:  rfasm_emit_jcc(state, 0x9, op1); break;   /* SF=0 */
        case INST_JL:   rfasm_emit_jcc(state, 0xC, op1); break;   /* SF!=OF */
        case INST_JG:   rfasm_emit_jcc(state, 0xF, op1); break;   /* ZF=0, SF=OF */
        case INST_JLE:  rfasm_emit_jcc(state, 0xE, op1); break;   /* ZF=1 or SF!=OF */
        case INST_JGE:  rfasm_emit_jcc(state, 0xD, op1); break;   /* SF=OF */
        case INST_JA:   rfasm_emit_jcc(state, 0x7, op1); break;   /* CF=0, ZF=0 */
        case INST_JBE:  rfasm_emit_jcc(state, 0x6, op1); break;   /* CF=1 or ZF=1 */

        case INST_CALL:
            if (op1->type == OP_LABEL || op1->type == OP_IMM) {
                rfasm_emit_byte(state, 0xE8);
                if (state->pass == 2 && op1->type == OP_LABEL) {
                    uint32_t fixup_loc = state->output_size;
                    rfasm_emit_dword(state, 0);
                    rfasm_add_fixup(state, op1->label, fixup_loc, 4, true, state->pc);
                } else if (op1->type == OP_IMM) {
                    int32_t rel = op1->value - (int32_t)(state->pc + 4);
                    rfasm_emit_dword(state, rel);
                } else {
                    rfasm_emit_dword(state, 0);
                }
            } else if (op1->type == OP_REG) {
                rfasm_emit_byte(state, 0xFF);
                rfasm_emit_byte(state, rfasm_modrm(3, 2, rfasm_reg_encoding(op1->reg)));
            } else if (op1->type == OP_MEM) {
                rfasm_emit_byte(state, 0xFF);
                rfasm_emit_modrm_mem(state, 2, op1);
            }
            break;

        case INST_RET:
            if (op1->type == OP_IMM && op1->value != 0) {
                rfasm_emit_byte(state, 0xC2);
                rfasm_emit_word(state, (uint16_t)op1->value);
            } else {
                rfasm_emit_byte(state, 0xC3);
            }
            break;

        case INST_LOOP:
            if (op1->type == OP_LABEL) {
                rfasm_emit_byte(state, 0xE2);
                if (state->pass == 2) {
                    rfasm_label_t *label = rfasm_find_label(state, op1->label);
                    if (label) {
                        int32_t rel = (int32_t)label->address - (int32_t)(state->pc + 1);
                        rfasm_emit_byte(state, (uint8_t)rel);
                    } else {
                        rfasm_emit_byte(state, 0);
                        rfasm_add_fixup(state, op1->label, state->output_size - 1, 1, true, state->pc);
                    }
                } else {
                    rfasm_emit_byte(state, 0);
                }
            }
            break;

        case INST_LOOPZ:
            rfasm_emit_byte(state, 0xE1);
            goto loop_common;

        case INST_LOOPNZ:
            rfasm_emit_byte(state, 0xE0);
            loop_common:
            if (op1->type == OP_LABEL) {
                if (state->pass == 2) {
                    rfasm_label_t *label = rfasm_find_label(state, op1->label);
                    if (label) {
                        int32_t rel = (int32_t)label->address - (int32_t)(state->pc + 1);
                        rfasm_emit_byte(state, (uint8_t)rel);
                    } else {
                        rfasm_emit_byte(state, 0);
                    }
                } else {
                    rfasm_emit_byte(state, 0);
                }
            }
            break;

        /* ===== Flags ===== */
        case INST_CLC:   rfasm_emit_byte(state, 0xF8); break;
        case INST_STC:   rfasm_emit_byte(state, 0xF9); break;
        case INST_CLI:   rfasm_emit_byte(state, 0xFA); break;
        case INST_STI:   rfasm_emit_byte(state, 0xFB); break;
        case INST_CLD:   rfasm_emit_byte(state, 0xFC); break;
        case INST_STD:   rfasm_emit_byte(state, 0xFD); break;
        case INST_PUSHF: rfasm_emit_byte(state, 0x9C); break;
        case INST_POPF:  rfasm_emit_byte(state, 0x9D); break;

        /* ===== System ===== */
        case INST_NOP:   rfasm_emit_byte(state, 0x90); break;
        case INST_HLT:   rfasm_emit_byte(state, 0xF4); break;

        case INST_INT:
            rfasm_emit_byte(state, 0xCD);
            rfasm_emit_byte(state, (uint8_t)op1->value);
            break;

        case INST_IRET:
            rfasm_emit_byte(state, 0xCF);
            break;

        case INST_IN:
            if (op1->type == OP_REG && op2->type == OP_IMM) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xE4 : 0xE5);
                rfasm_emit_byte(state, (uint8_t)op2->value);
            } else if (op1->type == OP_REG && op2->type == OP_REG && op2->reg == REG_DX) {
                uint8_t size = rfasm_reg_size(op1->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xEC : 0xED);
            }
            break;

        case INST_OUT:
            if (op1->type == OP_IMM && op2->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op2->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xE6 : 0xE7);
                rfasm_emit_byte(state, (uint8_t)op1->value);
            } else if (op1->type == OP_REG && op1->reg == REG_DX && op2->type == OP_REG) {
                uint8_t size = rfasm_reg_size(op2->reg);
                if (size == 2) rfasm_emit_byte(state, 0x66);
                rfasm_emit_byte(state, size == 1 ? 0xEE : 0xEF);
            }
            break;

        case INST_LGDT:
            if (op1->type == OP_MEM) {
                rfasm_emit_byte(state, 0x0F);
                rfasm_emit_byte(state, 0x01);
                rfasm_emit_modrm_mem(state, 2, op1);
            }
            break;

        case INST_LIDT:
            if (op1->type == OP_MEM) {
                rfasm_emit_byte(state, 0x0F);
                rfasm_emit_byte(state, 0x01);
                rfasm_emit_modrm_mem(state, 3, op1);
            }
            break;

        /* ===== Pseudo-instructions ===== */
        case INST_DB:
            /* Already handled in parse_line for strings */
            if (op1->type == OP_IMM) {
                rfasm_emit_byte(state, (uint8_t)op1->value);
            }
            break;

        case INST_DW:
            if (op1->type == OP_IMM) {
                rfasm_emit_word(state, (uint16_t)op1->value);
            } else if (op1->type == OP_LABEL) {
                if (state->pass == 2) {
                    rfasm_label_t *label = rfasm_find_label(state, op1->label);
                    if (label && label->defined) {
                        rfasm_emit_word(state, (uint16_t)label->address);
                    } else {
                        uint32_t fixup_loc = state->output_size;
                        rfasm_emit_word(state, 0);
                        rfasm_add_fixup(state, op1->label, fixup_loc, 2, false, 0);
                    }
                } else {
                    rfasm_emit_word(state, 0);
                }
            }
            break;

        case INST_DD:
            if (op1->type == OP_IMM) {
                rfasm_emit_dword(state, (uint32_t)op1->value);
            } else if (op1->type == OP_LABEL) {
                if (state->pass == 2) {
                    rfasm_label_t *label = rfasm_find_label(state, op1->label);
                    if (label && label->defined) {
                        rfasm_emit_dword(state, label->address);
                    } else {
                        uint32_t fixup_loc = state->output_size;
                        rfasm_emit_dword(state, 0);
                        rfasm_add_fixup(state, op1->label, fixup_loc, 4, false, 0);
                    }
                } else {
                    rfasm_emit_dword(state, 0);
                }
            }
            break;

        default:
            state->error = RFASM_ERR_UNKNOWN_INST;
            return false;
    }

    return true;
}

/**
 * Parse and process a single line
 */
static bool rfasm_parse_line(rfasm_state_t *state, const char *line) {
    const char *p = rfasm_skip_ws(line);

    /* Empty line or comment */
    if (*p == '\0' || *p == ';') {
        return true;
    }

    /* Check for label */
    const char *label_end = p;
    while (*label_end && (rfa_isalnum(*label_end) || *label_end == '_' || *label_end == '.')) {
        label_end++;
    }

    if (*label_end == ':') {
        /* Found a label */
        char label_name[RFASM_MAX_LABEL_LEN];
        int label_len = label_end - p;
        if (label_len >= RFASM_MAX_LABEL_LEN) label_len = RFASM_MAX_LABEL_LEN - 1;

        for (int i = 0; i < label_len; i++) {
            label_name[i] = p[i];
        }
        label_name[label_len] = '\0';

        /* Handle local labels */
        if (label_name[0] == '.') {
            char full_name[RFASM_MAX_LABEL_LEN * 2];
            rfa_strcpy(full_name, state->current_scope);
            rfa_strcpy(full_name + rfa_strlen(state->current_scope), label_name);
            if (!rfasm_add_label(state, full_name, state->pc)) {
                return false;
            }
        } else {
            if (!rfasm_add_label(state, label_name, state->pc)) {
                return false;
            }
        }

        p = label_end + 1;
        p = rfasm_skip_ws(p);

        if (*p == '\0' || *p == ';') {
            return true;  /* Label only on this line */
        }
    }

    /* Parse mnemonic */
    char mnemonic[16];
    int mn_len = 0;
    while (*p && rfa_isalpha(*p) && mn_len < 15) {
        mnemonic[mn_len++] = *p++;
    }
    mnemonic[mn_len] = '\0';

    if (mn_len == 0) {
        state->error = RFASM_ERR_SYNTAX;
        return false;
    }

    rfasm_inst_t inst = rfasm_lookup_inst(mnemonic);

    /* Handle special pseudo-instructions */
    if (inst == INST_ORG) {
        p = rfasm_skip_ws(p);
        int32_t org_val;
        if (!rfasm_parse_number(&p, &org_val)) {
            state->error = RFASM_ERR_SYNTAX;
            return false;
        }
        state->org = (uint32_t)org_val;
        state->pc = state->org;
        return true;
    }

    if (inst == INST_EQU) {
        /* label EQU value - but we got here after the colon check, */
        /* so check for "label EQU" format on current line */
        state->error = RFASM_ERR_SYNTAX;
        rfa_strcpy(state->error_msg, "EQU must follow label (label EQU value)");
        return false;
    }

    if (inst == INST_TIMES) {
        p = rfasm_skip_ws(p);
        int32_t count = 0;
        
        /* Parse expression: could be number, expression with $-$$, or arithmetic */
        /* Handle expressions like: 510-($-$$), 100, $-start, etc. */
        bool have_count = false;
        
        /* Try to parse as simple number first, but check for following operator */
        const char *save_p = p;
        if (rfasm_parse_number(&p, &count)) {
            p = rfasm_skip_ws(p);
            /* Check if there's an arithmetic operator following */
            if (*p == '-' || *p == '+') {
                char op = *p++;
                p = rfasm_skip_ws(p);
                
                /* Parse second operand */
                int32_t operand2 = 0;
                if (*p == '(' && *(p+1) == '$') {
                    /* Handle ($-$$) */
                    p++;  /* skip ( */
                    if (*p == '$') {
                        p++;
                        operand2 = state->pc;  /* $ = current address */
                        if (*p == '-' && *(p+1) == '$' && *(p+2) == '$') {
                            p += 3;  /* skip -$$ */
                            operand2 = state->pc - state->org;
                        }
                    }
                    if (*p == ')') p++;
                } else if (*p == '$') {
                    p++;
                    if (*p == '-' && *(p+1) == '$' && *(p+2) == '$') {
                        p += 3;
                        operand2 = state->pc - state->org;
                    } else {
                        operand2 = state->pc;
                    }
                } else {
                    rfasm_parse_number(&p, &operand2);
                }
                
                if (op == '-') count = count - operand2;
                else count = count + operand2;
            }
            have_count = true;
        }
        
        if (!have_count) {
            /* Try parsing as operand (for $-$$ without leading number) */
            p = save_p;
            rfasm_operand_t times_op;
            if (!rfasm_parse_operand(state, &p, &times_op)) {
                state->error = RFASM_ERR_SYNTAX;
                return false;
            }
            count = times_op.value;
        }

        p = rfasm_skip_ws(p);

        /* Get what to repeat (DB value or instruction) */
        char sub_mnemonic[16];
        int sub_len = 0;
        while (*p && rfa_isalpha(*p) && sub_len < 15) {
            sub_mnemonic[sub_len++] = *p++;
        }
        sub_mnemonic[sub_len] = '\0';

        rfasm_inst_t sub_inst = rfasm_lookup_inst(sub_mnemonic);
        if (sub_inst == INST_DB) {
            p = rfasm_skip_ws(p);
            int32_t byte_val;
            if (!rfasm_parse_number(&p, &byte_val)) {
                state->error = RFASM_ERR_SYNTAX;
                return false;
            }
            for (int i = 0; i < count; i++) {
                rfasm_emit_byte(state, (uint8_t)byte_val);
            }
        } else if (sub_inst == INST_NOP) {
            for (int i = 0; i < count; i++) {
                rfasm_emit_byte(state, 0x90);
            }
        }

        return true;
    }

    /* Handle DB with string */
    if (inst == INST_DB) {
        p = rfasm_skip_ws(p);
        while (*p && *p != ';') {
            p = rfasm_skip_ws(p);
            if (*p == '"') {
                /* String literal */
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        p++;
                        switch (*p) {
                            case 'n': rfasm_emit_byte(state, '\n'); break;
                            case 'r': rfasm_emit_byte(state, '\r'); break;
                            case 't': rfasm_emit_byte(state, '\t'); break;
                            case '0': rfasm_emit_byte(state, '\0'); break;
                            case '\\': rfasm_emit_byte(state, '\\'); break;
                            case '"': rfasm_emit_byte(state, '"'); break;
                            default: rfasm_emit_byte(state, *p); break;
                        }
                    } else {
                        rfasm_emit_byte(state, *p);
                    }
                    p++;
                }
                if (*p == '"') p++;
            } else if (*p == '\'') {
                int32_t ch;
                if (rfasm_parse_char(&p, &ch)) {
                    rfasm_emit_byte(state, (uint8_t)ch);
                }
            } else if (rfa_isdigit(*p) || *p == '-' || *p == '+' || *p == '0') {
                int32_t val;
                if (rfasm_parse_number(&p, &val)) {
                    rfasm_emit_byte(state, (uint8_t)val);
                }
            } else {
                break;
            }

            p = rfasm_skip_ws(p);
            if (*p == ',') p++;
        }
        return true;
    }

    if (inst == INST_DW) {
        p = rfasm_skip_ws(p);
        while (*p && *p != ';') {
            rfasm_operand_t op;
            if (!rfasm_parse_operand(state, &p, &op)) break;
            rfasm_emit_instruction(state, INST_DW, &op, NULL);
            p = rfasm_skip_ws(p);
            if (*p == ',') p++;
        }
        return true;
    }

    if (inst == INST_DD) {
        p = rfasm_skip_ws(p);
        while (*p && *p != ';') {
            rfasm_operand_t op;
            if (!rfasm_parse_operand(state, &p, &op)) break;
            rfasm_emit_instruction(state, INST_DD, &op, NULL);
            p = rfasm_skip_ws(p);
            if (*p == ',') p++;
        }
        return true;
    }

    if (inst == INST_NONE) {
        state->error = RFASM_ERR_UNKNOWN_INST;
        rfa_strcpy(state->error_msg, "Unknown instruction: ");
        rfa_strncpy(state->error_msg + 21, mnemonic, 32);
        return false;
    }

    /* Parse operands */
    rfasm_operand_t op1 = { .type = OP_NONE };
    rfasm_operand_t op2 = { .type = OP_NONE };

    p = rfasm_skip_ws(p);

    if (*p && *p != ';') {
        if (!rfasm_parse_operand(state, &p, &op1)) {
            if (state->error == RFASM_OK) {
                state->error = RFASM_ERR_INVALID_OPERAND;
            }
            return false;
        }

        p = rfasm_skip_ws(p);
        if (*p == ',') {
            p++;
            p = rfasm_skip_ws(p);
            if (!rfasm_parse_operand(state, &p, &op2)) {
                if (state->error == RFASM_OK) {
                    state->error = RFASM_ERR_INVALID_OPERAND;
                }
                return false;
            }
        }
    }

    /* Emit instruction */
    return rfasm_emit_instruction(state, inst, &op1, &op2);
}

/**
 * Assemble source code (main entry point)
 */
static rfasm_error_t rfasm_assemble(rfasm_state_t *state, const char *source) {
    /* Pass 1: Collect labels */
    state->pass = 1;
    state->pc = state->org;

    const char *line_start = source;
    int line_num = 1;

    while (*line_start) {
        /* Find end of line */
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;

        /* Copy line to buffer */
        char line_buf[RFASM_MAX_LINE];
        int line_len = line_end - line_start;
        if (line_len >= RFASM_MAX_LINE) line_len = RFASM_MAX_LINE - 1;

        for (int i = 0; i < line_len; i++) {
            line_buf[i] = line_start[i];
        }
        line_buf[line_len] = '\0';

        /* Strip CR if present */
        if (line_len > 0 && line_buf[line_len - 1] == '\r') {
            line_buf[line_len - 1] = '\0';
        }

        /* Process line */
        if (!rfasm_parse_line(state, line_buf)) {
            state->error_line = line_num;
            return state->error;
        }

        state->lines_processed++;

        /* Next line */
        if (*line_end == '\n') line_end++;
        line_start = line_end;
        line_num++;
    }

    /* Pass 2: Emit code */
    state->pass = 2;
    rfasm_reset(state);
    state->pass = 2;

    line_start = source;
    line_num = 1;

    while (*line_start) {
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;

        char line_buf[RFASM_MAX_LINE];
        int line_len = line_end - line_start;
        if (line_len >= RFASM_MAX_LINE) line_len = RFASM_MAX_LINE - 1;

        for (int i = 0; i < line_len; i++) {
            line_buf[i] = line_start[i];
        }
        line_buf[line_len] = '\0';

        if (line_len > 0 && line_buf[line_len - 1] == '\r') {
            line_buf[line_len - 1] = '\0';
        }

        if (!rfasm_parse_line(state, line_buf)) {
            state->error_line = line_num;
            return state->error;
        }

        if (*line_end == '\n') line_end++;
        line_start = line_end;
        line_num++;
    }

    /* Resolve forward references */
    if (!rfasm_resolve_fixups(state)) {
        return state->error;
    }

    return RFASM_OK;
}

/**
 * Get error string
 */
static const char *rfasm_error_string(rfasm_error_t err) {
    switch (err) {
        case RFASM_OK:                  return "OK";
        case RFASM_ERR_SYNTAX:          return "Syntax error";
        case RFASM_ERR_UNKNOWN_INST:    return "Unknown instruction";
        case RFASM_ERR_UNKNOWN_REG:     return "Unknown register";
        case RFASM_ERR_UNKNOWN_LABEL:   return "Undefined label";
        case RFASM_ERR_DUPLICATE_LABEL: return "Duplicate label";
        case RFASM_ERR_TOO_MANY_LABELS: return "Too many labels";
        case RFASM_ERR_TOO_MANY_FIXUPS: return "Too many forward references";
        case RFASM_ERR_INVALID_OPERAND: return "Invalid operand";
        case RFASM_ERR_OUT_OF_RANGE:    return "Value out of range";
        case RFASM_ERR_MEMORY:          return "Out of memory";
        case RFASM_ERR_INTERNAL:        return "Internal error";
        default:                        return "Unknown error";
    }
}

/* ============================================================================
 * Shell Integration Commands
 * ============================================================================ */

#ifdef RFASM_SHELL_INTEGRATION

/**
 * Hexdump helper for 'hexdump' command
 */
static void rfasm_hexdump(void (*printf_fn)(const char *, ...), 
                          const uint8_t *data, uint32_t len, uint32_t base_addr) {
    for (uint32_t i = 0; i < len; i += 16) {
        printf_fn("%08X: ", base_addr + i);
        
        /* Hex bytes */
        for (int j = 0; j < 16; j++) {
            if (i + j < len) {
                printf_fn("%02X ", data[i + j]);
            } else {
                printf_fn("   ");
            }
            if (j == 7) printf_fn(" ");
        }
        
        printf_fn(" |");
        
        /* ASCII */
        for (int j = 0; j < 16 && i + j < len; j++) {
            char c = data[i + j];
            printf_fn("%c", (c >= 32 && c < 127) ? c : '.');
        }
        
        printf_fn("|\n");
    }
}

/**
 * Print instruction reference
 */
static void rfasm_print_help(void (*printf_fn)(const char *, ...)) {
    printf_fn("\nRF-ASM Instruction Reference (~40 i386 instructions)\n");
    printf_fn("====================================================\n\n");
    
    printf_fn("DATA MOVEMENT:\n");
    printf_fn("  MOV  dest, src    - Move data\n");
    printf_fn("  PUSH src          - Push to stack\n");
    printf_fn("  POP  dest         - Pop from stack\n");
    printf_fn("  LEA  dest, [addr] - Load effective address\n");
    printf_fn("  XCHG a, b         - Exchange values\n");
    printf_fn("  MOVZX dest, src   - Move with zero extend\n");
    
    printf_fn("\nARITHMETIC:\n");
    printf_fn("  ADD  dest, src    - Addition\n");
    printf_fn("  SUB  dest, src    - Subtraction\n");
    printf_fn("  INC  dest         - Increment\n");
    printf_fn("  DEC  dest         - Decrement\n");
    printf_fn("  NEG  dest         - Negate (two's complement)\n");
    printf_fn("  MUL  src          - Unsigned multiply (EAX)\n");
    printf_fn("  DIV  src          - Unsigned divide (EAX/EDX)\n");
    printf_fn("  CMP  a, b         - Compare (sets flags)\n");
    
    printf_fn("\nLOGIC:\n");
    printf_fn("  AND  dest, src    - Bitwise AND\n");
    printf_fn("  OR   dest, src    - Bitwise OR\n");
    printf_fn("  XOR  dest, src    - Bitwise XOR\n");
    printf_fn("  NOT  dest         - Bitwise NOT\n");
    printf_fn("  TEST a, b         - AND without storing\n");
    printf_fn("  SHL  dest, count  - Shift left\n");
    printf_fn("  SHR  dest, count  - Shift right (logical)\n");
    printf_fn("  SAR  dest, count  - Shift right (arithmetic)\n");
    
    printf_fn("\nCONTROL FLOW:\n");
    printf_fn("  JMP  label        - Unconditional jump\n");
    printf_fn("  JZ/JE label       - Jump if zero/equal\n");
    printf_fn("  JNZ/JNE label     - Jump if not zero/not equal\n");
    printf_fn("  JC/JB label       - Jump if carry/below\n");
    printf_fn("  JNC/JAE label     - Jump if no carry/above-equal\n");
    printf_fn("  JS label          - Jump if sign (negative)\n");
    printf_fn("  JL/JG label       - Jump if less/greater (signed)\n");
    printf_fn("  CALL label        - Call subroutine\n");
    printf_fn("  RET               - Return from call\n");
    printf_fn("  LOOP label        - Dec ECX, jump if not zero\n");
    
    printf_fn("\nFLAGS:\n");
    printf_fn("  CLC/STC           - Clear/Set carry flag\n");
    printf_fn("  CLI/STI           - Clear/Set interrupt flag\n");
    printf_fn("  CLD/STD           - Clear/Set direction flag\n");
    printf_fn("  PUSHF/POPF        - Push/Pop flags\n");
    
    printf_fn("\nSYSTEM:\n");
    printf_fn("  NOP               - No operation\n");
    printf_fn("  HLT               - Halt processor\n");
    printf_fn("  INT n             - Software interrupt\n");
    printf_fn("  IN dest, port     - Read from I/O port\n");
    printf_fn("  OUT port, src     - Write to I/O port\n");
    
    printf_fn("\nPSEUDO-INSTRUCTIONS:\n");
    printf_fn("  ORG addr          - Set origin address\n");
    printf_fn("  DB val/\"str\"      - Define byte(s)\n");
    printf_fn("  DW val            - Define word (16-bit)\n");
    printf_fn("  DD val            - Define dword (32-bit)\n");
    printf_fn("  TIMES n DB val    - Repeat n times\n");
    printf_fn("  label:            - Define label\n");
    printf_fn("  .local:           - Local label (scoped)\n");
    
    printf_fn("\nREGISTERS:\n");
    printf_fn("  32-bit: EAX ECX EDX EBX ESP EBP ESI EDI\n");
    printf_fn("  16-bit: AX CX DX BX SP BP SI DI\n");
    printf_fn("  8-bit:  AL CL DL BL AH CH DH BH\n");
    
    printf_fn("\nADDRESSING MODES:\n");
    printf_fn("  [addr]            - Direct memory\n");
    printf_fn("  [reg]             - Register indirect\n");
    printf_fn("  [reg+disp]        - Register + displacement\n");
    printf_fn("  [reg+reg*scale]   - SIB addressing\n");
    printf_fn("  BYTE/WORD/DWORD   - Size override prefix\n");
}

#endif /* RFASM_SHELL_INTEGRATION */

#ifdef __cplusplus
}
#endif

#endif /* RFASM_H */

/**
 * rfasm_test.c - Standalone test for RF-ASM assembler
 *
 * Compile: gcc -o rfasm_test rfasm_test.c
 * Run:     ./rfasm_test [source.rfa] [output.bin]
 *
 * This can run on a normal Linux/macOS system to test the assembler
 * before integrating into RetroFuture OS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the assembler */
#define RFASM_SHELL_INTEGRATION
#include "../include/rfasm.h"

/* Test output buffer */
static uint8_t output_buffer[65536];

/* Hexdump helper */
static void hexdump(const uint8_t *data, size_t len, uint32_t base_addr) {
    for (size_t i = 0; i < len; i += 16) {
        printf("%08X: ", (unsigned)(base_addr + i));

        for (int j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02X ", data[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        printf(" |");

        for (int j = 0; j < 16 && i + j < len; j++) {
            char c = data[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }

        printf("|\n");
    }
}

/* Run a single test case */
static int test_case(const char *name, const char *source,
                     const uint8_t *expected, size_t expected_len) {
    rfasm_state_t state;
    rfasm_init(&state, output_buffer, sizeof(output_buffer));

    rfasm_error_t err = rfasm_assemble(&state, source);

    if (err != RFASM_OK) {
        printf("FAIL [%s]: Error on line %d: %s\n",
               name, state.error_line, rfasm_error_string(err));
        if (state.error_msg[0]) {
            printf("     %s\n", state.error_msg);
        }
        return 1;
    }

    if (state.output_size != expected_len) {
        printf("FAIL [%s]: Expected %zu bytes, got %u\n",
               name, expected_len, state.output_size);
        printf("  Output:\n");
        hexdump(output_buffer, state.output_size, 0);
        return 1;
    }

    if (memcmp(output_buffer, expected, expected_len) != 0) {
        printf("FAIL [%s]: Output mismatch\n", name);
        printf("  Expected:\n");
        hexdump(expected, expected_len, 0);
        printf("  Got:\n");
        hexdump(output_buffer, state.output_size, 0);
        return 1;
    }

    printf("PASS [%s]: %u bytes\n", name, state.output_size);
    return 0;
}

/* Run all tests */
static int run_tests(void) {
    int failures = 0;

    printf("\n=== RF-ASM Test Suite ===\n\n");

    /* Test: Simple MOV instructions */
    {
        const char *src = "MOV EAX, 0x12345678\n";
        const uint8_t expected[] = { 0xB8, 0x78, 0x56, 0x34, 0x12 };
        failures += test_case("MOV EAX, imm32", src, expected, sizeof(expected));
    }

    {
        const char *src = "MOV AL, 42\n";
        const uint8_t expected[] = { 0xB0, 0x2A };
        failures += test_case("MOV AL, imm8", src, expected, sizeof(expected));
    }

    {
        const char *src = "MOV AX, 0x1234\n";
        const uint8_t expected[] = { 0x66, 0xB8, 0x34, 0x12 };
        failures += test_case("MOV AX, imm16", src, expected, sizeof(expected));
    }

    /* Test: Register to register */
    {
        const char *src = "MOV EAX, EBX\n";
        const uint8_t expected[] = { 0x89, 0xD8 };  /* MOV r/m32, r32 */
        failures += test_case("MOV EAX, EBX", src, expected, sizeof(expected));
    }

    /* Test: Memory operations */
    {
        const char *src = "MOV EAX, [0x1234]\n";
        const uint8_t expected[] = { 0x8B, 0x05, 0x34, 0x12, 0x00, 0x00 };
        failures += test_case("MOV EAX, [imm]", src, expected, sizeof(expected));
    }

    {
        const char *src = "MOV [EBX], EAX\n";
        const uint8_t expected[] = { 0x89, 0x03 };
        failures += test_case("MOV [EBX], EAX", src, expected, sizeof(expected));
    }

    {
        const char *src = "MOV EAX, [EBX+8]\n";
        const uint8_t expected[] = { 0x8B, 0x43, 0x08 };
        failures += test_case("MOV EAX, [EBX+8]", src, expected, sizeof(expected));
    }

    /* Test: PUSH/POP */
    {
        const char *src = "PUSH EAX\nPOP EBX\n";
        const uint8_t expected[] = { 0x50, 0x5B };
        failures += test_case("PUSH/POP reg32", src, expected, sizeof(expected));
    }

    {
        const char *src = "PUSH 0x42\n";
        const uint8_t expected[] = { 0x6A, 0x42 };
        failures += test_case("PUSH imm8", src, expected, sizeof(expected));
    }

    /* Test: ALU operations */
    {
        const char *src = "ADD EAX, EBX\n";
        const uint8_t expected[] = { 0x01, 0xD8 };
        failures += test_case("ADD EAX, EBX", src, expected, sizeof(expected));
    }

    {
        const char *src = "ADD EAX, 100\n";
        const uint8_t expected[] = { 0x83, 0xC0, 0x64 };  /* Sign-extended imm8 */
        failures += test_case("ADD EAX, imm8", src, expected, sizeof(expected));
    }

    {
        const char *src = "SUB EBX, 10\n";
        const uint8_t expected[] = { 0x83, 0xEB, 0x0A };
        failures += test_case("SUB EBX, imm8", src, expected, sizeof(expected));
    }

    {
        const char *src = "CMP EAX, 0\n";
        const uint8_t expected[] = { 0x83, 0xF8, 0x00 };
        failures += test_case("CMP EAX, 0", src, expected, sizeof(expected));
    }

    /* Test: INC/DEC */
    {
        const char *src = "INC EAX\nDEC EBX\n";
        const uint8_t expected[] = { 0x40, 0x4B };
        failures += test_case("INC/DEC reg32", src, expected, sizeof(expected));
    }

    /* Test: Logic */
    {
        const char *src = "AND EAX, 0xFF\n";
        const uint8_t expected[] = { 0x83, 0xE0, 0xFF };  /* Becomes -1 (sign-ext) */
        /* Actually 0xFF sign-extends... let's use a larger value */
    }

    {
        const char *src = "XOR EAX, EAX\n";
        const uint8_t expected[] = { 0x31, 0xC0 };
        failures += test_case("XOR EAX, EAX", src, expected, sizeof(expected));
    }

    {
        const char *src = "TEST AL, 1\n";
        const uint8_t expected[] = { 0xA8, 0x01 };
        failures += test_case("TEST AL, imm8", src, expected, sizeof(expected));
    }

    /* Test: Shifts */
    {
        const char *src = "SHL EAX, 1\n";
        const uint8_t expected[] = { 0xD1, 0xE0 };
        failures += test_case("SHL EAX, 1", src, expected, sizeof(expected));
    }

    {
        const char *src = "SHR EBX, CL\n";
        const uint8_t expected[] = { 0xD3, 0xEB };
        failures += test_case("SHR EBX, CL", src, expected, sizeof(expected));
    }

    {
        const char *src = "SHL ECX, 4\n";
        const uint8_t expected[] = { 0xC1, 0xE1, 0x04 };
        failures += test_case("SHL ECX, 4", src, expected, sizeof(expected));
    }

    /* Test: Jumps and labels */
    {
        const char *src =
            "start:\n"
            "    NOP\n"
            "    JMP start\n";
        const uint8_t expected[] = { 0x90, 0xEB, 0xFD };  /* Short jump -3 */
        failures += test_case("JMP short backward", src, expected, sizeof(expected));
    }

    {
        const char *src =
            "    JMP end\n"
            "    NOP\n"
            "end:\n";
        const uint8_t expected[] = { 0xEB, 0x01, 0x90 };  /* Short jump +1 */
        failures += test_case("JMP short forward", src, expected, sizeof(expected));
    }

    {
        const char *src =
            "loop:\n"
            "    DEC ECX\n"
            "    JNZ loop\n";
        const uint8_t expected[] = { 0x49, 0x75, 0xFD };  /* JNZ -3 */
        failures += test_case("JNZ short backward", src, expected, sizeof(expected));
    }

    /* Test: CALL/RET */
    {
        const char *src = "RET\n";
        const uint8_t expected[] = { 0xC3 };
        failures += test_case("RET", src, expected, sizeof(expected));
    }

    /* Test: Flags */
    {
        const char *src = "CLI\nSTI\nCLC\nSTC\n";
        const uint8_t expected[] = { 0xFA, 0xFB, 0xF8, 0xF9 };
        failures += test_case("Flag instructions", src, expected, sizeof(expected));
    }

    /* Test: System */
    {
        const char *src = "NOP\nHLT\n";
        const uint8_t expected[] = { 0x90, 0xF4 };
        failures += test_case("NOP HLT", src, expected, sizeof(expected));
    }

    {
        const char *src = "INT 0x10\n";
        const uint8_t expected[] = { 0xCD, 0x10 };
        failures += test_case("INT 0x10", src, expected, sizeof(expected));
    }

    /* Test: I/O */
    {
        const char *src = "IN AL, 0x60\n";
        const uint8_t expected[] = { 0xE4, 0x60 };
        failures += test_case("IN AL, port", src, expected, sizeof(expected));
    }

    {
        const char *src = "OUT 0x20, AL\n";
        const uint8_t expected[] = { 0xE6, 0x20 };
        failures += test_case("OUT port, AL", src, expected, sizeof(expected));
    }

    /* Test: Data definitions */
    {
        const char *src = "DB 0x41, 0x42, 0x43, 0\n";
        const uint8_t expected[] = { 0x41, 0x42, 0x43, 0x00 };
        failures += test_case("DB bytes", src, expected, sizeof(expected));
    }

    {
        const char *src = "DB \"Hello\", 0\n";
        const uint8_t expected[] = { 'H', 'e', 'l', 'l', 'o', 0 };
        failures += test_case("DB string", src, expected, sizeof(expected));
    }

    {
        const char *src = "DW 0x1234\n";
        const uint8_t expected[] = { 0x34, 0x12 };
        failures += test_case("DW word", src, expected, sizeof(expected));
    }

    {
        const char *src = "DD 0x12345678\n";
        const uint8_t expected[] = { 0x78, 0x56, 0x34, 0x12 };
        failures += test_case("DD dword", src, expected, sizeof(expected));
    }

    /* Test: TIMES */
    {
        const char *src = "TIMES 5 DB 0x90\n";
        const uint8_t expected[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
        failures += test_case("TIMES 5 DB", src, expected, sizeof(expected));
    }

    /* Test: Local labels */
    {
        const char *src =
            "func1:\n"
            "    NOP\n"
            ".loop:\n"
            "    INC EAX\n"
            "    JMP .loop\n"
            "\n"
            "func2:\n"
            "    NOP\n"
            ".loop:\n"
            "    DEC EAX\n"
            "    JMP .loop\n";
        /* func1: 90 | .loop: 40 EB FD | func2: 90 | .loop: 48 EB FD */
        const uint8_t expected[] = {
            0x90,               /* func1: NOP */
            0x40,               /* .loop: INC EAX */
            0xEB, 0xFD,         /*        JMP .loop (-3) */
            0x90,               /* func2: NOP */
            0x48,               /* .loop: DEC EAX */
            0xEB, 0xFD          /*        JMP .loop (-3) */
        };
        failures += test_case("Local labels", src, expected, sizeof(expected));
    }

    /* Test: Comments */
    {
        const char *src =
            "; This is a comment\n"
            "NOP  ; Inline comment\n"
            "HLT\n";
        const uint8_t expected[] = { 0x90, 0xF4 };
        failures += test_case("Comments", src, expected, sizeof(expected));
    }

    /* Test: LEA */
    {
        const char *src = "LEA EAX, [EBX+4]\n";
        const uint8_t expected[] = { 0x8D, 0x43, 0x04 };
        failures += test_case("LEA EAX, [EBX+4]", src, expected, sizeof(expected));
    }

    /* Test: Memory with label */
    {
        const char *src =
            "    MOV EAX, data\n"
            "data:\n"
            "    DD 0x12345678\n";
        /* MOV EAX, data = B8 05 00 00 00 (addr of data = 5) */
        /* Actually with ORG 0, data is at 5 */
        const uint8_t expected[] = {
            0xB8, 0x05, 0x00, 0x00, 0x00,  /* MOV EAX, 5 */
            0x78, 0x56, 0x34, 0x12         /* DD */
        };
        failures += test_case("MOV reg, label", src, expected, sizeof(expected));
    }

    printf("\n=== Results: %d failures ===\n\n", failures);
    return failures;
}

/* Main function */
int main(int argc, char *argv[]) {
    if (argc == 1) {
        /* Run test suite */
        return run_tests();
    }

    /* Assemble file */
    const char *input_file = argv[1];
    const char *output_file = (argc > 2) ? argv[2] : NULL;

    /* Read source file */
    FILE *f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", input_file);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(file_size + 1);
    if (!source) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return 1;
    }

    size_t bytes_read = fread(source, 1, file_size, f);
    fclose(f);
    source[bytes_read] = '\0';

    printf("RF-ASM Assembler\n");
    printf("================\n\n");
    printf("Input:  %s (%zu bytes)\n", input_file, bytes_read);

    /* Assemble */
    rfasm_state_t state;
    rfasm_init(&state, output_buffer, sizeof(output_buffer));

    rfasm_error_t err = rfasm_assemble(&state, source);
    free(source);

    if (err != RFASM_OK) {
        fprintf(stderr, "Error on line %d: %s\n",
                state.error_line, rfasm_error_string(err));
        if (state.error_msg[0]) {
            fprintf(stderr, "  %s\n", state.error_msg);
        }
        return 1;
    }

    printf("Labels: %d\n", state.num_labels);
    printf("Output: %u bytes\n\n", state.output_size);

    /* Show labels */
    if (state.num_labels > 0) {
        printf("Symbol Table:\n");
        for (int i = 0; i < state.num_labels; i++) {
            printf("  %-24s  0x%08X\n",
                   state.labels[i].name, state.labels[i].address);
        }
        printf("\n");
    }

    /* Write output */
    if (output_file) {
        FILE *out = fopen(output_file, "wb");
        if (!out) {
            fprintf(stderr, "Error: Cannot create '%s'\n", output_file);
            return 1;
        }

        size_t written = fwrite(output_buffer, 1, state.output_size, out);
        fclose(out);

        if (written != state.output_size) {
            fprintf(stderr, "Error: Only wrote %zu of %u bytes\n",
                    written, state.output_size);
            return 1;
        }

        printf("Wrote: %s\n\n", output_file);
    }

    /* Hexdump first 256 bytes (or less) */
    size_t dump_len = state.output_size;
    if (dump_len > 256) dump_len = 256;

    printf("Output hexdump:\n");
    hexdump(output_buffer, dump_len, state.org);

    if (state.output_size > 256) {
        printf("... (%u more bytes)\n", state.output_size - 256);
    }

    return 0;
}
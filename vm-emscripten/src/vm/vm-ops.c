#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "vm.h"


/**
 * Internal memory objects
*/
float ANALOG_IN[ANALOG_IN_COUNT];
float ANALOG_OUT[ANALOG_OUT_COUNT];

uint8_t BINARY_IN[BINARY_IN_COUNT];
uint8_t BINARY_OUT[BINARY_OUT_COUNT];

struct reg_t VARIABLE_IO[BINARY_IN_COUNT];


/**
 * Helper to convert a two-byte value to an integer in the range 0x0000-0xffff
 */
#define BYTES_TO_ADDR(one, two) (one + (256 * two))

/**
 * Trivial helper to test arrays are not out of bounds.
 */
int bound_test(svm_t *svm, uint32_t test, uint32_t count)
{
    if (test >= count)
    {
        svm_default_error_handler(svm, "Register out of bounds");
        return -1; // Should never happen
    }
    return 0;
}

#define BOUNDS_TEST_REGISTER(r) \
    bound_test(svm, r, REGISTER_COUNT);

/**
 * Foward declarations for code in this module which is not exported.
 */
char *get_string_reg(svm_t *cpu, int reg);
int get_int_reg(svm_t *cpu, int reg);
char *string_from_stack(svm_t *svm);
uint8_t next_byte(svm_t *svm);

/**
 * Helper to return the string-content of a register.
 *
 * NOTE: This function is not exported outside this compilation-unit.
 */
char *get_string_reg(svm_t *cpu, int reg)
{
    if (cpu->registers[reg].type == STRING)
        return (cpu->registers[reg].content.string);

    svm_default_error_handler(cpu, "The register deesn't contain a string");
    return NULL;
}

/**
 * Helper to return the integer-content of a register.
 *
 * NOTE: This function is not exported outside this compilation-unit.
 */
int get_int_reg(svm_t *cpu, int reg)
{
    if (cpu->registers[reg].type == INTEGER)
        return (cpu->registers[reg].content.integer);

    svm_default_error_handler(cpu, "The register doesn't contain an integer");
    return 0;
}

/**
 * Helper to return the number-content of a register.
 *
 * NOTE: This function is not exported outside this compilation-unit.
 */
float get_float_reg(svm_t *cpu, int reg)
{
    if (cpu->registers[reg].type == FLOAT)
        return (cpu->registers[reg].content.number);

    svm_default_error_handler(cpu, "The register doesn't contain an number");
    return 0;
}

/**
 * Helper to clear the string-content of a register.
 *
 * NOTE: This function is not exported outside this compilation-unit.
 */
void clear_string_reg(svm_t *cpu, int reg)
{
    /* Free the existing string, if present */
    if ((cpu->registers[reg].type == STRING) && (cpu->registers[reg].content.string))
        free(cpu->registers[reg].content.string);
}

/**
 * Strings are stored inline in the program-RAM.
 *
 * An example script might contain something like:
 *
 *   store #1, "Steve Kemp"
 *
 * This is encoded as:
 *
 *   OP_STRING_STORE, REG1, LEN1, LEN2, "String data", ...
 *
 * Here we assume the IP is pointing to len1 and we read the length, then
 * the string, and bump the IP as we go.
 *
 * The end result should be we've updated the IP to point past the end
 * of the string, and we've copied it into newly allocated RAM.
 *
 * NOTE: This function is not exported outside this compilation-unit.
 */
char *string_from_stack(svm_t *svm)
{
    /* the string length */
    uint32_t len1 = next_byte(svm);
    uint32_t len2 = next_byte(svm);

    /* build up the length 0-64k */
    int len = BYTES_TO_ADDR(len1, len2);

    /* bump IP one more to point to the start of the string-data. */
    svm->ip += 1;

    /* allocate enough RAM to contain the string. */
    char *tmp = (char *)malloc(len + 1);
    if (tmp == NULL)
        svm_default_error_handler(svm, "RAM allocation failure.");

    /**
     * Zero the allocated memory, and copy the string-contents over.
     *
     * The copy is inefficient - but copes with embedded NULL.
     */
    memset(tmp, '\0', len + 1);
    for (int i = 0; i < (int)len; i++)
    {
        if (svm->ip >= 0xFFFF)
            svm->ip = 0;

        tmp[i] = svm->code[svm->ip];
        svm->ip++;
    }

    svm->ip--;
    return tmp;
}

/**
 * Read and return the next byte from the current instruction-pointer.
 *
 * This function ensures that reading will wrap around the address-space
 * of the virtual CPU.
 */
uint8_t next_byte(svm_t *svm)
{
    svm->ip += 1;

    if (svm->ip >= 0xFFFF)
        svm->ip = 0;

    return (svm->code[svm->ip]);
}

/**
 ** Start implementation of virtual machine opcodes.
 **
 **/

void op_unknown(svm_t *svm)
{
    int instruction = svm->code[svm->ip];
    jsprintf("%04X - op_unknown(%02X)\n", svm->ip, instruction);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Break out of our main intepretter loop.
 */
void op_exit(struct svm *svm)
{
    svm->running = 0;

    /* handle the next instruction - which won't happen */
    svm->ip += 1;
}

/**
 * No-operation / NOP.
 */
void op_nop(struct svm *svm)
{
    (void)svm;

    if (getenv("DEBUG") != NULL)
        jsprintf("nop()\n");

    /* handle the next instruction */
    svm->ip += 1;
}

void op_divide(struct svm *svm)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the source register */
    uint32_t src1 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the source register */
    uint32_t src2 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("DIV(Register:%d = Register:%d / Register:%d)\n", reg, src1, src2);

    /* if the result-register stores a string .. free it */
    clear_string_reg(svm, reg);

    /*
     * Ensure both source registers have integer values.
     */
    int val1 = get_int_reg(svm, src1);
    int val2 = get_int_reg(svm, src2);

    if (val2 == 0)
    {
        svm_default_error_handler(svm, "Division by zero!");
        return;
    }

    /**
     * Store the result.
     */
    svm->registers[reg].content.integer = val1 / val2;
    svm->registers[reg].type = INTEGER;

    /**
     * Zero result?
     */
    if (svm->registers[reg].content.integer == 0)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store the contents of one register in another.
 */
void op_reg_store(struct svm *svm)
{
    (void)svm;

    /* get the destination register */
    uint32_t dst = next_byte(svm);
    BOUNDS_TEST_REGISTER(dst);

    /* get the source register */
    uint32_t src = next_byte(svm);
    BOUNDS_TEST_REGISTER(src);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Reg%02x will be set to contents of Reg%02x)\n", dst, src);

    /* Free the existing string, if present */
    clear_string_reg(svm, dst);

    /* if storing a string - then use strdup */
    if (svm->registers[src].type == STRING)
    {
        svm->registers[dst].type = STRING;
        svm->registers[dst].content.string = strdup(svm->registers[src].content.string);
    }
    else
    {
        svm->registers[dst].type = svm->registers[src].type;
        svm->registers[dst].content.integer = svm->registers[src].content.integer;
    }

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store an integer in a register.
 */
void op_int_store(struct svm *svm)
{
    /* get the register number to store in */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the value */
    uint32_t val1 = next_byte(svm);
    uint32_t val2 = next_byte(svm);
    int value = BYTES_TO_ADDR(val1, val2);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE_INT(Reg:%02x) => %04d [Hex:%04x]\n", reg, value, value);

    /* if the register stores a string .. free it */
    clear_string_reg(svm, reg);

    svm->registers[reg].content.integer = value;
    svm->registers[reg].type = INTEGER;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Print the integer contents of the given register.
 */
void op_int_print(struct svm *svm)
{
    /* get the register number to print */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("INT_PRINT(Register %d)\n", reg);

    /* get the register contents. */
    int val = get_int_reg(svm, reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("[STDOUT] Register R%02d => %d [Hex:%04x]\n", reg, val, val);
    else
        jsprintf("0x%04X", val);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Convert the integer contents of a register to a string
 */
void op_int_tostring(struct svm *svm)
{
    /* get the register number to convert */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("INT_TOSTRING(Register %d)\n", reg);

    /* get the contents of the register */
    int cur = get_int_reg(svm, reg);

    /* allocate a buffer. */
    svm->registers[reg].type = STRING;
    svm->registers[reg].content.string = malloc(10);

    /* store the string-value */
    memset(svm->registers[reg].content.string, '\0', 10);
    sprintf(svm->registers[reg].content.string, "%d", cur);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Generate a random integer and store in the specified register.
 */
void op_int_random(struct svm *svm)
{
    /* get the register to save the output to */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("INT_RANDOM(Register %d)\n", reg);

    /**
     * If we already have a string in the register delete it.
     */
    clear_string_reg(svm, reg);

    /* set the value. */
    svm->registers[reg].type = INTEGER;
    svm->registers[reg].content.integer = rand() % 0xFFFF;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store an integer in a register.
 */
void op_float_store(struct svm *svm)
{
    /* get the register number to store in */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the value */
    uint32_t val1 = next_byte(svm);
    uint32_t val2 = next_byte(svm);
    int exp = BYTES_TO_ADDR(val1, val2);
    uint32_t val3 = next_byte(svm);
    uint32_t val4 = next_byte(svm);
    int mant = BYTES_TO_ADDR(val3, val4);

    float value = ldexp((float)mant / 65535, exp);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE_FLOAT(Reg:%02x) => %04f [Hex:%04x]\n", reg, value, *(int *)(&value));

    /* if the register stores a string .. free it */
    clear_string_reg(svm, reg);

    svm->registers[reg].content.number = value;
    svm->registers[reg].type = FLOAT;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Print the integer contents of the given register.
 */
void op_float_print(struct svm *svm)
{
    /* get the register number to print */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("FLOAT_PRINT(Register %d)\n", reg);

    /* get the register contents. */
    float val = get_float_reg(svm, reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("[STDOUT] Register R%02d => %f [Hex:%04x]\n", reg, val, *(int *)(&val));
    else
        jsprintf("%04f", val);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Convert the integer contents of a register to a string
 */
void op_float_tostring(struct svm *svm)
{
    /* get the register number to convert */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("FLOAT_TOSTRING(Register %d)\n", reg);

    /* get the contents of the register */
    float cur = get_float_reg(svm, reg);

    /* allocate a buffer. */
    svm->registers[reg].type = STRING;
    svm->registers[reg].content.string = malloc(10);

    /* store the string-value */
    memset(svm->registers[reg].content.string, '\0', 10);
    sprintf(svm->registers[reg].content.string, "%f", cur);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store a string in a register.
 */
void op_string_store(struct svm *svm)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the string to store */
    char *str = string_from_stack(svm);

    /**
     * If we already have a string in the register delete it.
     */
    clear_string_reg(svm, reg);

    /**
     * Now store the new string.
     */
    svm->registers[reg].type = STRING;
    svm->registers[reg].content.string = str;

    if (getenv("DEBUG") != NULL)
        jsprintf("STRING_STORE(Register %d) = '%s'\n", reg, str);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Print the (string) contents of a register.
 */
void op_string_print(struct svm *svm)
{
    /* get the reg number to print */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("STRING_PRINT(Register %d)\n", reg);

    /* get the contents of the register */
    char *str = get_string_reg(svm, reg);

    /* print */
    if (getenv("DEBUG") != NULL)
        jsprintf("[stdout] register R%02d => %s\n", reg, str);
    else
        jsprintf("%s", str);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Concatenate two strings, and store the result.
 */
void op_string_concat(struct svm *svm)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the source register */
    uint32_t src1 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the source register */
    uint32_t src2 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("STRING_CONCAT(Register:%d = Register:%d + Register:%d)\n",
               reg, src1, src2);

    /*
     * Ensure both source registers have string values.
     */
    char *str1 = get_string_reg(svm, src1);
    char *str2 = get_string_reg(svm, src2);

    /**
     * Allocate RAM for two strings.
     */
    int len = strlen(str1) + strlen(str2) + 1;

    /**
     * Zero.
     */
    char *tmp = malloc(len);
    memset(tmp, '\0', len);

    /**
     * Assign.
     */
    sprintf(tmp, "%s%s", str1, str2);

    /* if the destination-register currently contains a string .. free it */
    clear_string_reg(svm, reg);

    svm->registers[reg].content.string = tmp;
    svm->registers[reg].type = STRING;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Invoke the C system() function against a string register.
 */
void op_string_system(struct svm *svm)
{
    /* get the reg */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("STRING_SYSTEM(Register %d)\n", reg);

    /* Get the value we're to execute */
    char *str = get_string_reg(svm, reg);

    if (getenv("FUZZ") != NULL)
    {
        jsprintf("Fuzzing - skipping execution of: %s\n", str);
        return;
    }

    int result = 0;
    result = system(str);

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Convert a string to an int.
 */
void op_string_toint(struct svm *svm)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("STRING_TOINT(Register:%d)\n", reg);

    /* get the string and convert to integer */
    char *str = get_string_reg(svm, reg);
    int i = atoi(str);

    /* free the old version */
    free(svm->registers[reg].content.string);

    /* set the int. */
    svm->registers[reg].type = INTEGER;
    svm->registers[reg].content.integer = i;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Unconditional jump
 */
void op_jump_to(struct svm *svm)
{
    /**
     * Read the two bytes which will build up the destination
     */
    uint32_t off1 = next_byte(svm);
    uint32_t off2 = next_byte(svm);

    /**
     * Convert to the offset in our code-segment.
     */
    int offset = BYTES_TO_ADDR(off1, off2);

    if (getenv("DEBUG") != NULL)
        jsprintf("JUMP_TO(Offset:%d [Hex:%04X]\n", offset, offset);

    svm->ip = offset;
}

/**
 * Jump to the given address if the Z-flag is set.
 */
void op_jump_z(struct svm *svm)
{
    /**
     * Read the two bytes which will build up the destination
     */
    uint32_t off1 = next_byte(svm);
    uint32_t off2 = next_byte(svm);

    /**
     * Convert to the offset in our code-segment.
     */
    int offset = BYTES_TO_ADDR(off1, off2);

    if (getenv("DEBUG") != NULL)
        jsprintf("JUMP_Z(Offset:%d [Hex:%04X]\n", offset, offset);

    if (svm->jmp)
    {
        svm->ip = offset;
    }
    else
    {
        /* handle the next instruction */
        svm->ip += 1;
    }
}

/**
 * Jump to the given address if the Z flag is NOT set.
 */
void op_jump_nz(struct svm *svm)
{
    /**
     * Read the two bytes which will build up the destination
     */
    uint32_t off1 = next_byte(svm);
    uint32_t off2 = next_byte(svm);

    /**
     * Convert to the offset in our code-segment.
     */
    int offset = BYTES_TO_ADDR(off1, off2);

    if (getenv("DEBUG") != NULL)
        jsprintf("JUMP_NZ(Offset:%d [Hex:%04X]\n", offset, offset);

    if (!svm->jmp)
    {
        svm->ip = offset;
    }
    else
    {
        /* handle the next instruction */
        svm->ip += 1;
    }
}

void reg_add(struct svm *svm, uint8_t out, uint8_t lhs, uint8_t rhs)
{
    if (svm->registers[lhs].type == FLOAT || svm->registers[rhs].type == FLOAT)
    {
        svm->registers[out].content.number =
            (svm->registers[lhs].type == FLOAT ? get_float_reg(svm, lhs) : get_int_reg(svm, lhs)) + (svm->registers[rhs].type == FLOAT ? get_float_reg(svm, rhs) : get_int_reg(svm, rhs));
        svm->registers[out].type = FLOAT;
    }
    else
    {
        svm->registers[out].type = INTEGER;
        svm->registers[out].content.integer = get_int_reg(svm, lhs) + get_int_reg(svm, rhs);
    }
}

void reg_and(struct svm *svm, uint8_t out, uint8_t lhs, uint8_t rhs)
{
    if (svm->registers[lhs].type == FLOAT || svm->registers[rhs].type == FLOAT)
    {
        svm->registers[out].content.number = svm->registers[lhs].content.integer & svm->registers[rhs].content.integer;
        svm->registers[out].type = FLOAT;
    }
    else
    {
        svm->registers[out].type = INTEGER;
        svm->registers[out].content.integer = get_int_reg(svm, lhs) & get_int_reg(svm, rhs);
    }
}

void reg_sub(struct svm *svm, uint8_t out, uint8_t lhs, uint8_t rhs)
{
    if (svm->registers[lhs].type == FLOAT || svm->registers[rhs].type == FLOAT)
    {
        svm->registers[out].content.number =
            (svm->registers[lhs].type == FLOAT ? get_float_reg(svm, lhs) : get_int_reg(svm, lhs)) - (svm->registers[rhs].type == FLOAT ? get_float_reg(svm, rhs) : get_int_reg(svm, rhs));
        svm->registers[out].type = FLOAT;
    }
    else
    {
        svm->registers[out].type = INTEGER;
        svm->registers[out].content.integer = get_int_reg(svm, lhs) - get_int_reg(svm, rhs);
    }
}

void reg_mul(struct svm *svm, uint8_t out, uint8_t lhs, uint8_t rhs)
{
    if (svm->registers[lhs].type == FLOAT || svm->registers[rhs].type == FLOAT)
    {
        svm->registers[out].content.number =
            (svm->registers[lhs].type == FLOAT ? get_float_reg(svm, lhs) : get_int_reg(svm, lhs)) * (svm->registers[rhs].type == FLOAT ? get_float_reg(svm, rhs) : get_int_reg(svm, rhs));
        svm->registers[out].type = FLOAT;
    }
    else
    {
        svm->registers[out].type = INTEGER;
        svm->registers[out].content.integer = get_int_reg(svm, lhs) * get_int_reg(svm, rhs);
    }
}

void reg_xor(struct svm *svm, uint8_t out, uint8_t lhs, uint8_t rhs)
{
    if (svm->registers[lhs].type == FLOAT || svm->registers[rhs].type == FLOAT)
    {
        svm->registers[out].content.number = svm->registers[lhs].content.integer ^ svm->registers[rhs].content.integer;
        svm->registers[out].type = FLOAT;
    }
    else
    {
        svm->registers[out].type = INTEGER;
        svm->registers[out].content.integer = get_int_reg(svm, lhs) ^ get_int_reg(svm, rhs);
    }
}

void reg_or(struct svm *svm, uint8_t out, uint8_t lhs, uint8_t rhs)
{
    if (svm->registers[lhs].type == FLOAT || svm->registers[rhs].type == FLOAT)
    {
        svm->registers[out].content.number = svm->registers[lhs].content.integer | svm->registers[rhs].content.integer;
        svm->registers[out].type = FLOAT;
    }
    else
    {
        svm->registers[out].type = INTEGER;
        svm->registers[out].content.integer = get_int_reg(svm, lhs) | get_int_reg(svm, rhs);
    }
}

/**
 * This is a macro definition for a "math" operation.
 *
 * It's a little longer than I'd usually use for a macro, but it saves
 * all the typing and redundency defining: add, sub, div, mod, xor, or.
 *
 */
void math_operation(struct svm *svm, void (*operator)(struct svm *, uint8_t, uint8_t, uint8_t), const char *ope)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the source register */
    uint32_t src1 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the source register */
    uint32_t src2 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("(Register:%d = Register:%d %s Register:%d)\n", reg, src1, ope, src2);

    /* if the result-register stores a string .. free it */
    clear_string_reg(svm, reg);

    /*
     * Ensure both source registers have integer values.
     */
    operator(svm, reg, src1, src2);

    /**
     * Zero result?
     */
    if (svm->registers[reg].content.integer == 0)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

void op_add(struct svm *in)
{
    // reg_result = reg1 + reg2 ;
    math_operation(in, reg_add, "add");
}

void op_and(struct svm *in)
{
    // reg_result = reg1 & reg2 ;
    math_operation(in, reg_and, "and");
}

void op_sub(struct svm *in)
{
    // reg_result = reg1 - reg2 ;
    math_operation(in, reg_sub, "sub");
}

void op_mul(struct svm *in)
{
    // reg_result = reg1 * reg2 ;
    math_operation(in, reg_mul, "mul");
}

void op_xor(struct svm *in)
{
    // reg_result = reg1 ^ reg2 ;
    math_operation(in, reg_xor, "xor");
}

void op_or(struct svm *in)
{
    // reg_result = reg1 | reg2 ;
    math_operation(in, reg_or, "or");
}

/**
 * Increment the given (integer) register.
 */
void op_inc(struct svm *svm)
{
    /* get the register number to increment */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("INC_OP(Register %d)\n", reg);

    /* get, incr, set */
    int cur = get_int_reg(svm, reg);
    cur += 1;
    svm->registers[reg].content.integer = cur;

    if (svm->registers[reg].content.integer == 0)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Decrement the given (integer) register.
 */
void op_dec(struct svm *svm)
{
    /* get the register number to decrement */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("DEC_OP(Register %d)\n", reg);

    /* get, decr, set */
    int cur = get_int_reg(svm, reg);
    cur -= 1;
    svm->registers[reg].content.integer = cur;

    if (svm->registers[reg].content.integer == 0)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Compare two registers.  Set the Z-flag if equal.
 */
void op_cmp_reg(struct svm *svm)
{
    /* get the source register */
    uint32_t reg1 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg1);

    /* get the source register */
    uint32_t reg2 = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg2);

    if (getenv("DEBUG") != NULL)
        jsprintf("CMP(Register:%d vs Register:%d)\n", reg1, reg2);

    svm->jmp = 0;

    if (svm->registers[reg1].type == svm->registers[reg2].type)
    {
        if (svm->registers[reg1].type == STRING)
        {
            if (strcmp(svm->registers[reg1].content.string,
                       svm->registers[reg2].content.string) == 0)
                svm->jmp = 1;
        }
        else
        {
            if (svm->registers[reg1].content.integer ==
                svm->registers[reg2].content.integer)
                svm->jmp = 1;
        }
    }

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Compare a register contents with a constant integer.
 */
void op_cmp_immediate(struct svm *svm)
{
    /* get the source register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the integer to compare with */
    uint32_t val1 = next_byte(svm);
    uint32_t val2 = next_byte(svm);
    int val = BYTES_TO_ADDR(val1, val2);

    if (getenv("DEBUG") != NULL)
        jsprintf("CMP_IMMEDIATE(Register:%d vs %d [Hex:%04X])\n", reg, val, val);

    svm->jmp = 0;

    int cur = (int)get_int_reg(svm, reg);

    if (cur == val)
        svm->jmp = 1;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Compare a register contents with the given string.
 */
void op_cmp_string(struct svm *svm)
{
    /* get the source register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* Now we get the string to compare against from the stack */
    char *str = string_from_stack(svm);

    /* get the string content from the register */
    char *cur = get_string_reg(svm, reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("Comparing register-%d ('%s') - with string '%s'\n", reg, cur, str);

    /* compare */
    if (strcmp(cur, str) == 0)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Does the given register contain a string?  Set the Z-flag if so.
 */
void op_is_string(struct svm *svm)
{
    /* get the register to test */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("is register %02X a string?\n", reg);

    if (svm->registers[reg].type == STRING)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Does the given register contain an integer?  Set the Z-flag if so.
 */
void op_is_integer(struct svm *svm)
{
    /* get the register to test */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    if (getenv("DEBUG") != NULL)
        jsprintf("is register %02X an integer?\n", reg);

    if (svm->registers[reg].type == INTEGER)
        svm->jmp = 1;
    else
        svm->jmp = 0;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Read from a given address into the specified register.
 */
void op_peek(struct svm *svm)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the address to read from the second register */
    uint32_t addr = next_byte(svm);
    BOUNDS_TEST_REGISTER(addr);

    if (getenv("DEBUG") != NULL)
        jsprintf("LOAD_FROM_RAM(Register:%d will contain contents of address %04X)\n",
               reg, addr);

    /* get the address from the register */
    int adr = get_int_reg(svm, addr);
    if (adr < 0 || adr > 0xffff)
        svm_default_error_handler(svm, "Reading from outside RAM");

    /* Read the value from RAM */
    int val = svm->code[adr];

    /* if the destination currently contains a string .. free it */
    clear_string_reg(svm, reg);

    svm->registers[reg].content.integer = val;
    svm->registers[reg].type = INTEGER;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Write a register-contents to memory.
 */
void op_poke(struct svm *svm)
{
    /* get the destination register */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* get the address to write to from the second register */
    uint32_t addr = next_byte(svm);
    BOUNDS_TEST_REGISTER(addr);

    /* Get the value we're to store. */
    int val = get_int_reg(svm, reg);

    /* Get the address we're to store it in. */
    int adr = get_int_reg(svm, addr);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE_IN_RAM(Address %04X set to %02X)\n", adr, val);

    if (adr < 0 || adr > 0xffff)
        svm_default_error_handler(svm, "Writing outside RAM");

    /* do the necessary */
    svm->code[adr] = val;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Copy a chunk of memory.
 */
void op_memcpy(struct svm *svm)
{
    /* get the register number to store to */
    uint32_t dest_reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(dest_reg);

    /* get the register number to copy from */
    uint32_t src_reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(src_reg);

    /* get the register number with the size */
    uint32_t size_reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(size_reg);

    /**
     * Now handle the copy.
     */
    int src = get_int_reg(svm, src_reg);
    int dest = get_int_reg(svm, dest_reg);
    int size = get_int_reg(svm, size_reg);

    if ((src < 0) || (dest < 0))
    {
        svm_default_error_handler(svm, "cannot copy to/from negative addresses");
        return;
    }

    if (getenv("DEBUG") != NULL)
    {
        jsprintf("Copying %4x bytes from %04x to %04X\n", size, src, dest);
    }

    /** Slow, but copes with nulls and allows debugging. */
    for (int i = 0; i < size; i++)
    {
        int sc = src + i;
        int dt = dest + i;

        /*
         * Handle wrap-around.
         *
         * So copying 0x00FF bytes from 0xFFFE will actually
         * wrap around to 0x00FE.
         */
        while (sc >= 0xFFFF)
            sc -= 0xFFFF;
        while (dt >= 0xFFFF)
            dt -= 0xFFFF;

        if (getenv("DEBUG") != NULL)
        {
            jsprintf("\tCopying from: %04x Copying-to %04X\n", sc, dt);
        }

        svm->code[dt] = svm->code[sc];
    }

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Push the contents of a given register onto the stack.
 */
void op_stack_push(struct svm *svm)
{
    /* get the register to push */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* Get the value we're to store. */
    struct reg_t val = svm->registers[reg];
    if (svm->registers[reg].type == STRING)
    {
        val.content.string = strdup(svm->registers[reg].content.string);
    }

    if (getenv("DEBUG") != NULL)
    {
        if (val.type == INTEGER)
            jsprintf("PUSH(Register %d integer[=%04x])\n", reg, val.content.integer);
        if (val.type == FLOAT)
            jsprintf("PUSH(Register %d number[=%f])\n", reg, val.content.number);
        if (val.type == STRING)
            jsprintf("PUSH(Register %d string[=%s])\n", reg, val.content.string);
    }

    /* store it */
    svm->SP += 1;
    svm->stack[svm->SP] = val;

    /**
     * Ensure the stack hasn't overflown.
     */
    int sp_size = sizeof(svm->stack) / sizeof(svm->stack[0]);
    if (svm->SP >= sp_size)
        svm_default_error_handler(svm, "stack overflow - stack is full");

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Pop the topmost entry from the stack into the given register.
 */
void op_stack_pop(struct svm *svm)
{
    /* get the register to pop */
    uint32_t reg = next_byte(svm);
    BOUNDS_TEST_REGISTER(reg);

    /* ensure we're not outside the stack. */
    if (svm->SP <= 0)
        svm_default_error_handler(svm, "stack overflow - stack is empty");

    /* Get the value from the stack. */
    struct reg_t val = svm->stack[svm->SP];
    svm->SP -= 1;

    if (getenv("DEBUG") != NULL)
    {
        if (val.type == INTEGER)
            jsprintf("POP(Register %d integer[=%04x])\n", reg, val.content.integer);
        if (val.type == FLOAT)
            jsprintf("POP(Register %d number[=%f])\n", reg, val.content.number);
        if (val.type == STRING)
            jsprintf("POP(Register %d string[=%s])\n", reg, val.content.string);
    }

    /* if the register stores a string .. free it */
    clear_string_reg(svm, reg);

    svm->registers[reg] = val;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Return from a call - by popping the return address from the stack
 * and jumping to it.
 */
void op_stack_ret(struct svm *svm)
{
    /* ensure we're not outside the stack. */
    if (svm->CSP <= 0)
        svm_default_error_handler(svm, "stack overflow - stack is empty");

    /* Get the value from the stack. */
    int val = svm->call_stack[svm->CSP];
    svm->CSP -= 1;

    if (getenv("DEBUG") != NULL)
    {
        jsprintf("RET() => %04x\n", val);
    }

    /* update our instruction pointer. */
    svm->ip = val;
}

/**
 * Call a routine - push the return address onto the stack.
 */
void op_stack_call(struct svm *svm)
{
    /**
     * Read the two bytes which will build up the destination
     */
    uint32_t off1 = next_byte(svm);
    uint32_t off2 = next_byte(svm);

    /**
     * Convert to the offset in our code-segment.
     */
    int offset = BYTES_TO_ADDR(off1, off2);

    int sp_size = sizeof(svm->call_stack) / sizeof(svm->call_stack[0]);
    svm->CSP += 1;

    if (svm->CSP >= sp_size)
        svm_default_error_handler(svm, "stack overflow - stack is full!");

    /**
     * Now we've got to save the address past this instruction
     * on the stack so that the "ret(urn)" instruction will go
     * to the correct place.
     */
    svm->call_stack[svm->CSP] = svm->ip + 1;

    /**
     * Now we've saved the return-address we can update the IP
     */
    svm->ip = offset;
}

/**
 * Load an binary values in a register.
 */
void op_binary_load(struct svm *svm)
{
    /* get the destination register */
    uint32_t dst = next_byte(svm);
    BOUNDS_TEST_REGISTER(dst);

    /* get the source binary address */
    uint32_t src = next_byte(svm);
    bound_test(svm, src, BINARY_IN_COUNT);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Reg%02x will be set to contents of Binary%02x)\n", dst, src);

    /* Free the existing string, if present */
    clear_string_reg(svm, dst);

    /* storing a binary (0xFF - 8-bits) as integer */
    svm->registers[dst].type = INTEGER;
    svm->registers[dst].content.integer = BINARY_IN[src];

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store an register integer value in a binary output.
 */
void op_binary_save(struct svm *svm)
{
    /* get the destination register */
    uint32_t src = next_byte(svm);
    BOUNDS_TEST_REGISTER(src);

    /* get the source binary address */
    uint32_t dst = next_byte(svm);
    bound_test(svm, dst, BINARY_OUT_COUNT);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Binary%02x will be set to contents of Reg%02x)\n", dst, src);

    /* storing a binary (0xFF - 8-bits) as integer */
    if (svm->registers[src].type == INTEGER)
        BINARY_OUT[dst] = svm->registers[src].content.integer;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Load an analog value in a register.
 */
void op_analog_load(struct svm *svm)
{
    /* get the destination register */
    uint32_t dst = next_byte(svm);
    BOUNDS_TEST_REGISTER(dst);

    /* get the source analog address */
    uint32_t src = next_byte(svm);
    bound_test(svm, src, ANALOG_IN_COUNT);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Reg%02x will be set to contents of Analog%02x)\n", dst, src);

    /* Free the existing string, if present */
    clear_string_reg(svm, dst);

    /* storing a analog as float */
    svm->registers[dst].type = FLOAT;
    svm->registers[dst].content.number = ANALOG_IN[src];

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store an register integer value in a binary output.
 */
void op_analog_save(struct svm *svm)
{
    /* get the destination register */
    uint32_t src = next_byte(svm);
    BOUNDS_TEST_REGISTER(src);

    /* get the source analog address */
    uint32_t dst = next_byte(svm);
    bound_test(svm, dst, ANALOG_OUT_COUNT);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Analog%02x will be set to contents of Reg%02x)\n", dst, src);

    /* storing a analog as float */
    if (svm->registers[src].type == FLOAT)
        ANALOG_OUT[dst] = svm->registers[src].content.number;
    if (svm->registers[src].type == INTEGER)
        ANALOG_OUT[dst] = svm->registers[src].content.integer;

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Load an variable value in a register.
 */
void op_variable_load(struct svm *svm)
{
    /* get the destination register */
    uint32_t dst = next_byte(svm);
    BOUNDS_TEST_REGISTER(dst);

    /* get the source binary address */
    uint32_t src = next_byte(svm);
    bound_test(svm, src, ANALOG_IN_COUNT);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Reg%02x will be set to contents of Variable%02x)\n", dst, src);

    /* Free the existing string, if present */
    clear_string_reg(svm, dst);

    /* storing a variable in register */
    svm->registers[dst] = VARIABLE_IO[src];

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 * Store an register integer value in a binary output.
 */
void op_variable_save(struct svm *svm)
{
    /* get the destination register */
    uint32_t src = next_byte(svm);
    BOUNDS_TEST_REGISTER(src);

    /* get the source binary address */
    uint32_t dst = next_byte(svm);
    bound_test(svm, dst, ANALOG_OUT_COUNT);

    if (getenv("DEBUG") != NULL)
        jsprintf("STORE(Variable%02x will be set to contents of Reg%02x)\n", dst, src);

    /* storing a variable */
    VARIABLE_IO[dst] = svm->registers[src];

    /* handle the next instruction */
    svm->ip += 1;
}

/**
 ** End implementation of virtual machine opcodes.
 **
 **/

/**
 * Map the opcodes to the handlers.
 */
void opcode_init(svm_t *svm)
{
    /**
     * Initialize the random seed for the rendom opcode (INT_RANDOM)
     */
    srand(time(NULL));

    /**
     * All instructions will default to unknown.
     */
    for (int i = 0; i <= 255; i++)
        svm->opcodes[i] = op_unknown;

    /* early opcodes */
    svm->opcodes[EXIT] = op_exit;

    /* numbers */
    svm->opcodes[INT_STORE] = op_int_store;
    svm->opcodes[INT_PRINT] = op_int_print;
    svm->opcodes[INT_TOSTRING] = op_int_tostring;
    svm->opcodes[INT_RANDOM] = op_int_random;

    svm->opcodes[FLOAT_STORE] = op_float_store;
    svm->opcodes[FLOAT_PRINT] = op_float_print;
    svm->opcodes[FLOAT_TOSTRING] = op_float_tostring;

    svm->opcodes[BINARY_LOAD] = op_binary_load;
    svm->opcodes[BINARY_SAVE] = op_binary_save;
    svm->opcodes[ANALOG_LOAD] = op_analog_load;
    svm->opcodes[ANALOG_SAVE] = op_analog_save;
    svm->opcodes[VARIABLE_LOAD] = op_variable_load;
    svm->opcodes[VARIABLE_SAVE] = op_variable_save;

    /* jumps */
    svm->opcodes[JUMP_TO] = op_jump_to;
    svm->opcodes[JUMP_NZ] = op_jump_nz;
    svm->opcodes[JUMP_Z] = op_jump_z;

    /* math */
    svm->opcodes[ADD] = op_add;
    svm->opcodes[AND] = op_and;
    svm->opcodes[SUB] = op_sub;
    svm->opcodes[MUL] = op_mul;
    svm->opcodes[DIV] = op_divide;
    svm->opcodes[XOR] = op_xor;
    svm->opcodes[OR] = op_or;
    svm->opcodes[INC] = op_inc;
    svm->opcodes[DEC] = op_dec;

    /* strings */
    svm->opcodes[STRING_STORE] = op_string_store;
    svm->opcodes[STRING_PRINT] = op_string_print;
    svm->opcodes[STRING_CONCAT] = op_string_concat;
    svm->opcodes[STRING_SYSTEM] = op_string_system;
    svm->opcodes[STRING_TOINT] = op_string_toint;

    /* comparisons/tests */
    svm->opcodes[CMP_REG] = op_cmp_reg;
    svm->opcodes[CMP_IMMEDIATE] = op_cmp_immediate;
    svm->opcodes[CMP_STRING] = op_cmp_string;
    svm->opcodes[IS_STRING] = op_is_string;
    svm->opcodes[IS_INTEGER] = op_is_integer;

    /* misc */
    svm->opcodes[NOP] = op_nop;
    svm->opcodes[STORE_REG] = op_reg_store;

    /* PEEK/POKE */
    svm->opcodes[PEEK] = op_peek;
    svm->opcodes[POKE] = op_poke;
    svm->opcodes[MEMCPY] = op_memcpy;

    /* stack */
    svm->opcodes[STACK_PUSH] = op_stack_push;
    svm->opcodes[STACK_POP] = op_stack_pop;
    svm->opcodes[STACK_RET] = op_stack_ret;
    svm->opcodes[STACK_CALL] = op_stack_call;
}

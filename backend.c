//backend.c
/*
 * ============================================================
 *  CPU Instruction Cycle Simulator — Backend (C Implementation)
 *  Based on Von Neumann Architecture
 *  Simulates: Fetch → Decode → Execute cycle
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Constants ──────────────────────────────────────────── */
#define MEMORY_SIZE     500
#define MAX_PROGRAM     100
#define MAX_LINE_LEN    64

/* ─── Opcode Enumeration ─────────────────────────────────── */
typedef enum {
    OP_LOAD  = 0,   /* AC = mem[addr]          */
    OP_STORE = 1,   /* mem[addr] = AC           */
    OP_ADD   = 2,   /* AC = AC + mem[addr]      */
    OP_SUB   = 3,   /* AC = AC - mem[addr]      */
    OP_MUL   = 4,   /* AC = AC * mem[addr]      */
    OP_AND   = 5,   /* AC = AC & mem[addr]      */
    OP_OR    = 6,   /* AC = AC | mem[addr]      */
    OP_JMP   = 7,   /* PC = addr (unconditional)*/
    OP_HALT  = 8,   /* Stop execution           */
    OP_NOP   = 9,   /* No operation             */
    OP_UNKNOWN = -1
} Opcode;

/* ─── CPU Status Flags ───────────────────────────────────── */
typedef struct {
    int zero;       /* Set if result is 0       */
    int carry;      /* Set on unsigned overflow  */
    int negative;   /* Set if result is negative */
    int overflow;   /* Set on signed overflow    */
} Flags;

/* ─── Instruction Structure ──────────────────────────────── */
typedef struct {
    Opcode opcode;
    int    operand;          /* Memory address or immediate value */
    char   mnemonic[16];     /* String form, e.g. "LOAD"          */
} Instruction;

/* ─── CPU State ──────────────────────────────────────────── */
typedef struct {
    int         PC;          /* Program Counter                   */
    int         AC;          /* Accumulator                       */
    Instruction IR;          /* Instruction Register              */
    Flags       flags;       /* Status flags                      */
    
    // Pipeline Registers
    int         ALU_out;     /* Output from ALU                   */
    int         Mem_out;     /* Data read from memory             */
    int         halt_signal; /* Signal to stop simulation         */
} CPU;

/* ─── Global Memory ──────────────────────────────────────── */
int memory[MEMORY_SIZE];

/* ─── Program Storage (decoded instructions) ─────────────── */
Instruction program[MAX_PROGRAM];
int         program_size = 0;

/* ─── Utility: String to Opcode ──────────────────────────── */
Opcode parse_opcode(const char *str) {
    if (strcmp(str, "LOAD")  == 0) return OP_LOAD;
    if (strcmp(str, "STORE") == 0) return OP_STORE;
    if (strcmp(str, "ADD")   == 0) return OP_ADD;
    if (strcmp(str, "SUB")   == 0) return OP_SUB;
    if (strcmp(str, "MUL")   == 0) return OP_MUL;
    if (strcmp(str, "AND")   == 0) return OP_AND;
    if (strcmp(str, "OR")    == 0) return OP_OR;
    if (strcmp(str, "JMP")   == 0) return OP_JMP;
    if (strcmp(str, "HALT")  == 0) return OP_HALT;
    if (strcmp(str, "NOP")   == 0) return OP_NOP;
    return OP_UNKNOWN;
}

/* ─── Load Program from Array of Strings ─────────────────── */
void load_program(const char *lines[], int count) {
    program_size = 0;
    for (int i = 0; i < count && i < MAX_PROGRAM; i++) {
        char mnemonic[16] = {0};
        int  operand = 0;
        int  has_operand = 0;

        if (sscanf(lines[i], "%15s %d", mnemonic, &operand) == 2)
            has_operand = 1;
        else
            sscanf(lines[i], "%15s", mnemonic);

        program[program_size].opcode  = parse_opcode(mnemonic);
        program[program_size].operand = has_operand ? operand : 0;
        strncpy(program[program_size].mnemonic, mnemonic, 15);
        program_size++;
    }
}

/* ─── CPU Initialisation ─────────────────────────────────── */
void cpu_init(CPU *cpu) {
    cpu->PC            = 0;
    cpu->AC            = 0;
    cpu->IR.opcode     = OP_NOP;
    cpu->IR.operand    = 0;
    cpu->IR.mnemonic[0]= '\0';
    cpu->flags.zero    = 0;
    cpu->flags.carry   = 0;
    cpu->flags.negative= 0;
    cpu->flags.overflow= 0;
    cpu->ALU_out       = 0;
    cpu->Mem_out       = 0;
    cpu->halt_signal   = 0;
}

/* ─── Memory Bounds Check ────────────────────────────────── */
int safe_mem_read(int addr) {
    if (addr < 0 || addr >= MEMORY_SIZE) {
        fprintf(stderr, "  [ERROR] Memory read out of bounds: addr=%d\n", addr);
        return 0;
    }
    return memory[addr];
}

void safe_mem_write(int addr, int value) {
    if (addr < 0 || addr >= MEMORY_SIZE) {
        fprintf(stderr, "  [ERROR] Memory write out of bounds: addr=%d\n", addr);
        return;
    }
    memory[addr] = value;
}

/* ─── Update Status Flags ────────────────────────────────── */
void update_flags(CPU *cpu, int result) {
    cpu->flags.zero     = (result == 0) ? 1 : 0;
    cpu->flags.negative = (result <  0) ? 1 : 0;
    /* Carry and overflow set by caller if needed */
}

/* ─── Print CPU State ────────────────────────────────────── */
void print_state(const CPU *cpu) {
    printf("  ┌──────────────────────────────────────┐\n");
    printf("  │  PC=%-4d  AC=%-6d  IR=\"%s %s\"    \n",
           cpu->PC, cpu->AC,
           cpu->IR.mnemonic,
           (cpu->IR.opcode != OP_HALT && cpu->IR.opcode != OP_NOP)
               ? "" : "");
    printf("  │  Flags: Z=%d  C=%d  N=%d  V=%d\n",
           cpu->flags.zero, cpu->flags.carry,
           cpu->flags.negative, cpu->flags.overflow);
    printf("  └──────────────────────────────────────┘\n");
}

/* ─── FETCH Stage ────────────────────────────────────────── */
void fetch(CPU *cpu) {
    if (cpu->PC < 0 || cpu->PC >= program_size) {
        fprintf(stderr, "  [HALT] PC=%d out of program bounds.\n", cpu->PC);
        cpu->IR.opcode = OP_HALT;
        return;
    }
    cpu->IR = program[cpu->PC];
    printf("  [FETCH]   PC=%d → IR=\"%s",
           cpu->PC, cpu->IR.mnemonic);
    if (cpu->IR.opcode != OP_HALT && cpu->IR.opcode != OP_NOP)
        printf(" %d", cpu->IR.operand);
    printf("\"\n");
    cpu->PC++;   /* Advance PC after fetch */
    printf("            PC incremented → PC=%d\n", cpu->PC);
}

/* ─── DECODE Stage ───────────────────────────────────────── */
void decode(const CPU *cpu) {
    const char *type;
    switch (cpu->IR.opcode) {
        case OP_LOAD:
        case OP_STORE:  type = "Memory Access";  break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_AND:
        case OP_OR:     type = "ALU Operation";  break;
        case OP_JMP:    type = "Control Flow";   break;
        case OP_HALT:   type = "Control (Halt)"; break;
        default:        type = "Unknown";        break;
    }
    printf("  [DECODE]  opcode=\"%s\"  operand=%d  type=%s\n",
           cpu->IR.mnemonic, cpu->IR.operand, type);
}

/* ─── ALU — Perform Arithmetic/Logic Operation ───────────── */
int alu_operate(CPU *cpu, Opcode op, int operand_val) {
    int result = cpu->AC;
    cpu->flags.carry    = 0;
    cpu->flags.overflow = 0;

    switch (op) {
        case OP_ADD:
            result = cpu->AC + operand_val;
            /* Detect unsigned carry */
            if ((unsigned)result < (unsigned)cpu->AC) cpu->flags.carry = 1;
            /* Detect signed overflow */
            if (((cpu->AC ^ result) & (operand_val ^ result)) < 0) cpu->flags.overflow = 1;
            break;
        case OP_SUB:
            result = cpu->AC - operand_val;
            if (operand_val > cpu->AC) cpu->flags.carry = 1;
            break;
        case OP_MUL:
            result = cpu->AC * operand_val;
            break;
        case OP_AND:
            result = cpu->AC & operand_val;
            break;
        case OP_OR:
            result = cpu->AC | operand_val;
            break;
        default:
            break;
    }
    return result;
}

/* ─── EXECUTE Stage ──────────────────────────────────────── */
void execute(CPU *cpu) {
    int addr = cpu->IR.operand;
    int mem_val;

    switch (cpu->IR.opcode) {
        case OP_LOAD:
        case OP_STORE:
            // Calculate effective address
            cpu->ALU_out = addr;
            printf("  [EXECUTE] Address calc: %d\n", addr);
            break;

        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_AND:
        case OP_OR:
            // For a strict 5-stage pipeline, MEM value should be read in MEM stage.
            // But since this is a simple accumulator architecture where operands
            // are in memory directly (not registers), we simulate it as:
            mem_val = safe_mem_read(addr); 
            cpu->ALU_out = alu_operate(cpu, cpu->IR.opcode, mem_val);
            printf("  [EXECUTE] %s: %d %s mem[%d](%d) = %d\n",
                   cpu->IR.mnemonic, cpu->AC,
                   (cpu->IR.opcode == OP_ADD) ? "+" :
                   (cpu->IR.opcode == OP_SUB) ? "-" :
                   (cpu->IR.opcode == OP_MUL) ? "*" :
                   (cpu->IR.opcode == OP_AND) ? "&" : "|",
                   addr, mem_val, cpu->ALU_out);
            break;

        case OP_JMP:
            cpu->ALU_out = addr;
            printf("  [EXECUTE] JMP target calc: %d\n", addr);
            break;

        case OP_NOP:
            printf("  [EXECUTE] NOP: no operation\n");
            break;

        case OP_HALT:
            printf("  [EXECUTE] HALT signal generated.\n");
            cpu->halt_signal = 1;
            break;

        default:
            fprintf(stderr, "  [ERROR] Unknown opcode in execute: %d\n", cpu->IR.opcode);
            break;
    }
}

/* ─── MEMORY Stage ───────────────────────────────────────── */
void memory_access(CPU *cpu) {
    switch (cpu->IR.opcode) {
        case OP_LOAD:
            cpu->Mem_out = safe_mem_read(cpu->ALU_out);
            printf("  [MEMORY]  Read mem[%d] = %d\n", cpu->ALU_out, cpu->Mem_out);
            break;
        case OP_STORE:
            safe_mem_write(cpu->ALU_out, cpu->AC);
            printf("  [MEMORY]  Write mem[%d] = %d\n", cpu->ALU_out, cpu->AC);
            break;
        default:
            printf("  [MEMORY]  No memory access needed\n");
            break;
    }
}

/* ─── WRITE BACK Stage ────────────────────────────────────── */
void write_back(CPU *cpu) {
    switch (cpu->IR.opcode) {
        case OP_LOAD:
            cpu->AC = cpu->Mem_out;
            update_flags(cpu, cpu->AC);
            printf("  [WRITEBK] AC = %d\n", cpu->AC);
            break;

        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_AND:
        case OP_OR:
            cpu->AC = cpu->ALU_out;
            update_flags(cpu, cpu->AC);
            printf("  [WRITEBK] AC = %d\n", cpu->AC);
            break;

        case OP_JMP:
            cpu->PC = cpu->ALU_out;
            printf("  [WRITEBK] PC updated to %d\n", cpu->PC);
            break;

        case OP_HALT:
        case OP_NOP:
        case OP_STORE:
        default:
            printf("  [WRITEBK] No writeback needed\n");
            break;
    }
}

/* ─── Main Simulation Loop ───────────────────────────────── */
void run_simulation(CPU *cpu) {
    int cycle = 0;

    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   CPU Instruction Cycle Simulator        ║\n");
    printf("║   5-Stage Pipeline Architecture          ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    while (!cpu->halt_signal) {
        printf("━━━ Cycle %-3d ━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n", ++cycle);

        fetch(cpu);
        if (cpu->IR.opcode == OP_HALT) {
            cpu->halt_signal = 1;
            break;
        }

        decode(cpu);
        execute(cpu);
        memory_access(cpu);
        write_back(cpu);

        print_state(cpu);
        printf("\n");

        if (cycle >= 10000) {
            fprintf(stderr, "[WARN] Cycle limit reached (10000). Force halting.\n");
            break;
        }
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Simulation complete. Total cycles: %d\n", cycle);
    printf("Final State → PC=%d | AC=%d\n", cpu->PC, cpu->AC);
    printf("Flags → Z=%d C=%d N=%d V=%d\n",
           cpu->flags.zero, cpu->flags.carry,
           cpu->flags.negative, cpu->flags.overflow);
}

/* ─── Entry Point ────────────────────────────────────────── */
int main(void) {
    CPU cpu;
    cpu_init(&cpu);

    /* ── Initialise memory with data values ── */
    memset(memory, 0, sizeof(memory));
    memory[200] = 15;    /* First operand  */
    memory[201] = 8;     /* Second operand */
    memory[202] = 3;     /* Third operand  */

    /* ── Define the program ── */
    const char *source[] = {
        "LOAD 200",      /* AC = 15           */
        "ADD 201",       /* AC = 15 + 8 = 23  */
        "SUB 202",       /* AC = 23 - 3 = 20  */
        "STORE 300",     /* mem[300] = 20     */
        "LOAD 201",      /* AC = 8            */
        "ADD 202",       /* AC = 8 + 3 = 11   */
        "STORE 301",     /* mem[301] = 11     */
        "HALT"
    };
    int src_count = sizeof(source) / sizeof(source[0]);

    printf("Program (%d instructions):\n", src_count);
    for (int i = 0; i < src_count; i++)
        printf("  [%02d] %s\n", i, source[i]);
    printf("\nMemory Preload:\n");
    printf("  mem[200]=%d  mem[201]=%d  mem[202]=%d\n\n",
           memory[200], memory[201], memory[202]);

    /* ── Load and run ── */
    load_program(source, src_count);
    run_simulation(&cpu);

    /* ── Show memory results ── */
    printf("\nMemory Results:\n");
    printf("  mem[300] = %d (expected 20)\n", memory[300]);
    printf("  mem[301] = %d (expected 11)\n", memory[301]);

    return 0;
}

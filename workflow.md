# CPU Instruction Cycle Simulator — Workflow Documentation

## Overview

This document describes the complete execution workflow of the CPU Instruction Cycle Simulator, tracing data and control flow from program input through each stage of the Von Neumann Fetch→Decode→Execute cycle.

---

## 1. System Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     PROGRAM INPUT                               │
│   Source text: "LOAD 200", "ADD 201", "HALT", ...               │
└───────────────────────────┬─────────────────────────────────────┘
                            │  load_program()
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                   INSTRUCTION MEMORY                            │
│   program[0] = {OP_LOAD, 200}                                   │
│   program[1] = {OP_ADD,  201}                                   │
│   program[N] = {OP_HALT, 0  }                                   │
└───────────────────────────┬─────────────────────────────────────┘
                            │
              ┌─────────────▼─────────────┐
              │       MAIN LOOP           │
              │  while (!halt) {          │
              │    fetch()                │
              │    decode()               │
              │    halt = execute()       │
              │  }                        │
              └─────────────┬─────────────┘
                            │
      ┌─────────────────────┼─────────────────────┐
      │                     │                     │
      ▼                     ▼                     ▼
 ┌─────────┐         ┌──────────┐         ┌──────────────┐
 │  FETCH  │         │  DECODE  │         │   EXECUTE    │
 │         │         │          │         │              │
 │PC→IR    │ ──────▶ │opcode+   │ ──────▶ │ ALU / Mem /  │
 │PC++     │         │operand   │         │ JMP / HALT   │
 └─────────┘         └──────────┘         └──────┬───────┘
                                                  │
                    ┌─────────────────────────────┤
                    │                             │
                    ▼                             ▼
             ┌─────────────┐             ┌──────────────┐
             │  REGISTERS  │             │    MEMORY    │
             │  PC, AC, IR │             │ mem[0..499]  │
             │  Flags      │             │              │
             └─────────────┘             └──────────────┘
```

---

## 2. Stage-by-Stage Workflow

### Stage 1 — FETCH

**Purpose:** Retrieve the next instruction from program memory using the Program Counter.

**Steps:**
1. Read `program[PC]` → load into Instruction Register (IR)
2. Copy opcode and operand into `cpu->IR`
3. Increment `PC` by 1 (PC++)
4. Log: `[FETCH] PC=N → IR="OPCODE OPERAND"`

**Data flow:**
```
program[PC]  ──▶  cpu->IR.opcode
                  cpu->IR.operand
                  cpu->IR.mnemonic
PC           ──▶  PC + 1
```

**Boundary check:** If `PC >= program_size`, IR is set to HALT and a warning is issued.

---

### Stage 2 — DECODE

**Purpose:** Interpret the instruction sitting in IR — identify what operation to perform and on what data.

**Steps:**
1. Read `cpu->IR.opcode`
2. Classify as: Memory Access, ALU Operation, Control Flow, or Halt
3. Log the opcode, operand, and type

**No registers are modified at this stage.** Decode is purely an identification step.

**Classification table:**

| Opcode        | Category        |
|---------------|-----------------|
| LOAD, STORE   | Memory Access   |
| ADD, SUB, MUL, AND, OR | ALU Operation |
| JMP           | Control Flow    |
| HALT          | Control (Halt)  |
| NOP           | No Operation    |

---

### Stage 3 — EXECUTE

**Purpose:** Carry out the decoded instruction — route to the correct operation and update CPU state.

**Dispatch logic (switch-case on opcode):**

```
opcode == LOAD   →  AC = memory[operand]
opcode == STORE  →  memory[operand] = AC
opcode == ADD    →  result = ALU(AC + memory[operand])  → AC = result
opcode == SUB    →  result = ALU(AC - memory[operand])  → AC = result
opcode == MUL    →  result = ALU(AC * memory[operand])  → AC = result
opcode == AND    →  result = ALU(AC & memory[operand])  → AC = result
opcode == OR     →  result = ALU(AC | memory[operand])  → AC = result
opcode == JMP    →  PC = operand
opcode == HALT   →  return 1  (signal loop to stop)
```

**After ALU operations**, `update_flags()` is called:
- Zero flag (Z) ← set if result == 0
- Negative flag (N) ← set if result < 0
- Carry flag (C) ← set on unsigned overflow (ADD)
- Overflow flag (V) ← set on signed overflow (ADD)

---

## 3. Complete Cycle Trace (Example Program)

**Memory Preload:** `mem[200]=15`, `mem[201]=8`, `mem[202]=3`

| Cycle | Instruction | Fetch | Decode | Execute | AC | PC |
|-------|-------------|-------|--------|---------|----|----|
| 1 | LOAD 200 | IR←program[0], PC→1 | OP=LOAD, addr=200 | AC=mem[200]=15 | 15 | 1 |
| 2 | ADD 201 | IR←program[1], PC→2 | OP=ADD, addr=201 | AC=15+8=23 | 23 | 2 |
| 3 | SUB 202 | IR←program[2], PC→3 | OP=SUB, addr=202 | AC=23-3=20 | 20 | 3 |
| 4 | STORE 300 | IR←program[3], PC→4 | OP=STORE, addr=300 | mem[300]=20 | 20 | 4 |
| 5 | LOAD 201 | IR←program[4], PC→5 | OP=LOAD, addr=201 | AC=mem[201]=8 | 8 | 5 |
| 6 | ADD 202 | IR←program[5], PC→6 | OP=ADD, addr=202 | AC=8+3=11 | 11 | 6 |
| 7 | STORE 301 | IR←program[6], PC→7 | OP=STORE, addr=301 | mem[301]=11 | 11 | 7 |
| 8 | HALT | IR←program[7], PC→8 | OP=HALT | Loop exits | 11 | 8 |

---

## 4. ALU Sub-Workflow

The ALU (`alu_operate()`) is invoked only during arithmetic/logic execute stages:

```
Input:  cpu->AC  (current accumulator)
        operand_val = memory[IR.operand]
        op = IR.opcode

Reset:  carry=0, overflow=0

Switch(op):
  ADD → result = AC + operand_val
        if unsigned overflow → carry=1
        if signed overflow   → overflow=1

  SUB → result = AC - operand_val
        if operand_val > AC → carry=1

  MUL → result = AC * operand_val

  AND → result = AC & operand_val

  OR  → result = AC | operand_val

Output: result (written back to AC by execute())
        flags updated by update_flags()
```

---

## 5. Memory Access Workflow

```
LOAD addr:
  1. Validate addr (0 ≤ addr < 500)
  2. AC ← memory[addr]
  3. update_flags(AC)

STORE addr:
  1. Validate addr (0 ≤ addr < 500)
  2. memory[addr] ← AC
  3. (flags not changed on STORE)
```

Out-of-bounds access prints an error and reads/writes are silently skipped.

---

## 6. Control Flow (JMP)

```
JMP addr:
  1. PC ← addr     (unconditional branch)
  2. Next fetch will read program[addr]
  3. No flags modified
```

JMP allows loops and subroutine-like structures within the program.

---

## 7. Error and Edge Cases

| Condition | Behavior |
|-----------|----------|
| PC >= program_size | IR set to HALT, warning printed |
| Memory addr < 0 or >= 500 | Error printed, value returned as 0 |
| Unknown opcode string | `OP_UNKNOWN` assigned, error at execute |
| Cycle count > 10,000 | Force halt with warning (infinite loop guard) |
| JMP to invalid address | Caught next cycle by PC bounds check |

---

## 8. File Responsibilities

| File | Role |
|------|------|
| `main.c` | Entry point, memory init, program load, calls `run_simulation()` |
| `cpu.h` | CPU struct, Flags struct, Instruction struct, Opcode enum |
| `memory.h` | `safe_mem_read()`, `safe_mem_write()` with bounds checks |
| `fetch.c` | `fetch()` — reads program[PC], loads IR, increments PC |
| `decode.c` | `decode()` — classifies instruction, logs type |
| `execute.c` | `execute()` — dispatches to ALU or memory, updates registers |
| `alu.c` | `alu_operate()` — arithmetic and logic operations, flag updates |
| `flags.c` | `update_flags()` — updates Z, N flags from result |

---

## 9. Build & Run

```bash
# Compile
gcc -Wall -o cpu_sim main.c fetch.c decode.c execute.c alu.c flags.c

# Run
./cpu_sim

# Expected output
# Cycle-by-cycle FETCH/DECODE/EXECUTE logs
# Final state: PC, AC, Flags
# Memory verification
```

---

## 10. Extending the Simulator

- **Add instructions:** Add entry to `Opcode` enum → handle in `parse_opcode()` and `execute()`
- **Conditional JMP:** Add `JZ`, `JN` opcodes that check flags before setting PC
- **Subroutines:** Add `CALL`/`RET` with a stack pointer register
- **Pipeline simulation:** Split stages across separate threads with inter-stage buffers
- **GUI/Web frontend:** Map register/memory state to JavaScript objects for browser rendering (see `frontend.html`)

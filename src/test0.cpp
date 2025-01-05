#include <iostream>
#include <vector>
#include <string>
#include <queue>

using namespace std;

// Constants
const int NUM_REGISTERS = 32;
const int MEMORY_SIZE = 32;

// Data Structures
struct Instruction {
    string op;      // Operation (e.g., lw, sw, add, sub, beq, nop)
    int rd, rs, rt; // Registers
    int immediate;  // Immediate value for lw/sw/beq
    int address;    // Memory address for lw/sw
};

struct CPU {
    vector<int> registers;
    vector<int> memory;
    vector<Instruction> instructions;
    
    // Pipeline stages: IF, ID, EX, MEM, WB
    queue<Instruction> pipeline[5];
    
    int pc;    // Program counter
    int cycle; // Cycle count
    
    // Record output for each clock cycle
    vector<string> clockOutput;

    CPU() {
        registers.resize(NUM_REGISTERS, 1);
        registers[0] = 0;  // $0 is always 0
        memory.resize(MEMORY_SIZE, 1);
        pc = 0;
        cycle = 0;
    }

    void loadInstructions(const vector<Instruction>& insts) {
        instructions = insts;
    }

    void simulate();
    void fetch();
    bool decode(); // Modified to return stall flag
    void execute();
    void memoryAccess();
    void writeBack();
    void advancePipeline();
    void printPipeline();
    void printFinalResults();
};

// Helper function to print control signals
string controlSignals(const string& op) {
    if (op == "lw")  return "RegDst=0 ALUSrc=1 Branch=0 MemRead=1 MemWrite=0 RegWrite=1 MemToReg=1";
    if (op == "sw")  return "RegDst=X ALUSrc=1 Branch=0 MemRead=0 MemWrite=1 RegWrite=0 MemToReg=X";
    if (op == "add") return "RegDst=1 ALUSrc=0 Branch=0 MemRead=0 MemWrite=0 RegWrite=1 MemToReg=0";
    if (op == "sub") return "RegDst=1 ALUSrc=0 Branch=0 MemRead=0 MemWrite=0 RegWrite=1 MemToReg=0";
    if (op == "beq") return "RegDst=X ALUSrc=0 Branch=1 MemRead=0 MemWrite=0 RegWrite=0 MemToReg=X";
    if (op == "nop") return "NOP";
    return "RegDst=X ALUSrc=X Branch=X MemRead=X MemWrite=X RegWrite=X MemToReg=X";
    
    /*
    if (op == "lw")  return "01 010 11";
    if (op == "sw")  return "X1 001 0X";
    if (op == "add") return "10 000 10";
    if (op == "sub") return "10 000 10";
    if (op == "beq") return "X0 100 0X";
    if (op == "nop") return "NOP";
    return "XX XXX XX";
    */
}

// Fetch Instruction (IF)
void CPU::fetch() {
    // Check if PC is within instruction range
    if (pc < (int)instructions.size()) {
        Instruction inst = instructions[pc];
        pipeline[0].push(inst);           // Place into IF stage (pipeline[0])
        clockOutput.push_back(inst.op + " IF");
        pc++; // Point to the next instruction (Predict Not Taken: fetch next)
    }
}

// Decode Instruction (ID) + Hazard Detection
bool CPU::decode() {
    if (pipeline[0].empty()) return false;

    Instruction inst = pipeline[0].front();

    bool needsStall = false;

    // ------------------------------------------------------------
    // Forwarding / Hazard Detection:
    // ------------------------------------------------------------

    // Check EX stage (pipeline[2])
    if (!pipeline[2].empty()) {
        Instruction prevEX = pipeline[2].front();
        
        bool prevEXWritesRegister =
            (prevEX.op == "add" || prevEX.op == "sub" || prevEX.op == "lw");

        if (prevEXWritesRegister && prevEX.rd != 0) {
            if (prevEX.rd == inst.rs || prevEX.rd == inst.rt) {
                if (prevEX.op == "lw") {
                    needsStall = true;
                } else {
                    cout << "[Forwarding from EX] " << inst.op << " depends on " 
                         << prevEX.op << " (rd=" << prevEX.rd << ")\n";
                }
            }
        }
    }

    // Check MEM stage (pipeline[3])
    if (!pipeline[3].empty()) {
        Instruction prevMEM = pipeline[3].front();
        
        bool prevMEMWritesRegister =
            (prevMEM.op == "add" || prevMEM.op == "sub" || prevMEM.op == "lw");
        
        if (prevMEMWritesRegister && prevMEM.rd != 0) {
            if (prevMEM.rd == inst.rs || prevMEM.rd == inst.rt) {
                cout << "[Forwarding from MEM] " << inst.op << " depends on " 
                     << prevMEM.op << " (rd=" << prevMEM.rd << ")\n";
            }
        }
    }

    // NEW: Check if 'beq' depends on 'lw' result from WB
    if (inst.op == "beq") {
        if (!pipeline[3].empty()) {
            Instruction prevMEM = pipeline[3].front();

            if ((prevMEM.op == "add" || prevMEM.op == "sub" || prevMEM.op == "lw") && (prevMEM.rd == inst.rs || prevMEM.rd == inst.rt)) {
                needsStall = true;
                cout << "[Stall for BEQ] Waiting for " << prevMEM.op 
                    << " (rd=" << prevMEM.rd << ") to write back\n";
            }
        }
    }
    if (needsStall) {
        cout << "Cycle " << cycle << ": Data hazard detected, stalling pipeline\n";
        // Insert a bubble (NOP) into the ID stage
        Instruction bubble = {"nop", -1, -1, -1, -1};
        pipeline[1].push(bubble);
        clockOutput.push_back("NOP inserted due to stall");
        return true; // Indicate that a stall occurred
    }

    // No stall needed, proceed normally
    pipeline[0].pop();
    pipeline[1].push(inst);
    clockOutput.push_back(inst.op + " ID");
    return false; // No stall
}

// Execute Instruction (EX)
void CPU::execute() {
    if (pipeline[1].empty()) return;

    Instruction inst = pipeline[1].front();
    pipeline[1].pop();

    if (inst.op == "nop") {
        pipeline[2].push(inst);
        clockOutput.push_back("NOP EX");
        return;
    }

    // If beq, determine branch
    if (inst.op == "beq") {
        int rsValue = registers[inst.rs];
        int rtValue = registers[inst.rt];

        // Predict Not Taken: if branch is taken, flush IF stage
        if (rsValue == rtValue) {
            // Update PC with immediate (relative to current PC)
            pc += (inst.immediate-1);

            // Flush IF stage
            while (!pipeline[0].empty()) {
                pipeline[0].pop();
            }

            cout << "[Branch taken] Flushing IF, PC set to " << pc << "\n";
        } else {
            cout << "[Branch not taken] Continue\n";
        }
    }
    else if (inst.op == "add") {
        registers[inst.rd] = registers[inst.rs] + registers[inst.rt];
    }
    else if (inst.op == "sub") {
        registers[inst.rd] = registers[inst.rs] - registers[inst.rt];
    }
    // lw and sw handle memory addresses elsewhere

    pipeline[2].push(inst);
    clockOutput.push_back(inst.op + " EX " + controlSignals(inst.op));
}

// Memory Access (MEM)
void CPU::memoryAccess() {
    if (pipeline[2].empty()) return;

    Instruction inst = pipeline[2].front();
    pipeline[2].pop();

    if (inst.op == "nop") {
        pipeline[3].push(inst);
        clockOutput.push_back("NOP MEM");
        return;
    }

    if (inst.op == "lw") {
        // Load from memory address / 4
        registers[inst.rd] = memory[inst.address / 4];
    }
    else if (inst.op == "sw") {
        // Store to memory address / 4
        memory[inst.address / 4] = registers[inst.rs];
    }

    pipeline[3].push(inst);
    clockOutput.push_back(inst.op + " MEM " + controlSignals(inst.op));
}

// Write Back (WB)
void CPU::writeBack() {
    if (pipeline[3].empty()) return;

    Instruction inst = pipeline[3].front();
    pipeline[3].pop();

    if (inst.op == "nop") {
        clockOutput.push_back("NOP WB");
        return;
    }

    // add, sub, lw have already written back in EX/MEM stages
    // Just log the WB stage
    clockOutput.push_back(inst.op + " WB " + controlSignals(inst.op));
}

// Pipeline Advancement
void CPU::advancePipeline() {
    clockOutput.clear();

    // Process pipeline stages
    writeBack();
    memoryAccess();
    execute();

    // Decode and handle stalls
    bool stall = decode();

    // Fetch only if no stall
    if (!stall) {
        fetch();
    }

    cout << "Clock Cycle " << ++cycle << ":\n";
    for (const auto& out : clockOutput) {
        cout << out << endl;
    }
    cout << endl;
}

// Print Final Results
void CPU::printFinalResults() {
    cout << "## Final Result:\n";
    cout << "Total Cycles: " << cycle << endl;

    cout << "Final Register Values:\n";
    for (int i = 0; i < NUM_REGISTERS; i++) {
        cout << registers[i] << " ";
    }
    cout << endl;

    cout << "Final Memory Values:\n";
    for (int i = 0; i < MEMORY_SIZE; i++) {
        cout << memory[i] << " ";
    }
    cout << endl;
}

// Simulate CPU Execution
void CPU::simulate() {
    // Continue until pipeline is empty and no more instructions to fetch
    while ( pc < (int)instructions.size() ||
            !pipeline[0].empty() ||
            !pipeline[1].empty() ||
            !pipeline[2].empty() ||
            !pipeline[3].empty() )
    {
        advancePipeline();
    }
    printFinalResults();
}

int main() {
    CPU cpu;

    // Instruction example (modifiable as needed)
    vector<Instruction> instructions = {
        {"sub", 1, 4, 4, -1},
        {"beq", -1, 1, 2, 2},
        {"add", 2, 3, 3, -1},
        {"lw", 1, 0, -1, 4},
        {"add", 4, 5, 6, -1},
        /*
// 1
        {"lw", 2, 0, -1, 8},
        {"lw", 3, 0, -1, 16},
        {"add", 6, 4, 5, -1},
        {"sw", 6, 0, -1, 24},
        
        lw$2, 8($0)
        lw$3, 16($0)
        add $6, $4, $5
        sw$6, 24($0)
        

// 2
        {"lw", 2, 0, -1, 8},
        {"lw", 3, 0, -1, 16},
        {"add", 4, 2, 3, -1},
        {"sw", 4, 0, -1, 24},
        
        lw$2, 8($0)
        lw$3, 16($0)
        add $4, $2, $3
        sw$4, 24($0)
        

// 3
        {"lw", 2, 0, -1, 8},
        {"lw", 3, 0, -1, 16},
        {"beq", -1, 2, 3, 1},
        {"add", 4, 2, 3, -1},
        {"sw", 4, 0, -1, 24},
        
        lw$2, 8($0)
        lw$3, 16($0)
        beq$2, $3, 1
        add $4, $2, $3
        sw$4, 24($0)
        
Cycle 1
lw: IF
Cycle 2
lw: ID
lw: IF
Cycle 3
lw: EX 01 010 11
lw: ID
beq: IF
Cycle 4
lw: MEM 010 11
lw: EX 01 010 11
beq: ID
add: IF
Cycle 5
lw: WB 11
lw: MEM 010 11
beq: ID
add: IF
Cycle 6
lw: WB 11
beq: ID
add: IF
Cycle 7
beq: EX X0 100 0X
sw: IF
Cycle 8
beq: MEM 100 0X
sw: ID
Cycle 9
beq: WB 0X
sw: EX X1 001 0X
Cycle 10
sw: MEM 001 0X
Cycle 11
sw: WB 0X

// 4
        {"add", 1, 2, 3, -1},
        {"add", 4, 1, 1, -1},
        {"sub", 4, 4, 1, -1},
        {"beq", -1, 4, 1, -2},
        {"add", 4, 1, 4, -1},
        {"sw", 4, 0, -1, 4},
        
        add $1, $2, $3
        add $4, $1, $1
        sub $4, $4, $1
        beq$4, $1, -2
        add $4, $1, $4
        sw$4, 4($0)
        

// 5
        {"sub", 1, 4, 4, -1},
        {"beq", -1, 1, 2, 2},
        {"add", 2, 3, 3, -1},
        {"lw", 1, 0, -1, 4},
        {"add", 4, 5, 6, -1},
        
        sub $1, $4, $4
        beq$1, $2, 2
        add $2, $3, $3
        lw$1, 4($0)
        add $4, $5, $6
        

// 6
        {"lw", 8, 0, -1, 8},
        {"beq", -1, 4, 8, 1},
        {"sub", 2, 7, 9, -1},
        {"sw", 2, 0, -1, 8},
        
        lw$8, 8($0)
        beq$4, $8, 1
        sub $2, $7, $9
        sw$2, 8($0)
        

// 7
        {"add", 1, 1, 2, -1},
        {"add", 1, 1, 3, -1},
        {"add", 1, 1, 4, -1},
        {"sw", 1, 0, -1, 8},
        
        add $1, $1, $2
        add $1, $1, $3
        add $1, $1, $4
        sw$1, 8($0)
        

// 8
        {"lw", 4, 0, -1, 8},
        {"beq", -1, 4, 4, 1},
        {"add", 4, 4, 4, -1},
        {"sub", 4, 4, 4, -1},
        {"beq", -1, 4, 1, -1},
        {"sw", 4, 0, -1, 8},
        
        lw$4, 8($0)
        beq$4, $4, 1
        add $4, $4, $4
        sub $4, $4, $4
        beq$4, $1, -1
        sw$4, 8($0)
        */
    };

    cpu.loadInstructions(instructions);
    cpu.simulate();

    return 0;
}
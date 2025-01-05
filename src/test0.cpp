#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <fstream>
#include <sstream>
using namespace std;

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
        registers[0] = 0;
        memory.resize(MEMORY_SIZE, 1);
        pc = 0;
        cycle = 0;
    }

    void loadInstructions(const vector<Instruction>& insts) {
        instructions = insts;
    }

    void simulate(ofstream& output);
    void fetch();
    bool decode(); // Modified to return stall flag
    void execute();
    void memoryAccess();
    void writeBack();
    void advancePipeline(ofstream& output);
    void printPipeline();
    void printFinalResults(ofstream& output);
};

string controlSignals(const string& op) {
    if (op == "lw")  return "RegDst=0 ALUSrc=1 Branch=0 MemRead=1 MemWrite=0 RegWrite=1 MemToReg=1";
    if (op == "sw")  return "RegDst=X ALUSrc=1 Branch=0 MemRead=0 MemWrite=1 RegWrite=0 MemToReg=X";
    if (op == "add") return "RegDst=1 ALUSrc=0 Branch=0 MemRead=0 MemWrite=0 RegWrite=1 MemToReg=0";
    if (op == "sub") return "RegDst=1 ALUSrc=0 Branch=0 MemRead=0 MemWrite=0 RegWrite=1 MemToReg=0";
    if (op == "beq") return "RegDst=X ALUSrc=0 Branch=1 MemRead=0 MemWrite=0 RegWrite=0 MemToReg=X";
    if (op == "nop") return "NOP";
    return "RegDst=X ALUSrc=X Branch=X MemRead=X MemWrite=X RegWrite=X MemToReg=X";
}

Instruction parseInstruction(const string& line) {
    istringstream iss(line);
    string op, rd, rs, rt, offset;

    Instruction inst;
    inst.rd = inst.rs = inst.rt = -1;
    inst.immediate = inst.address = -1;

    iss >> op;
    inst.op = op;

    if (op == "lw" || op == "sw") {
        iss >> rd >> offset;
        rd.pop_back();
        size_t start = offset.find('(');
        size_t end = offset.find(')');
        inst.rd = stoi(rd.substr(1));
        inst.address = stoi(offset.substr(0, start));
        inst.rs = stoi(offset.substr(start + 2, end - start - 2));
    } 
    else if (op == "beq") {
        iss >> rs >> rt >> offset;
        rs.pop_back();
        rt.pop_back();
        inst.rs = stoi(rs.substr(1));
        inst.rt = stoi(rt.substr(1));
        inst.immediate = stoi(offset);
    } 
    else if (op == "add" || op == "sub") {
        iss >> rd >> rs >> rt;
        rd.pop_back();
        rs.pop_back();
        inst.rd = stoi(rd.substr(1));
        inst.rs = stoi(rs.substr(1));
        inst.rt = stoi(rt.substr(1));
    }

    return inst;
}

// Fetch Instruction (IF)
void CPU::fetch() {
    // Check if PC is within instruction range
    if (pc < (int)instructions.size()) {
        Instruction inst = instructions[pc];
        pipeline[0].push(inst);           // Place into IF stage (pipeline[0])
        clockOutput.push_back(inst.op + " IF");
        pc++; //(Predict Not Taken: fetch next)
    }
}

// Decode Instruction (ID) + Hazard Detection
bool CPU::decode() {
    if (pipeline[0].empty()) return false;

    Instruction inst = pipeline[0].front();
    bool needsStall = false;

    // 檢查 EX 階段
    if (!pipeline[2].empty()) {
        Instruction prevEX = pipeline[2].front();
        bool prevEXWritesRegister =
            (prevEX.op == "add" || prevEX.op == "sub" || prevEX.op == "lw");

        if (prevEXWritesRegister && prevEX.rd != 0) {
            if (prevEX.rd == inst.rs || prevEX.rd == inst.rt) {
                if (prevEX.op == "lw") {
                    // lw 在 EX 階段會 stall
                    needsStall = true;
                } else {
                    // add / sub 在 EX -> EX forwarding
                    std::cout << "[Forwarding from EX] " 
                              << inst.op << " depends on " 
                              << prevEX.op << " (rd=" << prevEX.rd << ")\n";
                }
            }
        }
    }

    // 檢查 MEM 階段
    if (!pipeline[3].empty()) {
        Instruction prevMEM = pipeline[3].front();
        bool prevMEMWritesRegister =
            (prevMEM.op == "add" || prevMEM.op == "sub" || prevMEM.op == "lw");

        if (prevMEMWritesRegister && prevMEM.rd != 0) {
            if (prevMEM.rd == inst.rs || prevMEM.rd == inst.rt) {
                std::cout << "[Forwarding from MEM] " 
                          << inst.op << " depends on " 
                          << prevMEM.op << " (rd=" << prevMEM.rd << ")\n";
            }
        }
    }

    if (inst.op == "beq") {
        // 檢查 EX 階段 (pipeline[2])
        if (!pipeline[2].empty()) {
            Instruction exInst = pipeline[2].front();
            if (exInst.rd != 0 && (exInst.rd == inst.rs || exInst.rd == inst.rt)) {
                if (exInst.op == "sub" || exInst.op == "lw") {
                    needsStall = true;
                    std::cout << "[Stall for BEQ] " 
                              << exInst.op << " in EX stage (rd=" << exInst.rd 
                              << "), waiting for it to finish EX\n";
                }
            }
        }

        // 檢查 MEM 階段 (pipeline[3])
        if (!pipeline[3].empty()) {
            Instruction memInst = pipeline[3].front();
            if (memInst.rd != 0 && (memInst.rd == inst.rs || memInst.rd == inst.rt)) {
                if (memInst.op == "sub") {
                    // 不 stall
                }
                else if (memInst.op == "lw") {
                    needsStall = true;
                    std::cout << "[Stall for BEQ] lw in MEM stage (rd=" 
                              << memInst.rd << "), waiting for it to finish WB\n";
                }
            }
        }
    }

    if (needsStall) {
        std::cout << "Cycle " << cycle << ": Data hazard detected, stalling pipeline\n";
        Instruction bubble = {"nop", -1, -1, -1, -1};
        pipeline[1].push(bubble);
        clockOutput.push_back("NOP inserted due to stall");
        return true;
    }

    pipeline[0].pop();      // IF -> remove
    pipeline[1].push(inst); // ID <- inst
    clockOutput.push_back(inst.op + " ID");
    return false;
}

// Updated Branch Handling in Execute (EX)
void CPU::execute() {
    if (pipeline[1].empty()) return;

    Instruction inst = pipeline[1].front();
    pipeline[1].pop();

    if (inst.op == "nop") {
        pipeline[2].push(inst);
        clockOutput.push_back("NOP EX");
        return;
    }

    if (inst.op == "beq") {
        int rsValue = registers[inst.rs];
        int rtValue = registers[inst.rt];

        if (rsValue == rtValue) {
            // Branch taken
            pc = pc + inst.immediate - 1;
            while (!pipeline[0].empty()) {
                pipeline[0].pop();
            }
            cout << "[Branch taken] Flushing IF, PC set to " << pc << "\n";
        } else {
            // Branch not taken
            cout << "[Branch not taken] Continue to next instruction\n";
        }
    } 
    else if (inst.op == "add") {
        registers[inst.rd] = registers[inst.rs] + registers[inst.rt];
    } 
    else if (inst.op == "sub") {
        registers[inst.rd] = registers[inst.rs] - registers[inst.rt];
    }

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

    clockOutput.push_back(inst.op + " WB " + controlSignals(inst.op));
}

// Pipeline Advancement
void CPU::advancePipeline(ofstream& output) {
    clockOutput.clear();

    writeBack();
    memoryAccess();
    execute();

    // Decode and handle stalls
    bool stall = decode();

    // Fetch only if no stall
    if (!stall) {
        fetch();
    }

    // 輸出此 clock cycle
    output << "Clock Cycle " << ++cycle << ":\n";
    for (const auto& out : clockOutput) {
        output << out << endl;
    }
    output << endl;
}

// Print Final Results
void CPU::printFinalResults(ofstream& output) {
    output << "## Final Result:\n";
    output << "Total Cycles: " << cycle << endl;

    output << "Final Register Values:\n";
    for (int i = 0; i < NUM_REGISTERS; i++) {
        output << registers[i] << " ";
    }
    output << endl;

    output << "Final Memory Values:\n";
    for (int i = 0; i < MEMORY_SIZE; i++) {
        output << memory[i] << " ";
    }
    output << endl;
}

// Simulate CPU Execution
void CPU::simulate(ofstream& output) {
    while ( pc < (int)instructions.size() ||
            !pipeline[0].empty() ||
            !pipeline[1].empty() ||
            !pipeline[2].empty() ||
            !pipeline[3].empty() )
    {
        advancePipeline(output);
    }
    printFinalResults(output);
}

int main() {
    CPU cpu;

    ifstream input("../../input/test8.txt");
    ofstream output("../../output/test8output.txt");

    if (!input.is_open() || !output.is_open()) {
        cerr << "Error opening input or output file." << endl;
        return 1;
    }

    vector<Instruction> instructions;
    string line;
    while (getline(input, line)) {
        instructions.push_back(parseInstruction(line));
    }

    cpu.loadInstructions(instructions);
    cpu.simulate(output);

    input.close();
    output.close();

    return 0;
}
